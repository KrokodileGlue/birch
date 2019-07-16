#include <kdg/kdgu.h>

#include <stdint.h>
#include <stdlib.h>

#include "util.h"

char *
load_file(const char *p)
{
	char *b = NULL;
	FILE *f = NULL;
	int len = -1;

	f = fopen(p, "rb");
	if (!f) return NULL;
	if (fseek(f, 0L, SEEK_END)) return fclose(f), NULL;
	len = ftell(f);
	if (len == -1) return fclose(f), NULL;
	b = malloc(len + 10);
	if (!b) return fclose(f), NULL;
	if (fseek(f, 0L, SEEK_SET)) return fclose(f), free(b), NULL;
	len = fread(b, 1, len, f);
	if (ferror(f)) return fclose(f), free(b), NULL;
	fclose(f);
	memset(b + len, 0, 10);

	return b;
}

uint64_t
hash(const char *d, size_t len)
{
	uint64_t hash = 5381;

	for (size_t i = 0; i < len; i++)
		hash = ((hash << 5) + hash) + d[i];

	return hash;
}

char *
tostring(const struct kdgu *k)
{
	char *buf = malloc(k->len + 1);
	memcpy(buf, k->s, k->len);
	buf[k->len] = 0;
	return buf;
}
