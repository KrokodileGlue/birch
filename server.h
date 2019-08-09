struct server {
	char *name;
	struct net *net;
	pthread_t thread;
};

struct server *server_new(struct birch *b,
                          const char *network,
                          const char *address,
                          int port);
void server_join(struct server *s,
                 const char *chan);
bool server_cmp(void *a,
                void *b);
