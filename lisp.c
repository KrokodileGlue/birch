#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>

#include <kdg/kdgu.h>

#include "list.h"
#include "util.h"
#include "irc.h"
#include "birch.h"
#include "server.h"
#include "lisp/lex.h"
#include "lisp/lisp.h"
#include "lisp/eval.h"
#include "lisp/gc.h"
#include "lisp/error.h"
#include "lisp/parse.h"
#include "builtin.h"

void
lisp_init(struct birch *b)
{
	add_builtin(b->env, "in", builtin_in);
	add_builtin(b->env, "save", builtin_save);
	add_builtin(b->env, "connect", builtin_connect);
	add_builtin(b->env, "join", builtin_join);
	add_builtin(b->env, "stdout", builtin_stdout);
	add_builtin(b->env, "boundp", builtin_boundp);
}

static void
send_value(struct birch *b,
           struct env *env,
           const char *server,
           const char *channel,
           struct value v)
{
	if (b->env->output->len) {
		char *buf = malloc(b->env->output->len + 1);
		memcpy(buf, b->env->output->s, b->env->output->len);
		buf[b->env->output->len] = 0;
		birch_send(b, server, channel, true, "%s", buf);
		kdgu_free(b->env->output);
		free(buf);
		/* TODO: Check malloc everywhere. */
		b->env->output = kdgu_news("");
		return;
	}

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

static struct value
quickstring(struct env *env, const char *str)
{
	struct value v = gc_alloc(env, VAL_STRING);
	string(v) = kdgu_news(str);
	return v;
}

static void
do_msg_hook(struct birch *b, struct env *env, struct line *l)
{
	struct value bind = find(env, make_symbol(env, "msg-hook"));
	if (bind.type == VAL_NIL) return;

	struct value arg = NIL;

	arg = cons(env, quickstring(env, l->trailing), arg);
	arg = cons(env, quickstring(env, l->nick), arg);
	arg = cons(env, quickstring(env, l->date), arg);
	arg = quote(env, arg);

	for (struct value hook = cdr(bind);
	     hook.type != VAL_NIL;
	     hook = cdr(hook)) {
		struct value res = eval(env,
		                        cons(env,
		                             car(hook),
		                             cons(env, arg, NIL)));
		if (res.type == VAL_ERROR) {
			puts("msg-hook error:");
			puts(tostring(string(res)));
		}
	}
}

void
lisp_interpret_line(struct birch *b,
                    const char *server,
                    struct line *l)
{
	do_msg_hook(b, birch_get_env(b, server, l->middle[0]), l);

	if (b->env->output->len) {
		char *buf = tostring(b->env->output);
		birch_send(b, server, l->middle[0], true, "%s", buf);
		kdgu_free(b->env->output);
		free(buf);
		b->env->output = kdgu_news("");
	}

	//send_value(b, env, server, l->middle[0], eval(env, shite));
}
