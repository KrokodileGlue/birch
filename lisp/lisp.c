#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "error.h"
#include "builtin.h"
#include "lisp.h"
#include "eval.h"

const char **value_name = (const char *[]){
	"int",
	"cell",
	"string",
	"symbol",
	"builtin",
	"function",
	"macro",
	"env",
	"array",
	":",
	"keyword",
	",",
	",@",
	"moved",
	"true",
	"nil",
	"rparen",
	"dot",
	"error",
};

struct value *Dot = &(struct value){VAL_DOT,{0},NULL,NOWHERE};
struct value *RParen = &(struct value){VAL_RPAREN,{0},NULL,NOWHERE};
struct value *Nil = &(struct value){VAL_NIL,{0},NULL,NOWHERE};
struct value *True = &(struct value){VAL_TRUE,{0},NULL,NOWHERE};

struct value *
new_value(struct location *loc)
{
	struct value *v = malloc(sizeof *v);
	memset(v, 0, sizeof *v);
	v->loc = loc;
	return v;
}

struct value *
quote(struct value *v)
{
	return cons(make_symbol(v->loc, "quote"), cons(v, Nil));
}

struct value *
backtick(struct value *v)
{
	return cons(make_symbol(v->loc, "backtick"), cons(v, Nil));
}

struct value *
list_length(struct value *list)
{
	int len = 0;

	while (list->type != VAL_NIL) {
		if (list->type == VAL_CELL) {
			list = list->cdr;
			len++;
			continue;
		}

		return error(list->loc,
		             "a non-dotted list was expected here");
	}

	struct value *r = new_value(list->loc);

	r->type = VAL_INT;
	r->i = len;

	return r;
}

struct value *
copy_value(struct value *v)
{
	struct value *ret = new_value(v->loc);
	ret->type = v->type;

	switch (v->type) {
	case VAL_CELL:
		ret->car = copy_value(v->car);
		if (ret->car->type == VAL_ERROR) return ret->car;
		ret->cdr = copy_value(v->cdr);
		if (ret->cdr->type == VAL_ERROR) return ret->cdr;
		break;
	case VAL_INT:
		ret->i = v->i;
		break;
	case VAL_SYMBOL:
	case VAL_STRING:
		ret->s = kdgu_copy(v->s);
		break;
	case VAL_COMMAT:
	case VAL_COMMA:
		ret->keyword = copy_value(v->keyword);
		if (ret->keyword->type == VAL_ERROR) return ret->keyword;
		break;
	case VAL_NIL:
	case VAL_TRUE: break;
	default:
		return error(v->loc,
		             "bug: unimplemented value copier for "
		             "value of type `%s'",
		             value_name[v->type]);
	}

	return ret;
}

struct value *
print_value(FILE *f, struct value *v)
{
	kdgu *out = kdgu_news("");
	char buf[64];

	switch (v->type) {
	case VAL_COMMA:
		kdgu_chrappend(out, ',');
		kdgu_append(out, print_value(f, v->keyword)->s);
		break;
	case VAL_SYMBOL: out = kdgu_copy(v->s); break;
	case VAL_STRING:
		kdgu_append(out, v->s);
		break;
	case VAL_INT:
		sprintf(buf, "%d", v->i);
		out = kdgu_news(buf);
		break;
	case VAL_TRUE: out = kdgu_news("t");        break;
	case VAL_NIL:  out = kdgu_news("()");       break;
	case VAL_FUNCTION:
		//out = print_tree(f, v);
		out = kdgu_news("TODO");
		break;
	case VAL_BUILTIN:
		sprintf(buf, "<builtin:%p>", v->prim);
		out = kdgu_news(buf);
		break;
	case VAL_ARRAY:
		for (unsigned i = 0; i < v->num; i++) {
			struct value *e = print_value(f, v->arr[i]);
			if (e->type == VAL_ERROR) return e;
			assert(e->type == VAL_STRING);
			kdgu_append(out, e->s);
		}
		break;
	case VAL_KEYWORD:
		sprintf(buf, "&%.*s", v->keyword->s->len, v->keyword->s->s);
		out = kdgu_news(buf);
		break;
	case VAL_KEYWORDPARAM:
		sprintf(buf, ":%.*s", v->keyword->s->len, v->keyword->s->s);
		out = kdgu_news(buf);
		break;
	case VAL_CELL:
		kdgu_chrappend(out, '(');

		while (v->type == VAL_CELL) {
			struct value *e = print_value(f, v->car);
			if (e->type == VAL_ERROR) return e;
			kdgu_append(out, e->s);
			if (v->cdr->type == VAL_CELL) kdgu_chrappend(out, ' ');
			v = v->cdr;
		}

		if (v->type != VAL_NIL) {
			kdgu_chrappend(out, ' ');
			kdgu_append(out, print_value(f, v)->s);
		}

		kdgu_chrappend(out, ')');
		break;
	case VAL_ERROR:
		kdgu_append(out, print_error(f, v)->s);
		break;
	default:
		return error(v->loc,
		             "bug: unimplemented printer for"
		             " expression of type `%s'",
		             TYPE_NAME(v->type));
	}

	struct value *e = new_value(v->loc);
	e->type = VAL_STRING;
	e->s = out;

	return e;
}

struct value *
cons(struct value *car, struct value *cdr)
{
	struct value *v = new_value(car->loc);
	v->type = VAL_CELL;
	v->car = car;
	v->cdr = cdr;
	return v;
}

struct value *
acons(struct value *x, struct value *y, struct value *a)
{
	return cons(cons(x, y), a);
}

