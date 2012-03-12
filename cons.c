/*
 * cons.c -- LISP-like "CONS" cell management
 *
 * Copyright 2008-2009 Dale Schumacher.  ALL RIGHTS RESERVED.
 */
#include "cons.h"
#include "abe.h"

#include "dbug.h"
DBUG_UNIT("cons");

CELL		nil__cons = { as_cons(&nil__cons), as_cons(&nil__cons), GC_PHASE_Z, 0U };
static int	cons_cnt = 0;

BOOL
_nilp(CONS* p)
{
	return nilp(p);
}

#if 0
BOOL
_actorp(CONS* p)
{
	return (BOOL)(consp(p) && !nilp(p) && funcp((p)->first));
}
#endif

CONS*
cons(CONS* a, CONS* d)
{
	CONS* p = NULL;

	p = gc_cons(a, d);
#if 1
	if (!consp(p)) {
		DBUG_PRINT("cons", ("ALERT! alignment error! p=16#%08lx", (ulint)p));
		fprintf(stderr, "ALERT! alignment error\n");
		abort();
	}
#endif
	++cons_cnt;
	XDBUG_PRINT("cons", ("allocated free cell #%d @%p[%p;%p]", cons_cnt, p, a, d));
	return p;
}

CONS*
_car(CONS* p)
{
	XDBUG_ENTER("car");
	XDBUG_PRINT("", ("p=16#%08lx", (ulint)p));
	if (nilp(p)) {
		DBUG_PRINT("car", ("ALERT! car(NIL)"));
/*		fprintf(stderr, "ALERT! car(NIL)\n"); */
		XDBUG_RETURN NIL;
	}
#if 1
	if (actorp(p)) {
		DBUG_PRINT("car", ("ALERT! use _THIS(ACTOR)"));
		fprintf(stderr, "ALERT! car(ACTOR)\n");
		abort();
	}
#endif
	assert(consp(p));
#if 0
	XDBUG_RETURN (p->first);
#else
	XDBUG_RETURN gc_first(p);
#endif
}

CONS*
_cdr(CONS* p)
{
	XDBUG_ENTER("cdr");
	XDBUG_PRINT("", ("p=16#%08lx", (ulint)p));
	if (nilp(p)) {
		DBUG_PRINT("cdr", ("ALERT! cdr(NIL)"));
/*		fprintf(stderr, "ALERT! cdr(NIL)\n"); */
		XDBUG_RETURN NIL;
	}
#if 1
	if (actorp(p)) {
		DBUG_PRINT("cdr", ("ALERT! use _MINE(ACTOR)"));
		fprintf(stderr, "ALERT! cdr(ACTOR)\n");
		abort();
	}
#endif
	assert(consp(p));
#if 0
	XDBUG_RETURN (p->rest);
#else
	XDBUG_RETURN gc_rest(p);
#endif
}

CONS*
rplaca(CONS* p, CONS* a)
{
#if 0
	assert(consp(p));
	assert(!nilp(p));
	return (p->first = a);
#else
	gc_set_first(p, a);
	return a;
#endif
}

CONS*
rplacd(CONS* p, CONS* d)
{
#if 0
	assert(consp(p));
	assert(!nilp(p));
	return (p->rest = d);
#else
	gc_set_rest(p, d);
	return d;
#endif
}

BOOL
equal(CONS* x, CONS* y)
{
	if (x == y) {
		return TRUE;
	}
	if (nilp(x) || nilp(y)) {
		return FALSE;
	}
	if (actorp(x) || actorp(y)) {
		return FALSE;
	}
	if (consp(x) && consp(y)) {
		return (BOOL)(equal(car(x), car(y)) && equal(cdr(x), cdr(y)));
	}
	return FALSE;
}

CONS*
append(CONS* x, CONS* y)
{
	assert(consp(x));
	assert(consp(y));
	if (nilp(x)) {
		return y;
	}
	/* FIXME: consider a non-recursive implementation to minimize stack usage */
	return cons(car(x), append(cdr(x), y));
}

CONS*
reverse(CONS* list)
{
	CONS* rev = NIL;

	while (!nilp(list)) {
		assert(consp(list));
		rev = cons(car(list), rev);
		list = cdr(list);
	}
	return rev;
}

