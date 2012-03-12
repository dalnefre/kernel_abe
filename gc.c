/*
 * gc.c -- garbage collected cell management
 *
 * This algorithm is based on Henry Baker's "Treadmill"
 *
 * Copyright 2009 Dale Schumacher.  ALL RIGHTS RESERVED.
 */
#include "gc.h"
#include "abe.h"

#include "dbug.h"
DBUG_UNIT("gc");

static WORD	gc_phase__mark = -1U;	/* current garbage collection phase marker */
static WORD	gc_phase__prev = -1U;	/* previous garbage collection phase marker */

CELL	gc_aged__cell = { as_cons(0U), NIL, GC_PHASE_Z, 0U };
CELL	gc_scan__cell = { as_cons(0U), NIL, GC_PHASE_Z, 0U };
CELL	gc_fresh__cell = { as_cons(0U), NIL, GC_PHASE_Z, 0U };
CELL	gc_free__cell = { as_cons(0U), NIL, GC_PHASE_Z, 0U };
CELL	gc_perm__cell = { as_cons(0U), NIL, GC_PHASE_Z, 0U };

static void
gc_initialize()
{
	if (gc_phase__mark != -1U) {
		return;		/* already initialized */
	}
	DBUG_PRINT("", ("gc_initialize"));
	gc_phase__prev = GC_PHASE_0;
	gc_phase__mark = GC_PHASE_1;
	GC_SET_NEXT(GC_AGED_LIST, GC_AGED_LIST);
	GC_SET_PREV(GC_AGED_LIST, GC_AGED_LIST);
	GC_SET_NEXT(GC_SCAN_LIST, GC_SCAN_LIST);
	GC_SET_PREV(GC_SCAN_LIST, GC_SCAN_LIST);
	GC_SET_NEXT(GC_FRESH_LIST, GC_FRESH_LIST);
	GC_SET_PREV(GC_FRESH_LIST, GC_FRESH_LIST);
	GC_SET_NEXT(GC_FREE_LIST, GC_FREE_LIST);
	GC_SET_PREV(GC_FREE_LIST, GC_FREE_LIST);
	GC_SET_NEXT(GC_PERM_LIST, GC_PERM_LIST);
	GC_SET_PREV(GC_PERM_LIST, GC_PERM_LIST);
}

void
gc_insert_before(CELL* p, CELL* item)
/* insert <item> before <p> in list */
{
	GC_SET_NEXT(item, p);
	GC_SET_PREV(item, GC_PREV(p));
	GC_SET_NEXT(GC_PREV(p), item);
	GC_SET_PREV(p, item);
}

void
gc_insert_after(CELL* p, CELL* item)
/* insert <item> after <p> in list */
{
	GC_SET_PREV(item, p);
	GC_SET_NEXT(item, GC_NEXT(p));
	GC_SET_PREV(GC_NEXT(p), item);
	GC_SET_NEXT(p, item);
}

CELL*
gc_extract(CELL* item)
/* extract <item> from list */
{
	GC_SET_NEXT(GC_PREV(item), GC_NEXT(item));
	GC_SET_PREV(GC_NEXT(item), GC_PREV(item));
	GC_SET_NEXT(item, item);	/* for safety */
	GC_SET_PREV(item, item);	/* for safety */
	return item;
}

void
gc_push(CELL* list, CELL* item)
/* insert <item> at the beginning of <list> */
{
	GC_SET_SIZE(list, GC_SIZE(list) + 1);
	gc_insert_after(list, item);
}

CELL*
gc_pop(CELL* list)
/* extract <item> from the beginning of <list> */
{
	CELL* p = GC_NEXT(list);
	
	if (p == list) {
		return NULL;		/* error - list empty */
	}
	GC_SET_SIZE(list, GC_SIZE(list) - 1);
	return gc_extract(p);
}

void
gc_put(CELL* list, CELL* item)
/* insert <item> at the end of <list> */
{
	GC_SET_SIZE(list, GC_SIZE(list) + 1);
	gc_insert_before(list, item);
}

