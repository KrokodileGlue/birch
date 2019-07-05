#include <string.h>
#include <assert.h>
#include <kdg/kdgu.h>

#include "../util.h"
#include "lex.h"
#include "lisp.h"
#include "error.h"
#include "eval.h"
#include "util.h"
#include "gc.h"

/*
 * Evaluates each element in `list` and returns the result of the last
 * evaluation. If evaluating an element results in an error,
 * evaluation is stopped and the error is returned. Returns nil if
 * given the empty list.
 */

struct value
progn(struct env *env, struct value list)
{
	struct value r = NIL;

	for (struct value lp = list;
	     lp.type != VAL_NIL;
	     lp = cdr(lp)) {
		r = eval(env, car(lp));
		if (r.type == VAL_ERROR) return r;
	}

	return r;
}

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
 * Executes the body of the function or builtin `fn`. Returns an error
 * if `fn` is not a function or builtin.
 *
 * N.B. this function handles macros, but only after they've been
 * expanded.
 */

static struct value
apply(struct env *env,
      struct value fn,
      struct value fnargs)
{
	if (fn.type == VAL_BUILTIN)
		return (builtin(fn))(env, fnargs);

	/* If it's not a builtin it must be a function. */

	if (fn.type != VAL_FUNCTION)
		return error(env, "function application requires"\
		             " a function value (this is %s %s)",
		             IS_VOWEL(*TYPE_NAME(fn.type))
		             ? "an" : "a",
		             TYPE_NAME(fn.type));;

	struct value args = NIL, keys = NIL;

	for (struct value arg = fnargs;
	     arg.type != VAL_NIL;
	     arg = cdr(arg)) {
		if (car(arg).type == VAL_KEYWORDPARAM) {
			keys = append(env, keys,
			              cons(env, obj(car(arg)).keyword,
			                   car(cdr(arg))));
			arg = cdr(arg);
		} else {
			args = append(env, args, car(arg));
		}
	}

	/* This is kinda complicated. */

	if (list_length(env, args).integer
	    < list_length(env, obj(fn).param).integer
	    - list_length(env, obj(fn).optional).integer
	    - list_length(env, obj(fn).key).integer)
		return error(env, "invalid number of arguments");

	if (list_length(env, args).integer
	    > list_length(env, obj(fn).param).integer
	    && obj(fn).rest.type == VAL_NIL)
		return error(env, "too many arguments");

	args = eval_list(env, args);
	if (args.type == VAL_ERROR) return args;
	struct env *newenv = push_env(env, obj(fn).param, args);

	for (struct value opt = obj(fn).optional;
	     opt.type != VAL_NIL;
	     opt = cdr(opt)) {
		struct value bind = find(newenv, car(car(opt)));

		if (bind.type == VAL_NIL)
			add_variable(newenv, car(car(opt)), cdr(car(opt)));
		else if (cdr(bind).type == VAL_NIL)
			cdr(bind) = cdr(car(opt));
	}

	for (struct value key = obj(fn).key;
	     key.type != VAL_NIL;
	     key = cdr(key)) {
		struct value bind = find(newenv, car(car(key)));
		if (bind.type == VAL_NIL)
			add_variable(newenv, car(car(key)), car(car(key)));
		else if (cdr(bind).type == VAL_NIL)
			cdr(bind) = cdr(car(key));
	}

	if (obj(fn).rest.type == VAL_NIL) {
		struct value p = obj(fn).param, q = args;

		while (p.type != VAL_NIL) {
			p = cdr(p), q = cdr(q);
			if (q.type == VAL_NIL) break;
		}

		if (q.type != VAL_NIL)
			add_variable(newenv, obj(fn).rest, q);
		else
			add_variable(newenv, obj(fn).rest, NIL);
	}

	for (struct value key = keys;
	     key.type != VAL_NIL;
	     key = cdr(key)) {
		struct value bind = find(newenv, car(car(key)));
		if (bind.type == VAL_NIL)
			add_variable(newenv, car(car(key)), cdr(car(key)));
		else cdr(bind) = cdr(car(key));
	}

	return progn(newenv, obj(fn).body);
}

/*
 * Evaluates every element of `list` in order and returns a new list
 * containing the resulting values. If evaluating an element results
 * in an error, evaluation is stopped and the error is returned.
 */

struct value
eval_list(struct env *env, struct value list)
{
	struct value head = NIL, tail = NIL;

	for (struct value l = list;
	     l.type != VAL_NIL;
	     l = cdr(l)) {
		if (!IS_LIST(l) || !IS_LIST(cdr(l)))
			return list_length(env, l);
		struct value tmp = eval(env, car(l));
		if (tmp.type == VAL_NIL) return NIL;
		if (tmp.type == VAL_ERROR) return tmp;
		if (head.type == VAL_NIL) {
			head = tail = cons(env, tmp, NIL);
			continue;
		}
		cdr(tail) = cons(env, tmp, NIL);
		tail = cdr(tail);
	}

	return head;
}

/*
 * Evaluates a node and returns the result.
 */

struct value
eval(struct env *env, struct value v)
{
	switch (v.type) {
	/*
	 * These are values that don't require any further
	 * interpretation.
	 */

	case VAL_INT:     case VAL_STRING:
	case VAL_BUILTIN: case VAL_FUNCTION:
	case VAL_ERROR:   case VAL_ARRAY:
	case VAL_TRUE:    case VAL_NIL:
	case VAL_KEYWORD: case VAL_KEYWORDPARAM:
		return v;

	case VAL_COMMA: case VAL_COMMAT:
		return error(env, "stray comma outside of"
		             " backtick expression");
		break;

	/*
	 * Since this node is not being evaluated as a list, it must
	 * be a function or builtin call.
	 */

	case VAL_CELL: {
		struct value expanded = expand(env, v);
		if (expanded.obj != v.obj) return eval(env, expanded);
		struct value fn = eval(env, car(v));
		struct value args = cdr(v);
		if (fn.type == VAL_ERROR) return fn;
		return apply(env, fn, args);
	}

	/*
	 * Evaluating a bare symbol means we should look up what it
	 * means and return that.
	 */

	case VAL_SYMBOL: {
		struct value bind = find(env, v);
		if (bind.type != VAL_NIL) return cdr(bind);
		return error(env, "undeclared identifier `%s'", tostring(string(v)));
	}

	/* This should never happen. */

	default:
		return error(env, "bug: unimplemented evaluator");
	}

	return error(env, "bug: unreachable");
}