int
length(CONS* list)
{
	int n = 0;

	while (!nilp(list)) {
		assert(consp(list));
		++n;
		list = cdr(list);
	}
	return n;
}

CONS*
replace(CONS* form, CONS* map)
/* replace mapped items in form with corresponding values */
{
	CONS* mapping;

	if (nilp(form)) {
		form = NIL;
	} else {
		mapping = map_find(map, form);
		if (nilp(mapping)) {
			if (consp(form)) {
				/* FIXME: consider a non-recursive implementation to minimize stack usage */
				form = cons(
					replace(car(form), map), 
					replace(cdr(form), map)
				);
/* ---- FALL THRU TO RETURN
			} else {
				form = form;
*/
			}
		} else {
			form = cdr(mapping);
		}
	}
	return form;
}

CONS*
map_find(CONS* map, CONS* key)
/* return the CONS mapping <key>, or NIL if not found */
{
	CONS* entry = NIL;

	for (;;) {
		if (nilp(map)) {
			return NIL;
		}
		assert(consp(map));
		entry = car(map);
		assert(consp(entry));
		if (car(entry) == key) {
			return entry;
		}
		map = cdr(map);
	}
	return entry;
}

CONS*
map_get_def(CONS* map, CONS* key, CONS* def)
/* return the value associated with <key>, or <def> if not found */
{
	CONS* pair = NIL;

	assert(map != NULL);
	assert(key != NULL);
	pair = map_find(map, key);
	if (nilp(pair)) {
		return def;
	}
	return cdr(pair);
}

CONS*
_map_get(CONS* map, CONS* key)
/* return the value associated with <key>, or NULL if not found */
{
	return map_get_def(map, key, NULL);
}

CONS*
map_put(CONS* map, CONS* key, CONS* val)
/* add a CONS mapping <key> to <val> in <map>, returning the new map */
{
	XDBUG_ENTER("map_put");
	XDBUG_PRINT("", ("map=16#%08lx", (ulint)map));
	assert(consp(map) || nilp(map));
	XDBUG_PRINT("", ("key=16#%08lx", (ulint)key));
	assert(key != NULL);
	XDBUG_PRINT("", ("val=16#%08lx", (ulint)val));
	assert(val != NULL);
	XDBUG_RETURN cons(cons(key, val), map);
}

CONS*
map_put_all(CONS* map, CONS* amap)
/* add all entries from <amap> to <map>, returning the new map */
{
	while (consp(amap) && !nilp(amap)) {
		CONS* entry = car(amap);
		if (consp(entry)) {
			map = map_put(map, car(entry), cdr(entry));
		}
		amap = cdr(amap);
	}
	return map;
}

CONS*
map_def(CONS* map, CONS* keys, CONS* values)
/* add corresponding key/value pairs to <map>, returning the new map */
{
	assert(consp(map));
	while (consp(keys) && !nilp(keys) && consp(values) && !nilp(values)) {
		map = map_put(map, car(keys), car(values));
		keys = cdr(keys);
		values = cdr(values);
	}
	return map;
}

CONS*
map_remove(CONS* map, CONS* key)
/* remove any value associated with key, returning the new map w/ all duplicates removed */
{
	CONS* m;
	CONS* p;
	CONS* k;

	m = NIL;
	while (!nilp(map)) {
		assert(consp(map));
		p = car(map);
		assert(consp(p));
		k = car(p);
		if (k != key) {
			if (nilp(map_find(m, k))) {
				m = cons(p, m);
			}
		}
		map = cdr(map);
	}
	return reverse(m);
}

CONS*
map_cut(CONS* map, CONS* key)
/* destructively remove the first mapping for key, returning the new map */
{
	CONS* head;
	CONS* prev;
	
	head = map;
	prev = NIL;
	for (;;) {
		if (nilp(map)) {
			return head;
		}
		assert(consp(map));
		if (car(car(map)) == key) {
			if (nilp(prev)) {
				return cdr(map);
			}
			rplacd(prev, cdr(map));
			return head;
		}
		prev = map;
		map = cdr(map);
	}
}