CELL*
gc_pull(CELL* list)
/* extract <item> from the end of <list> */
{
	CELL* p = GC_PREV(list);
	
	if (p == list) {
		return NULL;		/* error - list empty */
	}
	GC_SET_SIZE(list, GC_SIZE(list) - 1);
	return gc_extract(p);
}

void
gc_append_list(CELL* to, CELL* from)
/* append <from> at the end of <to> */
{
	if (GC_NEXT(from) == from) {
		return;			/* no-op - from empty */
	}
	GC_SET_SIZE(to, GC_SIZE(to) + GC_SIZE(from));
	GC_SET_SIZE(from, 0);
	GC_SET_NEXT(GC_PREV(from), to);
	GC_SET_PREV(GC_NEXT(from), GC_PREV(to));
	GC_SET_NEXT(GC_PREV(to), GC_NEXT(from));
	GC_SET_PREV(to, GC_PREV(from));
	GC_SET_NEXT(from, from);
	GC_SET_PREV(from, from);
}

WORD
gc_count(CELL* list)
/* count items in <list> */
{
	WORD n = 0;
	CELL* p = list;
	
	while (GC_NEXT(p) != list) {
		++n;
		p = GC_NEXT(p);
	}
	return n;
}

void
gc_sanity_check(CELL* list)
/* check <list> for internal consistency */
{
	long n = 0;
	CELL* p = list;
	CELL* q = NULL;

	DBUG_ENTER("gc_sanity_check");
	XDBUG_PRINT("", ("gc_phase = 0x%x", gc_phase__mark));
	assert(((gc_phase__prev == GC_PHASE_0) && (gc_phase__mark == GC_PHASE_1))
	    || ((gc_phase__prev == GC_PHASE_1) && (gc_phase__mark == GC_PHASE_0)));
	DBUG_PRINT("", ("list = %p", list));
	for (;;) {
/*		DBUG_HEXDUMP(p, sizeof(CELL)); */
		assert(n >= 0);				/* prevent possible endless loop */
		assert(p != NULL);
		assert((as_word(p) & (sizeof(WORD) - 1)) == 0);	/* alignment */
		q = GC_NEXT(p);
		XDBUG_PRINT("", ("q = %p", q));
		assert(q != NULL);
		assert((as_word(q) & (sizeof(WORD) - 1)) == 0);	/* alignment */
		assert(GC_PREV(q) == p);
		if (q == list) {
			break;		/* done */
		}
		++n;
		p = q;
	}
	DBUG_PRINT("", ("count = %u", n));
	assert(n == GC_SIZE(list));		/* cached size mismatch */
	DBUG_RETURN;
}

static void
gc_age_cells()
/* move "fresh" cells to the "aged" list for possible collection */
{
	DBUG_ENTER("gc_age_cells");
	DBUG_PRINT("gc", ("%u cells available in free list", GC_SIZE(GC_FREE_LIST)));
	DBUG_PRINT("gc", ("moving %u cells from fresh to aged", GC_SIZE(GC_FRESH_LIST)));
	gc_append_list(GC_AGED_LIST, GC_FRESH_LIST);
	DBUG_PRINT("gc", ("%u cells on aged list to be scanned", GC_SIZE(GC_AGED_LIST)));
	if (gc_phase__mark == GC_PHASE_0) {
		gc_phase__prev = GC_PHASE_0;
		gc_phase__mark = GC_PHASE_1;
	} else {
		gc_phase__prev = GC_PHASE_1;
		gc_phase__mark = GC_PHASE_0;
	}
	DBUG_PRINT("gc", ("gc_phase = 0x%x", gc_phase__mark));
	DBUG_RETURN;
}

static void
gc_scan_cell(CELL* p)
/* move a live cell (from the "aged" list) to the "scan" list */
{
	WORD mark;

	DBUG_ENTER("gc_scan_cell");
	DBUG_PRINT("gc", ("p = %p", p));
	mark = GC_MARK(p);
	if (mark == gc_phase__mark) {
		DBUG_PRINT("gc", ("cell already marked"));
		DBUG_RETURN;		/* cell already marked in this phase */
	}
	assert(mark == gc_phase__prev);
	GC_SET_SIZE(GC_AGED_LIST, GC_SIZE(GC_AGED_LIST) - 1);
	p = gc_extract(p);	
	GC_SET_MARK(p, gc_phase__mark);
	gc_put(GC_SCAN_LIST, p);
	DBUG_RETURN;
}

