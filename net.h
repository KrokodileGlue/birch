struct net *net_open(const char *name, int port);
void net_close(struct net *n);

/*
 * Reads a line from the server.
 */

void net_read(const struct net *n, char *buf);

/*
 * Sends a formatted string to the server. Does not concern itself
 * with the validness of the string as a single IRC command, e.g. it
 * doesn't check for (or add) \r\n at the end of the transmission.
 */

void net_send(const struct net *n, const char *fmt, ...);
