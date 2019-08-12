#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include <curl/curl.h>
#include <pthread.h>

#include "lisp/lisp.h"
#include "birch.h"
#include "server.h"
#include "list.h"
#include "net.h"

int
main(void)
{
	curl_global_init(CURL_GLOBAL_ALL);

	struct birch *b = birch_new();
	if (birch_config(b, "birch.lisp")) return 1;

	for (struct list *l = b->server; l; l = l->next) {
		struct server *s = l->data;
		pthread_join(s->thread, NULL);
	}

	curl_global_cleanup();

	return 0;
}
