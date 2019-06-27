#pragma once
#include "birch.h"

#include "lisp/error.h"
#include "lisp/lex.h"
#include "lisp/util.h"
#include "lisp/parse.h"
#include "lisp/eval.h"

void lisp_interpret_line(struct birch *b, const char *server, struct line *l);
void lisp_interpret(struct birch *b, const char *line);
