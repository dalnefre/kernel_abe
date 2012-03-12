/*
 * actor.c -- experimental Actor-model runtime
 *
 * Copyright 2008-2009 Dale Schumacher.  ALL RIGHTS RESERVED.
 */
#include <sys/time.h>		/* gettimeofday(), struct timeval */
#include "actor.h"
#include "abe.h"

#include "dbug.h"
DBUG_UNIT("actor");

BEH
_this(CONS* self)
{
	CONS* p;

	XDBUG_ENTER("this");
	assert(actorp(self));
	p = MK_CONS(self);
#if 0
	XDBUG_RETURN MK_BEH(p->first);
#else
	XDBUG_RETURN MK_BEH(gc_first(p));
#endif
}

CONS*
_mine(CONS* self)
{
	CONS* p;

	XDBUG_ENTER("mine");
	assert(actorp(self));
	p = MK_CONS(self);
#if 0
	XDBUG_RETURN (p->rest);
#else
	XDBUG_RETURN gc_rest(p);
#endif
}

/**
sink_beh:
	BEHAVIOR {}
	$s -> [
		// ignore $s (but print it for debugging)
	]
	DONE
**/
BEH_DECL(sink_beh)
/*
 * Ignore (but print for debugging) all messages.
 */
{
	DBUG_ENTER("sink_beh");
	DBUG_PRINT("", ("=SINK= %s", cons_to_str(WHAT)));
	DBUG_RETURN;
}

/**
error_msg:
	BEHAVIOR {}
	$s -> [
		// convert $s to a string and print
		// to both stderr and the debug log
	]
	DONE
**/
BEH_DECL(error_msg)
{
	char* s;

	DBUG_ENTER("error_msg");
	s = cons_to_str(WHAT);
	DBUG_PRINT("", ("=ERROR= %s", s));
	fprintf(stderr, "=ERROR= %s\n", s);
	DBUG_RETURN;
}

/**
assert_msg:
	BEHAVIOR {expect:$expect, message:$message}
	$actual -> IF not(equals($expect, $actual)) [
		// print optional $message and abort
		BECOME BEHAVIOR {} $m -> [] DONE
	]
	DONE
**/
BEH_DECL(assert_msg)
{
	CONS* expect = map_get(MINE, ATOM("expect"));
	CONS* message = map_get(MINE, ATOM("message"));
	CONS* actual = WHAT;

	DBUG_ENTER("assert_msg");
	if (!equal(expect, actual)) {
		DBUG_PRINT("", ("expect=%s", cons_to_str(expect)));
		DBUG_PRINT("", ("actual=%s", cons_to_str(actual)));
		fprintf(stderr, "assert_msg: FAILED! %s\n", (atomp(message) ? atom_str(message) : ""));
		abort();
	}
	BECOME(sink_beh, NIL);
	DBUG_RETURN;
}

static CONS*	free_config = NIL;			/* list of available recycleable cells */
static int		free_config_cnt = 0;		/* number of recycleable cells allocated now */
static int		free_config_max = 0;		/* peak number of recycleable cells allocated */

static CONS*
cq_cons(CONS* a, CONS* d)	/* allocate recycleable cell */
{
	CONS* p;

	XDBUG_ENTER("cq_cons");
	if (!nilp(free_config)) {
		assert(consp(free_config));
		p = free_config;
		free_config = cdr(p);
		rplaca(p, a);
		rplacd(p, d);
	} else {
		p = gc_perm(a, d);		/* non-garbage-collected cell allocation */
	}
	if (++free_config_cnt > free_config_max) {
		free_config_max = free_config_cnt;
	}
	XDBUG_RETURN p;
}

static CONS*
cq_free(CONS* p)				/* recycle cell */
{
	XDBUG_ENTER("cq_free");
	if (!nilp(p)) {
		rplaca(p, NIL);
		rplacd(p, free_config);
		free_config = p;
		--free_config_cnt;
	} else {
		DBUG_PRINT("cq_free", ("ALERT! cq_free(NIL)"));
	}
	XDBUG_RETURN NIL;
}

