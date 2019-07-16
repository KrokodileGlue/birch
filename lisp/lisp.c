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

#include "../table.h"
#include "../registry.h"
#include "../birch.h"
#include "../util.h"

const char **value_name = (const char *[]){
	"null",
	"nil",
	"int",
	"cell",
	"string",
	"symbol",
	"builtin",
	"function",
	"macro",
	"env",
	"array",
	"keywordparam",
	"keyword",
	"comma",
	"commat",
	"true",
	"rparen",
	"dot",
	"error",
	"note",
};

struct value
quote(struct env *env, struct value v)
{
	return cons(env, make_symbol(env, "quote"), cons(env, v, NIL));
}

struct value
backtick(struct env *env, struct value v)
{
	return cons(env, make_symbol(env, "backtick"), cons(env,v, NIL));
}

struct value
list_length(struct env *env, struct value list)
{
	int len = 0;

	while (list.type != VAL_NIL) {
		if (list.type == VAL_CELL) {
			list = cdr(list);
			len++;
			continue;
		}

		return error(env, "a non-dotted list was expected");
	}

	return (struct value){VAL_INT, {len}};
}

struct value
print_value(struct env *env, struct value v)
{
	kdgu *out = kdgu_news("");
	char buf[256];

	switch (v.type) {
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
		sprintf(buf, "%d", v.integer);
		out = kdgu_news(buf);
		break;
	case VAL_TRUE: out = kdgu_news("t");        break;
	case VAL_NIL:  out = kdgu_news("()");       break;
	case VAL_MACRO:
	case VAL_FUNCTION:
		if (obj(v).name) {
			kdgu *tmp = kdgu_copy(obj(v).name);
			kdgu_uc(tmp);
			kdgu_append(out, tmp);
		} else
			kdgu_append(out, &KDGU("anonymous function"));
		break;
	case VAL_BUILTIN:
		out = kdgu_news("<builtin>");
		break;
	case VAL_ARRAY:
		for (unsigned i = 0; i < obj(v).num; i++) {
			struct value e = print_value(env, array(v)[i]);
			if (e.type == VAL_ERROR) return e;
			assert(e.type == VAL_STRING);
			kdgu_append(out, string(e));
		}
		break;
	case VAL_KEYWORD:
		sprintf(buf, "&%.*s",
		        string(obj(v).keyword)->len, string(obj(v).keyword)->s);
		out = kdgu_news(buf);
		break;
	case VAL_KEYWORDPARAM:
		sprintf(buf, ":%.*s",
		        string(obj(v).keyword)->len, string(obj(v).keyword)->s);
		out = kdgu_news(buf);
		break;
	case VAL_CELL:
		kdgu_chrappend(out, '(');

		while (v.type == VAL_CELL) {
			struct value e = print_value(env, car(v));
			if (e.type == VAL_ERROR) return e;
			kdgu_append(out, string(e));
			if (cdr(v).type == VAL_CELL)
				kdgu_chrappend(out, ' ');
			v = cdr(v);
		}

		if (v.type != VAL_NIL) {
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
	case VAL_NULL:
		kdgu_append(out, &KDGU("null"));
		break;
	default:
		return error(env,
		             "bug: unimplemented printer for"
		             " expression of type `%s' (%d)",
		             TYPE_NAME(v.type), v.type);
	}

	struct value e = gc_alloc(env, VAL_STRING);
	string(e) = out;

	return e;
}

struct value
cons(struct env *env, struct value car, struct value cdr)
{
	struct value v = gc_alloc(env, VAL_CELL);
	car(v) = car;
	cdr(v) = cdr;
	return v;
}

struct value
acons(struct env *env, struct value x, struct value y, struct value a)
{
	return cons(env, cons(env, x, y), a);
}

struct value
make_symbol(struct env *env, const char *s)
{
	if (!strcmp(s, "nil")) return NIL;
	if (!strcmp(s, "t")) return TRUE;

	struct value sym = gc_alloc(env, VAL_SYMBOL);
	if (sym.type == VAL_NULL) return VNULL;
	string(sym) = kdgu_news(s);

	return sym;
}

struct value
expand(struct env *env, struct value v)
{
	if (v.type != VAL_CELL || car(v).type != VAL_SYMBOL)
		return v;

	struct value bind = find(env, car(v));

	if (bind.type == VAL_NIL || cdr(bind).type != VAL_MACRO)
		return v;

	struct value fn = cdr(bind);
	struct value args = cdr(v);

	struct env *newenv = push_env(env, obj(fn).param, args);

	for (struct value opt = obj(fn).optional;
	     opt.type != VAL_NIL;
	     opt = cdr(opt)) {
		struct value bind = find(newenv, car(car(opt)));

		if (bind.type == VAL_NIL)
			add_variable(newenv,
			             car(car(opt)),
			             cdr(car(opt)));
		else if (cdr(bind).type == VAL_NIL)
			cdr(bind) = cdr(car(opt));
	}

	if (rest(fn).type != VAL_NIL) {
		struct value p = obj(fn).param, q = args;

		while (p.type != VAL_NIL) {
			p = cdr(p), q = cdr(q);
			if (q.type == VAL_NIL) break;
		}

		add_variable(newenv, obj(fn).rest,
		             q.type != VAL_NIL ? q : NIL);
	}

	return progn(newenv, obj(fn).body);
}

/*
 * Looks up `sym` in `env`, moving up into higher lexical scopes as
 * necessary. `sym` is assumed to be a `VAL_SYMBOL`. returns NULL if
 * the symbol could not be resolved.
 */

struct value
find(struct env *env, struct value sym)
{
	/* Use this function carefully! */
	assert(sym.type == VAL_SYMBOL);

	/*
	 * We've walked up through every scope and haven't found the
	 * symbol. It must not exist.
	 */

	if (!env) return NIL;

	for (struct value c = env->vars;
	     c.type != VAL_NIL;
	     c = cdr(c)) {
		struct value bind = car(c);
		if (kdgu_cmp(string(sym), string(car(bind)), false, NULL))
			return bind;
	}

	return find(env->up, sym);
}

struct value
add_variable(struct env *env, struct value sym, struct value body)
{
	assert(sym.type == VAL_SYMBOL);
	env->vars = acons(env, sym, body, env->vars);
	return body;
}

void
add_builtin(struct env *env, const char *name, builtin *f)
{
	struct value sym = make_symbol(env, name);
	struct value prim = gc_alloc(env, VAL_BUILTIN);
	if (prim.type == VAL_NIL) return;
	obj(prim).builtin = f;
	add_variable(env, sym, prim);
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
	env->birch = b;
	env->vars = NIL;
	env->server = strdup(server);
	env->server = strdup(channel);
	env->output = kdgu_news("");

	/* Initialize garbage-collected objects. */
	env->obj = malloc(GC_MAX_OBJECT * sizeof *env->obj);
	memset(env->obj, 0, GC_MAX_OBJECT * sizeof *env->obj);

	/*
	 * Note: This should only be done here (in the initialization
	 * for the global environment) because it's redundant to load
	 * the same builtins when they're accessible to all channels
	 * through the global environment.
	 */
	load_builtins(env);

	return env;
}

struct env *
make_env(struct env *env, struct value map)
{
	struct env *r = malloc(sizeof *r);

	r->server = env->server;
	r->channel = env->channel;
	r->birch = env->birch;
	r->obj = env->obj;
	r->vars = map;
	r->up = env;
	r->output = NULL;

	return r;
}

struct env *
push_env(struct env *env, struct value vars, struct value values)
{
	struct value map = NIL, p = vars, q = values;

	while (p.type == VAL_CELL && q.type == VAL_CELL) {
		map = acons(env, car(p), car(q), map);
		p = cdr(p), q = cdr(q);
	}

	return make_env(env, map);
}
