#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>
#include <kdg/kdgu.h>

#include "lex.h"
#include "lisp.h"
#include "error.h"
#include "builtin.h"
#include "eval.h"
#include "gc.h"

#include "../birch.h"
#include "../util.h"

const char **value_name = (const char *[]){
	"nil",
	"int",
	"cell",
	"string",
	"symbol",
	"builtin",
	"function",
	"macro",
	"env",
	"keywordparam",
	"keyword",
	"comma",
	"commat",
	"true",
	"rparen",
	"dot",
	"eof",
	"error",
};

value
quote(struct env *env, value v)
{
	return cons(env, make_symbol(env, "quote"), cons(env, v, NIL));
}

value
backtick(struct env *env, value v)
{
	return cons(env, make_symbol(env, "backtick"), cons(env,v, NIL));
}

value
list_length(struct env *env, value list)
{
	int len = 0;

	while (type(list) != VAL_NIL) {
		if (type(list) == VAL_CELL) {
			list = cdr(list);
			len++;
			continue;
		}

		return error(env, "a non-dotted list was expected");
	}

	return mkint(len);
}

value
print_value(struct env *env, value v)
{
	kdgu *out = kdgu_news("");
	char buf[256];

	switch (type(v)) {
	case VAL_COMMA:
		kdgu_chrappend(out, ',');
		/* TODO: Ensure print_value returns a string. */
		kdgu_append(out, string(print_value(env, keyword(v))));
		break;
	case VAL_SYMBOL: out = kdgu_copy(string(v)); break;
	case VAL_STRING:
		kdgu_chrappend(out, '"');
		kdgu_append(out, string(v));
		kdgu_chrappend(out, '"');
		break;
	case VAL_INT:
		sprintf(buf, "%d", integer(v));
		out = kdgu_news(buf);
		break;
	case VAL_TRUE: out = kdgu_news("t");        break;
	case VAL_NIL:  out = kdgu_news("()");       break;
	case VAL_MACRO:
	case VAL_FUNCTION:
		if (function(v).name) {
			kdgu *tmp = kdgu_copy(function(v).name);
			kdgu_uc(tmp);
			kdgu_append(out, tmp);
		} else
			kdgu_append(out, &KDGU("anonymous function"));
		break;
	case VAL_BUILTIN:
		out = kdgu_news("<builtin>");
		break;
	case VAL_KEYWORD:
		sprintf(buf, "&%.*s",
		        string(keyword(v))->len, string(keyword(v))->s);
		out = kdgu_news(buf);
		break;
	case VAL_KEYWORDPARAM:
		sprintf(buf, ":%.*s",
		        string(keyword(v))->len, string(keyword(v))->s);
		out = kdgu_news(buf);
		break;
	case VAL_CELL:
		kdgu_chrappend(out, '(');

		while (type(v) == VAL_CELL) {
			value e = print_value(env, car(v));
			if (type(e) == VAL_ERROR) return e;
			kdgu_append(out, string(e));
			if (type(cdr(v)) == VAL_CELL)
				kdgu_chrappend(out, ' ');
			v = cdr(v);
		}

		if (type(v) != VAL_NIL) {
			kdgu_chrappend(out, ' ');
			/*
			 * TODO: This can break and probably other
			 * things in this function too.
			 */
			kdgu_append(out, string(print_value(env, v)));
		}

		kdgu_chrappend(out, ')');
		break;
	case VAL_DOT:
		kdgu_chrappend(out, '.');
		break;
	case VAL_ERROR:
		kdgu_append(out, string(print_error(env, v)));
		break;
	default:
		return error(env,
		             "bug: unimplemented printer for"
		             " expression of type `%s' (%d)",
		             TYPE_NAME(type(v)), type(v));
	}

	value e = gc_alloc(env, VAL_STRING);
	string(e) = out;

	return e;
}

value
cons(struct env *env, value car, value cdr)
{
	value v = gc_alloc(env, VAL_CELL);
	car(v) = car;
	cdr(v) = cdr;
	return v;
}

value
acons(struct env *env, value x, value y, value a)
{
	value left = cons(env, x, y);
	value ret = cons(env, left, a);
	return ret;
}

value
make_symbol(struct env *env, const char *s)
{
	if (!strcmp(s, "nil")) return NIL;
	if (!strcmp(s, "t")) return TRUE;

	value sym = gc_alloc(env, VAL_SYMBOL);
	string(sym) = kdgu_news(s);

	return sym;
}

