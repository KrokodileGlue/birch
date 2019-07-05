#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <kdg/kdgu.h>

#include "table.h"
#include "registry.h"
#include "util.h"

struct table *
table_new()
{
	struct table *t = malloc(sizeof *t);
	memset(t, 0, sizeof *t);
	return t;
}

struct table *
table_copy(struct table *t)
{
	struct table *r = table_new();

	for (size_t i = 0; i < TABLE_SIZE; i++)
		for (size_t j = 0; j < t->bucket[i].len; j++)
			table_add(r, t->bucket[i].key[j], table_lookup(t, t->bucket[i].key[j]));

	return r;
}

void
table_free(struct table *t)
{
	for (size_t i = 0; i < TABLE_SIZE; i++) {
		for (size_t j = 0; j < t->bucket[i].len; j++)
			free(t->bucket[i].key[j]);

		free(t->bucket[i].h);
		free(t->bucket[i].key);
		free(t->bucket[i].val);
	}

	free(t);
}

struct tree
table_add(struct table *t, char *key, struct tree v)
{
	uint64_t h = hash(key, strlen(key));

	int idx = h % TABLE_SIZE;
	struct bucket *b = t->bucket + idx;

	for (size_t i = 0; i < b->len; i++) {
		if (b->h[i] == h) {
			b->val[i] = v;
			return v;
		}
	}

	b->h   = realloc(b->h,   (b->len + 1) * sizeof *b->h);
	b->val = realloc(b->val, (b->len + 1) * sizeof *b->val);
	b->key = realloc(b->key, (b->len + 1) * sizeof *b->key);

	b->h  [b->len] = h;
	b->key[b->len] = strdup(key);
	b->val[b->len] = v;

	return b->val[b->len++];
}

struct tree
table_lookup(struct table *t, char *key)
{
	uint64_t h = hash(key, strlen(key));
	int idx = h % TABLE_SIZE;

	struct bucket *b = t->bucket + idx;

	for (size_t i = 0; i < b->len; i++)
		if (b->h[i] == h && !strcmp(key, b->key[i]))
			return b->val[i];

	return REG_NIL;
}
