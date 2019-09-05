struct birch {
	/* Low-level server objects. */
	struct list *server;

	/* Global Lisp environment. */
	struct env *env;

	/* Channel-specific Lisp environments. */
	struct list *channel;
};

struct birch *birch_new(void);
struct server *birch_connect(struct birch *b,
                             const char *network,
                             const char *address,
                             int port,
                             const char *user,
                             const char *nick,
                             const char *realname);
int birch_join(struct birch *b,
               const char *serv,
               const char *chan);
void birch(struct birch *b);

struct data {
	struct birch *b;
	struct server *server;
};

void *birch_main(void *data);

/* Public API kinda stuff. */
void birch_paste(struct birch *b,
                 const char *server,
                 const char *chan,
                 const char *str);
void birch_send(struct birch *b,
                const char *server,
                const char *chan,
                bool paste,
                const char *fmt,
                ...);
struct env *birch_get_env(struct birch *b,
                          const char *server,
                          const char *channel);
int birch_config(struct birch *b,
                 const char *path);
void send_value(struct birch *b,
                struct env *env,
                const char *server,
                const char *channel,
                value v);
