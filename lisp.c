#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <kdg/kdgu.h>

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

/* TODO: Get rid of this channel stuff. */
#define MAX_CHANNEL 2048

static struct {
	char *server, *chan;
	struct env *env;
} channel[MAX_CHANNEL];

static struct env *get_env(struct birch *b,
                           const char *server,
                           const char *chan);

struct value
builtin_channel(struct env *env, struct value v)
{
	char buf[256];
	memcpy(buf, string(car(v))->s, string(car(v))->len);
	buf[string(car(v))->len] = 0;
	struct env *e = get_env(env->birch, env->server, buf);
	return eval(e, cdr(v));
}

struct value
builtin_regget(struct env *env, struct value v)
{
	struct value val = eval(env, car(v));
	char buf[256];
	memcpy(buf, string(val)->s, string(val)->len);
	buf[string(val)->len] = 0;
	struct tree tree = reg_get(env->birch->reg, buf);
	struct value ret = NIL;

	switch (tree.type) {
	case TREE_NIL: return NIL;
	case TREE_INT: return (struct value){VAL_INT,{tree.integer}};
	case TREE_STRING:
		ret = gc_alloc(env, VAL_STRING);
		string(ret) = kdgu_news(tree.string);
		break;
	case TREE_BOOL:
		ret = tree.boolean ? TRUE : NIL;
		break;
	default:
		ret = error(env, "registry value cannot be"
		            " converted to a Lisp object");
	}

	return ret;
}

struct value
builtin_regset(struct env *env, struct value v)
{
	if (v.type == VAL_NIL)
		return error(env, "`regset' requires arguments");

	if (cdr(v).type == VAL_NIL)
		return error(env, "`regset' takes two arguments");

	struct value val = eval(env, car(v));
	struct value val2 = eval(env, car(cdr(v)));

	if (val.type != VAL_STRING || val2.type != VAL_STRING)
		return error(env, "arguments to `regset' must be strings");

	char buf[256];
	memcpy(buf, string(val)->s, string(val)->len);
	buf[string(val)->len] = 0;

	switch (val.type) {
	case VAL_INT: reg_set_int(env->birch->reg, buf, val2.integer); break;
	case VAL_STRING: {
		char buf2[256];
		memcpy(buf2, string(val2)->s, string(val2)->len);
		buf2[string(val2)->len] = 0;
		reg_set_string(env->birch->reg, buf, buf2);
	} break;
	default:
		return error(env, "registry value must be a"
		             " string or an integer");
	}

	return val2;
}

static struct env *
get_env(struct birch *b, const char *server, const char *chan)
{
	for (int i = 0; i < MAX_CHANNEL && channel[i].server; i++)
		if (!strcmp(channel[i].server, server)
		    && !strcmp(channel[i].chan, chan))
			return channel[i].env;

	/* We need to make a new channel. */
	for (int i = 0; i < MAX_CHANNEL; i++) {
		if (channel[i].server) continue;
		channel[i].server = strdup(server);
		channel[i].chan = strdup(chan);
		channel[i].env = new_environment(b, server);
		add_builtin(channel[i].env, "channel", builtin_channel);
		add_builtin(channel[i].env, "regset", builtin_regset);
		add_builtin(channel[i].env, "regget", builtin_regget);
		return channel[i].env;
	}

	/* This should never happen. */
	return NULL;
}

static void
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

	char buf[256];
	memcpy(buf, thing->s, thing->len);
	buf[thing->len] = 0;
	birch_send(b, server, channel, "%s", buf);
}

void
lisp_interpret_line(struct birch *b, const char *server, struct line *l)
{
	char *text = NULL;

	if (!strncmp(l->trailing, ".(", 2)) {
		text = strdup(l->trailing + 2);
	} else if (*l->trailing == '.') {
		text = malloc(strlen(l->trailing + 1) + 2);
		strcpy(text, l->trailing + 1);
		text[strlen(text) + 1] = 0;
		text[strlen(text)] = ')';
	} else {
		return;
	}

	struct env *env = get_env(b, server, l->middle[0]);
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
