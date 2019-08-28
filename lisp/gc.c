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

#define ALLOCATED(X) \
	((X) != VAL_NIL && (X) != VAL_TRUE && (X) != VAL_INT && (X) != VAL_DOT)

static void
free_object(struct env *env, struct value v)
{
	if (!ALLOCATED(v.type)) return;
	switch (v.type) {
	case VAL_SYMBOL:
	case VAL_STRING:
		kdgu_free(string(v));
		string(v) = NULL;
		break;
	default:;
	}
	bmp_free(env->gc->bmp, v.obj);
	type(v.obj) = VAL_NIL;
}

void
gc_sweep(struct env *env)
{
	struct gc *gc = env->gc;
	for (int i = 0; i < GC_MAX_OBJECT; i++) {
		struct value v = (struct value){type(i),{i}};
		if (!marked(v)) free_object(env, v);
	}
	memset(gc->mark, 0, (GC_MAX_OBJECT / 64) * sizeof *gc->mark);
}

struct value
gc_alloc(struct env *env, enum value_type type)
{
	if (!ALLOCATED(type))
		return (struct value){type,{0}};
	struct gc *gc = env->gc;
	int64_t idx = bmp_alloc(gc->bmp, GC_MAX_OBJECT);
	if (idx < 0 || idx >= GC_MAX_OBJECT) exit(1);
	type(idx) = type;
	//printf("alloc: %"PRId64"\n", idx);
	return (struct value){type, {idx}};
}

struct value
gc_copy(struct env *env, struct value v)
{
	if (!ALLOCATED(v.type))
		return v;

	struct value ret = gc_alloc(env, v.type);

	switch (v.type) {
	case VAL_COMMA:
	case VAL_COMMAT:
		keyword(ret) = gc_copy(env, keyword(v));
		break;
	case VAL_SYMBOL:
		string(ret) = kdgu_copy(string(v));
		break;
	case VAL_INT:
		ret.integer = v.integer;
		break;
	case VAL_STRING:
		string(ret) = kdgu_copy(string(v));
		break;
	case VAL_CELL:
		car(ret) = gc_copy(env, car(v));
		cdr(ret) = gc_copy(env, cdr(v));
		break;
	default:
		/* TODO: ... */
		printf("ojmsifjoisej: %s\n", TYPE_NAME(v.type));
	}

	return ret;
}

void
gc_mark(struct env *env, struct value v)
{
	if (ALLOCATED(v.type)) mark(v);
	switch (v.type) {
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
