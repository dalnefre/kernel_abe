/*
 * atom.c -- Atomic symbol management
 *
 * Copyright 2008-2009 Dale Schumacher.  ALL RIGHTS RESERVED.
 */
#include "atom.h"
#include "abe.h"

#include "dbug.h"
DBUG_UNIT("atom");

static CONS*	lu_atom_root = NULL;
static int		lu_cons_cnt = 0;

CONS*
lu_cons(CONS* a, CONS* d)
/* allocate a permanent cell (not garbage-collected) */
{
	CONS* p = NULL;

	p = gc_perm(a, d);
	++lu_cons_cnt;
	XDBUG_PRINT("perm", ("allocated permanent cell #%d @%p[%p;%p]", lu_cons_cnt, p, a, d));
	return p;
}

CONS*
lu_extend_atom(CONS* atom, int c)
/* return <atom> + <c> as a new atom, or atom of <c> if <atom> is NIL */
{
	CONS* root;
	CONS* node;
	CONS* ch = NUMBER(c);

	XDBUG_ENTER("lu_extend_atom");
	XDBUG_PRINT("", ("atom@%p = %s", atom, atom_str(atom)));
	XDBUG_PRINT("", ("c = %c(%d)", (isprint(c)?c:' '), c));
	if (lu_atom_root == NULL) {
		root = lu_cons(NIL, NIL);
		XDBUG_PRINT("", ("lu_atom_root = %p[%p;%p]", root, car(root), cdr(root)));
		lu_atom_root = MK_ATOM(root);
	}
	if ((atom == NULL) || nilp(atom)) {
		atom = lu_atom_root;
	}
	assert(atomp(atom));
	root = MK_CONS(atom);
	XDBUG_PRINT("", ("root = %p[%p;%p]", root, car(root), cdr(root)));
	node = cdr(root);
	for (;;) {
		XDBUG_PRINT("", ("node = %p[%p;%p]", node, car(node), cdr(node)));
		if (nilp(node)) {
			/* suffix not found, extend suffix list with new character */
			XDBUG_PRINT("", ("extending suffix list"));
			node = lu_cons(ch, car(root));
			node = lu_cons(node, NIL);
			rplacd(root, lu_cons(node, cdr(root)));
			break;
		}
		if (car(car(car(node))) == ch) {
			/* suffix found, return "atom" node */
			XDBUG_PRINT("", ("matched suffix list"));
			node = car(node);
			break;
		}
		node = cdr(node);
	}
	atom = MK_ATOM(node);
	assert(atomp(atom));
	XDBUG_RETURN atom;
}

CONS*
lu_atom(char* s)
/* lookup (or create) atom for <s> */
{
	CONS* atom;
	int c;
	
	XDBUG_ENTER("lu_atom");
	XDBUG_PRINT("", ("s@%p=%s", s, s));
	if ((s == NULL) || (*s == '\0')) {
		XDBUG_PRINT("", ("returning NIL"));
		XDBUG_RETURN NIL;
	}
	atom = NIL;
	while ((c = *s++) != '\0') {
		atom = lu_extend_atom(atom, c);
	}
	assert(atomp(atom));
	XDBUG_PRINT("", ("@%p = %s", atom, atom_str(atom)));
	XDBUG_RETURN atom;
}

char*
atom_str(CONS* atom)	/* warning: returns pointer to static buffer, do not nest calls! */
/* return a string representation of an atom */
{
	static char s[256];
	CONS* p;
	int n;
	
	if ((atom == NULL) || !atomp(atom)) {
		return "";
	}
	p = MK_CONS(atom);
	p = car(p);
	n = length(p);
	while (n >= sizeof(s)) {	/* atom too long... truncate it, silently */
		p = cdr(p);
		--n;
	}
	s[n] = '\0';
	while (--n >= 0) {
		s[n] = MK_INT(car(p));
		p = cdr(p);
	}
	assert(nilp(p));
	return s;
}

void
test_atom()
{
	CONS* p;
	CONS* q;
	char* s;
	char buf[256];

	DBUG_ENTER("test_atom");
	TRACE(printf("--test_atom--\n"));
	p = ATOM(NULL);
	q = ATOM("");
	assert(nilp(p));
	assert(p == q);
	assert(atom_str(q)[0] == '\0');
	
	s = "T";
	strncpy(buf, s, sizeof(buf - 1));
	p = ATOM(s);
	q = ATOM(buf);
	assert(p == q);
	q = ATOM("n");
	assert(p != q);
	s = "nil";
	strncpy(buf, s, sizeof(buf - 1));
	p = ATOM(s);
	assert(p != q);
	assert(strcmp(s, atom_str(p)) == 0);
	q = ATOM(buf);
	assert(p == q);
	p = ATOM("F");
	assert(p != q);
	
	p = MK_CONS(lu_atom_root);
	DBUG_PRINT("", ("lu_atom_root=%s", cons_to_str(p)));
	DBUG_RETURN;
}

void
report_atom_usage()
{
	TRACE(printf("lu_cons_cnt=%d\n", lu_cons_cnt));
}

void
test_number()
{
	int i;
	int j;
	CONS* p;
	
	DBUG_ENTER("test_number");
	TRACE(printf("--test_number--\n"));

	j = 0;
	p = NUMBER(j);
	DBUG_PRINT("", ("NUMBER(%d)=16#%08lx", j, (ulint)p));
	assert(numberp(p));
	i = MK_INT(p);
	assert(0 == i);
	assert(!nilp(p));
	assert(!consp(p));
	assert(!atomp(p));
#if NUMBER_IS_FUNC
#if TYPETAG_USES_2LSB
	assert(funcp(p));
	assert(!boolp(p));
#endif
#else
	assert(!funcp(p));
#endif

	j = 1;
	p = NUMBER(j);
	DBUG_PRINT("", ("NUMBER(%d)=16#%08lx", j, (ulint)p));
	assert(numberp(p));
	i = MK_INT(p);
	assert(i == j);
	assert(!nilp(p));
	assert(!consp(p));
	assert(!atomp(p));
#if NUMBER_IS_FUNC
	assert(funcp(p));
#else
	assert(!funcp(p));
#endif
	assert(!boolp(p));

	j = -1;
	p = NUMBER(j);
	DBUG_PRINT("", ("NUMBER(%d)=16#%08lx", j, (ulint)p));
	assert(numberp(p));
	i = MK_INT(p);
	assert(i == j);
	assert(!nilp(p));
	assert(!consp(p));
	assert(!atomp(p));
#if NUMBER_IS_FUNC
	assert(funcp(p));
#else
	assert(!funcp(p));
#endif
	assert(!boolp(p));

	j = 0x07FFFFFF;
	p = NUMBER(j);
	DBUG_PRINT("", ("NUMBER(%d)=16#%08lx", j, (ulint)p));
	assert(numberp(p));
	i = MK_INT(p);
	assert(i == j);

	j = -j;
	p = NUMBER(j);
	DBUG_PRINT("", ("NUMBER(%d)=16#%08lx", j, (ulint)p));
	assert(numberp(p));
	i = MK_INT(p);
	assert(i == j);
	
	DBUG_RETURN;
}
