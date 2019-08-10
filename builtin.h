struct value builtin_in(struct env *env, struct value v);
struct value builtin_connect(struct env *env, struct value v);
struct value builtin_join(struct env *env, struct value v);
struct value builtin_stdout(struct env *env, struct value v);
struct value builtin_boundp(struct env *env, struct value v);
struct value builtin_current_server(struct env *env, struct value v);
struct value builtin_current_channel(struct env *env, struct value v);
