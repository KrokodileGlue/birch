struct value progn(struct env *env, struct value v);
struct value eval_list(struct env *env, struct value v);
struct value eval(struct env *env, struct value v);
