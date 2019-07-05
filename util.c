#include <kdg/kdgu.h>

#include <stdint.h>
#include <stdlib.h>

#include "util.h"

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
