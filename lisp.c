#include <assert.h>

#include "lisp.h"

#define MAX_CHANNEL 2048

static struct {
	char *server, *chan;
	struct value *env;
} channel[MAX_CHANNEL];

struct value *global;

static struct value *
get_env(const char *server, const char *chan)
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
		return channel[i].env = new_environment();
	}

	/* This should never happen. */
	return NULL;
}

void
lisp_interpret_line(struct birch *b, const char *server, struct line *l)
{
	/* TODO: Thread stuff. */
	if (!global) global = new_environment();

	char *text = NULL;

	if (!strncmp(l->trailing, ",(", 2)) {
		text = strdup(l->trailing + 2);
	} else if (*l->trailing == ',') {
		text = malloc(strlen(l->trailing + 1) + 2);
		strcpy(text, l->trailing + 1);
		text[strlen(text) + 1] = 0;
		text[strlen(text)] = ')';
	} else {
		return;
	}

	struct value *env = get_env(server, l->middle[0]);
	struct lexer *lexer = new_lexer("wtfffff", text);
	struct value *shite = parse(env, lexer);

	struct value *e = eval(env, shite);

	assert(e);              /* ????? */

	struct value *print = print_value(stdout, e);
	kdgu *thing = print->s;

	char buf[256];
	memcpy(buf, thing->s, thing->len);
	buf[thing->len] = 0;
	birch_send(b, server, l->middle[0], "%s", buf);

}

void
lisp_interpret(struct birch *b, const char *l)
{
	/* TODO: Thread stuff. */
	if (!global) global = new_environment();

	struct lexer *lexer = new_lexer("wtfffff", l);
	struct value *shite = parse(global, lexer);

	struct value *e = eval(global, shite);

	if (!e || e->type != VAL_ERROR) return;
	print_error(stdout, e);
}
