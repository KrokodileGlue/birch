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
	while (getchar() != 'q');
	curl_global_cleanup();

	return 0;
}
