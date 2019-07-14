struct birch {
	/* The registry that holds all configuration data. */
	struct tree reg;

	/* Low-level server objects. */
	struct list *server;

	/* The global Lisp environment. */
	struct env *env;
};

struct birch *birch_new(struct tree reg);
void birch_connect(struct birch *b);
void birch_join(struct birch *b);
void birch(struct birch *b);

/* Public API kinda stuff. */
void birch_paste(struct birch *b,
                 const char *server,
                 const char *chan,
                 const char *buf);
void birch_send(struct birch *b,
                const char *server,
                const char *chan,
                const char *fmt,
                ...);
