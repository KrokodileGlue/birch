#include <string.h>
#include <assert.h>

#include "error.h"
#include "eval.h"
#include "lisp.h"
#include "util.h"

/*
 * Evaluates each element in `list` and returns the result of the last
 * evaluation. If evaluating an element results in an error,
 * evaluation is stopped and the error is returned. Returns nil if
 * given the empty list.
 */

struct value *
progn(struct value *env, struct value *list)
{
	struct value *r = NULL;

	for (struct value *lp = list;
	     lp->type != VAL_NIL;
	     lp = lp->cdr) {
		r = eval(env, lp->car);
		if (r && r->type == VAL_ERROR) return r;
	}

	return r ? r : Nil;
}

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
 * Executes the body of the function or builtin `fn`. Returns an error
 * if `fn` is not a function or builtin.
 *
 * N.B. this function handles macros, but only after they've been
 * expanded.
 */

static struct value *
apply(struct value *env,
      struct location *loc,
      struct value *fn,
      struct value *fnargs)
{
	if (fn->type == VAL_BUILTIN)
		return fn->prim(env, fnargs);

	/* If it's not a builtin it must be a function. */

	if (fn->type != VAL_FUNCTION) {
		struct value *e =
			error(loc, "function application requires "\
			      "a function value (this is %s %s)",
			      IS_VOWEL(*TYPE_NAME(fn->type))
			      ? "an" : "a",
			      TYPE_NAME(fn->type));
		/* e->cdr = error(fn->loc, "last defined here"); */
		/* e->cdr->type = VAL_NOTE; */
		return e;
	}

	struct value *args = Nil, *keys = Nil;

	for (struct value *arg = fnargs;
	     arg && arg->type != VAL_NIL;
	     arg = arg->cdr) {
		if (arg->car->type == VAL_KEYWORDPARAM) {
			keys = append(keys, cons(arg->car->keyword, arg->cdr->car));
			arg = arg->cdr;
		} else {
			args = append(args, arg->car);
		}
	}

	if (list_length(args)->i
	    < list_length(fn->param)->i
	    - list_length(fn->optional)->i
	    - list_length(fn->key)->i)
		return error(loc, "invalid number of arguments");

	if (list_length(args)->i > list_length(fn->param)->i && !fn->rest)
		return error(loc, "too many arguments");

	args = eval_list(env, args);
	if (args->type == VAL_ERROR) return args;
	struct value *newenv = push_env(fn->env, fn->param, args);

	for (struct value *opt = fn->optional;
	     opt && opt->type != VAL_NIL;
	     opt = opt->cdr) {
		struct value *bind = find(newenv, opt->car->car);
		if (!bind) add_variable(newenv, opt->car->car, opt->car->cdr);
		else if (!bind->cdr) bind->cdr = opt->car->cdr;
	}

	for (struct value *key = fn->key;
	     key && key->type != VAL_NIL;
	     key = key->cdr) {
		struct value *bind = find(newenv, key->car->car);
		if (!bind) add_variable(newenv, key->car->car, key->car->cdr);
		else if (!bind->cdr) bind->cdr = key->car->cdr;
	}

	if (fn->rest) {
		struct value *p = fn->param, *q = args;
		while (p->type != VAL_NIL) {
			p = p->cdr, q = q->cdr;
			if (q->type == VAL_NIL) break;
		}
		if (q->type != VAL_NIL)
			add_variable(newenv, fn->rest, q);
		else
			add_variable(newenv, fn->rest, Nil);
	}

	for (struct value *key = keys;
	     key && key->type != VAL_NIL;
	     key = key->cdr) {
		struct value *bind = find(newenv, key->car->car);
		if (!bind) add_variable(newenv, key->car->car, key->car->cdr);
		else bind->cdr = key->car->cdr;
	}

	return progn(newenv, fn->body);
}

/*
 * Evaluates every element of `list` in order and returns a new list
 * containing the resulting values. If evaluating an element results
 * in an error, evaluation is stopped and the error is returned.
 */

struct value *
eval_list(struct value *env, struct value *list)
{
	struct value *head = NULL, *tail = NULL;

	for (struct value *l = list;
	     l->type != VAL_NIL;
	     l = l->cdr) {
		if (!IS_LIST(l) || !IS_LIST(l->cdr))
			return list_length(l);
		struct value *tmp = eval(env, l->car);
		if (!tmp) return Nil;
		if (tmp->type == VAL_ERROR) return tmp;
		if (!head) {
			head = tail = cons(tmp, Nil);
			continue;
		}
		tail->cdr = cons(tmp, Nil);
		tail = tail->cdr;
	}

	return head ? head : Nil;
}

/*
 * Evaluates a node and returns the result.
 */

struct value *
eval(struct value *env, struct value *v)
{
	switch (v->type) {
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
		return error(v->loc, "stray comma outside of backtick"
		             " expression");
		break;

	/*
	 * Since this node is not being evaluated as a list, it must
	 * be a function or builtin call.
	 */

	case VAL_CELL: {
		struct value *expanded = expand(env, v);
		if (expanded != v) return eval(env, expanded);
		struct value *fn = eval(env, v->car);
		struct value *args = v->cdr;
		if (fn->type == VAL_ERROR) return fn;
		return apply(env, v->loc, fn, args);
	}

	/*
	 * Evaluating a bare symbol means we should look up what it
	 * means and return that.
	 */

	case VAL_SYMBOL: {
		struct value *bind = find(env, v);
		if (bind) return bind->cdr->loc = bind->car->loc, bind->cdr;
		return error(v->loc, "undeclared identifier");
	}

	/* This should never happen. */

	default:
		return error(v->loc, "bug: unimplemented evaluator");
	}

	return error(v->loc, "bug: unreachable");
}
