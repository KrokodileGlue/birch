#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "list.h"
#include "net.h"
#include "table.h"
#include "registry.h"
#include "birch.h"

int main(void)
{
	struct tree reg = reg_new();

	reg_set_string(reg, "server.freenode.address", "localhost");
	reg_set_int(reg, "server.freenode.port", 1221);

	reg_set_string(reg, "server.freenode.user", "birch");
	reg_set_string(reg, "server.freenode.nick", "birch");
	reg_set_string(reg, "server.freenode.name", "birch");
	reg_set_string(reg, "server.freenode.realname", "realname");

	//reg_set_bool(reg, "server.freenode.channel.#omp-fanclub.autojoin", true);
	reg_set_bool(reg, "server.freenode.channel.#test.autojoin", true);
	reg_set_bool(reg, "server.freenode.channel.#test2.autojoin", true);

	struct birch *bot = birch_new(reg);

	birch_connect(bot);
	birch_join(bot);
	birch(bot);

	return 0;
}
