/*
 * emit.h -- Character-stream i/o for cons cells
 *
 * NOTE: THESE PROCEDURES ARE INTENDED FOR DEBUGGING ONLY
 *       THEIR IMPLEMENTATION IS EXPEDIENT AND UGLY!
 *
 * Copyright 2008 Dale Schumacher.  ALL RIGHTS RESERVED.
 */
#ifndef EMIT_H
#define EMIT_H

#include "cons.h"

void	file_emit(char c, void* ctx);
void	emit_cons(CONS* cons, int indent, void (*emit)(char c, void* ctx), void* ctx);
char*	cons_to_str(CONS* cons);	/* warning: returns pointer to static buffer, do not nest calls! */
CONS*	str_to_cons(char* s);

void	test_emit();
void	test_str_to_cons();

#endif /* EMIT_H */
