#define TABLE_SIZE 32

struct table {
	struct bucket {
		uint64_t *h;
		char **key;
		struct tree *val;
		size_t len;
	} bucket[TABLE_SIZE];
};

struct table *table_new();
struct table *table_copy(struct table *t);
void table_free(struct table *t);
struct tree table_lookup(struct table *t, char *key);
struct tree table_add(struct table *t, char *key, struct tree v);

#define KEY (t_->bucket[i].key[j])
#define VAL (table_lookup(t_, t_->bucket[i].key[j]))

#define TABLE_FOR(T)	  \
	struct table *t_ = T; \
	for (size_t i = 0; i < TABLE_SIZE; i++) \
		for (size_t j = 0; j < t_->bucket[i].len; j++)
