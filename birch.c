#include <stdarg.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <dlfcn.h>

#include <kdg/kdgu.h>

#include "lisp/lex.h"
#include "lisp/lisp.h"
#include "lisp/error.h"
#include "lisp/util.h"
#include "lisp/parse.h"
#include "lisp/eval.h"
#include "lisp/gc.h"

#include "table.h"
#include "registry.h"
#include "list.h"
#include "server.h"
#include "net.h"
#include "irc.h"
#include "lisp.h"

#include "birch.h"

struct birch *
birch_new(struct tree reg)
{
	struct birch *b = malloc(sizeof *b);
	memset(b, 0, sizeof *b);
	b->reg = reg;
	b->env = new_environment(b, "global");
	lisp_init(b);
	return b;
}

void
birch_connect(struct birch *b)
{
	TABLE_FOR(reg_get(b->reg, "server").table)
		list_add(&b->server, server_new(KEY,
		   reg_get(b->reg, "server.%s", KEY)));
}

/*
 * Looks up a channel property. Evaluates to the channel table itself
 * if used with empty arguments. Only used in `join()`.
 */
#define CHANNEL_PROPERTY(X,...)	  \
	reg_get(b->reg, "server.%s.channel" X, server, __VA_ARGS__)

static void
join(struct birch *b, const char *server, struct server *serv)
{
	TABLE_FOR(CHANNEL_PROPERTY("","").table)
		if (CHANNEL_PROPERTY(".%s.autojoin", KEY).boolean)
			server_join(serv, KEY);
}

#undef CHANNEL_PROPERTY

void
birch_join(struct birch *b)
{
	/* For each server join its channels. */
	TABLE_FOR(reg_get(b->reg, "server").table)
		join(b, KEY, list_get(b->server, KEY, server_cmp));
}

static void
birch_main(struct birch *b, struct server *server)
{
	while (1) {
		char buf[512];
		net_read(server->net, buf);
		if (!strlen(buf)) break;
		struct line *line = line_new(buf);

		if (line->type == LINE_CMD && line->cmd == CMD_PING) {
			net_send(server->net, "PONG :%s\r\n", line->trailing);
			continue;
		}

		if (line->type != LINE_CMD || line->cmd != CMD_PRIVMSG)
			continue;

		lisp_interpret_line(b, server->name, line);
	}

	exit(0);
}

/*
 * Begins listener threads and launches the main I/O loop.
 */
void
birch(struct birch *b)
{
	struct list *l = b->server;

	/* Walk along the list of servers starting listen threads. */
	while (l) {
		struct server *server = (struct server *)l->data;
		if (fork() == 0) birch_main(b, server);
		l = l->next;
	}
}

void
birch_send(struct birch *b,
           const char *server,
           const char *chan,
           const char *fmt,
           ...)
{
	if (!strcmp(server, "global")) return;

	char buf[256];

	va_list args;
	va_start(args, fmt);
	vsnprintf(buf, sizeof buf, fmt, args);
	va_end(args);

	for (size_t i = 0; i < strlen(buf); i++)
		if (buf[i] == '\n')
			buf[i] = ' ';

	char bug[512];
	sprintf(bug, "PRIVMSG %s :%s\r\n", chan, buf);

	net_send(((struct server *)list_get(b->server, (void *)server, server_cmp))->net, bug);
}
