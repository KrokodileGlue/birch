struct gc {
	uint64_t *bmp;
	uint64_t *mark;

	struct {
		union {
			kdgu *string;

			struct {
				struct value car, cdr;
			} cell;

			struct {
				kdgu *name;
				struct value param;
				struct value body;
				struct env *env;

				struct value optional;
				struct value key;
				struct value rest;
				struct value docstring;
			} function;

			struct value keyword;

			builtin *builtin;
		};

		enum value_type type;
	} *obj;
};

#define keyword(X) (env->gc->obj[(X).obj].keyword)
#define builtin(X) (env->gc->obj[(X).obj].builtin)
#define string(X) (env->gc->obj[(X).obj].string)
#define car(X) (env->gc->obj[(X).obj].cell.car)
#define cdr(X) (env->gc->obj[(X).obj].cell.cdr)

#define function(X) (env->gc->obj[(X).obj].function)
#define optional(X) (env->gc->obj[(X).obj].function.optional)
#define param(X) (env->gc->obj[(X).obj].function.param)
#define env(X) (env->gc->obj[(X).obj].function.env)
#define key(X) (env->gc->obj[(X).obj].function.key)
#define body(X) (env->gc->obj[(X).obj].function.body)
#define rest(X) (env->gc->obj[(X).obj].function.rest)
#define name(X) (env->gc->obj[(X).obj].function.name)
#define docstring(X) (env->gc->obj[(X).obj].function.docstring)

#define type(X) (env->gc->obj[X].type)

#define marked(X) (env->gc->mark[(X).obj / 64]	\
                   & (1LL << ((X).obj % 64)))

#define mark(X) (env->gc->mark[(X).obj / 64]	\
                 = env->gc->mark[(X).obj / 64]	\
                 | (1LL << ((X).obj % 64)))

#define unmark(X) (env->gc->mark[(X).obj / 64]	\
                   = ~(~env->gc->mark[(X).obj / 64]	\
                       | (1LL << ((X).obj % 64))))

struct gc *gc_new(void);
struct value gc_alloc(struct env *env, enum value_type type);
struct value gc_copy(struct env *env, struct value v);
void gc_mark(struct env *env, struct value v);
void gc_sweep(struct env *env);

#define GC_MAX_OBJECT (10000*64)
