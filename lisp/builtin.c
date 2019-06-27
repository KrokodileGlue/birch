#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>

#include "eval.h"
#include "util.h"
#include "error.h"
#include "builtin.h"
#include "parse.h"

static struct value *
append(struct value *list, struct value *v)
{
	if (list->type == VAL_NIL) return cons(v, Nil);
	struct value *k = list;
	while (k->cdr->type != VAL_NIL) k = k->cdr;
	k->cdr = cons(v, Nil);
	return list;
}

/*
 * Verifies that `v` is a well-formed function and returns a new
 * function value built from it. `v->car` is the list of parameters
 * and `v->cdr` is the body of the function.
 */

static struct value *
make_function(struct value *env, struct value *v, int type)
{
	assert(type == VAL_FUNCTION || type == VAL_MACRO);
	assert(v);

	if (v->type != VAL_CELL
	    || !IS_LIST(v->car)
	    || !IS_LIST(v->cdr))
		error(v->loc, "malformed function definition");

	struct value *r = new_value(v->cdr->loc);
	struct value *param = Nil;

	r->optional = new_value(r->loc);
	r->optional->type = VAL_NIL;

	r->key = new_value(r->loc);
	r->key->type = VAL_NIL;

	if (!IS_LIST(v->car))
		return error(r->loc,
		             "expected parameter list here");

	for (struct value *p = v->car;
	     p->type == VAL_CELL;
	     p = p->cdr) {
		if (p->car->type == VAL_SYMBOL) {
			param = append(param, p->car);
			continue;
		}

		if (p->car->type == VAL_KEYWORD
		    && kdgu_cmp(p->car->keyword->s,
		                &KDGU("optional"), false, NULL)) {
			struct value *current = p;
			p = p->cdr;

			while (p->car->type == VAL_SYMBOL
			       || p->car->type == VAL_CELL) {
				if (p->car->type == VAL_SYMBOL) {
					r->optional = append(r->optional, cons(p->car, Nil));
					param = append(param, p->car);
				} else {
					r->optional = append(r->optional, cons(p->car->car, p->car->cdr->car));
					param = append(param, p->car->car);
				}

				if (p->cdr->type == VAL_CELL) p = p->cdr, current = current->cdr;
				else break;
			}

			if (p->cdr->type == VAL_NIL) break;
			p = current;
			continue;
		}

		if (p->car->type == VAL_KEYWORD
		    && kdgu_cmp(p->car->keyword->s,
		                &KDGU("key"), false, NULL)) {
			struct value *current = p;
			p = p->cdr;

			while (p->car->type == VAL_SYMBOL
			       || p->car->type == VAL_CELL) {
				if (p->car->type == VAL_SYMBOL) {
					r->key = append(r->key, cons(p->car, Nil));
					param = append(param, p->car);
				} else {
					r->key = append(r->key, cons(p->car->car, p->car->cdr->car));
					param = append(param, p->car->car);
				}

				if (p->cdr->type == VAL_CELL) p = p->cdr, current = current->cdr;
				else break;
			}

			if (p->cdr->type == VAL_NIL) break;
			p = current;
			continue;
		}

		if (p->car->type == VAL_KEYWORD
		    && kdgu_cmp(p->car->keyword->s,
		                &KDGU("rest"), false, NULL)) {
			if (p->type != VAL_CELL
			    || p->cdr->type != VAL_CELL
			    || p->cdr->car->type != VAL_SYMBOL)
				return error(p->loc, "expected a symbol");
			p = p->cdr;
			r->rest = p->car;
			if (p->cdr->type != VAL_NIL)
				return error(p->loc, "expected end of parameter list to follow REST parameter");
			break;
		}

		return error(p->car->loc,
		             "parameter name must be a symbol"
		             " (this is %s %s)",
		             IS_VOWEL(*TYPE_NAME(p->car->type))
		             ? "an" : "a",
		             TYPE_NAME(p->car->type));
	}

	r->type  = type;
	r->param = param;
	r->env   = env;

	if (v->cdr->car->type == VAL_STRING) {
		r->docstring = v->cdr->car;
		r->body = v->cdr->cdr;
	} else {
		r->body = v->cdr;
	}

	return r;
}

/*
 * Builds a function. Builds both named and anonymous functions.
 */

struct value *
builtin_fn(struct value *env, struct value *v)
{
	if (v->cdr->type != VAL_CELL)
		return error(v->loc, "missing list of parameters");

	/*
	 * If `v->car->type != VAL_SYMBOL` then this is an anonymous
	 * function.
	 */

	if (v->car->type != VAL_SYMBOL)
		return make_function(env, v, VAL_FUNCTION);

	/*
	 * Otherwise it's obviously a named function which should be
	 * added to the environment as a variable.
	 */

	struct value *fun = make_function(env, v->cdr, VAL_FUNCTION);
	if (fun->type == VAL_ERROR) return fun;

	return add_variable(env, v->car, fun);
}