static BOOL
gc_scan_value(CONS* s)
/* consider a value for addition to the "scan" list, return TRUE if queued */
{
	DBUG_ENTER("gc_scan_value");
	DBUG_PRINT("gc", ("s = 16#%08lx", (ulint)s));
	if (!nilp(s)) {
		if (actorp(s)) {
			s = MK_CONS(s);
		}
		if (consp(s)) {
			gc_scan_cell(as_cell(s));
			DBUG_RETURN TRUE;
		}
	}
	DBUG_RETURN FALSE;
}

static BOOL
gc_refresh_cell()
/* process a cell from the "scan" list, return FALSE if none remain */
{
	CELL* p;

	DBUG_ENTER("gc_refresh_cell");
	if (GC_SIZE(GC_SCAN_LIST) == 0) {
		DBUG_PRINT("gc", ("empty scan list"));
		DBUG_RETURN FALSE;
	}
	p = gc_pop(GC_SCAN_LIST);
	assert(p != NULL);
	gc_scan_value(GC_FIRST(p));
	gc_scan_value(GC_REST(p));
	gc_push(GC_FRESH_LIST, p);
	DBUG_RETURN TRUE;
}

static void
gc_free_cells()
/* move unmarked "aged" cells to "free" list after scanning */
{
	DBUG_ENTER("gc_free_cells");
	DBUG_PRINT("gc", ("%u cells marked in-use on fresh list", GC_SIZE(GC_FRESH_LIST)));
	gc_append_list(GC_FREE_LIST, GC_AGED_LIST);	
	DBUG_PRINT("gc", ("%u cells available in free list", GC_SIZE(GC_FREE_LIST)));
#if 1	/* FIXME: eventually remove these checks for better performance */
	gc_sanity_check(GC_AGED_LIST);
	gc_sanity_check(GC_SCAN_LIST);
	gc_sanity_check(GC_FRESH_LIST);
	gc_sanity_check(GC_FREE_LIST);
#endif
	DBUG_RETURN;
}

void
gc_full_collection(CONS* root)
/* perform a full garbage collection (NOT CONCURRENT!) */
{
	DBUG_ENTER("gc_full_collection");
	gc_age_cells();
	assert(consp(root));
	if (!nilp(root)) {
		gc_scan_cell(as_cell(root));	/* scan "root" */
	}
	while (gc_refresh_cell() == TRUE)
		;
	gc_free_cells();
	DBUG_RETURN;
}

/**
gc_scanning_actor:
	BEHAVIOR {}
	$ignored -> [
		IF gc_refresh_cell() [
			SEND SELF NIL
		] ELSE [
			gc_free_cells()
		]
	]
	DONE
**/
BEH_DECL(gc_scanning_actor)
{
	DBUG_ENTER("gc_scanning_actor");
	if (gc_refresh_cell() == TRUE) {
		SEND(SELF, NIL);			/* more aged cells to scan */
	} else {
		gc_free_cells();			/* scanning complete */
	}
	DBUG_RETURN;
}

void
gc_actor_collection(CONFIG* cfg, CONS* root)
/* initiate actor-based (CONCURRENT) garbage collection */
{
	CONS* actor;

	DBUG_ENTER("gc_actor_collection");
	gc_age_cells();			/* cells allocated after this are "fresh" */
	assert(consp(root));
	if (!nilp(root)) {
		gc_scan_cell(as_cell(root));	/* scan "root" */
	}
	actor = CFG_ACTOR(cfg, gc_scanning_actor, NIL);
	CFG_SEND(cfg, actor, NIL);
	DBUG_RETURN;
}

