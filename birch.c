#include <stdarg.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>

#include <kdg/kdgu.h>
#include <curl/curl.h>

#include "lisp/lex.h"
#include "lisp/lisp.h"
#include "lisp/error.h"
#include "lisp/util.h"
#include "lisp/parse.h"
#include "lisp/eval.h"
#include "lisp/gc.h"

#include "list.h"
#include "server.h"
#include "net.h"
#include "irc.h"
#include "lisp.h"
#include "util.h"

#include "birch.h"

struct birch *
birch_new(void)
{
	struct birch *b = malloc(sizeof *b);
	memset(b, 0, sizeof *b);
	b->env = new_environment(b, "global", "global");
	lisp_init(b);
	return b;
}

struct server *
birch_connect(struct birch *b,
              const char *network,
              const char *address,
              int port,
              const char *user,
              const char *nick,
              const char *realname)
{
	struct server *s = server_new(b, network, address, port);
	if (!s) return NULL;

	list_add(&b->server, s);

	net_send(s->net, "USER %s 0 * :%s\r\n", user, realname);
	net_send(s->net, "NICK %s\r\n", nick);

	return s;
}

int
birch_join(struct birch *b,
           const char *serv,
           const char *chan)
{
	struct server *s = list_get(b->server,
	                            (void *)serv,
	                            server_cmp);
	if (!s) return 1;
	server_join(s, chan);
	return 0;
}

void *
birch_main(void *data)
{
	struct birch *b = ((struct data *)data)->b;
	struct server *server = ((struct data *)data)->server;

	while (1) {
		char buf[512];
		net_read(server->net, buf);
		if (!strlen(buf)) break;

		time_t timer;
		time(&timer);

		struct line *line = line_new(buf, timer);

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
            const char *str)
{
	CURL *curl = curl_easy_init();
	char *out = NULL, *data = NULL, *buf = NULL;

	if (!curl) goto fail;
	data = curl_easy_escape(curl, str, strlen(str));
	if (!data) goto fail;
	buf = malloc(strlen(data) + 5);
	if (!buf) goto fail;

	sprintf(buf, "f:1=%s", data);
	printf("buf: %s\n", buf);

	/* TODO: Make the paste site customizable. */
	curl_easy_setopt(curl, CURLOPT_URL, "ix.io/");
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, receive);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, buf);

	if (curl_easy_perform(curl) != CURLE_OK) goto fail;

	birch_send(b, server, chan, false, "%s", out);
	return;

 fail:
	birch_send(b, server, chan, false, "Paste failed.");
 cleanup:
	free(data);
	free(buf);
	curl_easy_cleanup(curl);
}

void
birch_send(struct birch *b,
           const char *server,
           const char *chan,
           bool paste,
           const char *fmt,
           ...)
{
	if (!strcmp(server, "global")) return;

	struct server *serv = list_get(b->server,
	                               (void *)server,
	                               server_cmp);

	if (!serv) {
		/* TODO: Errors... */
		return;
	}

	int len = 512;
	char *buf = malloc(len + 1);

	while (true) {
		va_list args;
		va_start(args, fmt);
		int ret = vsnprintf(buf, len + 1, fmt, args);
		va_end(args);

		if (ret < len) break;

		if ((len *= 2) > 10000) {
			birch_send(b, server, chan, false,
			           "Output was too long.");
			free(buf);
			return;
		}

		/* TODO: Check realloc and stuff. */
		buf = realloc(buf, len + 1);
	}

	/*
	 * TODO: `out-hook' for filtering and other hooks (e.g. to
	 * control pastes).
	 */

	if (paste && strchr(buf, '\n')) {
		birch_paste(b, server, chan, buf);
		return;
	}

	char output[513];
	if (snprintf(output, 513, "PRIVMSG %s :%s\r\n", chan, buf) > 256) {
		birch_paste(b, server, chan, buf);
		return;
	}

	net_send(serv->net, "%s", output);
}

/*
 * Gets the Lisp environment for a specific channel, or the global
 * environment.
 */

struct env *
birch_get_env(struct birch *b,
              const char *server,
              const char *channel)
{
	if (!strcmp(server, "global"))
		return b->env;

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

	if (!strcmp(channel, "global")) {
		env = push_env(b->env, NIL, NIL);
		env->server = strdup(server);
		env->channel = strdup(channel);
		list_add(&b->channel, env);
		return env;
	}

	env = push_env(birch_get_env(b, server, "global"), NIL, NIL);
	env->server = strdup(server);
	env->channel = strdup(channel);
	list_add(&b->channel, env);

	return env;
}

int
birch_config(struct birch *b, const char *path)
{
	char *code = load_file(path);
	if (!code) return 1;

	struct env *env = b->env;
	struct lexer *lexer = new_lexer("*command*", code);
	struct value expr = NIL;

	do {
		struct token *t = tok(lexer);

		if (!t) {
			struct value init = find(env, make_symbol(env, "init"));
			/* TODO */
			if (init.type == VAL_NIL) exit(1);
			struct value call = gc_alloc(env, VAL_CELL);

			car(call) = cdr(init);
			cdr(call) = NIL;

			struct value val = eval(env, call);

			if (val.type == VAL_ERROR) {
				puts(tostring(string(val)));
				puts(tostring(string(print_value(env, expr))));
				return 1;
			}

			return 0;
		}

		if (t->type != '(') return 1;

		expr = parse(env, lexer);

		if (expr.type == VAL_ERROR) {
			puts(tostring(string(expr)));
			return 1;
		}

		struct value val = eval(env, expr);

		if (val.type == VAL_ERROR) {
			puts(tostring(string(val)));
			puts(tostring(string(print_value(env, expr))));
			return 1;
		}
	} while (expr.type != VAL_NIL);

	return 0;
}

void
send_value(struct birch *b,
           struct env *env,
           const char *server,
           const char *channel,
           struct value v)
{
	struct value print = v.type == VAL_STRING
		? v : print_value(env, v);

	/* TODO: This can only happen when the print itself fails. */
	if (print.type == VAL_ERROR) return;

	kdgu *thing = string(print);

	/* TODO: Think about when this can happen. */
	if (!thing || !thing->s) return;

	char *buf = malloc(thing->len + 1);
	memcpy(buf, thing->s, thing->len);
	buf[thing->len] = 0;
	birch_send(b, server, channel, true, "%s", buf);
	free(buf);
}
