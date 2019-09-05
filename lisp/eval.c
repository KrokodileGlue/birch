#include <string.h>
#include <assert.h>

#include <kdg/kdgu.h>

#include "lisp.h"

#include "../birch.h"
#include "../util.h"

#include "lex.h"
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

value
progn(struct env *env, value list)
{
	value r = NIL;

	for (value lp = list;
	     type(lp) != VAL_NIL;
	     lp = cdr(lp)) {
		r = eval(env, car(lp));
		if (type(r) == VAL_ERROR)
			return r;
	}

	return r;
}

static value
append(struct env *env, value list, value v)
{
	if (type(list) == VAL_NIL) return cons(env, v, NIL);
	value k = list;
	while (type(cdr(k)) != VAL_NIL) k = cdr(k);
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

static value
apply(struct env *env,
      value fn,
      value fnargs)
{
	if (type(fn) == VAL_BUILTIN)
		return (builtin(fn))(env, fnargs);

	/* If it's not a builtin it must be a function. */

	if (type(fn) != VAL_FUNCTION)
		return error(env, "function application requires"\
		             " a function value (this is %s %s)",
		             IS_VOWEL(*TYPE_NAME(type(fn)))
		             ? "an" : "a",
		             TYPE_NAME(type(fn)));;

	value args = NIL, keys = NIL;

	for (value arg = fnargs;
	     type(arg) != VAL_NIL;
	     arg = cdr(arg)) {
		if (type(car(arg)) == VAL_KEYWORDPARAM) {
			keys = append(env, keys,
			              cons(env, keyword(car(arg)),
			                   car(cdr(arg))));
			arg = cdr(arg);
		} else {
			args = append(env, args, car(arg));
		}
	}

	/* This is kinda complicated. */

	if (integer(list_length(env, args))
	    < integer(list_length(env, function(fn).param))
	    - integer(list_length(env, function(fn).optional))
	    - integer(list_length(env, function(fn).key)))
	    return error(env, "invalid number of arguments");

	if (integer(list_length(env, args))
	    > integer(list_length(env, function(fn).param))
	    && type(rest(fn)) == VAL_NIL)
		return error(env, "too many arguments");

	args = eval_list(env, args);
	if (type(args) == VAL_ERROR) return args;
	struct env *newenv = push_env(env, function(fn).param, args);

	for (value opt = function(fn).optional;
	     type(opt) != VAL_NIL;
	     opt = cdr(opt)) {
		value bind = find(newenv, car(car(opt)));

		if (type(bind) == VAL_NIL)
			add_variable(newenv, car(car(opt)), cdr(car(opt)));
		else if (type(cdr(bind)) == VAL_NIL)
			cdr(bind) = cdr(car(opt));
	}

	for (value key = function(fn).key;
	     type(key) != VAL_NIL;
	     key = cdr(key)) {
		value bind = find(newenv, car(car(key)));
		if (type(bind) == VAL_NIL)
			add_variable(newenv, car(car(key)), car(car(key)));
		else if (type(cdr(bind)) == VAL_NIL)
			cdr(bind) = cdr(car(key));
	}

	if (type(rest(fn)) != VAL_NIL) {
		value p = function(fn).param, q = args;

		while (type(p) != VAL_NIL) {
			p = cdr(p), q = cdr(q);
			if (type(q) == VAL_NIL) break;
		}

		if (type(q) != VAL_NIL)
			add_variable(newenv, function(fn).rest, q);
		else
			add_variable(newenv, function(fn).rest, NIL);
	}

	for (value key = keys;
	     type(key) != VAL_NIL;
	     key = cdr(key)) {
		value bind = find(newenv, car(car(key)));
		if (type(bind) == VAL_NIL)
			add_variable(newenv, car(car(key)), cdr(car(key)));
		else cdr(bind) = cdr(car(key));
	}

	return progn(newenv, function(fn).body);
}

/*
 * Evaluates every element of `list` in order and returns a new list
 * containing the resulting values. If evaluating an element results
 * in an error, evaluation is stopped and the error is returned.
 */

value
eval_list(struct env *env, value list)
{
	value head = NIL, tail = NIL;

	for (value l = list;
	     type(l) != VAL_NIL;
	     l = cdr(l)) {
		if (!IS_LIST(l) || !IS_LIST(cdr(l)))
			return list_length(env, l);

		value tmp = eval(env, car(l));

		if (type(tmp) == VAL_ERROR)
			return tmp;

		if (type(head) == VAL_NIL) {
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

value
eval(struct env *env, value v)
{
	env->birch->env->depth++;

	if (env->birch->env->recursion_limit > 0
	    && env->birch->env->depth
	    >= env->birch->env->recursion_limit) {
		env->birch->env->depth--;
		return error(env, "s-expression too complicated");
	}

	value ret = NIL;

	switch (type(v)) {
	/*
	 * These are values that don't require any further
	 * interpretation.
	 */

	case VAL_INT:     case VAL_STRING:
	case VAL_BUILTIN: case VAL_FUNCTION:
	case VAL_ERROR:   case VAL_TRUE:
	case VAL_NIL:     case VAL_KEYWORD:
	case VAL_KEYWORDPARAM:
		ret = v;
		break;

	case VAL_COMMA: case VAL_COMMAT:
		ret = error(env, "stray comma outside of"
		            " backtick expression");
		break;

	/*
	 * Since this node is not being evaluated as a list, it must
	 * be a function or builtin call.
	 */

	case VAL_CELL: {
		value expanded = expand(env, v);

		if (expanded != v) {
			ret = eval(env, expanded);
			break;
		}

		value fn = eval(env, car(v));
		value args = cdr(v);

		if (type(fn) == VAL_ERROR) {
			ret = fn;
			break;
		}

		ret = apply(env, fn, args);
		break;
	}

	/*
	 * Evaluating a bare symbol means we should look up what it
	 * means and return that.
	 */

	case VAL_SYMBOL: {
		value bind = find(env, v);

		if (type(bind) == VAL_NIL) {
			ret = error(env, "evaluation of unbound symbol `%s'",
			            tostring(string(v)));
			break;
		}

		ret = cdr(bind);
		break;
	}

	/* This should never happen. */

	default:
		ret = error(env, "bug: unimplemented evaluator");
	}

	env->birch->env->depth--;

	return ret;
}

value
eval_string(struct env *env, const char *code)
{
	struct lexer *lexer = new_lexer("*string*", code);
	struct token *t = tok(lexer);

	if (t->type != '(')
		return error(env, "malformed s-expression in"
		             " call to eval_string");

	#include "parse.h"

	value v = parse(env, lexer);
	if (type(v) == VAL_ERROR) return v;
	return eval(env, v);
}
