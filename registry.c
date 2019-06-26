#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdarg.h>

#include "registry.h"
#include "table.h"

static void
reg_set_value(struct value reg, const char *key, struct value val)
{
	char *tok[64] = {0}, *tmp = strdup(key), *t = NULL;
	unsigned len = 0;

	while ((t = strtok(t ? NULL : tmp, "."))) tok[len++] = strdup(t);

	struct table *table = reg.table;

	for (unsigned i = 0; i < len; i++) {
		if (i == len - 1) {
			table_add(table, tok[i], val);
			continue;
		}

		if (table_lookup(table, tok[i]).type == VAL_NIL)
			table_add(table, tok[i], (struct value)
			          {VAL_TABLE, .table = table_new()});

		table = table_lookup(table, tok[i]).table;
	}
}

void
reg_set_string(struct value reg, const char *key, const char *val)
{
	assert(reg.type == VAL_TABLE);
	reg_set_value(reg, key, (struct value)
	              {VAL_STRING, .string = strdup(val)});
}

void
reg_set_int(struct value reg, const char *key, int val)
{
	assert(reg.type == VAL_TABLE);
	reg_set_value(reg, key, (struct value)
	              {VAL_INT, .integer = val});
}

void
reg_set_bool(struct value reg, const char *key, bool val)
{
	assert(reg.type == VAL_TABLE);
	reg_set_value(reg, key, (struct value)
	              {VAL_BOOL, .boolean = val});
}

struct value
reg_get(struct value reg, const char *fmt, ...)
{
	char *tok[64] = {0}, buf[128], *t = NULL;
	unsigned len = 0;

	/* TODO: Clean up here? */
	va_list args;
	va_start(args, fmt);
	vsnprintf(buf, sizeof buf, fmt, args);
	va_end(args);

	while ((t = strtok(t ? NULL : buf, ".")))
		tok[len++] = strdup(t);

	struct table *table = reg.table;

	for (unsigned i = 0; i < len - 1; i++) {
		struct value t = table_lookup(table, tok[i]);
		if (t.type != VAL_TABLE) return NIL;
		table = t.table;
	}

	return table_lookup(table, tok[len - 1]);
}

static void
print_table(struct table *t, int depth)
{
	TABLE_FOR(t) {
		for (int i = 0; i < depth; i++) printf("    ");
		printf("%s:\n", KEY);
		reg_print(VAL, depth + 1);
	}
}

void
reg_print(struct value reg, int depth)
{
	if (reg.type != VAL_TABLE)
		for (int i = 0; i < depth; i++) printf("    ");
	switch (reg.type) {
	case VAL_NIL: printf("nil\n"); break;
	case VAL_TABLE: print_table(reg.table, depth + 1); break;
	case VAL_STRING: printf("string %s\n", reg.string); break;
	case VAL_INT: printf("integer %d\n", reg.integer); break;
	case VAL_BOOL: printf("boolean %s\n", reg.boolean ? "true" : "false"); break;
	}
}