static void
gc_allocate_cells(CELL* list_head)
/* allocate a new block of free cells */
{
	size_t n;
	CELL* p;
	char* tag;

	DBUG_ENTER("gc_alloc_cells");
	gc_initialize();
	tag = ((list_head == GC_FREE_LIST) ? "free" : "permanent");
#if 1
	n = ((1 << 12) / sizeof(CELL));		/* 4Kb allocation */
#else
	n = ((1 << 8) / sizeof(CELL));		/* 256b allocation */
#endif
	p = NEWxN(CELL, n + 1);
	p = as_cell((as_word(p) + (sizeof(CELL) >> 1)) & ~(sizeof(CELL) - 1));
	DBUG_PRINT("gc", ("%u %s cells allocated starting at %p", n, tag, p));
	while (n > 0) {
		--n;
		GC_SET_MARK(p, GC_PHASE_Z);
		gc_put(list_head, p);
		++p;
	}
	DBUG_PRINT("gc", ("%u cells available in %s list", GC_SIZE(list_head), tag));
	DBUG_RETURN;
}

CONS*
gc_perm(CONS* first, CONS* rest)
/* allocate and initialize a permanent cell (never garbage collected) */
{
	CELL* p;
	CONS* s;

	if (GC_SIZE(GC_PERM_LIST) == 0) {
		gc_allocate_cells(GC_PERM_LIST);
	}
	p = gc_pop(GC_PERM_LIST);
	assert(p != NULL);
	GC_SET_MARK(p, GC_PHASE_X);
	GC_SET_FIRST(p, first);
	GC_SET_REST(p, rest);
	s = as_cons(p);
	assert(consp(s));
	return s;
}

CONS*
gc_cons(CONS* first, CONS* rest)
/* allocate and initialize a new "cons" cell */
{
	CELL* p;
	CONS* s;

	if (GC_SIZE(GC_FREE_LIST) == 0) {
		gc_allocate_cells(GC_FREE_LIST);
	}
	p = gc_pop(GC_FREE_LIST);
	assert(p != NULL);
	GC_SET_MARK(p, gc_phase__mark);
	GC_SET_FIRST(p, first);
	GC_SET_REST(p, rest);
	gc_put(GC_FRESH_LIST, p);
	/* FIXME: start gc scan if too few free cells remain */
	s = as_cons(p);
	assert(consp(s));
	return s;
}

static CELL*
gc_check_access(CONS* cell)
/* ensure that accessed cells are considered "live" */
{
	CELL* p;

	assert(consp(cell));
	p = as_cell(cell);
	if (GC_MARK(p) == gc_phase__prev) {
		gc_scan_cell(p);	/* any "aged" cell accessed must be "live", so scan it */
	}
	return p;
}

CONS*
gc_first(CONS* cell)
{
	if (nilp(cell)) {
		return NIL;
	}
	return GC_FIRST(gc_check_access(cell));
}

CONS*
gc_rest(CONS* cell)
{
	if (nilp(cell)) {
		return NIL;
	}
	return GC_REST(gc_check_access(cell));
}

void
gc_set_first(CONS* cell, CONS* first)
{
	assert(!nilp(cell));
	GC_SET_FIRST(gc_check_access(cell), first);
}

void
gc_set_rest(CONS* cell, CONS* rest)
{
	assert(!nilp(cell));
	GC_SET_REST(gc_check_access(cell), rest);
}

#if 1
#define	N	256		/* number of free cells in an allocation block */
#else
#define	N	16		/* number of free cells in an allocation block */
#endif

