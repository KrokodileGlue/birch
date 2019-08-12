#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>

#include <kdg/kdgu.h>

#include "../birch.h"
#include "../util.h"

#include "lex.h"
#include "lisp.h"
#include "eval.h"
#include "util.h"
#include "error.h"
#include "builtin.h"
#include "parse.h"
#include "gc.h"

static struct value
append(struct env *env, struct value list, struct value v)
{
	if (list.type == VAL_NIL) return cons(env, v, NIL);
	struct value k = list;
	while (cdr(k).type != VAL_NIL) k = cdr(k);
	cdr(k) = cons(env, v, NIL);
	return list;
}

/*
 * Verifies that `v` is a well-formed function and returns a new
 * function value built from it. `car(v)` is the list of parameters
 * and `cdr(v)` is the body of the function.
 *
 * TODO: Rewrite the ugly parts.
 */

static struct value
make_function(struct env *env, struct value v, enum value_type type)
{
	assert(type == VAL_FUNCTION || type == VAL_MACRO);
	assert(v.type != VAL_NIL);

	if (v.type != VAL_CELL
	    || !IS_LIST(car(v))
	    || !IS_LIST(cdr(v)))
		return error(env, "malformed function definition");

	struct value r = gc_alloc(env, type);
	struct value param = NIL;

	optional(r) = NIL;
	key(r) = NIL;
	rest(r) = NIL;

	if (!IS_LIST(car(v)))
		return error(env, "expected parameter list here");

	for (struct value p = car(v);
	     p.type == VAL_CELL;
	     p = cdr(p)) {
		if (car(p).type == VAL_SYMBOL) {
			param = append(env, param, car(p));
			continue;
		}

		if (car(p).type == VAL_KEYWORD
		    && kdgu_cmp(string(keyword(car(p))),
		                &KDGU("optional"), false, NULL)) {
			struct value current = p;
			p = cdr(p);

			while (car(p).type == VAL_SYMBOL
			       || car(p).type == VAL_CELL) {
				if (car(p).type == VAL_SYMBOL) {
					optional(r) = append(env, optional(r), cons(env, car(p), NIL));
					param = append(env, param, car(p));
				} else {
					optional(r) = append(env, optional(r), cons(env, car(car(p)), car(cdr(car(p)))));
					param = append(env, param, car(car(p)));
				}

				if (cdr(p).type == VAL_CELL) p = cdr(p), current = cdr(current);
				else break;
			}

			if (cdr(p).type == VAL_NIL) break;
			p = current;
			continue;
		}

		if (car(p).type == VAL_KEYWORD
		    && kdgu_cmp(string(keyword(car(p))),
		                &KDGU("key"), false, NULL)) {
			struct value current = p;
			p = cdr(p);

			while (car(p).type == VAL_SYMBOL
			       || car(p).type == VAL_CELL) {
				if (car(p).type == VAL_SYMBOL) {
					key(r) = append(env, key(r), cons(env, car(p), NIL));
					param = append(env, param, car(p));
				} else {
					key(r) = append(env, key(r), cons(env, car(car(p)), car(cdr(car(p)))));
					param = append(env, param, car(car(p)));
				}

				if (cdr(p).type == VAL_CELL) p = cdr(p), current = cdr(current);
				else break;
			}

			if (cdr(p).type == VAL_NIL) break;
			p = current;
			continue;
		}

		if (car(p).type == VAL_KEYWORD
		    && kdgu_cmp(string(keyword(car(p))),
		                &KDGU("rest"), false, NULL)) {
			if (p.type != VAL_CELL
			    || cdr(p).type != VAL_CELL
			    || car(cdr(p)).type != VAL_SYMBOL)
				return error(env, "expected a symbol");
			p = cdr(p);
			rest(r) = car(p);
			if (cdr(p).type != VAL_NIL)
				return error(env, "expected end of parameter list to follow REST parameter");
			break;
		}

		return error(env,
		             "parameter name must be a symbol"
		             " (this is %s %s)",
		             IS_VOWEL(*TYPE_NAME(car(p).type))
		             ? "an" : "a",
		             TYPE_NAME(car(p).type));
	}

	param(r) = param;
	env(r)   = env;

	if (cdr(v).type == VAL_CELL
	    && car(cdr(v)).type == VAL_STRING) {
		/* TODO: Should this copy the thing? */
		docstring(r) = car(cdr(v));
		body(r) = cdr(cdr(v));
	} else {
		body(r) = cdr(v);
	}

	return r;
}

