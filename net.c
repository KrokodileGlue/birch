#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>

struct net {
	int sock;
	struct hostent *server;
	struct sockaddr_in addr;
};

static int read_char(int sock)
{
	int c, r = recv(sock, &c, 1, 0);
	return r <= 0 ? EOF : c;
}

void net_read(const struct net *n, char *buf)
{
	size_t len = 0;
	while (len >= 2
	       ? strncmp(buf + len - 2, "\r\n", 2)
	       : len >= 1 ? buf[len - 1] != EOF : 1)
		buf[len++] = read_char(n->sock), buf[len] = 0;
	len >= 2 ? (buf[len - 2] = 0) : (buf[len - 1] = 0);
}

void net_send(const struct net *n, const char *fmt, ...)
{
	//LOG("Sending: %s", l);
	/* TODO: Check this return value. */
	char buf[256];

	va_list args;
	va_start(args, fmt);
	vsnprintf(buf, sizeof buf, fmt, args);
	va_end(args);

	send(n->sock, buf, strlen(buf), 0);
}

struct net *net_open(const char *name, int port)
{
	struct net *s = malloc(sizeof *s);

	s->sock = socket(AF_INET, SOCK_STREAM, 0);
	s->server = gethostbyname(name);

	if (s->sock < 0 || !s->server) return free(s), NULL;

	memset(&s->addr, 0, sizeof s->addr);
	s->addr.sin_family = AF_INET;
	memcpy(&s->addr.sin_addr.s_addr,
	       s->server->h_addr_list[0],
	       s->server->h_length);
	s->addr.sin_port = htons(port);

	//LOG("Opening network connection to %s:%d...\n", name, port);

	if (connect(s->sock, (void *)&s->addr, sizeof s->addr) < 0)
		return free(s), NULL;

	return s;
}

void net_close(struct net *s)
{
	/* TODO: Check return and errors. */
	shutdown(s->sock, 2);
}
