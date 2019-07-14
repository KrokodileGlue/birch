#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <kdg/kdgu.h>

#include "lex.h"
#include "lisp.h"
#include "../util.h"
#include "error.h"
#include "gc.h"

struct value
error(struct env *env, const char *fmt, ...)
{
	struct value v = gc_alloc(env, VAL_ERROR);

	/* TODO: Stack smashing innit. */
	char buf[128];

	va_list args;
	va_start(args, fmt);

	/* TODO: Check the return value. */
	vsnprintf(buf, 128, fmt, args);
	va_end(args);

	string(v) = kdgu_news(buf);

	return v;
}

struct value
print_error(struct env *env, struct value e)
{
	/* TODO: Make this more fancy. Check `buf` size. */
	char buf[256];
	sprintf(buf, "%s: %s",
	        (char *[]){"error","note"}[e.type - VAL_ERROR],
	        tostring(string(e)));
	struct value v = gc_alloc(env, VAL_STRING);
	string(v) = kdgu_news(buf);
	return v;
}
