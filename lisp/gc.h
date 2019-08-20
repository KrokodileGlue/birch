struct gc {
	int64_t slot[VAL_ERROR + 1];
	uint64_t *bmp[VAL_ERROR + 1];

	kdgu **string;

	struct {
		struct value car, cdr;
	} *cell;

	struct {
		kdgu *name;
		struct value param;
		struct value body;
		struct env *env;

		struct value optional;
		struct value key;
		struct value rest;
		struct value docstring;
	} *function;

	struct value *keyword;

	builtin **builtin;

};

#define keyword(X) (env->gc->keyword[(X).obj])
#define builtin(X) (env->gc->builtin[(X).obj])
#define string(X) (env->gc->string[(X).obj])
#define car(X) (env->gc->cell[(X).obj].car)
#define cdr(X) (env->gc->cell[(X).obj].cdr)

#define function(X) (env->gc->function[(X).obj])
#define optional(X) (env->gc->function[(X).obj].optional)
#define param(X) (env->gc->function[(X).obj].param)
#define env(X) (env->gc->function[(X).obj].env)
#define key(X) (env->gc->function[(X).obj].key)
#define body(X) (env->gc->function[(X).obj].body)
#define rest(X) (env->gc->function[(X).obj].rest)
#define name(X) (env->gc->function[(X).obj].name)
#define docstring(X) (env->gc->function[(X).obj].docstring)

struct gc *gc_new(void);
struct value gc_alloc(struct env *env, enum value_type type);
struct value gc_copy(struct env *env, struct value v);
//void gc_mark(struct env *env, struct value *v);
