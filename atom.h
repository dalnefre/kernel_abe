/*
 * atom.h -- Atomic symbol management
 *
 * Copyright 2008-2009 Dale Schumacher.  ALL RIGHTS RESERVED.
 */
#ifndef ATOM_H
#define ATOM_H

#include <stddef.h>
#include "cons.h"

CONS*	lu_cons(CONS* a, CONS* d);			/* allocate a permanent cell (not garbage-collected) */
CONS*	lu_extend_atom(CONS* atom, int c);	/* return <atom> + <c> as a new atom */
CONS*	lu_atom(char* symbol); 				/* lookup (or create) atom for <symbol> */
char*	atom_str(CONS* atom);	/* warning: returns pointer to static buffer, do not nest calls! */

#define	ATOM(s)		lu_atom(s)
#define	ATOM_X(a,x)	lu_extend_atom((a),(x))
#define	NUMBER(n)	MK_NUMBER(n)

void	test_number();
void	report_atom_usage();
void	test_atom();

#endif /* ATOM_H */