CONS*
tv_create(time_t s, time_t us)
/*
 * Create a time tuple from raw time components (seconds and microseconds)
 */
{
	CONS* t = NIL;
	while (us > TICK_FREQ) {	/* normalize seconds */
		++s;
		us -= TICK_FREQ;
	}
	t = map_put(t, ATOM("us"), NUMBER(us));
	t = map_put(t, ATOM("s"), NUMBER(s));
	return t;
/*
	return cons(cons(ATOM("s"), NUMBER(tv->tv_sec)),
			cons(cons(ATOM("us"), NUMBER(tv->tv_usec)), NIL));
*/
}

CONS*
tv_increment(time_t s, time_t us, time_t d)
/*
 * Create a time tuple offset by d ticks (usecs) from raw time components
 */
{
	while (d > TICK_FREQ) {	/* avoid overflow */
		++s;
		d -= TICK_FREQ;
	}
	return tv_create(s, us + d);
}

int
tv_compare(time_t t0s, time_t t0us, time_t t1s, time_t t1us)
/*
 * Compare raw time components(return: -1 if t0<t1, 0 if t0==t1, 1 if t0>t1)
 */
{
	if (t0s < t1s) {
		return -1;
	}
	if (t0s > t1s) {
		return 1;
	}
	if (t0us < t1us) {
		return -1;
	}
	if (t0us > t1us) {
		return 1;
	}
	return 0;
}

CONFIG*
new_configuration(int q_limit)
/*
 * Create a new (empty) actor configuration.
 */
{
	CONFIG* cfg = NULL;
	CELL* cell;
	CONS* cq;
	struct timeval tv;

	DBUG_ENTER("new_configuration");
	cfg = NEW(CONFIG);
	assert(cfg != NULL);
	cell = as_cell(cfg);
	cq = CONFIG_QUEUE(cfg);
	assert(consp(cq));
	GC_SET_FIRST(cell, NIL);
	GC_SET_REST(cell, NIL);
	GC_SET_PREV(cell, cell);
	GC_SET_NEXT(cell, cell);
	cfg->gc_root = NIL;
	cfg->q_count = 0;
	cfg->q_entry = NIL;
	cfg->msg_cnt_hi = 0;
	cfg->msg_cnt_lo = 0;
	cfg->q_limit = q_limit;
	if (gettimeofday(&tv, NULL) == 0) {
		cfg->t_epoch = tv.tv_sec;
		DBUG_PRINT("timer", ("epoch=%d", cfg->t_epoch));
		cfg->t_now_s = (tv.tv_sec - cfg->t_epoch);
		cfg->t_now_us = tv.tv_usec;
		DBUG_PRINT("timer", ("t = %lus %luus", cfg->t_now_s, cfg->t_now_us));
	}
	cfg->t_queue = NIL;
	cfg->t_count = 0;
	DBUG_RETURN cfg;
}

void
cfg_add_gc_root(CONFIG* cfg, CONS* root)
/*
 * Add root to list of references preserved during garbage-collection.
 */
{
	DBUG_ENTER("cfg_add_gc_root");
	DBUG_PRINT("", ("root=@%p", root));
	cfg->gc_root = cons(root, cfg->gc_root);
	DBUG_RETURN;
}

static CONS*
cfg_gather_roots(CONFIG* cfg)
{
	CONS* root;
	CONS* node;
	CONS* entry;
	int n;

	DBUG_ENTER("cfg_gather_roots");
	root = cfg->gc_root;
	DBUG_PRINT("", ("length(gc_root)=%d", length(root)));
	/* protect pending messages */
	n = 0;
	node = CQ_PEEK(CONFIG_QUEUE(cfg));
	while (!nilp(node)) {
		entry = car(node);					/* entry is a permanent cell */
		root = cons(cdr(entry), root);		/* add message to root list */
		root = cons(car(entry), root);		/* add actor to root list */
		node = cdr(node);
		++n;
	}
	DBUG_PRINT("", ("n=%d q_count=%d", n, cfg->q_count));
	assert(n == cfg->q_count);
	/* protect delayed messages */
	n = 0;
	node = cfg->t_queue;
	while (!nilp(node)) {
		entry = cdr(car(node));				/* entry is a permanent cell */
		root = cons(cdr(entry), root);		/* add message to root list */
		root = cons(car(entry), root);		/* add actor to root list */
		node = cdr(node);
		++n;
	}
	DBUG_PRINT("", ("n=%d t_count=%d", n, cfg->t_count));
	assert(n == cfg->t_count);
	DBUG_RETURN root;
}

