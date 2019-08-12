#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>

#include <kdg/kdgu.h>

#include "lisp/lex.h"
#include "lisp/lisp.h"
#include "lisp/eval.h"
#include "lisp/gc.h"
#include "lisp/error.h"
#include "lisp/parse.h"

#include "list.h"
#include "util.h"
#include "irc.h"
#include "birch.h"
#include "server.h"

/*
 * Evaluates all arguments beyond the first argument in `v` as if they
 * were an expression submitted in the channel indicated by the first
 * argument in `v`. The channel indicator string may be either the
 * string "global", the name of a server, or a string in the format:
 * `network '/' channel`.
 *
 * Examples:
 *         <k> .in "freenode/#birch" setq greeting "Hello!"
 *     <birch> "Hello!"
 *
 *         <k> .in "global" setq greeting "Hello!"
 *     <birch> "Hello!"
 */

struct value
builtin_in(struct env *env, struct value v)
{
	if (v.type == VAL_NIL)
		return error(env, "`in' requires arguments");

	struct value place = eval(env, car(v));

	if (place.type == VAL_ERROR)
		return place;

	if (place.type != VAL_STRING)
		return error(env, "first argument to"
		             " `in' must be a string");

	/*
	 * Only one argument, probably something like
	 * `(in "global")`.
	 */
	if (cdr(v).type == VAL_NIL)
		return NIL;

	char *descriptor = tostring(string(place));
	char **tok;
	unsigned len;

	tokenize(descriptor, "/", &tok, &len);

	if (len < 1 || len > 2)
		error(env, "malformed channel"
		      " descriptor in call to `in'");

	const char *server = tok[0];

	if (len == 1 && !strcmp(server, "global"))
		return eval(env->birch->env, cdr(v));

	if (len == 1)
		return eval(birch_get_env(env->birch,
		                          server,
		                          "global"),
		            cdr(v));

	const char *channel = tok[1];

	if (!strlen(channel) || *channel != '#')
		return error(env, "channel descriptor in call"
		             " to `in' is invalid");

	if (!strlen(server))
		return error(env, "server descriptor in call"
		             " to `in' is invalid");

	struct env *e = birch_get_env(env->birch, server, channel);

	/*
	 * If `e` is NULL then we've run out of memory;
	 * `birch_get_env` will never return NULL for any other reason
	 * (it creates a new environment if the channel doesn't
	 * exist).
	 */
	if (!e) return NIL;

	return eval(e, cdr(v));
}

struct value
builtin_connect(struct env *env, struct value v)
{
	if (list_length(env, v).integer != 6)
		return error(env, "`connect' takes six arguments");

	v = eval_list(env, v);
	if (v.type == VAL_ERROR) return v;
	char *arg[6] = {NULL};
	int port = -1;

	for (int i = 0; i < 6; i++) {
		if (i == 2) {
			if (car(v).type != VAL_INT)
				return error(env, "the third argument"
				             " to `connect' must be"
				             " an integer");
			port = car(v).integer;
			v = cdr(v);
			continue;
		}

		if (car(v).type != VAL_STRING)
			return error(env, "all arguments to"
			             " `connect' must be strings"
			             " except for the third");

		arg[i] = tostring(string(car(v)));
		v = cdr(v);
	}

	struct server *s = birch_connect(env->birch,
	                                 arg[0],
	                                 arg[1],
	                                 port,
	                                 arg[3],
	                                 arg[4],
	                                 arg[5]);

	if (!s) return NIL;

	struct data *data = malloc(sizeof *data);
	*data = (struct data){env->birch, s};
	pthread_create(&s->thread, NULL, birch_main, data);

	return TRUE;
}

struct value
builtin_stdout(struct env *env, struct value v)
{
	struct value r = eval_list(env, v);
	if (r.type == VAL_ERROR) return r;

	kdgu *out = kdgu_news("");

	for (struct value p = r; p.type != VAL_NIL; p = cdr(p)) {
		if (car(p).type == VAL_STRING) {
			kdgu_append(out, string(car(p)));
			continue;
		}

		struct value e = print_value(env, car(p));
		if (e.type == VAL_ERROR) return e;
		kdgu_append(out, string(e));
	}

	kdgu_print(out, stdout);
	fflush(stdout);

	return NIL;
}

static struct value
quickstring(struct env *env, const char *str)
{
	struct value v = gc_alloc(env, VAL_STRING);
	string(v) = kdgu_news(str);
	return v;
}

struct value
builtin_join(struct env *env, struct value v)
{
	if (list_length(env, v).integer != 2)
		return error(env, "`join' takes two arguments");

	struct value server = eval(env, car(v));
	if (server.type == VAL_ERROR) return server;
	struct value channel = eval(env, car(cdr(v)));
	if (channel.type == VAL_ERROR) return channel;

	if (server.type != VAL_STRING || channel.type != VAL_STRING)
		return error(env, "arguments to `join'"
		             " must be strings");

	const char *serv = tostring(string(server)),
		*chan = tostring(string(channel));

	if (birch_join(env->birch, serv, chan))
		return NIL;

	struct value bind = find(env, make_symbol(env, "join-hook"));
	if (bind.type == VAL_NIL) return TRUE;

	struct value hooks = cdr(bind);

	for (struct value i = hooks; i.type != VAL_NIL; i = cdr(i)) {
		struct value call =
			cons(env, car(i),
			     cons(env, server,
			          cons(env, channel,
			               NIL)));
		struct value res = eval(env, call);
		if (res.type == VAL_ERROR) {
			puts("error in join-hook:");
			puts(tostring(string(res)));
			puts("in:");
			puts(tostring(string(print_value(env, call))));
		} else if (res.type != VAL_NIL) {
			send_value(env->birch, env, serv, chan, res);
		}
	}

	return TRUE;
}

struct value
builtin_current_server(struct env *env, struct value v)
{
	return quickstring(env, env->server);
}

struct value
builtin_current_channel(struct env *env, struct value v)
{
	return quickstring(env, env->channel);
}
