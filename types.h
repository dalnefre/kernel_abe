/*
 * types.h -- system-wide type definitions
 *
 * Copyright 2009 Dale Schumacher.  ALL RIGHTS RESERVED.
 */
#ifndef TYPES_H
#define TYPES_H

#include <stddef.h>
#include <time.h>

typedef ptrdiff_t WORD;
typedef unsigned int uint;
typedef unsigned long int ulint;

typedef struct cons CONS;
struct cons {
	CONS*	first;
	CONS*	rest;
};

typedef struct cell CELL;
struct cell {
	CONS*	first;		/* user-visible pointer to first list element */
	CONS*	rest;		/* user-visible pointer to the rest of the list */
	WORD	_prev;		/* private pointer to previous cell in gc chain */
	WORD	_next;		/* private pointer to next cell in gc chain */
};

typedef struct config CONFIG;
struct config {
	CELL	msg_queue;	/* must be first member to make CONFIG_QUEUE() macro work */
	CONS*	gc_root;	/* root reference to preserve during garbage collection */
	int		q_count;	/* number of messages waiting in the message queue */
	CONS*	q_entry;	/* (detached) queue entry for message being delivered */
	int		msg_cnt_hi;	/* total number of messages delivered (hi 31 bits) */
	int		msg_cnt_lo;	/* total number of messages delivered (lo 31 bits) */
	int		q_limit;	/* maximum number of messages waiting in queue */
	time_t	t_epoch;	/* starting value for seconds */
	time_t	t_now_s;	/* current time (seconds) */
	time_t	t_now_us;	/* current time (microseconds) */
	CONS*	t_queue;	/* timer queue for delivery of delayed messages */
	int		t_count;	/* number of delayed messages in timer queue */
};

#ifndef FALSE
typedef	CONS*	BOOL;
#define	FALSE 	((BOOL)(0))
#define TRUE	((BOOL)(1))
#define	boolp(p) (((p)==FALSE)||((p)==TRUE))
#endif /* FALSE */

#endif /* TYPES_H */
