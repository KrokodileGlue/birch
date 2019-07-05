struct server {
	char *name;
	struct net *net;
};

struct server *server_new(const char *name, struct tree reg);
void server_join(struct server *s, const char *chan);

static bool
server_cmp(void *a, void *b)
{
	return !strcmp(((struct server *)a)->name, b);
}