/*
 * Evaluates each expression in `list` and prints it to stdout.
 */

struct value *
builtin_print(struct value *env, struct value *list)
{
	struct value *r = eval_list(env, list);
	if (r->type == VAL_ERROR) return r;

	kdgu *out = kdgu_news("");

	for (struct value *p = r; p->type != VAL_NIL; p = p->cdr) {
		if (p->car->type == VAL_STRING) {
			kdgu_append(out, p->car->s);
			continue;
		}

		struct value *e = print_value(stdout, p->car);
		if (e->type == VAL_ERROR) return e;
		kdgu_append(out, e->s);
	}

	struct value *e = new_value(r->loc);
	e->type = VAL_STRING;
	e->s = out;

	return e;
}

struct value *
builtin_set(struct value *env, struct value *list)
{
	struct value *sym = eval(env, list->car);
	struct value *bind = find(env, sym);

	if (!bind) {
		struct value *value = eval(env, list->cdr->car);
		add_variable(env, sym, value);
		return value;
	}

	struct value *value = eval(env, list->cdr->car);
	bind->cdr = value;

	return value;
}

#define ARITHMETIC(X)	  \
	int sum = 0, first = 1; \
	for (struct value *args = eval_list(env, list); \
	     args->type != VAL_NIL; \
	     args = args->cdr) { \
		if (args->type == VAL_ERROR) return args; \
		if (args->car->type == VAL_INT) { \
			if (first) { \
				sum = args->car->i; \
				first = 0; \
			} \
			else sum X##= args->car->i; \
			continue; \
		} \
		return error(args->car->loc, \
		             "builtin `"#X"' takes only " \
		             "numeric arguments (got `%s')", \
		             TYPE_NAME(args->car->type)); \
	} \
	struct value *r = new_value(list->loc); \
	r->type = VAL_INT; \
	r->i = sum; \
	return r;

struct value *
builtin_add(struct value *env, struct value *list)
{
	ARITHMETIC(+);
}

struct value *
builtin_sub(struct value *env, struct value *list)
{
	ARITHMETIC(-);
}

struct value *
builtin_mul(struct value *env, struct value *list)
{
	ARITHMETIC(*);
}

struct value *
builtin_div(struct value *env, struct value *list)
{
	ARITHMETIC(/);
}

struct value *
builtin_eq(struct value *env, struct value *list)
{
	int sum = 0, first = 1;

	for (struct value *args = eval_list(env, list);
	     args->type != VAL_NIL;
	     args = args->cdr) {
		if (args->type == VAL_ERROR) return args;

		if (args->car->type == VAL_INT) {
			if (first) sum = args->car->i, first = 0;
			else if (args->car->i != sum)
				return Nil;
			continue;
		}

		return error(args->car->loc,
		             "builtin `=' takes only numeric"
		             " arguments (got `%s')",
		             TYPE_NAME(args->car->type));
	}

	return True;
}

struct value *
builtin_less(struct value *env, struct value *v)
{
	int sum = 0, first = 1;

	for (struct value *args = eval_list(env, v);
	     args->type != VAL_NIL;
	     args = args->cdr) {
		if (args->type == VAL_ERROR) return args;

		if (args->car->type == VAL_INT) {
			if (first) sum = args->car->i, first = 0;
			else if (args->car->i >= sum)
				return Nil;
			continue;
		}

		return error(args->car->loc,
		             "builtin `<' takes only numeric"
		             " arguments (got `%s')",
		             TYPE_NAME(args->car->type));
	}

	return True;
}

struct value *
builtin_if(struct value *env, struct value *list)
{
	struct value *cond = eval(env, list->car);

	if (!cond) return Nil;
	if (cond->type == VAL_ERROR) return cond;

	if (cond->type != VAL_NIL)
		return eval(env, list->cdr->car);

	/* Otherwise do the else branches. */
	return progn(env, list->cdr->cdr);
}

struct value *
builtin_quote(struct value *env, struct value *v)
{
	(void)env;
	return v->car;
}

struct value *
builtin_setq(struct value *env, struct value *list)
{
	return builtin_set(env, cons(quote(list->car), list->cdr));
}

struct value *
builtin_cons(struct value *env, struct value *v)
{
	struct value *a = eval(env, v->car),
		*b = eval(env, v->cdr->car);
	if (!a) return Nil;
	if (a->type == VAL_ERROR) return a;
	if (!b) return Nil;
	if (b->type == VAL_ERROR) return b;
	/* if (!IS_LIST(b)) return error(v->loc, "wahhh"); */
	return cons(a, b);
}

