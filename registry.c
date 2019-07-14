#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

#include "table.h"
#include "registry.h"

struct tree
reg_new(void)
{
	return (struct tree){TREE_TABLE, .table = table_new()};
}

static void
reg_set_tree(struct tree reg, const char *key, struct tree val)
{
	char *tok[64] = {0}, *tmp = strdup(key), *t = NULL;
	unsigned len = 0;

	while ((t = strtok(t ? NULL : tmp, ".")))
		tok[len++] = strdup(t);

	struct table *table = reg.table;

	for (unsigned i = 0; i < len; i++) {
		if (i == len - 1) {
			table_add(table, tok[i], val);
			continue;
		}

		if (table_lookup(table, tok[i]).type == TREE_NIL)
			table_add(table, tok[i], (struct tree)
			          {TREE_TABLE, .table = table_new()});

		table = table_lookup(table, tok[i]).table;
	}
}

void
reg_set_string(struct tree reg, const char *key, const char *val)
{
	assert(reg.type == TREE_TABLE);
	reg_set_tree(reg, key, (struct tree)
	              {TREE_STRING, .string = strdup(val)});
}

void
reg_set_int(struct tree reg, const char *key, int val)
{
	assert(reg.type == TREE_TABLE);
	reg_set_tree(reg, key, (struct tree)
	              {TREE_INT, .integer = val});
}

void
reg_set_bool(struct tree reg, const char *key, bool val)
{
	assert(reg.type == TREE_TABLE);
	reg_set_tree(reg, key, (struct tree)
	              {TREE_BOOL, .boolean = val});
}

struct tree
reg_get(struct tree reg, const char *fmt, ...)
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
		struct tree t = table_lookup(table, tok[i]);
		if (t.type != TREE_TABLE) return REG_NIL;
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
reg_print(struct tree reg, int depth)
{
	if (reg.type != TREE_TABLE)
		for (int i = 0; i < depth; i++)
			printf("    ");

	switch (reg.type) {
	case TREE_NIL: printf("nil\n"); break;
	case TREE_TABLE: print_table(reg.table, depth + 1); break;
	case TREE_STRING: printf("string %s\n", reg.string); break;
	case TREE_INT: printf("integer %d\n", reg.integer); break;
	case TREE_BOOL: printf("boolean %s\n", reg.boolean
	                       ? "true" : "false");
		break;
	}
}
