/*
 * VOLATILE: `VAL_NIL` must always be zero---the type of an object is
 * often treated as a boolean.
 */

enum value_type {
	VAL_NIL,

	VAL_INT,
	VAL_CELL,
	VAL_STRING,
	VAL_SYMBOL,
	VAL_BUILTIN,
	VAL_FUNCTION,
	VAL_MACRO,
	VAL_ENV,
	VAL_KEYWORDPARAM,
	VAL_KEYWORD,
	VAL_COMMA,
	VAL_COMMAT,

	VAL_TRUE,

	/* Dummies only used by the parser. */
	VAL_RPAREN,
	VAL_DOT,
	VAL_EOF,

	/* Hot potatoes. */
	VAL_ERROR,
};

typedef int value;

/* Environment. */
struct env {
	value vars;
	struct env *up;

	/*
	 * The names of the server and channel this environment is
	 * associated with, the name of the server and "global", or
	 * both are "global".
	 */
	char *server, *channel;

	struct birch *birch;
	struct object *obj;

	struct gc *gc;

	/* Security. */
	bool protect;
	int recursion_limit;
	int depth;
};

struct env *new_environment(struct birch *b,
                            const char *server,
                            const char *channel);
struct env *push_env(struct env *env, value vars, value values);
struct env *make_env(struct env *env, value map);
typedef value builtin(struct env *, value);
value list_length(struct env *env, value list);
value quote(struct env *env, value v);
value backtick(struct env *env, value v);
value add_variable(struct env *env, value sym, value body);
void add_builtin(struct env *env, const char *name, builtin *f);
value find(struct env *env, value sym);
void value_free(value v);
value cons(struct env *env, value car, value cdr);
value acons(struct env *env, value x, value y, value a);
value make_symbol(struct env *env, const char *s);
value expand(struct env *env, value v);
value print_value(struct env *env, value v);

/*
 * A global array mapping each type (as an integer index into the
 * array) to a string representing that type.
 */
const char **value_name;

value DOT, RPAREN, NIL, TRUE, VEOF;

/*
 * I think this reports the type as a character because in the parser
 * sometimes values actually represent characters. TODO?
 */
#define TYPE_NAME(X) (X > VAL_ERROR ? (char []){X, 0} : value_name[X])
#define IS_LIST(X) (type(X) == VAL_NIL || type(X) == VAL_CELL)