struct value *
builtin_car(struct value *env, struct value *v)
{
	v = eval(env, v->car);
	if (v->type == VAL_ERROR) return v;
	if (!IS_LIST(v))
		return error(v->loc,
		             "builtin `car' requires a list argument"
		             " (this is %s %s)",
		             IS_VOWEL(*TYPE_NAME(v->type))
		             ? "an" : "a",
		             TYPE_NAME(v->type));
	return v->car;
}

struct value *
builtin_cdr(struct value *env, struct value *v)
{
	return eval(env, v->car)->cdr;
}

struct value *
builtin_macro(struct value *env, struct value *v)
{
	struct value *fun = make_function(env, v->cdr, VAL_MACRO);
	if (fun->type == VAL_ERROR) return fun;
	return add_variable(env, v->car,fun);
}

struct value *
builtin_list(struct value *env, struct value *list)
{
	return eval_list(env, list);
}

struct value *
builtin_progn(struct value *env, struct value *v)
{
	return progn(env, v);
}

struct value *
builtin_while(struct value *env, struct value *v)
{
	struct value *c = Nil, *r = Nil;

	while ((c = eval(env, v->car)) && c->type == VAL_TRUE) {
		r = progn(env, v->cdr);
		if (r->type == VAL_ERROR) return r;
	}

	if (c->type == VAL_ERROR) return c;

	return r;
}

struct value *
builtin_nth(struct value *env, struct value *v)
{
	if (v->type != VAL_CELL
	    || v->cdr->type != VAL_CELL
	    || v->cdr->cdr->type != VAL_NIL)
		return error(v->loc,
		             "builtin `nth' requires two arguments");

	struct value *i = eval(env, v->cdr->car);
	if (i->type != VAL_INT)
		return error(v->loc,
		             "builtin `nth' requires a numeric second"
		             " argument");

	struct value *arr = eval(env, v->car);
	if (arr->type != VAL_ARRAY
	    && !IS_LIST(arr)
	    && arr->type != VAL_STRING)
		return error(v->loc,
		             "builtin `nth' requires an array, list, "
		             "or string argument",
		             " (this is %s %s)",
		             IS_VOWEL(*TYPE_NAME(arr->type))
		             ? "an" : "a",
		             TYPE_NAME(arr->type));

	/* TODO: lists */

	if (arr->type == VAL_STRING) {
		unsigned idx = 0;

		for (int j = 0; j < i->i; j++)
			kdgu_next(arr->s, &idx);

		struct value *str = new_value(v->loc);
		str->s = kdgu_getchr(arr->s, idx);
		str->type = VAL_STRING;
		return str;
	}

	return eval(env, v->car)->arr[i->i];
}

struct value *
builtin_length(struct value *env, struct value *v)
{
	if (v->cdr->type != VAL_NIL)
		return error(v->loc,
		             "builtin `length' takes one argument");

	struct value *arr = eval(env, v->car);

	if (arr->type == VAL_ARRAY) {
		struct value *l = new_value(v->loc);
		l->type = VAL_INT;
		l->i = arr->num;
		return l;
	} else if (IS_LIST(arr)) {
		return list_length(arr);
	} else if (arr->type == VAL_STRING) {
		struct value *l = new_value(v->loc);
		l->type = VAL_INT;
		l->i = kdgu_len(arr->s);
		return l;
	} else {
		return error(v->loc,
		             "builtin `length' takes a list, array,"
		             " or string argument (this is %s %s)",
		             IS_VOWEL(*TYPE_NAME(arr->type))
		             ? "an" : "a",
		             TYPE_NAME(arr->type));
	}

	/* Unreachable. */

	return Nil;
}

struct value *
builtin_s(struct value *env, struct value *v)
{
	struct value *pattern = eval(env, v->car);
	struct value *replace = eval(env, v->cdr->car);
	struct value *options = eval(env, v->cdr->cdr->car);
	struct value *subject = eval(env, v->cdr->cdr->cdr->car);

	if (pattern->type != VAL_STRING
	    || replace->type != VAL_STRING
	    || options->type != VAL_STRING
	    || subject->type != VAL_STRING)
		return error(v->loc, "malformed `s' application");

	int opt = KTRE_UNANCHORED;

	for (unsigned i = 0; i < options->s->len; i++) {
		switch (options->s->s[i]) {
		case 'g': opt |= KTRE_GLOBAL; break;
		case 'i': opt |= KTRE_INSENSITIVE; break;
		default: return error(v->loc,
		                      "unrecognized mode modifier");
		}
	}

	kdgu *res = ktre_replace(subject->s, pattern->s, replace->s, &KDGU("$"), opt);

	struct value *str = new_value(v->loc);
	str->type = VAL_STRING;
	str->s = res;

	return str;
}

