#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "error.h"
#include "lisp.h"

struct value *
error(struct location *loc, const char *fmt, ...)
{
	struct value *v = new_value(loc);
	v->type = VAL_ERROR;
	v->loc = loc;

	int l = strlen(fmt) + 1;
	v->errmsg = malloc(l);

	va_list args;
	va_start(args, fmt);
	int len = vsnprintf(v->errmsg, l, fmt, args) + 1;

	if (len > l) {
		v->errmsg = realloc(v->errmsg, len);
		va_start(args, fmt);
		vsnprintf(v->errmsg, len, fmt, args);
	}

	va_end(args);

	return v;
}

struct value *
print_error(FILE *f, struct value *e)
{
	/* TODO: Make this more fancy. */
	char buf[256];
	sprintf(buf, "%s: %u: %s",
	        (char *[]){"error","note"}[e->type - VAL_ERROR],
	        e->loc->column, e->errmsg);
	struct value *v = new_value(e->loc);
	v->type = VAL_STRING;
	v->s = kdgu_news(buf);
	return v;
}
