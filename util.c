#include <kdg/kdgu.h>

#include <stdarg.h>
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

/*
 * Divides the string pointed to by `str` into a dynamically-allocated
 * array of tokens as delimited by occurrences of any of the
 * characters in the string pointed to by `str`. Points `*tok` to the
 * token array and sets `*len` to the length of the array. Returns
 * zero on success, nonzero otherwise.
 */

int
tokenize(const char *str,
         const char *delim,
         char ***tok,
         unsigned *len)
{
	char *buf = NULL,       /* Modifiable copy of `str`. */
		*tmp = NULL;    /* Current token. */

	if (!tok || !len || !delim) return 1;
	*len = 0;

	/*
	 * `strlen(buf)` is almost certainly more than is needed, but
	 * it is definitely not less than is needed, so it's a
	 * practical estimate (the array shouldn't live very long
	 * anyway).
	 */
	*tok = malloc(strlen(str) * sizeof *tok);
	if (!*tok) goto error;

	buf = strdup(str);
	if (!buf) goto error;

	while ((tmp = strtok(tmp ? NULL : buf, delim)))
		(*tok)[(*len)++] = tmp;

	return 0;

 error:
	for (unsigned i = 0; i < *len; i++) free((*tok)[i]);
	free(*tok);
	free(buf);
	return 1;
}