void
test_gc()
/* internal unit test */
{
/*
	CELL* p;
	CELL* q;
*/
	CONS* r;
	CONS* s;

	DBUG_ENTER("test_gc");
	TRACE(printf("--test_gc--\n"));
	assert(sizeof(WORD) == sizeof(CONS*));
	assert(sizeof(WORD) == sizeof(CELL*));
	DBUG_PRINT("", ("sizeof(CELL) = %u", sizeof(CELL)));
	DBUG_PRINT("", ("gc_phase = 0x%x", gc_phase__mark));
	gc_initialize();
	DBUG_PRINT("", ("gc_phase = 0x%x", gc_phase__mark));

	gc_sanity_check(GC_AGED_LIST);
	gc_sanity_check(GC_SCAN_LIST);
	gc_sanity_check(GC_FRESH_LIST);
	gc_sanity_check(GC_FREE_LIST);
	gc_sanity_check(GC_PERM_LIST);
	
	gc_allocate_cells(GC_FREE_LIST);
	gc_sanity_check(GC_FREE_LIST);
	assert(GC_SIZE(GC_FREE_LIST) == N);
	
	s = NIL;
	s = gc_cons(NUMBER(1), s);
	s = gc_cons(NUMBER(2), s);
	s = gc_cons(NUMBER(-2), gc_rest(s));
	DBUG_PRINT("", ("s@%p = %s", s, cons_to_str(s)));
/*
	r = reverse(s);
	DBUG_PRINT("", ("r@%p = %s", r, cons_to_str(r)));
*/
	gc_sanity_check(GC_FRESH_LIST);
	gc_sanity_check(GC_FREE_LIST);
	assert(GC_SIZE(GC_AGED_LIST) == 0);
	assert(GC_SIZE(GC_SCAN_LIST) == 0);
	assert(GC_SIZE(GC_FRESH_LIST) == 3);
	assert(GC_SIZE(GC_FREE_LIST) == (N - 3));
	
	gc_age_cells();
	assert(GC_SIZE(GC_AGED_LIST) == 3);
	assert(GC_SIZE(GC_SCAN_LIST) == 0);
	assert(GC_SIZE(GC_FRESH_LIST) == 0);
	assert(GC_SIZE(GC_FREE_LIST) == (N - 3));
	
	gc_scan_cell(as_cell(s));	/* scan "root" */
	assert(GC_SIZE(GC_AGED_LIST) == 2);
	assert(GC_SIZE(GC_SCAN_LIST) == 1);
	assert(GC_SIZE(GC_FRESH_LIST) == 0);
	assert(GC_SIZE(GC_FREE_LIST) == (N - 3));
	
	r = gc_cons(NUMBER(-1), gc_rest(gc_rest(s)));
	DBUG_PRINT("", ("r@%p = %s", r, cons_to_str(r)));
	assert(GC_SIZE(GC_AGED_LIST) == 1);
	assert(GC_SIZE(GC_SCAN_LIST) == 2);
	assert(GC_SIZE(GC_FRESH_LIST) == 1);
	assert(GC_SIZE(GC_FREE_LIST) == (N - 4));
	
	assert(gc_refresh_cell() == TRUE);
	assert(GC_SIZE(GC_AGED_LIST) == 1);
	assert(GC_SIZE(GC_SCAN_LIST) == 1);
	assert(GC_SIZE(GC_FRESH_LIST) == 2);
	assert(GC_SIZE(GC_FREE_LIST) == (N - 4));

	assert(gc_refresh_cell() == TRUE);
	assert(GC_SIZE(GC_AGED_LIST) == 1);
	assert(GC_SIZE(GC_SCAN_LIST) == 0);
	assert(GC_SIZE(GC_FRESH_LIST) == 3);
	assert(GC_SIZE(GC_FREE_LIST) == (N - 4));

	assert(gc_refresh_cell() == FALSE);
	assert(GC_SIZE(GC_AGED_LIST) == 1);
	assert(GC_SIZE(GC_SCAN_LIST) == 0);
	assert(GC_SIZE(GC_FRESH_LIST) == 3);
	assert(GC_SIZE(GC_FREE_LIST) == (N - 4));

	gc_free_cells();
	assert(GC_SIZE(GC_AGED_LIST) == 0);
	assert(GC_SIZE(GC_SCAN_LIST) == 0);
	assert(GC_SIZE(GC_FRESH_LIST) == 3);
	assert(GC_SIZE(GC_FREE_LIST) == (N - 3));
	
	gc_age_cells();
	assert(GC_SIZE(GC_AGED_LIST) == 3);
	assert(GC_SIZE(GC_SCAN_LIST) == 0);
	assert(GC_SIZE(GC_FRESH_LIST) == 0);
	assert(GC_SIZE(GC_FREE_LIST) == (N - 3));
	
	gc_scan_cell(as_cell(r));	/* scan "root" */
	assert(GC_SIZE(GC_AGED_LIST) == 2);
	assert(GC_SIZE(GC_SCAN_LIST) == 1);
	assert(GC_SIZE(GC_FRESH_LIST) == 0);
	assert(GC_SIZE(GC_FREE_LIST) == (N - 3));
	
	assert(gc_refresh_cell() == TRUE);
	assert(GC_SIZE(GC_AGED_LIST) == 2);
	assert(GC_SIZE(GC_SCAN_LIST) == 0);
	assert(GC_SIZE(GC_FRESH_LIST) == 1);
	assert(GC_SIZE(GC_FREE_LIST) == (N - 3));

	assert(gc_refresh_cell() == FALSE);
	assert(GC_SIZE(GC_AGED_LIST) == 2);
	assert(GC_SIZE(GC_SCAN_LIST) == 0);
	assert(GC_SIZE(GC_FRESH_LIST) == 1);
	assert(GC_SIZE(GC_FREE_LIST) == (N - 3));

	gc_free_cells();
	assert(GC_SIZE(GC_AGED_LIST) == 0);
	assert(GC_SIZE(GC_SCAN_LIST) == 0);
	assert(GC_SIZE(GC_FRESH_LIST) == 1);
	assert(GC_SIZE(GC_FREE_LIST) == (N - 1));
	
	gc_full_collection(r);	/* all together now... */
	assert(GC_SIZE(GC_AGED_LIST) == 0);
	assert(GC_SIZE(GC_SCAN_LIST) == 0);
	assert(GC_SIZE(GC_FRESH_LIST) == 1);
	assert(GC_SIZE(GC_FREE_LIST) == (N - 1));

	gc_sanity_check(GC_AGED_LIST);
	gc_sanity_check(GC_SCAN_LIST);
	gc_sanity_check(GC_FRESH_LIST);
	gc_sanity_check(GC_FREE_LIST);
	gc_sanity_check(GC_PERM_LIST);
	DBUG_RETURN;
}

