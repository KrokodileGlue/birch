#pragma once

#include <stdlib.h>
#include <stdbool.h>

#if 0
#define LIST_TYPE(T,Y)	  \
	struct Y { \
		T data; \
		struct Y *next; \
	}

#define LIST(N,Y)	  \
	struct N *Y = NULL

#define LIST_ADD(N,Y,DATA)	  \
	do { \
		struct N *N##_tmp = Y; \
		if (!N##_tmp) { \
			Y = malloc(sizeof *Y); \
			Y->data = DATA; \
			Y->next = NULL; \
		} else { \
			while (N##_tmp->next) \
				N##_tmp = N##_tmp->next; \
			N##_tmp->next = malloc(sizeof *Y); \
			N##_tmp->next->data = DATA; \
			N##_tmp->next->next = NULL; \
		} \
	} while (0)

#define LIST_FOR(T,N,V,Z)	  \
	T V = Z->data; \
	for (struct N *V##_ = Z; \
	     V##_; \
	     V##_ = V##_->next, V##_ && (V = V##_->data))
#endif

struct list {
	void *data;
	struct list *next;
};

static void
list_add(struct list **l, void *data)
{
	if (!data) return;

	if (!*l) {
		*l = malloc(sizeof **l);
		(*l)->data = data;
		(*l)->next = NULL;
		return;
	}

	while ((*l)->next) *l = (*l)->next;
	(*l)->next = malloc(sizeof **l);
	*l = (*l)->next;
	(*l)->data = data;
	(*l)->next = NULL;
}

static void *
list_get(struct list *l, void *data, bool (*cmp)(void *a, void *b))
{
	while (l) {
		if (cmp(l->data, data)) return l->data;
		l = l->next;
	}

	return NULL;
}
