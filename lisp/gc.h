struct gc {
	uint64_t *bmp;
	uint64_t *mark;

	struct {
		union {
			kdgu *string;
			value keyword;
			builtin *builtin;
			int integer;

			struct {
				value car, cdr;
			} cell;

			struct {
				kdgu *name;
				value param;
				value body;
				struct env *env;

				value optional;
				value key;
				value rest;
				value docstring;
			} function;
		};

		enum value_type type;
	} *obj;
};

#define keyword(X) (env->gc->obj[(X)].keyword)
#define integer(X) (env->gc->obj[(X)].integer)
#define builtin(X) (env->gc->obj[(X)].builtin)
#define string(X) (env->gc->obj[(X)].string)
#define car(X) (env->gc->obj[(X)].cell.car)
#define cdr(X) (env->gc->obj[(X)].cell.cdr)

#define function(X) (env->gc->obj[(X)].function)
#define optional(X) (env->gc->obj[(X)].function.optional)
#define param(X) (env->gc->obj[(X)].function.param)
#define env(X) (env->gc->obj[(X)].function.env)
#define key(X) (env->gc->obj[(X)].function.key)
#define body(X) (env->gc->obj[(X)].function.body)
#define rest(X) (env->gc->obj[(X)].function.rest)
#define name(X) (env->gc->obj[(X)].function.name)
#define docstring(X) (env->gc->obj[(X)].function.docstring)

#define type(X) (env->gc->obj[X].type)

#define marked(X) (env->gc->mark[(X) / 64] & (1LL << ((X) % 64)))
#define mark(X) (env->gc->mark[(X) / 64]	\
                 = env->gc->mark[(X) / 64]	\
                 | (1LL << ((X) % 64)))

#define unmark(X) (env->gc->mark[(X) / 64]	\
                   = ~(~env->gc->mark[(X) / 64]	\
                       | (1LL << ((X) % 64))))

struct gc *gc_new(void);
value gc_alloc(struct env *env, enum value_type type);
value gc_copy(struct env *env, value v);
void gc_mark(struct env *env, value v);
void gc_sweep(struct env *env);

static value
mkint(struct env *env, int n)
{
	value v = gc_alloc(env, VAL_INT);
	integer(v) = n;
	return v;
}

#define mkint(X) mkint(env, (X));

#define GC_MAX_OBJECT (10000*64)
