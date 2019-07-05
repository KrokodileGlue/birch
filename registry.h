struct tree {
	enum tree_type {
		TREE_NIL,
		TREE_STRING,
		TREE_INT,
		TREE_BOOL,
		TREE_TABLE
	} type;

	union {
		char *string;
		int integer;
		struct table *table;
		bool boolean;
	};
};

#define REG_NIL ((struct tree){TREE_NIL,{0}})

struct tree *tree_new(enum tree_type type);

void reg_set_string(struct tree reg, const char *key, const char *val);
void reg_set_int(struct tree reg, const char *key, int val);
void reg_set_bool(struct tree reg, const char *key, bool val);
struct tree reg_get(struct tree reg, const char *fmt, ...);
void reg_print(struct tree reg, int depth);

static struct tree
reg_new(void)
{
	return (struct tree){TREE_TABLE, .table = table_new()};
}
