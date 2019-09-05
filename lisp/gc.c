#include <kdg/kdgu.h>
#include <stdint.h>
#include <assert.h>

#include "lisp.h"
#include "../list.h"
#include "../birch.h"
#include "lex.h"
#include "gc.h"
#include "error.h"

struct gc *
gc_new(void)
{
	struct gc *gc = malloc(sizeof *gc);
	memset(gc, 0, sizeof *gc);
	gc->obj = calloc(GC_MAX_OBJECT, sizeof *gc->obj);
	gc->bmp = calloc(GC_MAX_OBJECT / 64, sizeof *gc->bmp);
	gc->mark = calloc(GC_MAX_OBJECT / 64, sizeof *gc->mark);
	return gc;
}

static int64_t
bmp_alloc(uint64_t *bmp, int64_t slots)
{
	slots /= 64;

	for (int64_t i = 0; i < slots; i++) {
		if (*bmp == 0xFFFFFFFFFFFFFFFFLL) {
			bmp++;
			continue;
		}

		int pos = ffsll(~*bmp) - 1;
		*bmp |= 1LL << pos;

		return i * 64 + pos;
	}

	return -1;
}

static void
bmp_free(uint64_t *bmp, uint64_t idx)
{
	bmp[idx / 64] = ~(~bmp[idx / 64] | (1LL << (idx % 64)));
}

static void
free_object(struct env *env, value v)
{
	switch (type(v)) {
	case VAL_SYMBOL:
	case VAL_STRING:
		kdgu_free(string(v));
		string(v) = NULL;
		break;
	default:;
	}
	bmp_free(env->gc->bmp, v);
	type(v) = VAL_NIL;
}

void
gc_sweep(struct env *env)
{
	struct gc *gc = env->gc;
	/*
	 * Begin sweeping just after the constants so they don't get
	 * free'd and we don't have to mark them.
	 */
	for (int i = VEOF + 1; i < GC_MAX_OBJECT; i++)
		if (!marked(i))
			free_object(env, i);
	memset(gc->mark, 0, (GC_MAX_OBJECT / 64) * sizeof *gc->mark);
}

value
gc_alloc(struct env *env, enum value_type type)
{
	struct gc *gc = env->gc;
	int64_t idx = bmp_alloc(gc->bmp, GC_MAX_OBJECT);
	if (idx < 0 || idx >= GC_MAX_OBJECT) exit(1);
	type(idx) = type;
	return idx;
}

value
gc_copy(struct env *env, value v)
{
	value ret = gc_alloc(env, type(v));

	switch (type(v)) {
	case VAL_COMMA:
	case VAL_COMMAT:
		keyword(ret) = gc_copy(env, keyword(v));
		break;
	case VAL_SYMBOL:
		string(ret) = kdgu_copy(string(v));
		break;
	case VAL_INT:
		integer(ret) = integer(v);
		break;
	case VAL_STRING:
		string(ret) = kdgu_copy(string(v));
		break;
	case VAL_CELL:
		car(ret) = gc_copy(env, car(v));
		cdr(ret) = gc_copy(env, cdr(v));
		break;
	case VAL_NIL:
	case VAL_TRUE:
		break;
	default:
		/* TODO: ... */
		printf("ojmsifjoisej: %s\n", TYPE_NAME(type(v)));
	}

	return ret;
}

void
gc_mark(struct env *env, value v)
{
	mark(v);
	switch (type(v)) {
	case VAL_CELL:
		gc_mark(env, car(v));
		gc_mark(env, cdr(v));
		break;
	case VAL_COMMA:
	case VAL_COMMAT:
	case VAL_KEYWORDPARAM:
	case VAL_KEYWORD:
		gc_mark(env, keyword(v));
		break;
	case VAL_MACRO:
	case VAL_FUNCTION:
		gc_mark(env, param(v));
		gc_mark(env, body(v));
		gc_mark(env, optional(v));
		gc_mark(env, key(v));
		gc_mark(env, rest(v));
		gc_mark(env, docstring(v));
		break;
	default:;
	}
}