struct value *
builtin_expand(struct value *env, struct value *v)
{
	return expand(env, v);
}

struct value *
builtin_eval(struct value *env, struct value *v)
{
	return eval(env, eval(env, v->car));
}

struct value *
builtin_read_string(struct value *env, struct value *v)
{
	char *s = malloc(v->car->s->len + 1);
	memcpy(s, v->car->s->s, v->car->s->len);
	s[v->car->s->len] = 0;

	struct lexer *lexer = new_lexer("*string*", s);
	struct token *t = tok(lexer, CODE);

	if (t->type != '(')
		return error(v->loc, "expected `(' to begin "
		             "s-expression in string contents");

	return parse(env, lexer);
}

struct value *
builtin_documentation(struct value *env, struct value *v)
{
	return eval(env, v->car)->docstring;
}

static void
append2(struct value *list, struct value *v)
{
	struct value *k = list;
	while (k->cdr->type != VAL_NIL) {
		if (IS_LIST(k) && k->cdr && !IS_LIST(k->cdr)) {
			k->cdr = cons(k->cdr, v);
			return;
		}
		k = k->cdr;
	}
	k->cdr = v;
}

struct value *
builtin_backtick(struct value *env, struct value *v)
{
	struct value *head = copy_value(v->car), *tail = head;
	struct value *penis = tail;

	/* print_tree(stdout, head); */

	while (tail && tail->type != VAL_NIL) {
		if (tail->car->type == VAL_COMMA) {
			struct value *tmp = eval(env, tail->car->keyword);
			if (tmp->type == VAL_ERROR) return tmp;
			tmp = cons(tmp, Nil);
			append2(tmp, tail->cdr);

			if (penis == tail) {
				head = tmp;
				penis = head;
				tail = tail->cdr;
			} else {
				penis->cdr = tmp;
				penis = tmp;
				tail = tail->cdr;
			}
		} else if (tail->car->type == VAL_COMMAT) {
			struct value *tmp = eval(env, tail->car->keyword);
			if (tmp->type == VAL_ERROR) return tmp;
			if (tmp->type != VAL_CELL)
				return error(tmp->loc, "expected a list");
			tmp = copy_value(tmp);
			append2(tmp, tail->cdr);

			if (penis == tail) {
				head = tmp;
				penis = head;
				tail = tail->cdr;
			} else {
				penis->cdr = tmp;
				penis = tmp;
				tail = tail->cdr;
			}
		} else {
			if (tail->car->type == VAL_CELL) {
				tail->car = builtin_backtick(env, cons(tail->car, Nil));
				if (tail->car->type == VAL_ERROR) return tail->car;
			}

			if (tail == penis)
				tail = tail->cdr;
			else {
				tail = tail->cdr;
				penis = penis->cdr;
			}
		}
	}

	return head;
}

void
load_builtins(struct value *env)
{
	add_builtin(env, "documentation", builtin_documentation);
	add_builtin(env, "read-string", builtin_read_string);
	add_builtin(env, "backtick", builtin_backtick);
	add_builtin(env, "expand",  builtin_expand);
	add_builtin(env, "length",  builtin_length);
	add_builtin(env, "lambda",  builtin_fn);
	add_builtin(env, "print",   builtin_print);
	add_builtin(env, "progn",   builtin_progn);
	add_builtin(env, "defmacro",builtin_macro);
	add_builtin(env, "macro",   builtin_macro);
	add_builtin(env, "while",   builtin_while);
	add_builtin(env, "quote",   builtin_quote);
	add_builtin(env, "list",    builtin_list);
	add_builtin(env, "cons",    builtin_cons);
	add_builtin(env, "eval",    builtin_eval);
	add_builtin(env, "setq",    builtin_setq);
	add_builtin(env, "nth",     builtin_nth);
	add_builtin(env, "set",     builtin_set);
	add_builtin(env, "car",     builtin_car);
	add_builtin(env, "cdr",     builtin_cdr);
	add_builtin(env, "defun",   builtin_fn);
	add_builtin(env, "fn",      builtin_fn);
	add_builtin(env, "if",      builtin_if);
	add_builtin(env, "+",       builtin_add);
	add_builtin(env, "-",       builtin_sub);
	add_builtin(env, "*",       builtin_mul);
	add_builtin(env, "/",       builtin_div);
	add_builtin(env, "=",       builtin_eq);
	add_builtin(env, "<",       builtin_less);
	add_builtin(env, "s",       builtin_s);
}
