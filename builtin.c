#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <kdg/kdgu.h>

#include "list.h"
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

/*
 * Evaluates all arguments beyond the first argument in `v` as if they
 * were an expression submitted in the channel indicated by the first
 * argument in `v`. The channel indicator string may be either the
 * string "global" or a string in the format: `network '/' channel`.
 *
 * Examples:
 *         <k> .channel "freenode/#birch" setq greeting "Hello!"
 *     <birch> "Hello!"
 *
 *         <k> .channel "global" setq greeting "Hello!"
 *     <birch> "Hello!"
 */

struct value
builtin_channel(struct env *env, struct value v)
{
	/* TODO: Make print work with `channel`. */

	if (v.type == VAL_NIL)
		return error(env, "`channel' requires arguments");

	if (cdr(v).type == VAL_NIL)
		return error(env, "`channel' takes at"
		             " least two arguments");

	if (car(v).type != VAL_STRING)
		return error(env, "first argument to"
		             " `channel' must be a string");

	/* TODO: This seems volatile. */
	if (kdgu_cmp(string(car(v)), &KDGU("global"), true, NULL))
		return eval(env->birch->env, cdr(v));

	char *descriptor = tostring(string(car(v))),
		*server = strdup(descriptor),
		*channel = strdup(descriptor);

	/*
	 * We've guaranteed that the descriptor doesn't specify the
	 * global environment, so it must have a slash at this point.
	 */
	if (!strchr(descriptor, '/')) {
		free(descriptor), free(server), free(channel);
		return error(env, "malformed channel descriptor");
	}

	strcpy(channel, strchr(descriptor, '/') + 1);
	strcpy(server, descriptor);

	/* No longer needed. */
	free(descriptor);

	/*
	 * Truncate `server` to the portion of the descriptor behind
	 * the slash.
	 */
	*strchr(server, '/') = 0;

	/*
	 * TODO: Ensure that the channel name is valid according to
	 * the IRC RFC(s).
	 */

	if (!strlen(channel) || *channel != '#')
		return error(env, "channel descriptor in call"
		             " to `channel' is invalid");

	if (!strlen(server))
		return error(env, "server descriptor in call"
		             " to `channel' is invalid");

	struct env *e = birch_get_env(env->birch, server, channel);
	free(server), free(channel);

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
print_thing(FILE *f, struct tree reg, const char *sofar)
{
	switch (reg.type) {
	case TREE_BOOL: fprintf(f, "(regset \"%s\" %s)\n", sofar, reg.boolean ? "t" : "()"); break;
	case TREE_INT: fprintf(f, "(regset \"%s\" %d)\n", sofar, reg.integer); break;
	case TREE_NIL: fprintf(f, "(regset \"%s\" nil)\n", sofar); break;
	case TREE_STRING: fprintf(f, "(regset \"%s\" \"%s\")\n", sofar, reg.string); break;
	case TREE_TABLE: {
		TABLE_FOR(reg.table) {
			/* TODO: Buffer overruns. */
			char buf[256];
			if (sofar)
				sprintf(buf, "%s.%s", sofar, KEY);
			else
				sprintf(buf, "%s", KEY);
			print_thing(f, VAL, buf);
		}
	} break;
	}
}

static void
print_thing2(FILE *f, struct env *env)
{
	struct env *env2 = env;

	for (struct value v = env->birch->env->vars;
	     v.type != VAL_NIL;
	     v = cdr(v)) {
		struct env *env = env2->birch->env;
		struct value bind = car(v);

		/* TODO: This doesn't allow builtin aliases. */
		if (cdr(bind).type == VAL_BUILTIN) continue;

		fprintf(f, "(setq %s '%s)\n", tostring(string(car(bind))), tostring(string(print_value(env, cdr(bind)))));
	}

	/* For each channel... */
	for (struct list *list = env->birch->channel;
	     list;
	     list = list->next) {
		struct env *e = list->data;
		fprintf(f, "(channel \"%s/%s\"", e->server, e->channel);
		for (struct value v = e->vars;
		     v.type != VAL_NIL;
		     v = cdr(v)) {
			struct value bind = car(v);
			fprintf(f, "\n(setq %s '%s)", tostring(string(car(bind))), tostring(string(print_value(e, cdr(bind)))));
		}
		fprintf(f, ")\n");
	}
}

struct value
builtin_save(struct env *env, struct value v)
{
	(void)v;                /* Suppress warning. */

	struct tree config_file = reg_get(env->birch->reg, "bot.config-file");

	if (config_file.type == TREE_NIL) {
		struct value error = gc_alloc(env, VAL_STRING);
		string(error) =
			kdgu_news("You must specify a config file"
			          " with `bot.config-file' before"
			          " using this command.");
		return error;
	}

	if (config_file.type != TREE_STRING) {
		struct value error = gc_alloc(env, VAL_STRING);
		string(error) =
			kdgu_news("The `bot.config-file' registry"
			          " entry must be a string.");
		return error;
	}

	FILE *f = fopen(config_file.string, "w");

	if (!f) {
		struct value error = gc_alloc(env, VAL_STRING);
		string(error) =
			kdgu_news("Output file could not"
			          " be opened for writing.");
		return error;
	}

	print_thing(f, env->birch->reg, NULL);
	print_thing2(f, env);

	fclose(f);

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
