char *load_file(const char *p);
uint64_t hash(const char *d, size_t len);
char *tostring(const struct kdgu *k);
int tokenize(const char *str,
             const char *delim,
             char ***tok,
             unsigned *len);
