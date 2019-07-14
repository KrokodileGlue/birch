struct list {
	void *data;
	struct list *next;
};

void list_add(struct list **l, void *data);
void *list_get(struct list *l, void *data, bool (*cmp)(void *a, void *b));
