#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>

#include <kdg/kdgu.h>

#include "lisp.h"
#include "../birch.h"
#include "../util.h"

#include "lex.h"
#include "eval.h"
#include "util.h"
#include "error.h"
#include "builtin.h"
#include "parse.h"
#include "gc.h"

static value
append(struct env *env, value list, value v)
{
	if (!type(list)) return cons(env, v, NIL);
	value k = list;
	while (type(cdr(k))) k = cdr(k);
	cdr(k) = cons(env, v, NIL);
	return list;
}

/*
 * Verifies that `v` is a well-formed function and returns a new
 * function value built from it. `car(v)` is the list of parameters
 * and `cdr(v)` is the body of the function.
 *
 * TODO: Rewrite.
 */

static value
make_function(struct env *env, value v, enum value_type type)
{
	assert(type == VAL_FUNCTION || type == VAL_MACRO);
	assert(type(v));

	if (type(v) != VAL_CELL
	    || !IS_LIST(car(v))
	    || !IS_LIST(cdr(v)))
		return error(env, "malformed function definition");

	value r = gc_alloc(env, type);
	value param = NIL;

	optional(r) = NIL;
	key(r) = NIL;
	rest(r) = NIL;

	if (!IS_LIST(car(v)))
		return error(env, "expected parameter list here");

	for (value p = car(v);
	     type(p) == VAL_CELL;
	     p = cdr(p)) {
		if (type(car(p)) == VAL_SYMBOL) {
			param = append(env, param, car(p));
			continue;
		}

		if (type(car(p)) == VAL_KEYWORD
		    && kdgu_cmp(string(keyword(car(p))),
		                &KDGU("optional"), false, NULL)) {
			value current = p;
			p = cdr(p);

			while (type(car(p)) == VAL_SYMBOL
			       || type(car(p)) == VAL_CELL) {
				if (type(car(p)) == VAL_SYMBOL) {
					optional(r) = append(env, optional(r), cons(env, car(p), NIL));
					param = append(env, param, car(p));
				} else {
					optional(r) = append(env, optional(r), cons(env, car(car(p)), car(cdr(car(p)))));
					param = append(env, param, car(car(p)));
				}

				if (type(cdr(p)) == VAL_CELL) p = cdr(p), current = cdr(current);
				else break;
			}

			if (!type(cdr(p))) break;
			p = current;
			continue;
		}

		if (type(car(p)) == VAL_KEYWORD
		    && kdgu_cmp(string(keyword(car(p))),
		                &KDGU("key"), false, NULL)) {
			value current = p;
			p = cdr(p);

			while (type(car(p)) == VAL_SYMBOL
			       || type(car(p)) == VAL_CELL) {
				if (type(car(p)) == VAL_SYMBOL) {
					key(r) = append(env, key(r), cons(env, car(p), NIL));
					param = append(env, param, car(p));
				} else {
					key(r) = append(env, key(r), cons(env, car(car(p)), car(cdr(car(p)))));
					param = append(env, param, car(car(p)));
				}

				if (type(cdr(p)) == VAL_CELL) p = cdr(p), current = cdr(current);
				else break;
			}

			if (!type(cdr(p))) break;
			p = current;
			continue;
		}

		if (type(car(p)) == VAL_KEYWORD
		    && kdgu_cmp(string(keyword(car(p))),
		                &KDGU("rest"), false, NULL)) {
			if (type(p) != VAL_CELL
			    || type(cdr(p)) != VAL_CELL
			    || type(car(cdr(p))) != VAL_SYMBOL)
				return error(env, "expected a symbol");
			p = cdr(p);
			rest(r) = car(p);
			if (type(cdr(p)))
				return error(env, "expected end of parameter list to follow REST parameter");
			break;
		}

		return error(env,
		             "parameter name must be a symbol"
		             " (this is %s %s)",
		             IS_VOWEL(*TYPE_NAME(type(car(p))))
		             ? "an" : "a",
		             TYPE_NAME(type(car(p))));
	}

	param(r) = param;
	env(r) = env;

	if (type(cdr(v)) == VAL_CELL
	    && type(car(cdr(v))) == VAL_STRING) {
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

value
builtin_fn(struct env *env, value v)
{
	if (!type(v))
		return error(env, "`defun' requires arguments");

	if (type(cdr(v)) != VAL_CELL)
		return error(env, "missing list of parameters");

	/*
	 * If `type(car(v)) != VAL_SYMBOL` then this is an anonymous
	 * function (because it has no name).
	 */

	if (type(car(v)) != VAL_SYMBOL)
		return make_function(env, v, VAL_FUNCTION);

	value sym = car(v);

	/*
	 * Otherwise it's obviously a named function which should be
	 * added to the environment as a variable.
	 */

	value fun = make_function(env, cdr(v), VAL_FUNCTION);
	if (type(fun) == VAL_ERROR) return fun;
	name(fun) = kdgu_copy(string(sym));

	value bind = find(env, sym);

	if (!type(bind))
		return add_variable(env, sym, fun);

	return cdr(bind) = fun;
}

value
builtin_set(struct env *env, value v)
{
	if (type(v) != VAL_CELL || !type(cdr(v)))
		return error(env, "`set' requires two arguments");

	value sym = eval(env, car(v));

	if (type(sym) == VAL_ERROR)
		return sym;

	if (type(sym) != VAL_SYMBOL)
		return error(env, "the first argument to `set'"
		             " must be a symbol");

	value bind = find(env, sym);

	if (!type(bind))
		return error(env, "undeclared identifier in `set'");

	value value = eval(env, car(cdr(v)));

	if (type(value) == VAL_ERROR)
		return value;

	return cdr(bind) = value;
}

value
builtin_def(struct env *env, value v)
{
	if (type(v) != VAL_CELL || !type(cdr(v)))
		return error(env, "`def' requires two arguments");

	value sym = eval(env, car(v));

	if (type(sym) == VAL_ERROR)
		return sym;

	if (type(sym) != VAL_SYMBOL)
		return error(env, "the first argument to `def'"
		             " must be a symbol");

	value value = eval(env, car(cdr(v)));
	return add_variable(env, sym, value);
}

value
builtin_defq(struct env *env, value v)
{
	if (integer(list_length(env, v)) != 2)
		return error(env, "`defq' requires two arguments");

	if (type(car(v)) != VAL_SYMBOL)
		return error(env, "the first argument to `defq'"
		             " must be a symbol");

	return builtin_def(env, cons(env, quote(env, car(v)), cdr(v)));
}

value
builtin_setq(struct env *env, value v)
{
	/*
	 * Don't need to check `list_length` for errors because `v` is
	 * always a well-formed list and `list_length` cannot fail in
	 * any other case (it doesn't allocate anything).
	 */
 	if (integer(list_length(env, v)) != 2)
		return error(env, "`setq' requires two arguments");

	if (type(car(v)) != VAL_SYMBOL)
		return error(env, "the first argument to `setq'"
		             " must be a symbol");

	return builtin_set(env, cons(env, quote(env, car(v)), cdr(v)));
}

#define ARITHMETIC(X)	  \
	int sum = 0, first = 1; \
	for (value args = eval_list(env, list); \
	     type(args); \
	     args = cdr(args)) { \
		if (type(args) == VAL_ERROR) return args; \
		if (type(car(args)) == VAL_INT) { \
			if (*#X == '/' && integer(car(args)) == 0) \
				return error(env, \
				             "division by zero is" \
				             " forbidden."); \
			if (first) { \
				sum = integer(car(args)); \
				first = 0; \
			} \
			else sum X##= integer(car(args)); \
			continue; \
		} \
		return error(env, \
		             "builtin `"#X"' takes only " \
		             "numeric arguments (got `%s')", \
		             TYPE_NAME(type(car(args)))); \
	} \
	return mkint(sum);

value
builtin_add(struct env *env, value list)
{
	ARITHMETIC(+);
}

value
builtin_sub(struct env *env, value list)
{
	ARITHMETIC(-);
}

value
builtin_mul(struct env *env, value list)
{
	ARITHMETIC(*);
}

value
builtin_div(struct env *env, value list)
{
	ARITHMETIC(/);
}

value
builtin_inteq(struct env *env, value list)
{
	int sum = 0, first = 1;

	for (value args = eval_list(env, list);
	     type(args);
	     args = cdr(args)) {
		if (type(args) == VAL_ERROR) return args;

		if (type(car(args)) == VAL_INT) {
			if (first) sum = integer(car(args)), first = 0;
			else if (integer(car(args)) != sum)
				return NIL;
			continue;
		}

		return error(env,
		             "builtin `=' takes only numeric"
		             " arguments (got `%s')",
		             TYPE_NAME(type(car(args))));
	}

	return TRUE;
}

value
builtin_less(struct env *env, value v)
{
	int sum = 0, first = 1;

	for (value args = eval_list(env, v);
	     type(args);
	     args = cdr(args)) {
		if (type(args) == VAL_ERROR) return args;

		if (type(car(args)) == VAL_INT) {
			if (first) sum = integer(car(args)), first = 0;
			else if (integer(car(args)) >= sum)
				return NIL;
			continue;
		}

		return error(env,
		             "builtin `<' takes only numeric"
		             " arguments (got `%s')",
		             TYPE_NAME(type(car(args))));
	}

	return TRUE;
}

value
builtin_more(struct env *env, value v)
{
	int sum = 0, first = 1;

	for (value args = eval_list(env, v);
	     type(args);
	     args = cdr(args)) {
		if (type(args) == VAL_ERROR) return args;

		if (type(car(args)) == VAL_INT) {
			if (first) sum = integer(car(args)), first = 0;
			else if (integer(car(args)) <= sum)
				return NIL;
			continue;
		}

		return error(env,
		             "builtin `<' takes only numeric"
		             " arguments (got `%s')",
		             TYPE_NAME(type(car(args))));
	}

	return TRUE;
}

value
builtin_mod(struct env *env, value v)
{
	v = eval_list(env, v);
	if (type(v) == VAL_ERROR) return v;
	if (integer(list_length(env, v)) != 2)
		return error(env, "builtin `%' takes two arguments");
	if (type(car(v)) != VAL_INT || type(car(cdr(v))) != VAL_INT)
		return error(env, "builtin `%' takes only"
		             " numeric arguments");
	return mkint(integer(car(v)) % integer(car(cdr(v))));
}

value
builtin_cond(struct env *env, value v)
{
	if (integer(list_length(env, v)) < 0)
		return error(env, "builtin `cond' requires arguments");

	value cond = eval(env, car(v));
	if (type(cond) == VAL_ERROR) return cond;
	if (!type(cond)) return NIL;
	return progn(env, cdr(v));
}

value
builtin_if(struct env *env, value v)
{
	if (!type(v))
		return error(env, "`if' requires arguments");

	if (type(cdr(v)) != VAL_CELL)
		return error(env, "missing body");

	value cond = eval(env, car(v));

	if (type(cond) == VAL_ERROR)
		return cond;

	if (type(cond))
		return eval(env, car(cdr(v)));

	/* Otherwise do the else branches. */
	return progn(env, cdr(cdr(v)));
}

value
builtin_quote(struct env *env, value v)
{
	(void)env;
	if (!type(v)) return v;
	return car(v);
}

value
builtin_cons(struct env *env, value v)
{
	value a = eval(env, car(v)),
		b = eval(env, car(cdr(v)));
	if (type(a) == VAL_ERROR) return a;
	if (type(b) == VAL_ERROR) return b;
	return cons(env, a, b);
}

value
builtin_car(struct env *env, value v)
{
	if (!type(v)) return NIL;
	v = eval(env, car(v));
	if (type(v) == VAL_ERROR) return v;
	if (!IS_LIST(v))
		return error(env,
		             "builtin `car' requires a list argument"
		             " (this is %s %s)",
		             IS_VOWEL(*TYPE_NAME(type(v)))
		             ? "an" : "a",
		             TYPE_NAME(type(v)));
	return type(v) ? car(v) : v;
}

value
builtin_cdr(struct env *env, value v)
{
	if (!type(v)) return NIL;
	v = eval(env, car(v));
	if (type(v) == VAL_ERROR) return v;
	if (!IS_LIST(v))
		return error(env,
		             "builtin `cdr' requires a list argument"
		             " (this is %s %s)",
		             IS_VOWEL(*TYPE_NAME(type(v)))
		             ? "an" : "a",
		             TYPE_NAME(type(v)));

	return type(v) ? cdr(v) : v;
}

value
builtin_macro(struct env *env, value v)
{
	if (!type(v))
		return error(env, "`defmacro' requires arguments");

	if (type(cdr(v)) != VAL_CELL)
		return error(env, "missing list of parameters");

	value fun = make_function(env, cdr(v), VAL_MACRO);
	if (type(fun) == VAL_ERROR) return fun;
	name(fun) = kdgu_copy(string(car(v)));

	return add_variable(env, car(v), fun);
}

value
builtin_list(struct env *env, value list)
{
	return eval_list(env, list);
}

value
builtin_progn(struct env *env, value v)
{
	return progn(env, v);
}

value
builtin_while(struct env *env, value v)
{
	if (!type(v))
		return error(env, "`while' requires arguments");

	if (type(cdr(v)) != VAL_CELL)
		return error(env, "missing condition");

	value c = NIL, r = NIL;

	while (c = eval(env, car(v)), type(c)) {
		r = progn(env, cdr(v));
		if (type(r) == VAL_ERROR) return r;
	}

	if (type(c) == VAL_ERROR) return c;

	return r;
}

value
builtin_let(struct env *env, value v)
{
	if (integer(list_length(env, v)) < 1)
		return error(env, "builtin `let' requires"
		             " at least one argument");

	if (type(car(v)) != VAL_CELL)
		return error(env, "first argument to `let' must"
		             " be an initializer list");

	struct env *newenv = push_env(env, NIL, NIL);

	for (value i = car(v); type(i); i = cdr(i)) {
		if (type(car(i)) != VAL_CELL)
			return error(env, "invalid initializer"
			             " in `let'");

		value sym = car(car(i));
		value val = eval(newenv, car(cdr(car(i))));
		if (type(val) == VAL_ERROR) return val;
		add_variable(newenv, sym, val);
	}

	return progn(newenv, cdr(v));
}

value
builtin_nth(struct env *env, value v)
{
	if (type(v) != VAL_CELL
	    || type(cdr(v)) != VAL_CELL
	    || type(cdr(cdr(v))))
		return error(env,
		             "builtin `nth' requires two arguments");

	value i = eval(env, car(cdr(v)));
	if (type(i) == VAL_ERROR) return i;
	if (type(i) != VAL_INT)
		return error(env,
		             "builtin `nth' requires a numeric second"
		             " argument (this is %s %s)",
		             IS_VOWEL(*TYPE_NAME(type(i)))
		             ? "an" : "a",
		             TYPE_NAME(type(i)));

	if (integer(i) < 0) return error(env, "index must be positive");

	value val = eval(env, car(v));
	if (type(val) == VAL_ERROR) return val;
	if (!IS_LIST(val)
	    && type(val) != VAL_STRING)
		return error(env,
		             "builtin `nth' requires a list"
		             " or string argument (this is %s %s)",
		             IS_VOWEL(*TYPE_NAME(type(val)))
		             ? "an" : "a",
		             TYPE_NAME(type(val)));

	/* `val` is either a list or a string now. */

	if (!type(val)) return val;

	int j = 0;
	if (IS_LIST(val)) {
		while (j < integer(i)) {
			if (!type(cdr(val))) return NIL;
			val = cdr(val);
			j++;
		}

		return car(val);
	}

	unsigned idx = 0;

	for (int j = 0; j < integer(i); j++)
		kdgu_next(string(val), &idx);

	value str = gc_alloc(env, VAL_STRING);
	string(str) = kdgu_getchr(string(val), idx);
	return str;
}

value
builtin_length(struct env *env, value v)
{
	if (!type(v))
		return error(env, "builtin `length'"
		             " requires arguments");

	if (type(cdr(v)))
		return error(env, "builtin `length'"
		             " takes only one argument");

	value val = eval(env, car(v));

	if (IS_LIST(val)) {
		return list_length(env, val);
	} else if (type(val) == VAL_STRING) {
		return mkint(kdgu_len(string(val)));
	} else {
		return error(env,
		             "builtin `length' takes a list"
		             " or string argument (this is %s %s)",
		             IS_VOWEL(*TYPE_NAME(type(val)))
		             ? "an" : "a",
		             TYPE_NAME(type(val)));
	}

	/* Unreachable. */

	return NIL;
}

/*
 * TODO: Check up on this. It looks a bit messy.
 */

value
builtin_sed(struct env *env, value v)
{
	if (integer(list_length(env, v)) != 4)
		return error(env, "builtin `sed'"
		             " takes four arguments");

	v = eval_list(env, v);
	if (type(v) == VAL_ERROR) return v;

	value pattern = car(v);
	value replace = car(cdr(v));
	value options = car(cdr(cdr(v)));
	value subject = car(cdr(cdr(cdr(v))));

	if (type(pattern) != VAL_STRING
	    || type(replace) != VAL_STRING
	    || type(options) != VAL_STRING
	    || type(subject) != VAL_STRING)
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

	if (!res) return subject;

	value str = gc_alloc(env, VAL_STRING);
	string(str) = res;

	return str;
}

value
builtin_reverse(struct env *env, value v)
{
	if (integer(list_length(env, v)) != 1)
		return error(env, "builtin `reverse'"
		             " takes one argument");
	v = eval(env, car(v));
	if (type(v) == VAL_ERROR) return v;

	value err = list_length(env, v);
	if (type(err) == VAL_ERROR) return err;

	value ret = NIL;

	for (value i = v; type(i); i = cdr(i))
		ret = cons(env, car(i), ret);

	return ret;
}

value
builtin_match(struct env *env, value v)
{
	if (integer(list_length(env, v)) != 3)
		return error(env, "builtin `match' takes"
		             " three arguments");

	v = eval_list(env, v);
	if (type(v) == VAL_ERROR) return v;

	for (value i = v; type(i); i = cdr(i))
		if (type(car(i)) != VAL_STRING)
			return error(env, "builtin `match' takes"
			             " only string arguments"
			             " (this is %s %s)",
			             IS_VOWEL(*TYPE_NAME(type(car(i))))
			             ? "an" : "a",
			             TYPE_NAME(type(car(i))));

	kdgu *pattern = string(car(v));
	int opt = KTRE_UNANCHORED;

	for (unsigned i = 0; i < string(car(cdr(v)))->len; i++) {
		switch (string(car(cdr(v)))->s[i]) {
		case 'g': opt |= KTRE_GLOBAL; break;
		case 'i': opt |= KTRE_INSENSITIVE; break;
		default:
			return error(env, "unrecognized"
			             " mode modifier");
		}
	}

	ktre *re = ktre_compile(pattern, opt);
	int **vec;

	if (re->err)
		return error(env, "%s at index %d in %s",
		             re->err_str,
		             re->i,
		             tostring(pattern));

	if (!ktre_exec(re, string(car(cdr(cdr(v)))), &vec))
		return NIL;

	value ret = NIL;

	for (int i = 0; i < re->num_groups; i++) {
		/* TODO: Deal with multiple matches. */
		kdgu *str = ktre_getgroup(vec, 0, i, string(car(cdr(cdr(v)))));
		if (!str) str = kdgu_news("");
		value group = gc_alloc(env, VAL_STRING);
		string(group) = str;
		ret = cons(env, group, ret);
	}

	return builtin_reverse(env, cons(env, quote(env, ret), NIL));
}

value
builtin_expand(struct env *env, value v)
{
	return expand(env, v);
}

value
builtin_eval(struct env *env, value v)
{
	if (integer(list_length(env, v)) != 1)
		return error(env, "builtin `eval' takes"
		             " one argument");
	return eval(env, eval(env, car(v)));
}

static value
quickstring(struct env *env, const char *str)
{
	value v = gc_alloc(env, VAL_STRING);
	string(v) = kdgu_news(str);
	return v;
}

value
builtin_read_string(struct env *env, value v)
{
	if (integer(list_length(env, v)) != 1)
		return error(env, "builtin `read-string'"
		             " takes one argument");

	v = eval_list(env, v);
	if (type(v) == VAL_ERROR) return v;
	if (type(car(v)) != VAL_STRING)
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

	value expr = parse(env, lexer);
	if (type(expr) == VAL_ERROR) return expr;

	return cons(env,
	            expr,
	            quickstring(env, lexer->s + lexer->idx));
}

const char *help = "Birch is an IRC bot with a CL-like Lisp"
	" interface. Learn Common Lisp at "
	"<http://stevelosh.com/blog/2018/08/a-road-to-common-lisp/>"
	" and participate in development at "
	"<https://github.com/KrokodileGlue/birch>.";

value
builtin_documentation(struct env *env, value v)
{
	if (!type(v)) {
		value e = gc_alloc(env, VAL_STRING);
		string(e) = kdgu_news(help);
		return e;
	}

	v = eval(env, car(v));
	if (type(v) == VAL_ERROR) return v;
	if (type(v) != VAL_FUNCTION && type(v) != VAL_MACRO)
		return error(env, "`documentation' requires a"
		             " function argument");

	/* TODO: Ensure that all functions have a docstring. */
	return docstring(v);
}

value
builtin_streq(struct env *env, value v)
{
	v = eval_list(env, v);
	if (type(v) == VAL_ERROR) return v;

	for (value list = v; type(list); list = cdr(list)) {
		if (!type(cdr(list))) return TRUE;

		int type = type(car(list)) != VAL_STRING
			? type(car(list)) : type(car(cdr(list)));

		if (type(car(list)) == VAL_ERROR)
			return car(list);

		if (type(car(cdr(list))) == VAL_ERROR)
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

value
builtin_concatenate(struct env *env, value v)
{
	if (integer(list_length(env, v)) < 3)
		return error(env, "builtin `concatenate' takes"
		             " at least three arguments");

	v = eval_list(env, v);

	for (value list = v; type(list); list = cdr(list)) {
		if (!type(cdr(list))) return TRUE;

		int type = type(car(list)) != VAL_STRING
			? type(car(list)) : type(car(cdr(list)));

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
append2(struct env *env, value list, value v)
{
	value k = list;

	while (type(cdr(k))) {
		if (IS_LIST(k) && type(cdr(k)) && !IS_LIST(cdr(k))) {
			cdr(k) = cons(env, cdr(k), v);
			return;
		}

		k = cdr(k);
	}

	cdr(k) = v;
}

value
builtin_backtick(struct env *env, value v)
{
	value head = gc_copy(env, car(v)),
		tail = head,
		middle = tail;

	while (type(tail)) {
		if (type(car(tail)) == VAL_COMMA) {
			value tmp = eval(env, keyword(car(tail)));
			if (type(tmp) == VAL_ERROR) return tmp;
			tmp = cons(env, tmp, NIL);
			append2(env, tmp, cdr(tail));

			if (middle == tail) {
				head = tmp;
				middle = head;
				tail = cdr(tail);
			} else {
				cdr(middle) = tmp;
				middle = tmp;
				tail = cdr(tail);
			}
		} else if (type(car(tail)) == VAL_COMMAT) {
			value tmp = eval(env, keyword(car(tail)));
			if (type(tmp) == VAL_ERROR) return tmp;
			if (type(tmp) != VAL_CELL)
				return error(env, "expected a list");

			tmp = gc_copy(env, tmp);
			append2(env, tmp, cdr(tail));

			if (middle == tail) {
				head = tmp;
				middle = head;
				tail = cdr(tail);
			} else {
				cdr(middle) = tmp;
				middle = tmp;
				tail = cdr(tail);
			}
		} else {
			if (type(car(tail)) == VAL_CELL) {
				car(tail) = builtin_backtick(env, cons(env, car(tail), NIL));
				if (type(car(tail)) == VAL_ERROR) return car(tail);
			}

			if (tail == middle)
				tail = cdr(tail);
			else {
				tail = cdr(tail);
				middle = cdr(middle);
			}
		}
	}

	return head;
}

value
builtin_and(struct env *env, value v)
{
	for (value i = v; type(i); i = cdr(i)) {
		value val = eval(env, car(i));
		if (!type(val) || type(val) == VAL_ERROR)
			return val;
	}

	return TRUE;
}

value
builtin_or(struct env *env, value v)
{
	for (value i = v; type(i); i = cdr(i)) {
		value val = eval(env, car(i));
		if (type(val) || type(val) == VAL_ERROR)
			return val;
	}

	return NIL;
}

value
builtin_append(struct env *env, value v)
{
	v = eval_list(env, v);
	if (type(v) == VAL_ERROR) return v;

	value out = gc_alloc(env, VAL_STRING);
	string(out) = kdgu_news("");

	for (value i = v; type(i); i = cdr(i)) {
		if (type(car(i)) == VAL_STRING) {
			kdgu_append(string(out), string(car(i)));
			continue;
		}

		value val = print_value(env, car(i));
		if (type(val) == VAL_ERROR) return val;

		kdgu_append(string(out), string(val));
	}

	return out;
}

value
builtin_subseq(struct env *env, value v)
{
	int len = integer(list_length(env, v));
	if (len != 2 && len != 3)
		return error(env, "invalid number of arguments"
		             " to builtin `subseq'");
	v = eval_list(env, v);
	if (type(v) == VAL_ERROR) return v;

	/* TODO: Lists, vectors, arrays. */
	if (type(car(v)) != VAL_STRING)
		return NIL;

	if (len == 2) {
		value ret = gc_alloc(env, VAL_STRING);
		unsigned a = integer(car(cdr(v)));
		unsigned b = string(car(v))->len;
		string(ret) = kdgu_substr(string(car(v)), a, b);
		if (!string(ret)) string(ret) = kdgu_news("");
		return ret;
	}

	value ret = gc_alloc(env, VAL_STRING);
	unsigned a = integer(car(cdr(v)));
	unsigned b = integer(car(cdr(cdr(v))));

	if (b > string(car(v))->len)
		b = string(car(v))->len;

	string(ret) = kdgu_substr(string(car(v)), a, b);
	if (!string(ret)) string(ret) = kdgu_news("");

	return ret;
}

value
builtin_eq(struct env *env, value v)
{
	if (!type(v))
		return error(env, "builtin `eq' requires arguments");

	v = eval_list(env, v);

	if (type(v) == VAL_ERROR)
		return v;

	/*
	 * Use the first value as the reference value for comparison
	 * against the rest of the list.
	 */
	value ref = car(v);

	for (value i = cdr(v); type(i); i = cdr(i)) {
		value cmp = car(i);
		if (type(ref) != type(cmp)) return NIL;
		if (type(ref) == VAL_INT
		    && integer(ref) != integer(cmp))
			return NIL;
		else if (ref != cmp)
			return NIL;
	}

	return TRUE;
}

/*
 * TODO: Implement formatted strings.
 */

value
builtin_error(struct env *env, value v)
{
	if (integer(list_length(env, v)) != 1)
		return error(env, "builtin `error' takes"
		             " one argument");
	v = eval_list(env, v);
	if (type(v) == VAL_ERROR) return v;
	if (type(car(v)) != VAL_STRING)
		return error(env, "argument to `error'"
		             " must be a string");
	return error(env, tostring(string(car(v))));
}

value
builtin_with_demoted_errors(struct env *env, value v)
{
	value ret = progn(env, v);
	if (type(ret) == VAL_ERROR) return print_value(env, ret);
	return ret;
}

value
builtin_boundp(struct env *env, value v)
{
	if (integer(list_length(env, v)) != 1)
		return error(env, "builtin `boundp'"
		             " takes one argument");
	v = eval(env, car(v));
	if (type(v) == VAL_ERROR) return v;
	if (type(v) != VAL_SYMBOL)
		return error(env, "argument to `boundp'"
		             " must be a symbol");
	return type(find(env, v)) ? TRUE : NIL;
}

#define TYPE_PREDICATE(X,Y)	  \
	value \
	builtin_ ## X ## p(struct env *env, value v) \
	{ \
		v = eval_list(env, v); \
		if (type(v) == VAL_ERROR) return v; \
		for (value i = v; type(i); i = cdr(i)) \
			if (type(car(i)) != Y) \
				return NIL; \
		return TRUE; \
	}

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
	add_builtin(env, ">",       builtin_more);
	add_builtin(env, "%",       builtin_mod);
	add_builtin(env, "sed",     builtin_sed);
	add_builtin(env, "and",     builtin_and);
	add_builtin(env, "or",      builtin_or);
	add_builtin(env, "append",  builtin_append);

	add_builtin(env, "nilp",     builtin_nilp);
	add_builtin(env, "intp",     builtin_intp);
	add_builtin(env, "cellp",    builtin_cellp);
	add_builtin(env, "stringp",  builtin_stringp);
	add_builtin(env, "symbolp",  builtin_symbolp);
	add_builtin(env, "builtinp", builtin_builtinp);
	add_builtin(env, "functionp", builtin_functionp);
	add_builtin(env, "macrop",   builtin_macrop);

	add_builtin(env, "boundp",   builtin_boundp);
}
