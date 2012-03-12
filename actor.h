/*
 * actor.h -- experimental Actor-model runtime
 *
 * Copyright 2008-2009 Dale Schumacher.  ALL RIGHTS RESERVED.
 */
#ifndef ACTOR_H
#define ACTOR_H

#include "types.h"

#define	TICK_FREQ		(1000 * 1000)	/* number of timer ticks per second */

#define	CONFIG_QUEUE(cfg)	((CONS*)(cfg))

#define	BEH_SIG			CONFIG*
#define	BEH_PROTO		BEH_SIG abe__config
#define	BEH_DECL(name)	void name (BEH_PROTO)

typedef void (*BEH)(BEH_SIG);

#define	MK_BEH(p)		((BEH)((ptrdiff_t)MK_PTR(p)))
#define	_THIS(a)		_this(a)
#define	_MINE(a)		_mine(a)

#define CFG				(abe__config)
#define	SELF			car(CFG->q_entry)
#define	THIS			_THIS(SELF)
#define	MINE			_MINE(SELF)
#define	WHAT			cdr(CFG->q_entry)
#define	NOW				tv_create(CFG->t_now_s, CFG->t_now_us)
#define	CFG_ACTOR(c,b,s) abe__actor((c),(b),(s))
#define	ACTOR(b,s)		abe__actor(CFG,(b),(s))
#define	CFG_SEND(c,a,m)	abe__send((c),(a),(m))
#define	SEND(a,m)		abe__send(CFG,(a),(m))
#define	SEND_AFTER(t,a,m) abe__send_after(CFG,(t),(a),(m))
#define	BECOME(b,s)		abe__become(SELF,(b),(s))

BEH			_this(CONS* self);
CONS*		_mine(CONS* self);

BEH_DECL(sink_beh);
BEH_DECL(error_msg);
BEH_DECL(assert_msg);

CONS*		tv_create(time_t s, time_t us);
CONS*		tv_increment(time_t s, time_t us, time_t d);
int			tv_compare(time_t t0s, time_t t0us, time_t t1s, time_t t1us);
CONFIG*		new_configuration(int q_limit);
void		cfg_add_gc_root(CONFIG* cfg, CONS* root);
void		cfg_force_gc(CONFIG* cfg);
void		cfg_start_gc(CONFIG* cfg);
CONS*		abe__actor(CONFIG* cfg, BEH beh, CONS* state);
CONS*		abe__become(CONS* self, BEH beh, CONS* state);
void		abe__send(CONFIG* cfg, CONS* target, CONS* msg);
void		abe__send_after(CONFIG* cfg, CONS* delay, CONS* target, CONS* msg);
int			run_configuration(CONFIG* cfg, int msg_limit);

void		report_configuration(CONFIG* cfg);
void		report_actor_usage(CONFIG* cfg);

#endif /* ACTOR_H */