void
test_cons()
{
	CONS* p;
	CONS* q;
	CONS* r;

	DBUG_ENTER("test_cons");
	TRACE(printf("--test_cons--\n"));
	assert(nilp(NIL));
	assert(consp(NIL));
	assert(!atomp(NIL));
	assert(!funcp(NIL));
	assert(!numberp(NIL));
	
	assert(nilp((NIL)->first));
	assert((NIL)->first == NIL);
	assert(nilp((NIL)->rest));
	assert((NIL)->rest == NIL);
	
	p = cons(NIL, NIL);
	DBUG_PRINT("", ("p:cons(NIL,NIL)=%p", p));
	assert(p != NIL);
	assert(!nilp(p));
	assert(consp(p));
	assert(!atomp(p));
	assert(!funcp(p));
	assert(!numberp(p));
	assert(nilp(car(p)));
	assert(car(p) == NIL);
	assert(nilp(cdr(p)));
	assert(cdr(p) == NIL);
	
	q = cons(NIL, p);
	DBUG_PRINT("", ("q:cons(NIL,p)=%p", q));
	assert(q != NIL);
	assert(!nilp(q));
	assert(consp(q));
	assert(!atomp(q));
	assert(!funcp(q));
	assert(!numberp(q));
	assert(nilp(car(q)));
	assert(car(q) == NIL);
	assert(!nilp(cdr(q)));
	assert(consp(cdr(q)));
	assert(cdr(q) == p);
	
	DBUG_PRINT("", ("testing reverse()"));
	p = cons(NUMBER(0), cons(cons(NUMBER(1), cons(NUMBER(2), NIL)), NIL));
	DBUG_PRINT("", ("p=%s", cons_to_str(p)));
	q = reverse(p);
	DBUG_PRINT("", ("q=%s", cons_to_str(q)));
	assert(car(cdr(q)) == NUMBER(0));
	assert(car(car(q)) == NUMBER(1));
	assert(car(cdr(car(q))) == NUMBER(2));
	
	DBUG_PRINT("", ("testing map_remove()"));
	p = NIL;
	p = map_put(p, NUMBER(0), NUMBER(0));
	p = map_put(p, NUMBER(1), NUMBER(1));
	p = map_put(p, NUMBER(1), NUMBER(2));
	p = map_put(p, NUMBER(0), NUMBER(3));
	r = p;
	DBUG_PRINT("", ("p=%s", cons_to_str(p)));
	assert(length(p) == 4);
	assert(map_get(p, NUMBER(0)) == NUMBER(3));
	q = map_remove(p, NIL);
	DBUG_PRINT("", ("q=%s", cons_to_str(q)));
	assert(p != q);
	p = map_remove(r, NUMBER(0));
	DBUG_PRINT("", ("p=%s", cons_to_str(p)));
	assert(p != r);
	assert(length(p) == 1);
	q = map_cut(r, NUMBER(0));
	DBUG_PRINT("", ("q=%s", cons_to_str(q)));
	assert(r != q);
	assert(p != q);
	assert(length(p) == 1);
	assert(length(q) == 3);
	assert(length(r) == 4);
	assert(map_get(q, NUMBER(0)) == NUMBER(0));
	p = map_cut(q, NUMBER(0));
	DBUG_PRINT("", ("p=%s", cons_to_str(p)));
	assert(p == q);
	assert(length(p) == 2);
	assert(map_get(p, NUMBER(0)) == NULL);
	
	DBUG_RETURN;
}

void
report_cons_usage()
{
	report_cell_usage();
	TRACE(printf("cons_cnt=%d\n", cons_cnt));
	assert((NIL)->first == NIL);
	assert((NIL)->rest == NIL);
}

BOOL
assert_equal_cons(char* msg, CONS* expect, CONS* actual)
{
	if (!equal(expect, actual)) {
		DBUG_PRINT("", ("expect=%s", cons_to_str(expect)));
		DBUG_PRINT("", ("actual=%s", cons_to_str(actual)));
		fprintf(stderr, "assert_equal_cons: FAILED! %s\n", ((msg != NULL) ? msg : ""));
		abort();
	}
	return TRUE;
}

