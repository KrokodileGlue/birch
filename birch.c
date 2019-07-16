#include <stdarg.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <pthread.h>

#include <kdg/kdgu.h>
#include <curl/curl.h>

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
	b->env = new_environment(b, "global", "global");
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

struct data {
	struct birch *b;
	struct server *server;
};

static void *
birch_main(void *data)
{
	struct birch *b = ((struct data *)data)->b;
	struct server *server = ((struct data *)data)->server;

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

	return NULL;
}

/*
 * Begins listener threads and launches the main I/O loops.
 */

void
birch(struct birch *b)
{
	curl_global_init(CURL_GLOBAL_ALL);
	struct list *l = b->server;

	int idx = 0;
	pthread_t thread[10];   /* TODO: This hardcoded maximum. */

	/* Walk along the list of servers starting listen threads. */
	while (l) {
		struct data *data = malloc(sizeof *data);
		*data = (struct data){b, l->data};
		pthread_create(&thread[idx], NULL, birch_main, data);
		idx++;
		l = l->next;
	}

	for (int i = 0; i < idx; i++)
		pthread_join(thread[i], NULL);

	curl_global_cleanup();
}

static size_t
receive(void *ptr,
        size_t size,
        size_t nmemb,
        char **stream)
{
	if (!*stream) {
		*stream = malloc(size * nmemb + 1);
		memcpy(*stream, ptr, size * nmemb);
		(*stream)[size * nmemb] = 0;
	} else {
		*stream = realloc(*stream,
		                  strlen(*stream) + size * nmemb + 1);
		sprintf(*stream, "%s%.*s", *stream, (int)(size * nmemb), ptr);
	}

	return size * nmemb;
}

void
birch_paste(struct birch *b,
            const char *server,
            const char *chan,
            const char *buf)
{
	CURL *curl = curl_easy_init();
	if (!curl) return;
	char *out = NULL;

	char *data = curl_easy_escape(curl, buf, strlen(buf));
	char *bug = malloc(strlen(data) + 5);
	sprintf(bug, "f:1=%s", data);

	/* TODO: Make the paste site customizable. */
	curl_easy_setopt(curl, CURLOPT_URL, "ix.io/");
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, receive);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, bug);

	CURLcode res = curl_easy_perform(curl);

	if (res != CURLE_OK) {
		birch_send(b, server, chan,
		           "Line was too long and paste failed.");
		return;
	}

	/*
	 * This depends on the response being small enough to not get
	 * pasted. TODO?
	 */
	birch_send(b, server, chan, "%s", out);
	curl_easy_cleanup(curl);
}

void
birch_send(struct birch *b,
           const char *server,
           const char *chan,
           const char *fmt,
           ...)
{
	if (!strcmp(server, "global")) return;

	int len = 512;
	char *buf = malloc(len + 1);

	while (true) {
		va_list args;
		va_start(args, fmt);
		int ret = vsnprintf(buf, len + 1, fmt, args);
		va_end(args);

		if (ret < len) break;

		if (len *= 2 > 1000000) {
			birch_send(b, server, chan,
			           "Output was too long.");
			free(buf);
			return;
		}

		/* TODO: Check realloc and stuff. */
		buf = realloc(buf, len + 1);
	}

	struct tree ll = reg_get(b->reg, "server.%s.channel."
	                         "%s.max_line_length", server, chan);

	if (ll.type == TREE_NIL)
		ll = reg_get(b->reg, "server.%s.max_line_length", server);

	if (ll.type == TREE_INT && (int)strlen(buf) >= ll.integer) {
		birch_paste(b, server, chan, buf);
		return;
	}

	/*
	 * TODO: Other transformations might be good here, and they
	 * should mostly be turnoffable. This should probably be
	 * implemented in Lisp. out_hook?
	 */
	for (size_t i = 0; i < strlen(buf); i++)
		if (buf[i] == '\n')
			buf[i] = ' ';

	char bug[513];
	if (snprintf(bug, 513, "PRIVMSG %s :%s\r\n", chan, buf) > 512) {
		birch_paste(b, server, chan, buf);
		return;
	}

	net_send(((struct server *)list_get(b->server, (void *)server, server_cmp))->net, "%s", bug);
}

/*
 * Gets the Lisp environment for a specific channel, or the global
 * environment.
 */

struct env *
birch_get_env(struct birch *b, const char *server, const char *channel)
{
	if (!strcmp(channel, "global")) return b->env;

	struct channel {
		const char *server, *channel;
	};

	bool cmp(struct env *a, struct channel *b) {
		return !strcmp(a->server, b->server)
			&& !strcmp(a->channel, b->channel);
	}

	struct env *env = list_get(b->channel,
	                           &(struct channel){server, channel},
	                           (bool (*)(void *, void *))cmp);

	/*
	 * If the environment exists, fine. If it doesn't, a new one
	 * must be constructed for this channel.
	 */
	if (env) return env;

	env = push_env(b->env, NIL, NIL);
	env->server = strdup(server);
	env->channel = strdup(channel);
	list_add(&b->channel, env);

	return env;
}
