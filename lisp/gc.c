#include <kdg/kdgu.h>
#include <stdint.h>
#include <assert.h>

#include "lisp.h"
#include "../birch.h"
#include "lex.h"
#include "gc.h"
#include "error.h"

enum value_type alloc_type[] = {
	-1, /* VAL_NIL */
	-1, /* VAL_INT */
	VAL_CELL, /* VAL_CELL */
	VAL_STRING, /* VAL_STRING */
	VAL_STRING, /* VAL_SYMBOL */
	VAL_BUILTIN, /* VAL_BUILTIN */
	VAL_FUNCTION, /* VAL_FUNCTION */
	VAL_FUNCTION, /* VAL_MACRO */
	-1, /* VAL_ENV */
	VAL_KEYWORD, /* VAL_KEYWORDPARAM */
	VAL_KEYWORD, /* VAL_KEYWORD */
	VAL_KEYWORD, /* VAL_COMMA */
	VAL_KEYWORD, /* VAL_COMMAT */
	-1, /* VAL_TRUE */
	-1, /* VAL_RPAREN */
	-1, /* VAL_DOT */
	-1, /* VAL_EOF */
	VAL_STRING, /* VAL_ERROR */
};

int max_type[] = {
	-1, /* VAL_NIL */
	-1, /* VAL_INT */
	1000000, /* VAL_CELL */
	1000000, /* VAL_STRING */
	-1, /* VAL_SYMBOL */
	100, /* VAL_BUILTIN */
	100000, /* VAL_FUNCTION */
	-1, /* VAL_MACRO */
	-1, /* VAL_ENV */
	-1, /* VAL_KEYWORDPARAM */
	5000, /* VAL_KEYWORD */
	-1, /* VAL_COMMA */
	-1, /* VAL_COMMAT */
	-1, /* VAL_TRUE */
	-1, /* VAL_RPAREN */
	-1, /* VAL_DOT */
	-1, /* VAL_EOF */
	-1, /* VAL_ERROR */
};

struct gc *
gc_new(void)
{
	struct gc *gc = malloc(sizeof *gc);
	memset(gc, 0, sizeof *gc);
	gc->string = malloc(max_type[VAL_STRING] * sizeof *gc->string);
	gc->cell = malloc(max_type[VAL_CELL] * sizeof *gc->cell);
	gc->function = malloc(max_type[VAL_FUNCTION] * sizeof *gc->function);
	gc->keyword = malloc(max_type[VAL_KEYWORD] * sizeof *gc->keyword);
	gc->builtin = malloc(max_type[VAL_BUILTIN] * sizeof *gc->builtin);
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

struct value
gc_alloc(struct env *env, enum value_type type)
{
	enum value_type atype = alloc_type[type];
	if (atype < 0) return (struct value){type,{0}};
	int max = max_type[atype];

	struct gc *gc = env->gc;
	int64_t idx = bmp_alloc(gc->bmp[atype], gc->slot[atype]);

	if (idx == -1) {
		gc->slot[atype] += 64;
		gc->bmp[atype] = realloc(gc->bmp[atype],
		                              ((gc->slot[atype] / 64) + 1)
		                              * sizeof *gc->bmp[atype]);
		memset(&gc->bmp[atype][(gc->slot[atype] - 64) / 64], 0,
		       sizeof *gc->bmp[atype]);
		idx = bmp_alloc(gc->bmp[atype], gc->slot[atype]);
	}

	if (idx < 0 || idx >= max) exit(1);

	return (struct value){type, {idx}};
}

struct value
gc_copy(struct env *env, struct value v)
{
	if (v.type == VAL_NIL || v.type == VAL_TRUE)
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
