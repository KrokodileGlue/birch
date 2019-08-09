enum {
	CMD_PASS,
	CMD_NICK,
	CMD_USER,
	CMD_SERVER,
	CMD_OPER,
	CMD_QUIT,
	CMD_SQUIT,
	CMD_JOIN,
	CMD_PART,
	CMD_MODE,
	CMD_TOPIC,
	CMD_NAMES,
	CMD_LIST,
	CMD_INVITE,
	CMD_KICK,
	CMD_VERSION,
	CMD_STATS,
	CMD_LINKS,
	CMD_TIME,
	CMD_CONNECT,
	CMD_TRACE,
	CMD_ADMIN,
	CMD_INFO,
	CMD_PRIVMSG,
	CMD_NOTICE,
	CMD_WHO,
	CMD_WHOIS,
	CMD_WHOWAS,
	CMD_KILL,
	CMD_PING,
	CMD_PONG,
	CMD_ERROR,
	CMD_AWAY,
	CMD_REHASH,
	CMD_RESTART,
	CMD_SUMMON,
	CMD_USERS,
	CMD_WALLOPS,
	CMD_USERHOST,
	CMD_ISON
};

struct line {
	enum {
		LINE_REPLY,
		LINE_CMD
	} type;

	char *text, *prefix, **middle, *trailing;
	char *nick, *ident, *host, *date;
	unsigned num_middle;
	int cmd, reply;
};

struct line *line_new(const char *s, time_t timer);
void line_free(struct line *l);
