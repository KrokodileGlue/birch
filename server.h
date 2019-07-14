struct server {
	char *name;
	struct net *net;
};

struct server *server_new(const char *name, struct tree reg);
void server_join(struct server *s, const char *chan);
bool server_cmp(void *a, void *b);
