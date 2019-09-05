value progn(struct env *env, value v);
value eval_list(struct env *env, value v);
value eval(struct env *env, value v);
value eval_string(struct env *env, const char *code);
