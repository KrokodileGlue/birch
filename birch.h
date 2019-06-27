#pragma once

#include "registry.h"
#include "list.h"
#include "irc.h"

struct birch {
	/* The registry that holds all configuration data. */
	struct tree reg;

	/* Low-level server objects. */
	struct list *server;

	/* All active and inactive plugins. */
	struct list *plugin;

	struct list *msg_hook;
};

struct birch *birch_new(struct tree reg);
void birch_connect(struct birch *b);
void birch_join(struct birch *b);
void birch(struct birch *b);

typedef void (*msg_hook)(struct birch *, const char *, struct line *);

/* Public API kinda stuff. */
void birch_send(struct birch *b,
                const char *server,
                const char *chan,
                const char *fmt,
                ...);

/* Plugin related things. */
void birch_plug(struct birch *b, const char *path);
void birch_hook_msg(struct birch *b, msg_hook f);
