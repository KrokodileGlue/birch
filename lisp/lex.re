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

#define YYCTYPE unsigned char
#define YYFILL(X) do {} while (0)
#define YYMARKER (*a)
#define YYCURSOR (*b)

static int
lex(const unsigned char **a,
     const unsigned char **b,
     const unsigned char *YYLIMIT)
{
 loop:
	YYMARKER = YYCURSOR;

	/*!re2c
	dec = [1-9][0-9]*;
	bin = '0b' [01]+;
	hex = '0x' [0-9a-fA-F]+;
	oct = '0' [0-7]*;
	'\043/' [^/\000]* '/' { return TOK_RAW_STR; }
	'"' {
		while (*YYCURSOR && *YYCURSOR != '"') {
			if (*YYCURSOR == '\\') YYCURSOR++;
			YYCURSOR++;
		}
		if (*YYCURSOR != '"') return -1;
		YYCURSOR++;
		return TOK_STR;
	}
	";" [^\n\000]* { goto loop; }
	[ \t\v\r\n] { goto loop; }
	[a-zA-Z\[\]\{\}+=!$%^/\\`|*<>-]* { return TOK_IDENT; }
	[!-/:-@[-`{-~] { return **a; }
	dec { return TOK_INT; }
	bin | hex | oct { YYMARKER--; return TOK_INT; }
	* { YYCURSOR--; return TOK_EOF; }
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
		case '\\': r[j++] = '\\'; break;
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

	t->type = lex((const unsigned char **)&a,
	              (const unsigned char **)&b,
	              (const unsigned char *)l->e);
	if (t->type == TOK_EOF) return free(t), NULL;
	if (!b) return free(t), NULL;

	/* Assign the basic fields that all tokens have. */

	l->len = b - a;

	t->body = malloc(b - a + 1);
	memcpy(t->body, a, b - a);
	t->body[b - a] = 0;

	t->idx = l->idx;
	t->len = l->len;

	/* Fill out the type-specific fields. */

	switch (t->type) {
	case TOK_STR:
		t->s = kdgu_news(lex_escapes(t));
		break;
	case TOK_INT:
		t->i = strtol(t->body, NULL, 0);
		break;
	case TOK_RAW_STR:
		t->s = kdgu_new(KDGU_FMT_UTF8,
		                t->body + 1,
		                strlen(t->body) - 2);
		t->type = TOK_STR;
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
