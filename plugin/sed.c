#include <stdarg.h>
#include <stdio.h>

#include <kdg/kdgu.h>
#include <kdg/ktre.h>

#include "../birch.h"

static ktre *a;

struct channel {
	char name[32];
	char *speaker[2048];
	kdgu *hist[2048];
} chan[50];

void
hook(struct birch *b, const char *server, struct line *l)
{
	kdgu **hist = NULL;
	char **speaker = NULL;

	for (int i = 0; i < 50; i++) {
		if (!strcmp(chan[i].name, l->middle[0])) {
			hist = chan[i].hist;
			speaker = chan[i].speaker;
			break;
		}

		if (!strcmp(chan[i].name, "")) {
			strcpy(chan[i].name, l->middle[0]);
			hist = chan[i].hist;
			speaker = chan[i].speaker;
			break;
		}
	}

	int **vec = NULL;

	if (ktre_exec(a, &KDGU(l->trailing), &vec)) {
		kdgu *A = ktre_getgroup(vec, 0, 2, &KDGU(l->trailing));
		kdgu *B = ktre_getgroup(vec, 0, 3, &KDGU(l->trailing));
		kdgu *C = ktre_getgroup(vec, 0, 4, &KDGU(l->trailing));

		int mode = KTRE_UNANCHORED;

		for (unsigned i = 0; i < C->len; i++) {
			switch (C->s[i]) {
			case 'g': mode |= KTRE_GLOBAL; break;
			case 'i': mode |= KTRE_INSENSITIVE; break;
			}
		}

		ktre *regex = ktre_compile(A, mode | KTRE_DEBUG);
		if (!regex) return;

		if (regex->err) {
			birch_send(b, server, l->middle[0], "%s: %s", l->nick, regex->err_str);
			return;
		}

		kdgu *thing = NULL;
		char *nick = NULL;

		for (int i = 0; i < 2048; i++) {
			if (!hist[i]) return;
			if (!ktre_exec(regex, hist[i], &vec)) continue;
			thing = ktre_filter(regex, hist[i], B, &KDGU("\\"));
			nick = speaker[i];
			break;
		}

		if (!thing) return;

		char buf[256];
		memcpy(buf, thing->s, thing->len);
		buf[thing->len] = 0;
		if (!strcmp(l->nick, nick))
			birch_send(b, server, l->middle[0], "%s meant to say: %s", nick, buf);
		else
			birch_send(b, server, l->middle[0], "%s thinks %s meant to say: %s", l->nick, nick, buf);
	} else {
		memmove(hist + 1, hist, (2048 - 1) * sizeof *hist);
		memmove(speaker + 1, speaker, (2048 - 1) * sizeof *speaker);
		if (!strncmp(l->trailing, "\01ACTION ", 8)) {
			char thing[256];
			sprintf(thing, "* %s %s", l->nick, l->trailing + 8);
			*hist = kdgu_news(thing);
		} else
			*hist = kdgu_news(l->trailing);
		*speaker = strdup(l->nick);
	}
}

void
reg(struct birch *b)
{
	a = ktre_compile(&KDGU("^s([[:punct:]])(.*?)\\1(.*?)\\1(\\w*)$"), 0);
	birch_hook_msg(b, hook);
}