struct value *
make_symbol(struct location *loc, const char *s)
{
	if (!strcmp(s, "nil")) return Nil;
	if (!strcmp(s, "t")) return True;

	struct value *sym = new_value(loc);
	sym->type = VAL_SYMBOL;
	sym->s = kdgu_news(s);
	return sym;
}

struct value *
expand(struct value *env, struct value *v)
{
	if (v->type != VAL_CELL || v->car->type != VAL_SYMBOL)
		return v;
	struct value *bind = find(env, v->car);
	if (!bind || bind->cdr->type != VAL_MACRO) return v;
	struct value *fn = bind->cdr;
	struct value *args = v->cdr;
	struct value *newenv = push_env(fn->env, fn->param, args);

	for (struct value *opt = fn->optional;
	     opt && opt->type != VAL_NIL;
	     opt = opt->cdr) {
		struct value *bind = find(newenv, opt->car->car);
		if (!bind) add_variable(newenv, opt->car->car, opt->car->cdr);
		else if (!bind->cdr) bind->cdr = opt->car->cdr;
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

	return progn(newenv, fn->body);
}

/*
 * Looks up `sym` in `env`, moving up into higher lexical scopes as
 * necessary. `sym` is assumed to be a `VAL_SYMBOL`. returns NULL if
 * the symbol could not be resolved.
 */

struct value *
find(struct value *env, struct value *sym)
{
	assert(sym->type == VAL_SYMBOL);

	/*
	 * We've walked up through every scope and haven't found the
	 * symbol. It must not exist.
	 */

	if (!env) return NULL;

	for (struct value *c = env->vars;
	     c->type != VAL_NIL;
	     c = c->cdr) {
		struct value *bind = c->car;
		if (kdgu_cmp(sym->s, bind->car->s, false, NULL))
			return bind;
	}

	return find(env->up, sym);
}

struct value *
add_variable(struct value *env, struct value *sym, struct value *body)
{
	env->vars = acons(sym, body, env->vars);
	return body;
}

void
add_builtin(struct value *env, const char *name, builtin *f)
{
	struct value *sym = make_symbol(BUILTIN, name);
	struct value *prim = malloc(sizeof *prim);
	prim->type = VAL_BUILTIN;
	prim->prim = f;
	add_variable(env, sym, prim);
}

struct value *
new_environment(void)
{
	struct value *env = new_value(NOWHERE);
	env->vars = Nil;
	load_builtins(env);
	return env;
}

struct value *
make_env(struct value *vars, struct value *up)
{
	struct value *r = malloc(sizeof *r);
	r->vars = vars;
	r->up = up;
	return r;
}

struct value *
push_env(struct value *env, struct value *vars, struct value *values)
{
	struct value *map = Nil;
	struct value *p = vars, *q = values;

	for (;
	     p->type == VAL_CELL;
	     p = p->cdr, q = q->cdr) {
		if (q->type == VAL_NIL) return make_env(map, env);
		map = acons(p->car, q->car, map);
	}

	return make_env(map, env);
}

#define p(...) fprintf(f, __VA_ARGS__)

void
print_tree(FILE *f, struct value *v)
{
	static int depth = 0;
	static int l = 0, arm[2048] = {0};
	depth++, (depth != 1 && p("\n")), arm[l] = 0;

	for (int i = 0; i < l - 1; i++)
		arm[i] ? p("│   ") : p("    ");
	if (l) arm[l - 1] ? p("├───") : p("╰───");

	if (!v) {
		p("(null)");
		return;
	}

	switch (v->type) {
	case VAL_BUILTIN:
		p("(builtin:%p)", v->prim);
		break;
	case VAL_FUNCTION:
		p("(function)");
		l++;
		arm[l - 1] = 1;
		print_tree(f, v->param);
		print_tree(f, v->optional);
		arm[l - 1] = 0;
		print_tree(f, v->body);
		l--;
		break;
	case VAL_SYMBOL:
		p("(symbol:%.*s)", v->s->len, v->s->s);
		break;
	case VAL_CELL:
		p("(cell)");
		l++;
		arm[l - 1] = 1;
		print_tree(f, v->car);
		arm[l - 1] = 0;
		print_tree(f, v->cdr);
		l--;
		break;
	case VAL_STRING:
		p("\"%.*s\"", v->s->len, v->s->s);
		break;
	case VAL_INT:
		p("(int:%d)", v->i);
		break;
	case VAL_NIL:
		p("()");
		break;
	case VAL_TRUE:
		p("(true)");
		break;
	case VAL_ARRAY:
		p("[");
		for (unsigned i = 0; i < v->num; i++)
			print_tree(f, v->arr[i]);
		p("]");
		break;
	case VAL_KEYWORD:
		p("(keyword %.*s)", v->keyword->s->len, v->keyword->s->s);
		break;
	case VAL_ERROR:
		p("(error)");
		break;
	case VAL_COMMA:
		p("(,)");
		l++;
		arm[l - 1] = 0;
		print_tree(f, v->keyword);
		l--;
		break;
	case VAL_COMMAT:
		p("(,@)");
		l++;
		arm[l - 1] = 0;
		print_tree(f, v->keyword);
		l--;
		break;
	case VAL_KEYWORDPARAM:
		p("(:)");
		l++;
		arm[l - 1] = 0;
		print_tree(f, v->keyword);
		l--;
		break;
	default:
		p("(bug: unprintable value type %d)\n", v->type);
		exit(1);
	}

	depth--;
}