/*
 * Builds a function. Builds both named and anonymous functions.
 */

struct value
builtin_fn(struct env *env, struct value v)
{
	if (v.type == VAL_NIL)
		return error(env, "`defun' requires arguments");

	if (cdr(v).type != VAL_CELL)
		return error(env, "missing list of parameters");

	/*
	 * If `car(v).type != VAL_SYMBOL` then this is an anonymous
	 * function (because it has no name).
	 */

	if (car(v).type != VAL_SYMBOL)
		return make_function(env, v, VAL_FUNCTION);

	struct value sym = car(v);

	/*
	 * Otherwise it's obviously a named function which should be
	 * added to the environment as a variable.
	 */

	struct value fun = make_function(env, cdr(v), VAL_FUNCTION);
	if (fun.type == VAL_ERROR) return fun;
	name(fun) = kdgu_copy(string(sym));

	struct value bind = find(env, sym);

	if (bind.type == VAL_NIL)
		return add_variable(env, sym, fun);

	return cdr(bind) = fun;
}

struct value
builtin_set(struct env *env, struct value v)
{
	if (v.type != VAL_CELL || cdr(v).type == VAL_NIL)
		return error(env, "`set' requires two arguments");

	struct value sym = eval(env, car(v));

	if (sym.type == VAL_ERROR)
		return sym;

	if (sym.type != VAL_SYMBOL)
		return error(env, "the first argument to `set'"
		             " must be a symbol");

	struct value bind = find(env, sym);

	if (bind.type == VAL_NIL)
		return error(env, "undeclared identifier in `set'");

	struct value value = eval(env, car(cdr(v)));

	if (value.type == VAL_ERROR)
		return value;

	return cdr(bind) = value;
}

struct value
builtin_def(struct env *env, struct value v)
{
	if (v.type != VAL_CELL || cdr(v).type == VAL_NIL)
		return error(env, "`def' requires two arguments");

	struct value sym = eval(env, car(v));

	if (sym.type == VAL_ERROR)
		return sym;

	if (sym.type != VAL_SYMBOL)
		return error(env, "the first argument to `def'"
		             " must be a symbol");

	struct value value = eval(env, car(cdr(v)));
	return add_variable(env, sym, value);
}

struct value
builtin_defq(struct env *env, struct value v)
{
 	if (list_length(env, v).integer != 2)
		return error(env, "`defq' requires two arguments");

	if (car(v).type != VAL_SYMBOL)
		return error(env, "the first argument to `defq'"
		             " must be an identifier");

	return builtin_def(env, cons(env, quote(env, car(v)), cdr(v)));
}

struct value
builtin_setq(struct env *env, struct value v)
{
	/*
	 * Don't need to check `list_length` for errors because `v` is
	 * always a well-formed list and `list_length` cannot fail in
	 * any other case (it doesn't allocate anything).
	 */
 	if (list_length(env, v).integer != 2)
		return error(env, "`setq' requires two arguments");

	if (car(v).type != VAL_SYMBOL)
		return error(env, "the first argument to `setq'"
		             " must be an identifier");

	return builtin_set(env, cons(env, quote(env, car(v)), cdr(v)));
}

