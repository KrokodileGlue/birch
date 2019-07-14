#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "registry.h"
#include "server.h"
#include "net.h"

struct server *
server_new(const char *name, struct tree reg)
{
	struct server *s = malloc(sizeof *s);

	memset(s, 0, sizeof *s);
	s->name = strdup(name);

	/* TODO: Check this thing. */
	s->net = net_open(reg_get(reg, "address").string,
	                  reg_get(reg, "port").integer);

	net_send(s->net, "USER %s 0 * :%s\r\n",
	         reg_get(reg, "user").string,
	         reg_get(reg, "realname").string);
	net_send(s->net, "NICK %s\r\n",
	         reg_get(reg, "nick").string);

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
