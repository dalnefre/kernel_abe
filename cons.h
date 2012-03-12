/*
 * cons.h -- LISP-like "CONS" cell management
 *
 * Copyright 2008-2009 Dale Schumacher.  ALL RIGHTS RESERVED.
 */
#ifndef CONS_H
#define CONS_H

#include <assert.h>
#include "types.h"

/* select one-and-only-one of the following representation type-tag options */
#define	TYPETAG_USES_2LSB		1
#define	TYPETAG_USES_3LSB		0
#define	TYPETAG_USES_1LSB_2MSB	0

#if TYPETAG_USES_3LSB
#define	NUMBER_IS_FUNC	0	/* numberp(p) != funcp(p) */
#else
#define	NUMBER_IS_FUNC	1	/* numberp(p) == funcp(p) */
#endif

extern CELL		nil__cons;

#define	NIL		as_cons(&nil__cons)
#define	nilp(p)	((BOOL)((p)==NIL))
#if 0
#define	car(p)	((p)->first)
#define	cdr(p)	((p)->rest)
#else
#define	car(p)	_car(p)
#define	cdr(p)	_cdr(p)
#endif

BOOL	_nilp(CONS* cons);
#if 0
BOOL	_actorp(CONS* cons);
#endif
CONS*	cons(CONS* car, CONS* cdr);
CONS*	_car(CONS* cons);
CONS*	_cdr(CONS* cons);
CONS*	rplaca(CONS* cons, CONS* car);
CONS*	rplacd(CONS* cons, CONS* cdr);
BOOL	equal(CONS* x, CONS* y);
CONS*	append(CONS* x, CONS* y);
CONS*	reverse(CONS* list);
int		length(CONS* list);
CONS*	replace(CONS* form, CONS* map);
CONS*	map_find(CONS* map, CONS* key);
CONS*	map_get_def(CONS* map, CONS* key, CONS* def);
CONS*	_map_get(CONS* map, CONS* key);
CONS*	map_put(CONS* map, CONS* key, CONS* value);
CONS*	map_put_all(CONS* map, CONS* amap);
CONS*	map_def(CONS* map, CONS* keys, CONS* values);
CONS*	map_remove(CONS* map, CONS* key);
CONS*	map_cut(CONS* map, CONS* key);

#if TYPETAG_USES_2LSB

#define	BM_TYPE		((WORD)(0x00000003))
#define	BF_CONS		((WORD)(0x00000000))
#define	BF_FUNC		((WORD)(0x00000003))
#define	BF_ATOM		((WORD)(0x00000002))
#define	BF_NUMBER	((WORD)(0x00000003))
#define	BF_ACTOR	((WORD)(0x00000001))
#define	BF_OBJECT	((WORD)(0x00000001))

#define	MK_CONS(p)	((CONS*)((((WORD)(p)) & ~BM_TYPE) | BF_CONS))
#define	MK_ATOM(p)	((CONS*)((((WORD)(p)) & ~BM_TYPE) | BF_ATOM))
#define	MK_NUMBER(p) ((CONS*)((((WORD)(p))<<2) | BF_NUMBER))
#define	MK_FUNC(p)	((CONS*)((((WORD)(p))<<2) | BF_FUNC))
#define MK_REF(p)	MK_FUNC(p)
#define	MK_ACTOR(p)	((CONS*)((((WORD)(p)) & ~BM_TYPE) | BF_ACTOR))
#define	MK_OBJECT(p) ((CONS*)((((WORD)(p)) & ~BM_TYPE) | BF_OBJECT))

#define	MK_INT(p)	((int)(((WORD)(p))>>2))
#define	MK_PTR(p)	((void*)(((WORD)(p))>>2))

#define	consp(p)	((BOOL)(((p)!=FALSE)&&((((WORD)(p)) & BM_TYPE) == BF_CONS)))
#define	atomp(p)	((BOOL)((((WORD)(p)) & BM_TYPE) == BF_ATOM))
#define	funcp(p)	((BOOL)(((p)!=TRUE)&&((((WORD)(p)) & BM_TYPE) == BF_FUNC)))
#define	numberp(p)	((BOOL)((((WORD)(p)) & BM_TYPE) == BF_NUMBER))
#if 0
#define	actorp(p)	(BOOL)(consp(p) && !nilp(p) && funcp((p)->first))
#else
#define	actorp(p)	((BOOL)((((WORD)(p)) & BM_TYPE) == BF_ACTOR))
#endif
#define	objectp(p)	((BOOL)((((WORD)(p)) & BM_TYPE) == BF_OBJECT))

#endif /* TYPETAG_USES_2LSB */

#if TYPETAG_USES_3LSB

#define	BM_TYPE		((WORD)(0x00000007))
#define	BM_NATIVE	((WORD)(0x00000003))
#define	BF_CONS		((WORD)(0x00000000))
#define	BF_FUNC		((WORD)(0x00000001))
#define	BF_ATOM		((WORD)(0x00000004))
#define	BF_NUMBER	((WORD)(0x00000003))
#define	BF_ACTOR	((WORD)(0x00000006))
#define	BF_OBJECT	((WORD)(0x00000002))

