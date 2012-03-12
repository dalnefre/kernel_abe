/*
 * gc.h -- garbage collected cell management
 *
 * Copyright 2009 Dale Schumacher.  ALL RIGHTS RESERVED.
 */
#ifndef GC_H
#define GC_H

#include "types.h"

#define	GC_PHASE_Z		(0x00000000U)
#define	GC_PHASE_X		(0x40000000U)
#define	GC_PHASE_0		(0x80000000U)
#define	GC_PHASE_1		(0xC0000000U)
#define	GC_PHASE_MASK	(0x3FFFFFFFU)

#define	as_cell(p)		((CELL*)(p))
#define	as_cons(p)		((CONS*)(p))
#define	as_word(p)		((WORD)(p))
#define	as_indx(p)		(as_word(p) >> 2)
#define	as_addr(p)		as_cell(as_word(p) << 2)

#define	GC_SIZE(p)		as_word((p)->first)
#define	GC_SET_SIZE(p,n) ((p)->first = as_cons(n))
#define	GC_MARK(p)		((p)->_prev & ~GC_PHASE_MASK)
#define	GC_SET_MARK(p,m) ((p)->_prev = (((p)->_prev & GC_PHASE_MASK) | (m)))

#define	GC_FIRST(p)		((p)->first)
#define	GC_SET_FIRST(p,q) ((p)->first = (q))
#define	GC_REST(p)		((p)->rest)
#define	GC_SET_REST(p,q) ((p)->rest = (q))

#define	GC_PREV(p)		as_addr((p)->_prev)
#define	GC_SET_PREV(p,q) ((p)->_prev = as_indx(q) | GC_MARK(p))
#define	GC_NEXT(p)		as_addr((p)->_next)
#define	GC_SET_NEXT(p,q) ((p)->_next = as_indx(q))

extern CELL		gc_aged__cell;		/* list head for aged (possibly allocated) cells */
extern CELL		gc_scan__cell;		/* list head for cells to be scanned */
extern CELL		gc_fresh__cell;		/* list head for fresh (recently allocated) cells */
extern CELL		gc_free__cell;		/* list head for free (unallocated) cells */
extern CELL		gc_perm__cell;		/* list head for permanently allocated cells */

#define	GC_AGED_LIST	(&gc_aged__cell)
#define	GC_SCAN_LIST	(&gc_scan__cell)
#define	GC_FRESH_LIST	(&gc_fresh__cell)
#define	GC_FREE_LIST	(&gc_free__cell)
#define	GC_PERM_LIST	(&gc_perm__cell)

void	gc_insert_before(CELL* p, CELL* item);	/* insert <item> before <p> in list */
void	gc_insert_after(CELL* p, CELL* item);	/* insert <item> after <p> in list */
CELL*	gc_extract(CELL* item);					/* extract <item> from list */

void	gc_push(CELL* list, CELL* item);		/* insert <item> at the beginning of <list> */
CELL*	gc_pop(CELL* list);						/* extract <item> from the beginning of <list> */
void	gc_put(CELL* list, CELL* item);			/* insert <item> at the end of <list> */
CELL*	gc_pull(CELL* list);					/* extract <item> from the end of <list> */

void	gc_append_list(CELL* to, CELL* from);	/* append <from> at the end of <to> */
WORD	gc_count(CELL* list);					/* count items in <list> */
void	gc_sanity_check(CELL* list);			/* check <list> for internal consistency */

CONS*	gc_perm(CONS* first, CONS* rest);		/* allocate and initialize a permanent cell */
CONS*	gc_cons(CONS* first, CONS* rest);		/* allocate and initialize a new "cons" cell */
CONS*	gc_first(CONS* cell);					/* retrieve the first of the list */
CONS*	gc_rest(CONS* cell);					/* retrieve the rest of the list */
void	gc_set_first(CONS* cell, CONS* first);	/* overwrite the first of the list */
void	gc_set_rest(CONS* cell, CONS* rest);	/* overwrite the rest of the list */

void	gc_full_collection(CONS* root);			/* perform a full garbage collection (NOT CONCURRENT!) */
void	gc_actor_collection(CONFIG* cfg, CONS* root); /* initiate actor-based (CONCURRENT) collection */
void	test_gc();								/* internal unit test */
void	report_cell_usage();					/* display cell usage statistics */

#endif /* GC_H */
