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
#include "builtin.h"

void
lisp_init(struct birch *b)
{
	add_builtin(b->env, "in", builtin_in);
	add_builtin(b->env, "connect", builtin_connect);
	add_builtin(b->env, "join", builtin_join);
	add_builtin(b->env, "stdout", builtin_stdout);
	add_builtin(b->env, "birch-eval", builtin_birch_eval);
	add_builtin(b->env,
	            "current-server", builtin_current_server);
	add_builtin(b->env,
	            "current-channel", builtin_current_channel);
}

static struct value
quickstring(struct env *env, const char *str)
{
	struct value v = gc_alloc(env, VAL_STRING);
	string(v) = kdgu_news(str);
	return v;
}

static void
do_msg_hook(struct birch *b,
            const char *server,
            struct env *env,
            struct line *l)
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
			puts(tostring(string(print_value(env, car(hook)))));
		} else if (res.type != VAL_NIL) {
			send_value(b, env, server, l->middle[0], res);
		}
	}

	for (struct list *chan = b->channel; chan; chan = chan->next)
		gc_mark(chan->data, ((struct env *)chan->data)->vars);

	gc_mark(env, b->env->vars);

	/* It doesn't matter which environment is used here. */
	gc_sweep(env);
}

void
lisp_interpret_line(struct birch *b,
                    const char *server,
                    struct line *l)
{
	do_msg_hook(b,
	            server,
	            birch_get_env(b,
	                          server,
	                          l->middle[0]),
	            l);
}