#define	MK_CONS(p)	((CONS*)((((WORD)(p)) & ~BM_TYPE) | BF_CONS))
#define	MK_ATOM(p)	((CONS*)((((WORD)(p)) & ~BM_TYPE) | BF_ATOM))
#define	MK_NUMBER(p) ((CONS*)((((WORD)(p))<<2) | BF_NUMBER))
#define	MK_FUNC(p)	((CONS*)((((WORD)(p))<<2) | BF_FUNC))
#define MK_REF(p)	MK_FUNC(p)
#define	MK_ACTOR(p)	((CONS*)((((WORD)(p)) & ~BM_TYPE) | BF_ACTOR))
#define	MK_OBJECT(p) ((CONS*)((((WORD)(p)) & ~BM_TYPE) | BF_OBJECT))

#define	MK_INT(p)	((int)(((WORD)(p))>>2))
#define	MK_PTR(p)	((void*)(((WORD)(p))>>2))

#define	consp(p)	((BOOL)(((p)!=FALSE)&&((((WORD)(p)) & BM_TYPE) == BF_CONS)))
#define	atomp(p)	((BOOL)((((WORD)(p)) & BM_TYPE) == BF_ATOM))
#define	funcp(p)	((BOOL)(((p)!=TRUE)&&((((WORD)(p)) & BM_NATIVE) == BF_FUNC)))
#define	numberp(p)	((BOOL)((((WORD)(p)) & BM_NATIVE) == BF_NUMBER))
#define	actorp(p)	((BOOL)((((WORD)(p)) & BM_TYPE) == BF_ACTOR))
#define	objectp(p)	((BOOL)((((WORD)(p)) & BM_TYPE) == BF_OBJECT))

#endif /* TYPETAG_USES_3LSB */

#if TYPETAG_USES_1LSB_2MSB

#define	BM_TYPE		((WORD)(0xC0000001))
#define	BM_NATIVE	((WORD)(0x00000001))
#define	BF_CONS		((WORD)(0x00000000))
#define	BF_FUNC		((WORD)(0x00000001))
#define	BF_ATOM		((WORD)(0x40000000))
#define	BF_NUMBER	((WORD)(0x00000001))
#define	BF_ACTOR	((WORD)(0xC0000000))
#define	BF_OBJECT	((WORD)(0x80000000))

#define	MK_CONS(p)	((CONS*)((((WORD)(p)) & ~BM_TYPE) | BF_CONS))
#define	MK_ATOM(p)	((CONS*)((((WORD)(p)) & ~BM_TYPE) | BF_ATOM))
#define	MK_NUMBER(p) ((CONS*)((((WORD)(p))<<1) | BF_NUMBER))
#define	MK_FUNC(p)	((CONS*)((((WORD)(p))<<1) | BF_FUNC))
#define MK_REF(p)	MK_FUNC(p)
#define	MK_ACTOR(p)	((CONS*)((((WORD)(p)) & ~BM_TYPE) | BF_ACTOR))
#define	MK_OBJECT(p) ((CONS*)((((WORD)(p)) & ~BM_TYPE) | BF_OBJECT))

#define	MK_INT(p)	((int)(((WORD)(p))>>1))
#define	MK_PTR(p)	((void*)(((WORD)(p))>>1))

#define	consp(p)	((BOOL)(((p)!=FALSE)&&((((WORD)(p)) & BM_TYPE) == BF_CONS)))
#define	atomp(p)	((BOOL)((((WORD)(p)) & BM_TYPE) == BF_ATOM))
#define	funcp(p)	((BOOL)(((p)!=TRUE)&&((((WORD)(p)) & BM_NATIVE) == BF_FUNC)))
#define	numberp(p)	((BOOL)((((WORD)(p)) & BM_NATIVE) == BF_NUMBER))
#define	actorp(p)	((BOOL)((((WORD)(p)) & BM_TYPE) == BF_ACTOR))
#define	objectp(p)	((BOOL)((((WORD)(p)) & BM_TYPE) == BF_OBJECT))

#endif /* TYPETAG_USES_1LSB_2MSB */

#define	map_get(map,key)	map_get_def((map), (key), NULL)

void	test_cons();
void	report_cons_usage();
BOOL	assert_equal_cons(char* msg, CONS* expect, CONS* actual);

#define	assert_equal(e,a)	assert(equal((e),(a)))

/* O(1) queue management macros */
#define	CQ_EMPTY(q)		nilp(car(q))
#define	CQ_PUT(q, e)	rplacd((q), nilp(car(q)) ? rplaca((q), (e)) : rplacd(cdr(q), (e)))
#define	CQ_PUSH(q, e)	rplaca((q), nilp(rplacd((e), car(q))) ? rplacd((q), (e)) : (e))
#define	CQ_POP(q)		rplaca((q), cdr(car(q)))
#define	CQ_PEEK(q)		car(q)

#endif /* CONS_H */