void
cfg_force_gc(CONFIG* cfg)
/*
 * Force immediate garbage-collection. (WARNING: NOT CONCURRENT!)
 */
{
	CONS* root;

	DBUG_ENTER("cfg_force_gc");
	root = cfg_gather_roots(cfg);
	DBUG_PRINT("", ("length(root)=%d", length(root)));
	gc_full_collection(root);
	DBUG_RETURN;
}

void
cfg_start_gc(CONFIG* cfg)
/*
 * Start concurrent actor-based garbage-collection process.
 */
{
	CONS* root;

	DBUG_ENTER("cfg_start_gc");
	root = cfg_gather_roots(cfg);
	DBUG_PRINT("", ("length(root)=%d", length(root)));
	gc_actor_collection(cfg, root);
	DBUG_RETURN;
}

CONS*
abe__actor(CONFIG* cfg, BEH beh, CONS* state)
/*
 * Create a new actor with specified state/behavior.
 */
{
	CONS* actor = NIL;

	DBUG_ENTER("actor");
	XDBUG_PRINT("", ("beh=@%p state=@%p", beh, state));
	XDBUG_PRINT("", ("state=%s", cons_to_str(state)));
	actor = MK_ACTOR(cons(MK_FUNC(beh), state));
	DBUG_PRINT("", ("actor=%s", cons_to_str(actor)));
	DBUG_RETURN actor;
}

CONS*
abe__become(CONS* self, BEH beh, CONS* state)
/*
 * Update state/behavior of an actor.
 */
{
	DBUG_ENTER("become");
	assert(actorp(self));
	rplaca(MK_CONS(self), MK_FUNC(beh));
	rplacd(MK_CONS(self), state);
	DBUG_PRINT("", ("self=%s", cons_to_str(self)));
	DBUG_RETURN self;
}

void
abe__send(CONFIG* cfg, CONS* target, CONS* msg)
/*
 * Queue an asynchronous message for the target actor.
 */
{
	CONS* entry = NIL;
	CONS* cq = CONFIG_QUEUE(cfg);

	DBUG_ENTER("send");
	DBUG_PRINT("", ("target=%s", cons_to_str(target)));
	assert(actorp(target));
	DBUG_PRINT("", ("msg=%s", cons_to_str(msg)));
	entry = cq_cons(target, msg);
	CQ_PUT(cq, cq_cons(entry, NIL));
	XDBUG_PRINT("", ("queue=%s", cons_to_str(CQ_PEEK(cq))));
	++cfg->q_count;
	DBUG_PRINT("", ("%d message(s) queued", cfg->q_count));
	DBUG_RETURN;
}

static void
t_queue_insert(CONS** qp, CONS* t, time_t t0s, time_t t0us)
{
	time_t t1s;
	time_t t1us;

	/* FIXME: this method abuses knowledge of t_queue representation */
	while (!nilp(*qp)) {
		CONS* m = (*qp)->first->first;
		t1s = MK_INT(map_get_def(m, ATOM("s"), NUMBER(0)));
		t1us = MK_INT(map_get_def(m, ATOM("us"), NUMBER(0)));
		if (tv_compare(t0s, t0us, t1s, t1us) > 0) {
			break;
		}
		qp = &((*qp)->rest);
	}
	*qp = cq_cons(t, *qp);
}

