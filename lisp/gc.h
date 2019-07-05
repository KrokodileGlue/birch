struct object {
	union {
		kdgu *string;

		/* Cell. */
		struct {
			struct value car, cdr;
		};

		/* Function. */
		struct {
			kdgu *name;
			struct value param;
			struct value body;
			struct env *env;

			struct value optional;
			struct value key;
			struct value rest;
		};

		/* Array. */
		struct {
			struct value *array;
			unsigned num;
		};

		/* Keyword. */
		struct value keyword;

		/* Builtin. */
		builtin *builtin;
	};

	struct value docstring;
	bool used;
};

#define keyword(X) (env->obj[(X).obj].keyword)
#define builtin(X) (env->obj[(X).obj].builtin)
#define string(X) (env->obj[(X).obj].string)
#define array(X) (env->obj[(X).obj].array)
#define car(X) (env->obj[(X).obj].car)
#define cdr(X) (env->obj[(X).obj].cdr)
#define num(X) (env->obj[(X).obj].num)

#define obj(X) (env->obj[(X).obj])
#define optional(X) (env->obj[(X).obj].optional)
#define param(X) (env->obj[(X).obj].param)
#define env(X) (env->obj[(X).obj].env)
#define key(X) (env->obj[(X).obj].key)
#define body(X) (env->obj[(X).obj].body)
#define rest(X) (env->obj[(X).obj].rest)
#define name(X) (env->obj[(X).obj].name)
#define docstring(X) (env->obj[(X).obj].docstring)

#define GC_MAX_OBJECT 2048

struct value gc_alloc(struct env *env, enum value_type type);
struct value gc_copy(struct env *env, struct value v);
//void gc_mark(struct env *env, struct value *v);
