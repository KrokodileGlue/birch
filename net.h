struct net *net_open(const char *name, int port);
void net_close(struct net *n);

/*
 * Reads a line from the server.
 */
void net_read(const struct net *n, char *buf);

/*
 * Sends a line to the server.
 */
void net_send(const struct net *n, const char *fmt, ...);
