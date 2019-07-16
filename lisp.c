#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <kdg/kdgu.h>

#include "util.h"
#include "irc.h"
#include "table.h"
#include "registry.h"
#include "birch.h"
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
	add_builtin(b->env, "channel", builtin_channel);
	add_builtin(b->env, "regset", builtin_regset);
	add_builtin(b->env, "regget", builtin_regget);
	add_builtin(b->env, "save", builtin_save);
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
		birch_send(b, server, channel, "%s", buf);
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
	birch_send(b, server, channel, "%s", buf);
	free(buf);
}

static void
do_msg_hook(struct birch *b, struct env *env, struct line *l)
{
	struct value bind = find(env, make_symbol(env, "msg-hook"));
	if (bind.type == VAL_NIL) return;

	for (struct value msg_hook = cdr(bind);
	     msg_hook.type != VAL_NIL;
	     msg_hook = cdr(msg_hook)) {
		struct value hook = car(msg_hook);
		struct value arg = gc_alloc(env, VAL_STRING);
		string(arg) = kdgu_news(l->trailing);
		struct value call = cons(env, hook, cons(env, arg, NIL));
		eval(env, call);
	}
}

void
lisp_interpret_line(struct birch *b,
                    const char *server,
                    struct line *l)
{
	char *text = NULL;

	/* TODO: Do triggers. */
	if (!strncmp(l->trailing, ".(", 2)) {
		text = strdup(l->trailing + 2);
	} else if (*l->trailing == '.') {
		text = malloc(strlen(l->trailing + 1) + 2);
		strcpy(text, l->trailing + 1);
		text[strlen(text) + 1] = 0;
		text[strlen(text)] = ')';
	} else {
		do_msg_hook(b, birch_get_env(b, server, l->middle[0]), l);
		return;
	}

	struct env *env = birch_get_env(b, server, l->middle[0]);
	struct lexer *lexer = new_lexer("*birch*", text);
	struct value shite = parse(env, lexer);

	if (shite.type == VAL_ERROR) {
		send_value(b, env, server, l->middle[0], shite);
		return;
	}

	if (tok(lexer)) {
		birch_send(b, server, l->middle[0],
		           "error: incomplete command");
		return;
	}

	send_value(b, env, server, l->middle[0], eval(env, shite));
}