value
expand(struct env *env, value v)
{
	if (type(v) != VAL_CELL || type(car(v)) != VAL_SYMBOL)
		return v;

	value bind = find(env, car(v));

	if (type(bind) == VAL_NIL || type(cdr(bind)) != VAL_MACRO)
		return v;

	value fn = cdr(bind), args = cdr(v);

	struct env *newenv = push_env(env, function(fn).param, args);

	for (value opt = function(fn).optional;
	     type(opt) != VAL_NIL;
	     opt = cdr(opt)) {
		value bind = find(newenv, car(car(opt)));

		if (type(bind) == VAL_NIL)
			add_variable(newenv,
			             car(car(opt)),
			             cdr(car(opt)));
		else if (type(cdr(bind)) == VAL_NIL)
			cdr(bind) = cdr(car(opt));
	}

	if (type(rest(fn)) != VAL_NIL) {
		value p = function(fn).param, q = args;

		while (type(p) != VAL_NIL) {
			p = cdr(p), q = cdr(q);
			if (type(q) == VAL_NIL) break;
		}

		add_variable(newenv, function(fn).rest,
		             type(q) != VAL_NIL ? q : NIL);
	}

	return progn(newenv, function(fn).body);
}

/*
 * Looks up `sym` in `env`, moving up into higher lexical scopes as
 * necessary. `sym` is assumed to be a `VAL_SYMBOL`. returns NIL if
 * the symbol could not be resolved.
 */

value
find(struct env *env, value sym)
{
	/*
	 * We've walked up through every scope and haven't found the
	 * symbol. It must not exist.
	 */

	if (!env) return NIL;

	/* Use this function carefully! */
	assert(type(sym) == VAL_SYMBOL);

	for (value c = env->vars;
	     type(c) != VAL_NIL;
	     c = cdr(c)) {
		value bind = car(c);
		if (kdgu_cmp(string(sym),
		             string(car(bind)),
		             false,
		             NULL))
			return bind;
	}

	return find(env->up, sym);
}

value
add_variable(struct env *env, value sym, value body)
{
	assert(type(sym) == VAL_SYMBOL);
	env->vars = acons(env, sym, body, env->vars);
	return body;
}

void
add_builtin(struct env *env, const char *name, builtin *f)
{
	value sym = make_symbol(env, name);
	value prim = gc_alloc(env, VAL_BUILTIN);
	if (type(prim) == VAL_NIL) return;
	builtin(prim) = f;
	add_variable(env, sym, prim);
}

static void
load_macros(struct env *env)
{
	eval_string(env, "(defmacro null (x) ~(if ,x nil t))");
	eval_string(env, "(defmacro not (x) ~(null ,x))");
	eval_string(env, "\
(defmacro map (x y)\
  ~(if (cdr ,y)\
       (cons (,x (car ,y)) (map ,x (cdr ,y)))\
     (cons (,x (car ,y)) nil)))");
}

/*
 * This should only ever be called to construct the global
 * environment. All others should be constructed with `push_env` using
 * the global environment as the first parameter.
 */

struct env *
new_environment(struct birch *b,
                const char *server,
                const char *channel)
{
	struct env *env = malloc(sizeof *env);

	/* Initialize basic fields. */
	env->up = NULL;    /* The global environment has no parent. */
	env->gc = gc_new();
	env->birch = b;
	env->vars = NIL;
	env->server = strdup(server);
	env->channel = strdup(channel);
	env->protect = false;
	env->recursion_limit = -1;
	env->depth = 0;
	b->env = env;

	/* Initialize constant values. */
	NIL = gc_alloc(env, VAL_NIL);
	DOT = gc_alloc(env, VAL_DOT);
	RPAREN = gc_alloc(env, VAL_RPAREN);
	TRUE = gc_alloc(env, VAL_TRUE);
	VEOF = gc_alloc(env, VAL_EOF);

	/*
	 * Note: These should only be done here (in the initialization
	 * for the global environment) because it's redundant to load
	 * the same builtins and macros when they're accessible to all
	 * channels through the global environment.
	 */
	load_builtins(env);
	load_macros(env);

	return env;
}

struct env *
make_env(struct env *env, value map)
{
	struct env *r = malloc(sizeof *r);
	memcpy(r, env, sizeof *r);
	r->vars = map;
	r->up = env;
	return r;
}

struct env *
push_env(struct env *env, value vars, value values)
{
	value map = NIL, p = vars, q = values;

	while (type(p) == VAL_CELL && type(q) == VAL_CELL) {
		map = acons(env, car(p), car(q), map);
		p = cdr(p), q = cdr(q);
	}

	return make_env(env, map);
}
