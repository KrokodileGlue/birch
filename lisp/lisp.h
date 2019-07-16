struct value {
	enum value_type {
		VAL_NULL,
		VAL_NIL,

		VAL_INT,
		VAL_CELL,
		VAL_STRING,
		VAL_SYMBOL,
		VAL_BUILTIN,
		VAL_FUNCTION,
		VAL_MACRO,
		VAL_ENV,
		VAL_ARRAY,
		VAL_KEYWORDPARAM,
		VAL_KEYWORD,
		VAL_COMMA,
		VAL_COMMAT,

		/* Non-GC values. */
		VAL_TRUE,

		/* Dummies only used by the parser. */
		VAL_RPAREN,
		VAL_DOT,

		/* Hot potatoes. */
		VAL_ERROR,
		VAL_NOTE,
	} type;

	union {
		/*
		 * Index to the object stored by the GC system (if there is
		 * one).
		 */
		unsigned obj;
		int integer;
	};
};

/* Environment. */
struct env {
	struct value vars;
	struct env *up;

	/*
	 * The names of the server and channel this environment is
	 * associated with as they appears in the registry, or both
	 * are `global`.
	 */
	char *server, *channel;

	struct birch *birch;
	struct object *obj;

	/*
	 * The accumulation of the output of all print statements
	 * evaluated so far. This is reset in `../builtin.c` after
	 * each command is executed.
	 */
	kdgu *output;
};

struct env *new_environment(struct birch *b,
                            const char *server,
                            const char *channel);

struct env *push_env(struct env *env,
                     struct value vars,
                     struct value values);

struct env *make_env(struct env *env,
                     struct value map);

typedef struct value builtin(struct env *, struct value);

struct value list_length(struct env *env,
                         struct value list);

struct value quote(struct env *env,
                   struct value v);

struct value backtick(struct env *env,
                      struct value v);

struct value add_variable(struct env *env,
                          struct value sym,
                          struct value body);

void add_builtin(struct env *env,
                 const char *name,
                 builtin *f);

struct value find(struct env *env,
                  struct value sym);

void value_free(struct value v);

struct value cons(struct env *env,
                  struct value car,
                  struct value cdr);

struct value acons(struct env *env,
                   struct value x,
                   struct value y,
                   struct value a);

struct value make_symbol(struct env *env,
                         const char *s);

struct value expand(struct env *env,
                    struct value v);

struct value print_value(struct env *env,
                         struct value v);

/*
 * A global array mapping each type (as an integer index into the
 * array) to a string representing it.
 */
const char **value_name;

#define DOT (struct value){VAL_DOT,{0}}
#define RPAREN (struct value){VAL_RPAREN,{0}}
#define NIL (struct value){VAL_NIL,{0}}
#define TRUE (struct value){VAL_TRUE,{0}}
#define VNULL (struct value){VAL_NULL,{0}}

/*
 * I think this reports the type as a character because in the parser
 * sometimes values actually represent characters. TODO?
 */
#define TYPE_NAME(X) (X > VAL_NOTE ? (char []){X, 0} : value_name[X])
#define IS_LIST(X) ((X).type == VAL_NIL || (X).type == VAL_CELL)
