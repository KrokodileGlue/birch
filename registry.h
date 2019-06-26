#pragma once

#include <stdbool.h>

#include "table.h"

struct value {
	enum value_type {
		VAL_NIL,
		VAL_STRING,
		VAL_INT,
		VAL_BOOL,
		VAL_TABLE
	} type;

	union {
		char *string;
		int integer;
		struct table *table;
		bool boolean;
	};
};

#define NIL ((struct value){VAL_NIL,{0}})

struct value *value_new(enum value_type type);

void reg_set_string(struct value reg, const char *key, const char *val);
void reg_set_int(struct value reg, const char *key, int val);
void reg_set_bool(struct value reg, const char *key, bool val);
struct value reg_get(struct value reg, const char *fmt, ...);
void reg_print(struct value reg, int depth);

static struct value
reg_new(void)
{
	return (struct value){VAL_TABLE, .table = table_new()};
}
