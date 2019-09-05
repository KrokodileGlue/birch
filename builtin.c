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

value
builtin_in(struct env *env, value v)
{
	if (type(v) == VAL_NIL)
		return error(env, "`in' requires arguments");

	value place = eval(env, car(v));

	if (type(place) == VAL_ERROR)
		return place;

	if (type(place) != VAL_STRING)
		return error(env, "first argument to"
		             " `in' must be a string");

	/*
	 * Only one argument, probably something like
	 * `(in "global")`.
	 */
	if (type(cdr(v)) == VAL_NIL)
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

value
builtin_connect(struct env *env, value v)
{
	if (integer(list_length(env, v)) != 6)
		return error(env, "`connect' takes six arguments");

	v = eval_list(env, v);
	if (type(v) == VAL_ERROR) return v;
	char *arg[6] = {NULL};
	int port = -1;

	for (int i = 0; i < 6; i++) {
		if (i == 2) {
			if (type(car(v)) != VAL_INT)
				return error(env, "the third argument"
				             " to `connect' must be"
				             " an integer");
			port = integer(car(v));
			v = cdr(v);
			continue;
		}

		if (type(car(v)) != VAL_STRING)
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

value
builtin_stdout(struct env *env, value v)
{
	value r = eval_list(env, v);
	if (type(r) == VAL_ERROR) return r;

	kdgu *out = kdgu_news("");

	for (value p = r; type(p) != VAL_NIL; p = cdr(p)) {
		if (type(car(p)) == VAL_STRING) {
			kdgu_append(out, string(car(p)));
			continue;
		}

		value e = print_value(env, car(p));

		if (type(e) == VAL_ERROR)
			return e;

		kdgu_append(out, string(e));
	}

	kdgu_print(out, stdout);
	fflush(stdout);

	return NIL;
}

static value
quickstring(struct env *env, const char *str)
{
	value v = gc_alloc(env, VAL_STRING);
	string(v) = kdgu_news(str);
	return v;
}

value
builtin_join(struct env *env, value v)
{
	if (integer(list_length(env, v)) != 2)
		return error(env, "`join' takes two arguments");

	value server = eval(env, car(v));
	if (type(server) == VAL_ERROR) return server;
	value channel = eval(env, car(cdr(v)));
	if (type(channel) == VAL_ERROR) return channel;

	if (type(server) != VAL_STRING || type(channel) != VAL_STRING)
		return error(env, "arguments to `join'"
		             " must be strings");

	const char *serv = tostring(string(server)),
		*chan = tostring(string(channel));

	if (birch_join(env->birch, serv, chan))
		return NIL;

	value bind = find(env, make_symbol(env, "join-hook"));
	if (type(bind) == VAL_NIL) return TRUE;

	value hooks = cdr(bind);

	for (value i = hooks; type(i) != VAL_NIL; i = cdr(i)) {
		value call =
			cons(env, car(i),
			     cons(env, server,
			          cons(env, channel,
			               NIL)));
		value res = eval(env, call);
		if (type(res) == VAL_ERROR) {
			printf("error in join-hook: %s\nin: %s\n",
			       tostring(string(res)),
			       tostring(string(print_value(env, call))));
		} else if (type(res) != VAL_NIL) {
			send_value(env->birch, env, serv, chan, res);
		}
	}

	return TRUE;
}

value
builtin_current_server(struct env *env, value v)
{
	return quickstring(env, env->server);
}

value
builtin_current_channel(struct env *env, value v)
{
	return quickstring(env, env->channel);
}

value
builtin_birch_eval(struct env *env, value v)
{
	if (integer(list_length(env, v)) != 1)
		return error(env, "builtin `birch-eval'"
		             " takes one argument");
	value arg = eval(env, car(v));
	value limit =
		find(env, make_symbol(env, "recursion-limit"));
	env = birch_get_env(env->birch, env->server, env->channel);
	env->birch->env->protect = true;
	if (type(limit) != VAL_NIL && type(cdr(limit)) == VAL_INT)
		env->birch->env->recursion_limit = integer(cdr(limit));
	value val = eval(env, arg);
	env->birch->env->protect = false;
	env->birch->env->recursion_limit = -1;
	if (type(val) == VAL_ERROR) return print_value(env, val);
	return val;
}
