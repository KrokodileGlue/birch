#include <string.h>
#include <stdlib.h>
#include <kdg/kdgu.h>

#include "lex.h"

struct lexer *
new_lexer(const char *file, const char *s)
{
	struct lexer *l = malloc(sizeof *l);
	memset(l, 0, sizeof *l);

	l->s = s;
	l->e = s + strlen(s);
	l->file = file;

	return l;
}

#define YYCTYPE char
#define YYFILL(X) do {} while (0)
#define YYMARKER (*a)
#define YYCURSOR (*b)

static int
lex(const char **a,
     const char **b,
     unsigned *line,
     unsigned *column,
     const char *YYLIMIT)
{
 loop:
	YYMARKER = YYCURSOR;
	/*!re2c
	  dec = [1-9][0-9]*;
	  bin = '0b' [01]+;
	  hex = '0x' [0-9a-fA-F]+;
	  oct = '0' [0-7]*;
	  ident = [a-zA-Z+=!#$%^/*<>-];
	  "\n" { (*line)++, *column = 0; goto loop; }
	  [ \t\v\r] { (*column)++; goto loop; }
	  "#" .* { goto loop; }
	  ident [a-zA-Z0-9=-]* { return TOK_IDENT; }
	  [!-/:-@[-`{-~] { return **a; }
	  '"' ("\\\""|[^"])* '"' { return TOK_STR; }
	  dec { return TOK_INT; }
	  bin | hex | oct { YYMARKER--; return TOK_INT; }
	  * { *b = NULL; return -1; }
	  "\000" { YYCURSOR--; return TOK_EOF; }
	*/
}

static char *
lex_escapes(struct token *t)
{
	char *r = malloc(t->len + 1);
	unsigned j = 0;

	for (unsigned i = 1; i < t->len - 1; i++) {
		if (t->body[i] != '\\') {
			r[j++] = t->body[i];
			continue;
		}
		i++;
		switch (t->body[i]) {
		case 'n': r[j++] = '\n'; break;
		case 't': r[j++] = '\t'; break;
		case '"': r[j++] = '"'; break;
		case '\n': break;
		default:
			r[j++] = '\\';
			r[j++] = t->body[i];
		}
	}

	return r[j] = 0, r;
}

struct token *
tok(struct lexer *l)
{
	struct token *t = malloc(sizeof *t);
	const char *a = l->s + l->idx;
	const char *b = a;

	t->type = lex(&a, &b, &l->line, &l->column, l->e);
	if (t->type == TOK_EOF) return free(t), NULL;
	if (!b) return free(t), NULL;

	/* Assign the basic fields that all tokens have. */

	l->len = b - a;

	t->body = malloc(b - a + 1);
	memcpy(t->body, a, b - a);
	t->body[b - a] = 0;

	t->idx = l->idx;
	t->len = l->len;
	t->line = l->line;
	t->column = l->column;

	/*
	 * `lex()` can't count lines and columns inside token bodies,
	 * so we'll do it here.
	 */

	for (unsigned i = 0; i < l->len; i++) {
		if (t->body[i] == '\n')
			l->line++, l->column = 0;
		else l->column++;
	}

	/* Fill out the type-specific fields. */

	switch (t->type) {
	case TOK_STR:
		t->s = kdgu_news(lex_escapes(t));
		break;
	case TOK_INT:
		t->i = atoi(t->body);
		break;
	default:;
	}

	/*
	 * Update the lexer stream position by moving it to the end of
	 * the current token.
	 */

	l->idx = b - l->s;

	return t;
}