void
report_cell_usage()
{
	DBUG_ENTER("report_cell_usage");
	DBUG_PRINT("gc", ("GC_SIZE(GC_AGED_LIST)=%u", GC_SIZE(GC_AGED_LIST)));
	DBUG_PRINT("gc", ("GC_SIZE(GC_SCAN_LIST)=%u", GC_SIZE(GC_SCAN_LIST)));
	DBUG_PRINT("gc", ("GC_SIZE(GC_FRESH_LIST)=%u", GC_SIZE(GC_FRESH_LIST)));
	DBUG_PRINT("gc", ("GC_SIZE(GC_FREE_LIST)=%u", GC_SIZE(GC_FREE_LIST)));
	DBUG_PRINT("gc", ("GC_SIZE(GC_PERM_LIST)=%u", GC_SIZE(GC_PERM_LIST)));
	DEBUG(printf("GC_SIZE(GC_AGED_LIST)=%u\n", GC_SIZE(GC_AGED_LIST)));
	DEBUG(printf("GC_SIZE(GC_SCAN_LIST)=%u\n", GC_SIZE(GC_SCAN_LIST)));
	DEBUG(printf("GC_SIZE(GC_FRESH_LIST)=%u\n", GC_SIZE(GC_FRESH_LIST)));
	DEBUG(printf("GC_SIZE(GC_FREE_LIST)=%u\n", GC_SIZE(GC_FREE_LIST)));
	DEBUG(printf("GC_SIZE(GC_PERM_LIST)=%u\n", GC_SIZE(GC_PERM_LIST)));
	gc_sanity_check(GC_AGED_LIST);
	gc_sanity_check(GC_SCAN_LIST);
	gc_sanity_check(GC_FRESH_LIST);
	gc_sanity_check(GC_FREE_LIST);
	DBUG_RETURN;
}