void
abe__send_after(CONFIG* cfg, CONS* delay, CONS* target, CONS* msg)
/*
 * Queue a message for delayed delivery to the target actor.
 */
{
	CONS* t;
	time_t s;
	time_t us;

	DBUG_ENTER("send_after");
	assert(numberp(delay));
	DBUG_PRINT("", ("delay=%d", MK_INT(delay)));
	DBUG_PRINT("", ("target=%s", cons_to_str(target)));
	DBUG_PRINT("", ("msg=%s", cons_to_str(msg)));
	assert(actorp(target));
	t = tv_increment(cfg->t_now_s, cfg->t_now_us, MK_INT(delay));
	s = MK_INT(map_get_def(t, ATOM("s"), NUMBER(0)));
	us = MK_INT(map_get_def(t, ATOM("us"), NUMBER(0)));
	DBUG_PRINT("", ("t = %lus %luus", s, us));
	t = cq_cons(t, cq_cons(target, msg));
	t_queue_insert(&cfg->t_queue, t, s, us);
	++cfg->t_count;
	DBUG_PRINT("", ("t_count=%d", cfg->t_count));
	DBUG_PRINT("", ("t_queue=%s", cons_to_str(cfg->t_queue)));
#if 0	/* HACK: ignore delay */
	DBUG_PRINT("WARNING", ("SEND_AFTER DOES NOT DELAY"));
	abe__send(cfg, target, msg);
#endif
	DBUG_RETURN;
}

static BOOL
abe__dispatch(CONFIG* cfg)
/*
 * Dispatch a single message/event.
 *
 * returns: TRUE on success, FALSE if there are no pending messages
 */
{
	CONS* cq = CONFIG_QUEUE(cfg);

	DBUG_ENTER("dispatch");
	DBUG_PRINT("", ("%d message(s) queued", cfg->q_count));
	XDBUG_PRINT("", ("cq = %s", cons_to_str(cq)));
	if (!CQ_EMPTY(cq)) {
		CONS* node = CQ_PEEK(cq);
		CONS* entry = car(node);
		CONS* actor;
		CONS* msg;
		BEH beh;

		XDBUG_PRINT("", ("entry=%s", cons_to_str(entry)));
		CQ_POP(CONFIG_QUEUE(cfg));
		node = cq_free(node);
		XDBUG_PRINT("", ("entry=%s", cons_to_str(entry)));
		assert(consp(entry));
		actor = car(entry);
		assert(actorp(actor));
		beh = _THIS(actor);
		msg = cdr(entry);
		--cfg->q_count;
		DBUG_PRINT("", ("actor=%s", cons_to_str(actor)));
		DBUG_PRINT("", ("msg=%s", cons_to_str(msg)));
		cfg->q_entry = entry;
		(*beh)(cfg);			/* call actor behavior to handle message */
		cfg->q_entry = NIL;
		entry = cq_free(entry);
		if (++cfg->msg_cnt_lo < 0) {
			++cfg->msg_cnt_hi;
			cfg->msg_cnt_lo = 0;
		}
		DBUG_RETURN TRUE;
	}
	DBUG_PRINT("", ("message queue empty."));
	DBUG_RETURN FALSE;
}

static void
abe__clock_tick(CONFIG* cfg)
/*
 * Update real-time clock and send any delayed messages whose time has come.
 */
{
	CONS* t_node;
	struct timeval tv;

	if (gettimeofday(&tv, NULL) == 0) {
		cfg->t_now_s = (tv.tv_sec - cfg->t_epoch);
		cfg->t_now_us = tv.tv_usec;
		DBUG_PRINT("timer", ("t = %lus %luus", cfg->t_now_s, cfg->t_now_us));
	}
	while (!nilp(t_node = cfg->t_queue)) {
		CONS* t_entry = car(t_node);
		CONS* t_timer = car(t_entry);
		time_t s;
		time_t us;

		s = MK_INT(map_get_def(t_timer, ATOM("s"), NUMBER(0)));
		us = MK_INT(map_get_def(t_timer, ATOM("us"), NUMBER(0)));
		if (tv_compare(s, us, cfg->t_now_s, cfg->t_now_us) > 0) {
			break;		/* not yet time to send this message */
		}
		/* pop message from delay-timer queue and send it */
		--cfg->t_count;
		DBUG_PRINT("timer", ("t_count=%d", cfg->t_count));
		cfg->t_queue = cdr(t_node);
		abe__send(cfg, car(cdr(t_entry)), cdr(cdr(t_entry)));
		cq_free(cdr(t_entry));
		t_entry = cq_free(t_entry);
		t_node = cq_free(t_node);
	}
}

