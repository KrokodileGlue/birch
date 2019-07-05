#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#include "irc.h"

static const char *type_str[] = { "reply", "cmd" };

static const char *cmd_str[] = {
	"PASS",
	"NICK",
	"USER",
	"SERVER",
	"OPER",
	"QUIT",
	"SQUIT",
	"JOIN",
	"PART",
	"MODE",
	"TOPIC",
	"NAMES",
	"LIST",
	"INVITE",
	"KICK",
	"VERSION",
	"STATS",
	"LINKS",
	"TIME",
	"CONNECT",
	"TRACE",
	"ADMIN",
	"INFO",
	"PRIVMSG",
	"NOTICE",
	"WHO",
	"WHOIS",
	"WHOWAS",
	"KILL",
	"PING",
	"PONG",
	"ERROR",
	"AWAY",
	"REHASH",
	"RESTART",
	"SUMMON",
	"USERS",
	"WALLOPS",
	"USERHOST",
	"ISON"
};

int look_up_cmd(const char *cmd)
{
	for (size_t i = 0; i < sizeof cmd_str / sizeof *cmd_str; i++)
		if (!strcmp(cmd_str[i], cmd))
			return i;
	return -1;
}

struct line *line_new(const char *a)
{
	struct line *l = malloc(sizeof *l);
	memset(l, 0, sizeof *l);

	l->text = malloc(strlen(a) + 1);
	strcpy(l->text, a);

	/* Parse the prefix. */
	if (*a == ':') {
		a++;
		const char *b = a;
		while (*b && *b != ' ') b++;
		if (*b != ' ') return line_free(l), NULL;
		l->prefix = malloc(b - a + 1);
		strncpy(l->prefix, a, b - a);
		l->prefix[b - a] = 0;
		a = b + 1;
	}

	/* Parse the command. */
	if (*a) {
		const char *b = a;
		while (*b && *b != ' ') b++;
		char *cmd = malloc(b - a + 1);
		strncpy(cmd, a, b - a);
		cmd[b - a] = 0, a = b;

		if (strlen(cmd) == 3
		    && isdigit(cmd[0])
		    && isdigit(cmd[1])
		    && isdigit(cmd[2])) {
			l->type = LINE_REPLY;
			l->cmd = atoi(cmd);
		} else {
			l->type = LINE_CMD;
			l->cmd = look_up_cmd(cmd);
			free(cmd);
			if (l->cmd < 0) return line_free(l), NULL;
		}
	}

	/* Parameter parsing. */

	/* Parse the middle. */
	while (*a == ' ') {
		while (*a && *a == ' ') a++;
		if (*a == ':' || !*a) break;
		const char *b = a;
		while (*b && *b != ' ') b++;
		char *mid = malloc(b - a + 1);
		strncpy(mid, a, b - a);
		mid[b - a] = 0, a = b;
		l->middle = realloc(l->middle, ++l->num_middle
		                    * sizeof *l->middle);
		l->middle[l->num_middle - 1] = mid;
	}

	/* Parse the trailing. */
	if (*a == ':') {
		a++;
		l->trailing = malloc(strlen(a) + 1);
		strcpy(l->trailing, a);
	}

	if (l->type == LINE_CMD && l->cmd == CMD_PRIVMSG) {
		a = l->prefix;
		const char *b = a;
		while (*b && *b != '!') b++;
		l->nick = malloc(b - a + 1);
		strncpy(l->nick, a, b - a);
		l->nick[b - a] = 0, b++;
		if (*b == '~') b++;
		a = b;
		while (*b && *b != '@') b++;
		l->ident = malloc(b - a + 1);
		strncpy(l->ident, a, b - a);
		l->ident[b - a] = 0, b++, a = b;
		l->host = malloc(strlen(a) + 1);
		strcpy(l->host, a);
	}

	return l;
}

void line_free(struct line *l)
{
	if (!l) return;
	for (unsigned i = 0; i < l->num_middle; i++)
		free(l->middle[i]);
	free(l->text), free(l->prefix);
	free(l->middle), free(l->trailing);
	free(l);
}
