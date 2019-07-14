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
	/* TODO: This entire thing is terrible. */

	if (kdgu_cmp(string(car(v)), &KDGU("global"), true, NULL))
		return eval(env->birch->env, cdr(v));

	char *string = tostring(string(car(v)));

	char server[256], channel[256];
	strcpy(channel, strchr(string, '/') + 1);
	strcpy(server, string);
	*strchr(server, '/') = 0;

	struct env *e = get_env(env->birch, server, channel);

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

static void
print_thing(struct tree reg, const char *sofar)
{
	switch (reg.type) {
	case TREE_BOOL: printf("(regset \"%s\" %s)\n", sofar, reg.boolean ? "t" : "()"); break;
	case TREE_INT: printf("(regset \"%s\" %d)\n", sofar, reg.integer); break;
	case TREE_NIL: printf("(regset \"%s\" ())\n", sofar); break;
	case TREE_STRING: printf("(regset \"%s\" \"%s\")\n", sofar, reg.string); break;
	case TREE_TABLE: {
		TABLE_FOR(reg.table) {
			/* TODO: Buffer overruns. */
			char buf[256];
			if (sofar)
				sprintf(buf, "%s.%s", sofar, KEY);
			else
				sprintf(buf, "%s", KEY);
			print_thing(VAL, buf);
		}
	} break;
	}
}

static void
print_thing2(struct env *env)
{
	struct env *env2 = env;

	for (struct value v = env->birch->env->vars;
	     v.type != VAL_NIL;
	     v = cdr(v)) {
		struct env *env = env2->birch->env;
		struct value bind = car(v);
		printf("(setq %s %s)\n", tostring(string(car(bind))), tostring(string(print_value(env, cdr(bind)))));
	}

	for (int i = 0; i < MAX_CHANNEL; i++) {
		if (!channel[i].server) break;
		struct env *e = get_env(env->birch, env->server, channel[i].chan);
		printf("(channel \"%s/%s\"", channel[i].server, channel[i].chan);
		for (struct value v = e->vars;
		     v.type != VAL_NIL;
		     v = cdr(v)) {
			struct value bind = car(v);
			printf("\n(setq %s %s)", tostring(string(car(bind))), tostring(string(print_value(e, cdr(bind)))));
		}
		printf(")\n");
	}
}

struct value
builtin_save(struct env *env, struct value v)
{
	(void)v;                /* Suppress warning. */
	print_thing(env->birch->reg, NULL);
	print_thing2(env);
	struct value s = gc_alloc(env, VAL_STRING);
	string(s) = kdgu_news("Saved.");
	return s;
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
		return error(env, "arguments to `regset'"
		             " must be strings");

	char buf[256];
	memcpy(buf, string(val)->s, string(val)->len);
	buf[string(val)->len] = 0;

	switch (val.type) {
	case VAL_INT:
		reg_set_int(env->birch->reg, buf, val2.integer);
		break;
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
	if (!strcmp(chan, "global")) return b->env;

	for (int i = 0; i < MAX_CHANNEL && channel[i].server; i++)
		if (!strcmp(channel[i].server, server)
		    && !strcmp(channel[i].chan, chan))
			return channel[i].env;

	/* We need to make a new channel. */
	for (int i = 0; i < MAX_CHANNEL; i++) {
		if (channel[i].server) continue;
		channel[i].server = strdup(server);
		channel[i].chan = strdup(chan);
		/* TODO: Make the `new_environment` thing nicer. */
		channel[i].env = push_env(b->env, NIL, NIL);
		channel[i].env->server = strdup(server);
		return channel[i].env;
	}

	/* This should never happen. */
	return NULL;
}

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

void
lisp_interpret_line(struct birch *b,
                    const char *server,
                    struct line *l)
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
