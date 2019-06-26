#include <stdio.h>
#include <string.h>

#include "registry.h"
#include "server.h"
#include "net.h"

struct server *
server_new(const char *name, struct value reg)
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
	printf("joining %s:%s\n", s->name, chan);
	net_send(s->net, "JOIN %s\r\n", chan);
}
