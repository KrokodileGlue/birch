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
