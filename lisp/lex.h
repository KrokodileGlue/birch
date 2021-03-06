struct token {
	/*
	 * There's no point in giving punctuation characters their own
	 * token types or data field; we can just use the type field
	 * to store it instead, that way we can just do stuff like:
	 *
	 *   tok->type == '.'
	 *
	 * N.B. TOK_EOF must always be the final value in this enum.
	 */

	enum {
		TOK_INT,
		TOK_RAW_STR,
		TOK_STR,
		TOK_IDENT,
		TOK_EOF
	} type;

	char *body;            /* Region of input this is from.     */
	unsigned idx, len;

	/*
	 * Identifiers don't have their own data field because their
	 * fields would be identical to their bodies.
	 */

	union {
		kdgu *s;       /* String contents.                  */
		int i;         /* Integer.                          */
	};
};

struct lexer {
	const char *file;      /* Filename.                         */
	const char *s;         /* Input stream.                     */
	const char *e;         /* End of input stream.              */
	unsigned idx;          /* Current character.                */
	unsigned len, line, column;
};

struct lexer *new_lexer(const char *file, const char *s);
struct token *tok(struct lexer *l);
