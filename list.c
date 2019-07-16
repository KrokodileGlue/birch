#include <stdlib.h>
#include <stdbool.h>

#include "list.h"

void
list_add(struct list **l, void *data)
{
	if (!data) return;

	if (!*l) {
		*l = malloc(sizeof **l);
		(*l)->data = data;
		(*l)->next = NULL;
		return;
	}

	struct list *head = *l;

	while (head->next) head = head->next;
	head->next = malloc(sizeof *head);
	head = head->next;
	head->data = data;
	head->next = NULL;
}

void *
list_get(struct list *l, void *data, bool (*cmp)(void *a, void *b))
{
	while (l) {
		if (cmp(l->data, data)) return l->data;
		l = l->next;
	}

	return NULL;
}