#define	CLOCK_STEP_SIZE		256		/* number of messages between clock checks */

int
run_configuration(CONFIG* cfg, int msg_limit)
/*
 * Dispatch messages until the message queue is empty.
 * No more than <msg_limit> messages will be delivered.
 * Dispatch is aborted if the queue of waiting messages
 * exceeds the configuration limit.
 * 
 * returns: unused message budget or -1 if aborted
 */
{
	int clock_step = 0;

	DBUG_ENTER("run_configuration");
	while (msg_limit > 0) {
		if (--clock_step <= 0) {
			abe__clock_tick(cfg);
			clock_step = CLOCK_STEP_SIZE;
		}
		if (!abe__dispatch(cfg)) {
			break;
		}
		--msg_limit;
		if (cfg->q_count > cfg->q_limit) {
			DBUG_PRINT("", ("message queue limit exceeded!"));
			msg_limit = -1;
			break;
		}
	}
	DBUG_PRINT("", ("t_count=%d now=%lus %luus", cfg->t_count, cfg->t_now_s, cfg->t_now_us));
	DBUG_PRINT("", ("t_queue=%s", cons_to_str(cfg->t_queue)));
	DBUG_PRINT("", ("q_count=%d q_limit=%d msg_limit=%d", cfg->q_count, cfg->q_limit, msg_limit));
	DBUG_PRINT("", ("msg_cnt_hi=%d msg_cnt_lo=%d", cfg->msg_cnt_hi, cfg->msg_cnt_lo));
	DBUG_PRINT("", ("free_config_cnt=%d", free_config_cnt));
	DBUG_PRINT("", ("free_config_max=%d", free_config_max));
	DBUG_RETURN msg_limit;
}

void
report_configuration(CONFIG* cfg)
{
	CONS* p;
	CONS* q;
	char* m;
	CONS* cq = CONFIG_QUEUE(cfg);

	DBUG_ENTER("report_configuration");
	p = NIL;
	DBUG_PRINT("", ("NIL @%p = %s", p, cons_to_str(p)));

	DBUG_PRINT("", ("@%p {head:%p, tail:%p}", cq, car(cq), cdr(cq)));
	p = car(cq);
	assert(consp(p));
	DBUG_PRINT("", ("@%p = %s", p, cons_to_str(p)));
	
	assert(!nilp(p));
	p = car(p);
	DBUG_PRINT("", ("@%p = %s", p, cons_to_str(p)));
	assert(consp(p));
	q = car(p);
	DBUG_PRINT("", ("1st: actor=%p (behavior=%p, state=%p)", q, _THIS(q), _MINE(q)));
	assert(actorp(q));
	assert(funcp(car(MK_CONS(q))));
	assert(consp(cdr(MK_CONS(q))));
	
	m = (char*)cons(NIL, NIL);		/* get base address for dynamic allocations */
	DBUG_PRINT("", ("m=%p", m));
	DBUG_HEXDUMP((m - 256), 512);
	DBUG_RETURN;
}

static void
report_cq_usage()
{
	TRACE(printf("free_config_cnt=%d\n", free_config_cnt));
	TRACE(printf("free_config_max=%d\n", free_config_max));
/*	assert(free_config_cnt == 0); */
}

void
report_actor_usage(CONFIG* cfg)
{
	TRACE(printf("msg_cnt_hi=%d msg_cnt_lo=%d\n", cfg->msg_cnt_hi, cfg->msg_cnt_lo));
	report_cq_usage();
}
