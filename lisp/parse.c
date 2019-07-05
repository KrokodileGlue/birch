#include <stdlib.h>
#include <string.h>
#include <kdg/kdgu.h>

#include "lex.h"
#include "lisp.h"
#include "error.h"
#include "parse.h"
#include "eval.h"
#include "gc.h"

static struct value
parse_expr(struct env *env, struct lexer *l)
{
	struct token *t = tok(l);
	if (!t) return VNULL;

	/*
	 * The cast to int is here to suppress the `case value 'x' not
	 * in enumerated type` warning GCC emits.
	 */

	switch ((int)t->type) {
	case '(':  return parse(env, l);
	case ')':  return RPAREN;
	case '.':  return DOT;
	case '\'': return quote(env, parse_expr(env, l));
	case '`':  return backtick(env, parse_expr(env, l));

	case ',': {
		struct value v = gc_alloc(env, l->s[l->idx] == '@'
		                          ? VAL_COMMAT : VAL_COMMA);

		if (l->s[l->idx] == '@') {
			l->idx++;
			v.type = VAL_COMMAT;
			keyword(v) = parse_expr(env, l);
		} else {
			v.type = VAL_COMMA;
			keyword(v) = parse_expr(env, l);
		}

		return v;
	} break;

	/* Array. */
	case '[': {
		struct value v = gc_alloc(env, VAL_ARRAY);

		/* TODO: Hrm, this is incredibly dumb. */
		struct value arr[512];
		unsigned num = 0;

		while ((t = tok(l)) && t->type != ']')
			arr[num++] = parse_expr(env, l);

		array(v) = malloc(num * sizeof v);
		memcpy(array(v), arr, num * sizeof v);
		obj(v).num = num;

		return v;
	} break;

	/* Keyword. */
	case '&': {
		struct value v = gc_alloc(env, VAL_KEYWORD);
		keyword(v) = parse_expr(env, l);
		if (keyword(v).type != VAL_SYMBOL)
			return error(env, "expected a symbol");
		return v;
	} break;

	/* Keyword parameter. */
	case ':': {
		struct value v = gc_alloc(env, VAL_KEYWORDPARAM);
		keyword(v) = parse_expr(env, l);
		if (keyword(v).type != VAL_SYMBOL)
			return error(env, "expected a symbol");
		return v;
	} break;

	case TOK_IDENT:
		return make_symbol(env, t->body);

	case TOK_STR: {
		struct value v = gc_alloc(env, VAL_STRING);
		string(v) = kdgu_copy(t->s);
		return v;
	} break;

	case TOK_INT: return (struct value){VAL_INT, {t->i}};

	default:
		return error(env, "unexpected `%s'", t->body);
	}

	return VNULL;
}

struct value
parse(struct env *env, struct lexer *l)
{
	struct value head = NIL, tail = head;

	for (;;) {
		struct value o = parse_expr(env, l);

		if (o.type == VAL_NULL)
			return error(env, "unmatched `('");

		if (o.type == VAL_RPAREN) return head;

		if (o.type == VAL_DOT) {
			cdr(tail) = parse_expr(env, l);
			if (parse_expr(env, l).type != VAL_RPAREN)
				return error(env, "expected `)'");
			return head;
		}

		if (head.type == VAL_NIL) {
			head = cons(env, o, NIL);
			tail = head;
			continue;
		}

		cdr(tail) = cons(env, o, NIL);
		tail = cdr(tail);
	}
}