#define ARITHMETIC(X)	  \
	int sum = 0, first = 1; \
	for (struct value args = eval_list(env, list); \
	     args.type != VAL_NIL; \
	     args = cdr(args)) { \
		if (args.type == VAL_ERROR) return args; \
		if (car(args).type == VAL_INT) { \
			if (*#X == '/' && car(args).integer == 0) \
				return error(env, \
				             "division by zero is" \
				             " forbidden."); \
			if (first) { \
				sum = car(args).integer; \
				first = 0; \
			} \
			else sum X##= car(args).integer; \
			continue; \
		} \
		return error(env, \
		             "builtin `"#X"' takes only " \
		             "numeric arguments (got `%s')", \
		             TYPE_NAME(car(args).type)); \
	} \
	return (struct value){VAL_INT, {sum}};

struct value
builtin_add(struct env *env, struct value list)
{
	ARITHMETIC(+);
}

struct value
builtin_sub(struct env *env, struct value list)
{
	ARITHMETIC(-);
}

struct value
builtin_mul(struct env *env, struct value list)
{
	ARITHMETIC(*);
}

struct value
builtin_div(struct env *env, struct value list)
{
	ARITHMETIC(/);
}

struct value
builtin_inteq(struct env *env, struct value list)
{
	int sum = 0, first = 1;

	for (struct value args = eval_list(env, list);
	     args.type != VAL_NIL;
	     args = cdr(args)) {
		if (args.type == VAL_ERROR) return args;

		if (car(args).type == VAL_INT) {
			if (first) sum = car(args).integer, first = 0;
			else if (car(args).integer != sum)
				return NIL;
			continue;
		}

		return error(env,
		             "builtin `=' takes only numeric"
		             " arguments (got `%s')",
		             TYPE_NAME(car(args).type));
	}

	return TRUE;
}

struct value
builtin_less(struct env *env, struct value v)
{
	int sum = 0, first = 1;

	for (struct value args = eval_list(env, v);
	     args.type != VAL_NIL;
	     args = cdr(args)) {
		if (args.type == VAL_ERROR) return args;

		if (car(args).type == VAL_INT) {
			if (first) sum = car(args).integer, first = 0;
			else if (car(args).integer >= sum)
				return NIL;
			continue;
		}

		return error(env,
		             "builtin `<' takes only numeric"
		             " arguments (got `%s')",
		             TYPE_NAME(car(args).type));
	}

	return TRUE;
}

struct value
builtin_cond(struct env *env, struct value v)
{
	if (list_length(env, v).integer < 0)
		return error(env, "builtin `cond' requires arguments");

	struct value cond = eval(env, car(v));
	if (cond.type == VAL_ERROR) return cond;
	if (cond.type == VAL_NIL) return NIL;
	return progn(env, cdr(v));
}

struct value
builtin_if(struct env *env, struct value v)
{
	if (v.type == VAL_NIL)
		return error(env, "`if' requires arguments");

	if (cdr(v).type != VAL_CELL)
		return error(env, "missing body");

	struct value cond = eval(env, car(v));

	if (cond.type == VAL_NULL) return VNULL;
	if (cond.type == VAL_ERROR) return cond;

	if (cond.type != VAL_NIL)
		return eval(env, car(cdr(v)));

	/* Otherwise do the else branches. */
	return progn(env, cdr(cdr(v)));
}

struct value
builtin_quote(struct env *env, struct value v)
{
	(void)env;
	if (v.type == VAL_NIL) return NIL;
	return car(v);
}

struct value
builtin_cons(struct env *env, struct value v)
{
	struct value a = eval(env, car(v)),
		b = eval(env, car(cdr(v)));
	if (a.type == VAL_NULL) return VNULL;
	if (a.type == VAL_ERROR) return a;
	if (b.type == VAL_NULL) return VNULL;
	if (b.type == VAL_ERROR) return b;
	return cons(env, a, b);
}

struct value
builtin_car(struct env *env, struct value v)
{
	if (v.type == VAL_NIL) return NIL;
	v = eval(env, car(v));
	if (v.type == VAL_ERROR) return v;
	if (!IS_LIST(v))
		return error(env,
		             "builtin `car' requires a list argument"
		             " (this is %s %s)",
		             IS_VOWEL(*TYPE_NAME(v.type))
		             ? "an" : "a",
		             TYPE_NAME(v.type));
	return v.type == VAL_NIL ? NIL : car(v);
}

struct value
builtin_cdr(struct env *env, struct value v)
{
	if (v.type == VAL_NIL) return NIL;
	v = eval(env, car(v));
	if (v.type == VAL_ERROR) return v;
	if (!IS_LIST(v))
		return error(env,
		             "builtin `cdr' requires a list argument"
		             " (this is %s %s)",
		             IS_VOWEL(*TYPE_NAME(v.type))
		             ? "an" : "a",
		             TYPE_NAME(v.type));

	return v.type == VAL_NIL ? NIL : cdr(v);
}

struct value
builtin_macro(struct env *env, struct value v)
{
	if (v.type == VAL_NIL)
		return error(env, "`defmacro' requires arguments");

	if (cdr(v).type != VAL_CELL)
		return error(env, "missing list of parameters");

	struct value fun = make_function(env, cdr(v), VAL_MACRO);
	if (fun.type == VAL_ERROR) return fun;
	name(fun) = kdgu_copy(string(car(v)));

	return add_variable(env, car(v), fun);
}

struct value
builtin_list(struct env *env, struct value list)
{
	return eval_list(env, list);
}

struct value
builtin_progn(struct env *env, struct value v)
{
	return progn(env, v);
}

struct value
builtin_while(struct env *env, struct value v)
{
	if (v.type == VAL_NIL)
		return error(env, "`while' requires arguments");

	if (cdr(v).type != VAL_CELL)
		return error(env, "missing condition");

	struct value c = NIL, r = NIL;

	while (c = eval(env, car(v)), c.type != VAL_NIL) {
		r = progn(env, cdr(v));
		if (r.type == VAL_ERROR) return r;
	}

	if (c.type == VAL_ERROR) return c;

	return r;
}

struct value
builtin_let(struct env *env, struct value v)
{
	if (list_length(env, v).integer < 1)
		return error(env, "builtin `let' requires"
		             " at least one argument");

	if (car(v).type != VAL_CELL)
		return error(env, "first argument to `let' must"
		             " be an initializer list");

	struct env *newenv = push_env(env, NIL, NIL);

	for (struct value i = car(v); i.type != VAL_NIL; i = cdr(i)) {
		if (car(i).type != VAL_CELL)
			return error(env, "invalid initializer"
			             " in `let'");

		struct value val = eval(newenv, car(cdr(car(i))));
		if (val.type == VAL_ERROR) return val;

		add_variable(newenv, car(car(i)), val);
	}

	return progn(newenv, cdr(v));
}

struct value
builtin_nth(struct env *env, struct value v)
{
	if (v.type != VAL_CELL
	    || cdr(v).type != VAL_CELL
	    || cdr(cdr(v)).type != VAL_NIL)
		return error(env,
		             "builtin `nth' requires two arguments");

	struct value i = eval(env, car(cdr(v)));
	if (i.type == VAL_ERROR) return i;
	if (i.type != VAL_INT)
		return error(env,
		             "builtin `nth' requires a numeric second"
		             " argument (this is %s %s)",
		             IS_VOWEL(*TYPE_NAME(i.type))
		             ? "an" : "a",
		             TYPE_NAME(i.type));

	if (i.integer < 0) return error(env, "index must be positive");

	struct value val = eval(env, car(v));
	if (val.type == VAL_ERROR) return val;
	if (!IS_LIST(val)
	    && val.type != VAL_STRING)
		return error(env,
		             "builtin `nth' requires a list"
		             " or string argument (this is %s %s)",
		             IS_VOWEL(*TYPE_NAME(val.type))
		             ? "an" : "a",
		             TYPE_NAME(val.type));

	/* `val` is either a list or a string now. */

	if (val.type == VAL_NIL) return NIL;

	int j = 0;
	if (IS_LIST(val)) {
		while (j < i.integer) {
			if (cdr(val).type == VAL_NIL) return NIL;
			val = cdr(val);
			j++;
		}

		return car(val);
	}

	unsigned idx = 0;

	for (int j = 0; j < i.integer; j++)
		kdgu_next(string(val), &idx);

	struct value str = gc_alloc(env, VAL_STRING);
	string(str) = kdgu_getchr(string(val), idx);
	return str;
}

struct value
builtin_length(struct env *env, struct value v)
{
	if (v.type == VAL_NIL)
		return error(env, "builtin `length'"
		             " requires arguments");

	if (cdr(v).type != VAL_NIL)
		return error(env, "builtin `length'"
		             " takes only one argument");

	struct value val = eval(env, car(v));

	if (IS_LIST(val)) {
		return list_length(env, val);
	} else if (val.type == VAL_STRING) {
		return (struct value){VAL_INT,{kdgu_len(string(val))}};
	} else {
		return error(env,
		             "builtin `length' takes a list"
		             " or string argument (this is %s %s)",
		             IS_VOWEL(*TYPE_NAME(val.type))
		             ? "an" : "a",
		             TYPE_NAME(val.type));
	}

	/* Unreachable. */

	return NIL;
}

/*
 * TODO: Check up on this. It looks a bit messy.
 */

struct value
builtin_sed(struct env *env, struct value v)
{
	if (list_length(env, v).integer != 4)
		return error(env, "builtin `sed'"
		             " takes four arguments");

	struct value pattern = eval(env, car(v));
	struct value replace = eval(env, car(cdr(v)));
	struct value options = eval(env, car(cdr(cdr(v))));
	struct value subject = eval(env, car(cdr(cdr(cdr(v)))));

	if (pattern.type != VAL_STRING
	    || replace.type != VAL_STRING
	    || options.type != VAL_STRING
	    || subject.type != VAL_STRING)
		return error(env, "malformed `sed' application"
		             " (all arguments must be strings)");

	int opt = KTRE_UNANCHORED;

	for (unsigned i = 0; i < string(options)->len; i++) {
		switch (string(options)->s[i]) {
		case 'g': opt |= KTRE_GLOBAL; break;
		case 'i': opt |= KTRE_INSENSITIVE; break;
		default: return error(env,
		                      "unrecognized mode modifier");
		}
	}

	kdgu *res = ktre_replace(string(subject),
	                         string(pattern),
	                         string(replace),
	                         &KDGU("\\"),
	                         opt);

	if (!res) return NIL;

	struct value str = gc_alloc(env, VAL_STRING);
	string(str) = res;

	return str;
}

struct value
builtin_reverse(struct env *env, struct value v)
{
	if (list_length(env, v).integer != 1)
		return error(env, "builtin `reverse'"
		             " takes one argument");
	v = eval(env, car(v));
	if (v.type == VAL_ERROR) return v;

	struct value err = list_length(env, v);
	if (err.type == VAL_ERROR) return err;

	struct value ret = NIL;

	for (struct value i = v; i.type != VAL_NIL; i = cdr(i))
		ret = cons(env, car(i), ret);

	return ret;
}

struct value
builtin_match(struct env *env, struct value v)
{
	if (list_length(env, v).integer != 3)
		return error(env, "builtin `match' takes"
		             " three arguments");

	v = eval_list(env, v);

	if (car(v).type != VAL_STRING
	    || car(cdr(v)).type != VAL_STRING
	    || car(cdr(cdr(v))).type != VAL_STRING)
		return error(env, "builtin `match' takes"
		             " only string arguments");

	kdgu *pattern = string(car(v));
	int opt = KTRE_UNANCHORED;

	for (unsigned i = 0; i < string(car(cdr(v)))->len; i++) {
		switch (string(car(cdr(v)))->s[i]) {
		case 'g': opt |= KTRE_GLOBAL; break;
		case 'i': opt |= KTRE_INSENSITIVE; break;
		default: return error(env, "unrecognized"
		                      " mode modifier");
		}
	}

	ktre *re = ktre_compile(pattern, opt);
	int **vec;

	if (re->err)
		return error(env, "%s at index %d in ",
		             re->err_str,
		             re->i,
		             tostring(pattern));

	if (!ktre_exec(re, string(car(cdr(cdr(v)))), &vec))
		return NIL;

	struct value ret = NIL;

	for (int i = 0; i < re->num_groups; i++) {
		/* TODO: Deal with multiple matches. */
		kdgu *str = ktre_getgroup(vec, 0, i, string(car(cdr(cdr(v)))));
		if (!str) str = kdgu_news("");
		struct value group = gc_alloc(env, VAL_STRING);
		string(group) = str;
		ret = cons(env, group, ret);
	}

	return builtin_reverse(env, cons(env, quote(env, ret), NIL));
}

struct value
builtin_expand(struct env *env, struct value v)
{
	return expand(env, v);
}

struct value
builtin_eval(struct env *env, struct value v)
{
	if (list_length(env, v).integer != 1)
		return error(env, "builtin `eval' takes"
		             " one argument");
	return eval(env, eval(env, car(v)));
}

static struct value
quickstring(struct env *env, const char *str)
{
	struct value v = gc_alloc(env, VAL_STRING);
	string(v) = kdgu_news(str);
	return v;
}

struct value
builtin_read_string(struct env *env, struct value v)
{
	if (list_length(env, v).integer != 1)
		return error(env, "builtin `read-string'"
		             " takes one argument");

	v = eval_list(env, v);
	if (v.type == VAL_ERROR) return v;
	if (car(v).type != VAL_STRING)
		return error(env, "argument to `read-string'"
		             " must be a string");

	char *s = tostring(string(car(v)));

	struct lexer *lexer = new_lexer("*string*", s);
	struct token *t = tok(lexer);

	if (!t) return NIL;

	if (t->type != '(')
		return error(env, "builtin `read-string' expected"
		             " `(' to begin s-expression in"
		             " string contents");

	struct value expr = parse(env, lexer);
	if (expr.type == VAL_ERROR) return expr;

	return cons(env,
	            expr,
	            quickstring(env, lexer->s + lexer->idx));
}

const char *help = "Birch is an IRC bot with a CL-like Lisp"
	" interface. Learn Common Lisp at "
	"<http://stevelosh.com/blog/2018/08/a-road-to-common-lisp/>"
	" and participate in development at "
	"<https://github.com/KrokodileGlue/birch>.";

struct value
builtin_documentation(struct env *env, struct value v)
{
	if (v.type == VAL_NIL) {
		struct value e = gc_alloc(env, VAL_STRING);
		string(e) = kdgu_news(help);
		return e;
	}

	v = eval(env, car(v));
	if (v.type == VAL_ERROR) return v;
	if (v.type != VAL_FUNCTION && v.type != VAL_MACRO)
		return error(env, "`documentation' requires a"
		             " function argument");

	/* TODO: Ensure that all functions have a docstring. */
	return docstring(v);
}

struct value
builtin_streq(struct env *env, struct value v)
{
	v = eval_list(env, v);
	if (v.type == VAL_ERROR) return v;

	for (struct value list = v;
	     list.type != VAL_NIL;
	     list = cdr(list)) {
		if (cdr(list).type == VAL_NIL) return TRUE;

		int type = car(list).type != VAL_STRING
			? car(list).type : car(cdr(list)).type;

		if (car(list).type == VAL_ERROR)
			return car(list);

		if (car(cdr(list)).type == VAL_ERROR)
			return car(cdr(list));

		if (type != VAL_STRING)
			return error(env, "`string=' takes only"
			             " strings (this is %s %s)",
			             IS_VOWEL(*TYPE_NAME(type))
			             ? "an" : "a",
			             TYPE_NAME(type));

		if (!kdgu_cmp(string(car(list)),
		              string(car(cdr(list))),
		              false,
		              NULL))
			return NIL;
	}

	return TRUE;
}

struct value
builtin_concatenate(struct env *env, struct value v)
{
	if (list_length(env, v).integer < 3)
		return error(env, "builtin `concatenate' takes"
		             " at least three arguments");

	v = eval_list(env, v);

	for (struct value list = v;
	     list.type != VAL_NIL;
	     list = cdr(list)) {
		if (cdr(list).type == VAL_NIL) return TRUE;

		int type = car(list).type != VAL_STRING
			? car(list).type : car(cdr(list)).type;

		if (type != VAL_STRING)
			return error(env, "`concatenate' takes only"
			             " strings (this is %s %s)",
			             IS_VOWEL(*TYPE_NAME(type))
			             ? "an" : "a",
			             TYPE_NAME(type));

		if (!kdgu_cmp(string(car(list)),
		              string(car(cdr(list))),
		              false,
		              NULL))
			return NIL;
	}

	return TRUE;
}

static void
append2(struct env *env, struct value list, struct value v)
{
	struct value k = list;

	while (cdr(k).type != VAL_NIL) {
		if (IS_LIST(k)
		    && cdr(k).type != VAL_NIL
		    && !IS_LIST(cdr(k))) {
			cdr(k) = cons(env, cdr(k), v);
			return;
		}

		k = cdr(k);
	}

	cdr(k) = v;
}

struct value
builtin_backtick(struct env *env, struct value v)
{
	struct value head = gc_copy(env, car(v)),
		tail = head,
		middle = tail;

	while (tail.type != VAL_NIL) {
		if (car(tail).type == VAL_COMMA) {
			struct value tmp = eval(env, keyword(car(tail)));
			if (tmp.type == VAL_ERROR) return tmp;
			tmp = cons(env, tmp, NIL);
			append2(env, tmp, cdr(tail));

			if (middle.obj == tail.obj) {
				head = tmp;
				middle = head;
				tail = cdr(tail);
			} else {
				cdr(middle) = tmp;
				middle = tmp;
				tail = cdr(tail);
			}
		} else if (car(tail).type == VAL_COMMAT) {
			struct value tmp = eval(env, keyword(car(tail)));
			if (tmp.type == VAL_ERROR) return tmp;
			if (tmp.type != VAL_CELL)
				return error(env, "expected a list");

			tmp = gc_copy(env, tmp);
			append2(env, tmp, cdr(tail));

			if (middle.obj == tail.obj) {
				head = tmp;
				middle = head;
				tail = cdr(tail);
			} else {
				cdr(middle) = tmp;
				middle = tmp;
				tail = cdr(tail);
			}
		} else {
			if (car(tail).type == VAL_CELL) {
				car(tail) = builtin_backtick(env, cons(env, car(tail), NIL));
				if (car(tail).type == VAL_ERROR) return car(tail);
			}

			if (tail.obj == middle.obj)
				tail = cdr(tail);
			else {
				tail = cdr(tail);
				middle = cdr(middle);
			}
		}
	}

	return head;
}

struct value
builtin_or(struct env *env, struct value v)
{
	for (struct value i = v; i.type != VAL_NIL; i = cdr(i)) {
		struct value val = eval(env, car(v));
		if (val.type == VAL_ERROR) return val;
		if (val.type == VAL_TRUE) return TRUE;
	}

	return NIL;
}

struct value
builtin_and(struct env *env, struct value v)
{
	for (struct value i = v; i.type != VAL_NIL; i = cdr(i)) {
		struct value val = eval(env, car(i));
		if (val.type == VAL_ERROR) return val;
		if (val.type == VAL_NIL) return NIL;
	}

	return TRUE;
}

struct value
builtin_append(struct env *env, struct value v)
{
	v = eval_list(env, v);
	if (v.type == VAL_ERROR) return v;

	struct value out = gc_alloc(env, VAL_STRING);
	string(out) = kdgu_news("");

	for (struct value i = v; i.type != VAL_NIL; i = cdr(i)) {
		if (car(i).type == VAL_STRING) {
			kdgu_append(string(out), string(car(i)));
			continue;
		}

		struct value val = print_value(env, car(i));
		if (val.type == VAL_ERROR) return val;

		kdgu_append(string(out), string(val));
	}

	return out;
}

struct value
builtin_subseq(struct env *env, struct value v)
{
	int len = list_length(env, v).integer;
	if (len != 2 && len != 3)
		return error(env, "invalid number of arguments"
		             " to builtin `subseq'");
	v = eval_list(env, v);
	if (v.type == VAL_ERROR) return v;

	/* TODO: Lists, vectors, arrays. */
	if (car(v).type != VAL_STRING)
		return NIL;

	if (len == 2) {
		struct value ret = gc_alloc(env, VAL_STRING);
		unsigned a = car(cdr(v)).integer;
		unsigned b = string(car(v))->len;
		string(ret) = kdgu_substr(string(car(v)), a, b);
		if (!string(ret)) string(ret) = kdgu_news("");
		return ret;
	}

	struct value ret = gc_alloc(env, VAL_STRING);
	unsigned a = car(cdr(v)).integer;
	unsigned b = car(cdr(cdr(v))).integer;

	if (b > string(car(v))->len)
		b = string(car(v))->len;

	string(ret) = kdgu_substr(string(car(v)), a, b);
	if (!string(ret)) string(ret) = kdgu_news("");

	return ret;
}

struct value
builtin_eq(struct env *env, struct value v)
{
	if (v.type == VAL_NIL)
		return error(env, "builtin `eq' requires arguments");

	v = eval_list(env, v);

	if (v.type == VAL_ERROR)
		return v;

	/*
	 * Use the first value as the reference value for comparison
	 * against the rest of the list.
	 */
	struct value ref = car(v);

	for (struct value i = cdr(v); i.type != VAL_NIL; i = cdr(i)) {
		struct value cmp = car(i);
		if (ref.type != cmp.type) return NIL;
		if (ref.type == VAL_INT && ref.integer != cmp.integer)
			return NIL;
		else if (ref.obj != cmp.obj)
			return NIL;
	}

	return TRUE;
}

/*
 * TODO: Implement formatted strings.
 */

struct value
builtin_error(struct env *env, struct value v)
{
	if (list_length(env, v).integer != 1)
		return error(env, "builtin `error' takes"
		             " one argument");
	v = eval_list(env, v);
	if (v.type == VAL_ERROR) return v;
	if (car(v).type != VAL_STRING)
		return error(env, "argument to `error'"
		             " must be a string");
	return error(env, tostring(string(car(v))));
}

struct value
builtin_with_demoted_errors(struct env *env, struct value v)
{
	struct value ret = progn(env, v);
	if (ret.type == VAL_ERROR) return print_value(env, ret);
	return ret;
}

#define TYPE_PREDICATE(X,Y)	  \
	struct value \
	builtin_ ## X ## p(struct env *env, struct value v) \
	{ \
		v = eval_list(env, v); \
		if (v.type == VAL_ERROR) return v; \
		for (struct value i = v; \
		     i.type != VAL_NIL; \
		     i = cdr(i)) \
			if (car(i).type != Y) \
				return NIL; \
		return TRUE; \
	}

TYPE_PREDICATE(null,VAL_NULL)
TYPE_PREDICATE(nil,VAL_NIL)
TYPE_PREDICATE(int,VAL_INT)
TYPE_PREDICATE(cell,VAL_CELL)
TYPE_PREDICATE(string,VAL_STRING)
TYPE_PREDICATE(symbol,VAL_SYMBOL)
TYPE_PREDICATE(builtin,VAL_BUILTIN)
TYPE_PREDICATE(function,VAL_FUNCTION)
TYPE_PREDICATE(macro,VAL_MACRO)

void
load_builtins(struct env *env)
{
	add_builtin(env,
	            "with-demoted-errors",
	            builtin_with_demoted_errors);
	add_builtin(env, "documentation", builtin_documentation);
	add_builtin(env, "read-string", builtin_read_string);
	add_builtin(env, "backtick", builtin_backtick);
	add_builtin(env, "subseq",   builtin_subseq);
	add_builtin(env, "reverse", builtin_reverse);
	add_builtin(env, "defq",    builtin_defq);
	add_builtin(env, "def",     builtin_def);
	add_builtin(env, "string=", builtin_streq);
	add_builtin(env, "expand",  builtin_expand);
	add_builtin(env, "length",  builtin_length);
	add_builtin(env, "lambda",  builtin_fn);
	add_builtin(env, "error",   builtin_error);
	add_builtin(env, "progn",   builtin_progn);
	add_builtin(env, "defmacro",builtin_macro);
	add_builtin(env, "macro",   builtin_macro);
	add_builtin(env, "while",   builtin_while);
	add_builtin(env, "quote",   builtin_quote);
	add_builtin(env, "match",   builtin_match);
	add_builtin(env, "cond",    builtin_cond);
	add_builtin(env, "list",    builtin_list);
	add_builtin(env, "cons",    builtin_cons);
	add_builtin(env, "eval",    builtin_eval);
	add_builtin(env, "setq",    builtin_setq);
	add_builtin(env, "let",     builtin_let);
	add_builtin(env, "nth",     builtin_nth);
	add_builtin(env, "set",     builtin_set);
	add_builtin(env, "car",     builtin_car);
	add_builtin(env, "cdr",     builtin_cdr);
	add_builtin(env, "defun",   builtin_fn);
	add_builtin(env, "fn",      builtin_fn);
	add_builtin(env, "eq",      builtin_eq);
	add_builtin(env, "if",      builtin_if);
	add_builtin(env, "+",       builtin_add);
	add_builtin(env, "-",       builtin_sub);
	add_builtin(env, "*",       builtin_mul);
	add_builtin(env, "/",       builtin_div);
	add_builtin(env, "=",       builtin_inteq);
	add_builtin(env, "<",       builtin_less);
	add_builtin(env, "sed",     builtin_sed);
	add_builtin(env, "and",     builtin_and);
	add_builtin(env, "append",  builtin_append);

	add_builtin(env, "nullp",    builtin_nullp);
	add_builtin(env, "nilp",     builtin_nilp);
	add_builtin(env, "intp",     builtin_intp);
	add_builtin(env, "cellp",    builtin_cellp);
	add_builtin(env, "stringp",  builtin_stringp);
	add_builtin(env, "symbolp",  builtin_symbolp);
	add_builtin(env, "builtinp", builtin_builtinp);
	add_builtin(env, "functionp", builtin_functionp);
	add_builtin(env, "macrop",   builtin_macrop);
}
