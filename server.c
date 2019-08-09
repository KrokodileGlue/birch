#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include <kdg/kdgu.h>

#include "birch.h"
#include "server.h"
#include "net.h"

#include "lisp/lisp.h"

struct server *
server_new(struct birch *b,
           const char *network,
           const char *address,
           int port)
{
	struct server *s = malloc(sizeof *s);
	if (!s) return NULL;

	memset(s, 0, sizeof *s);

	s->name = strdup(network);
	if (!s->name) return free(s), NULL;
	s->net = net_open(address, port);
	if (!s->net) return free(s->name), free(s), NULL;

	return s;
}

void
server_join(struct server *s, const char *chan)
{
	net_send(s->net, "JOIN %s\r\n", chan);
}

bool
server_cmp(void *a, void *b)
{
	return !strcmp(((struct server *)a)->name, b);
}
