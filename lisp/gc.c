#include <kdg/kdgu.h>

#include "lex.h"
#include "lisp.h"
#include "gc.h"

static int
alloc(struct env *env)
{
	for (int i = 0; i < GC_MAX_OBJECT; i++) {
		if (env->obj[i].used) continue;
		env->obj[i].used = true;
		return i;
	}

	return -1;
}

struct value
gc_alloc(struct env *env, enum value_type type)
{
	/* TODO: Don't allocate non-allocated objects. */
	int idx = alloc(env);
	if (idx < 0) return VNULL;
	struct value v;
	v.type = type;
	v.obj = idx;
	return v;
}

struct value
gc_copy(struct env *env, struct value v)
{
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
	case VAL_NIL:
	case VAL_TRUE:
		break;
	default:
		/* TODO: ... */
		printf("ojmsifjoisej: %s\n", TYPE_NAME(v.type));
	}

	return ret;
}
