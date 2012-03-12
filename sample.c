/*
 *  sample.c -- Sample hand-coded Actors
 *
 * Copyright 2008 Dale Schumacher.  ALL RIGHTS RESERVED.
 */
#include "sample.h"
#include "abe.h"

#include "dbug.h"
DBUG_UNIT("sample");

BOOL sample_done = FALSE;
static time_t time_base = 0;

void
tick_init()
{
	time_t t0;
	time_t t1;

	DBUG_ENTER("tick_init");
	time(&t0);
	do {				/* synchronize timer at boundry */
		time(&t1);
	} while (t0 == t1);
	DBUG_PRINT("", ("t0=%d t1=%d", t0, t1));
	time_base = t0;
	DBUG_RETURN;
}

CONS*
tick_time()
{
	time_t t;

	DBUG_ENTER("tick_time");
	time(&t);
	DBUG_RETURN NUMBER(t - time_base);
}

BEH_DECL(print_text)
{
	CONS* text;

	DBUG_ENTER("print_text");
	text = map_get(WHAT, ATOM("text"));
	printf("%s", (atomp(text) ? atom_str(text) : cons_to_str(text)));
	DBUG_RETURN;
}

BEH_DECL(print_line)
{
	CONS* text;

	DBUG_ENTER("print_line");
	text = map_get(WHAT, ATOM("text"));
	printf("%s", (atomp(text) ? atom_str(text) : cons_to_str(text)));
	putchar('\n');
	DBUG_RETURN;
}

BEH_DECL(each_second)
{
	CONS* count = map_get(MINE, ATOM("count"));
	CONS* tick = map_get(WHAT, ATOM("time"));
	CONS* ticker = map_get(WHAT, ATOM("from"));

	DBUG_ENTER("each_second");
	DBUG_PRINT("", ("count=%d tick=%d", MK_INT(count), MK_INT(tick)));
	SEND(ACTOR(print_line, NIL), map_put(NIL, ATOM("text"), tick));
	if (numberp(count)) {
		int n = MK_INT(count);
		if (n <= 0) {
			if (consp(ticker) && funcp(car(ticker)) && consp(cdr(ticker))) {
				SEND(ticker, map_put(NIL, ATOM("op"), ATOM("stop")));
			}
			BECOME(sink_beh, NIL);
			DBUG_RETURN;
		}
		count = NUMBER(--n);
		BECOME(THIS, map_put(NIL, ATOM("count"), count));
	}
	DBUG_RETURN;
}

BEH_DECL(time_ticker)
{
	CONS* msg = WHAT;
	CONS* state = MINE;
	CONS* t0 = map_get(state, ATOM("time"));
	CONS* sec = map_get(state, ATOM("sec"));
	CONS* t1 = map_get(msg, ATOM("time"));
	CONS* op = map_get(msg, ATOM("op"));
	CONS* entry = car(msg);

	DBUG_ENTER("time_ticker");
	DBUG_PRINT("", ("t0=%p t1=%p op=%p", t0, t1, op));
	if (atomp(op)) {
		DBUG_PRINT("", ("op=%s", atom_str(op)));
		if (op == ATOM("stop")) {
			BECOME(sink_beh, NIL);
			DBUG_PRINT("", ("STOPPED."));
			TRACE(printf("time_ticker: STOPPED.\n"));
			sample_done = TRUE;				/* set global flag */
			DBUG_RETURN;					/* stop ticking */
		}
		DBUG_PRINT("", ("UNKNOWN OPERATION."));
		DBUG_RETURN;
	} else if (t0 != t1) {
		if (consp(sec) && funcp(car(sec)) && consp(cdr(sec))) {
			CONS* sec_msg = NIL;

			sec_msg = map_put(sec_msg, ATOM("from"), SELF);
			sec_msg = map_put(sec_msg, ATOM("time"), t1);
			SEND(sec, sec_msg);
		}
		state = map_put(NIL, ATOM("sec"), sec);
		state = map_put(state, ATOM("time"), t1);
		BECOME(THIS, state);
	}
	DBUG_PRINT("", ("msg=%s", cons_to_str(msg)));
	assert(car(entry) == ATOM("time"));
	t1 = tick_time();
	DBUG_PRINT("", ("new tick time is %p", t1));
	rplacd(entry, t1);	/* FIXME:  HACK!!  re-use the message to reduce garbage */
	DBUG_PRINT("", ("msg'=%s", cons_to_str(msg)));
	SEND_AFTER(NUMBER(TICK_FREQ / 3), SELF, msg);
	DBUG_RETURN;
}

void
start_ticker(CONFIG* cfg, int count)
{
	CONS* actor;
	CONS* state;
	CONS* msg;
	
	DBUG_ENTER("start_ticker");
	DBUG_PRINT("", ("count=%d", count));
	state = map_put(NIL, ATOM("count"), NUMBER(count));
	actor = CFG_ACTOR(cfg, each_second, state);
	
	state = map_put(NIL, ATOM("sec"), actor);
	msg = map_put(NIL, ATOM("time"), tick_time());
	CFG_SEND(cfg, CFG_ACTOR(cfg, time_ticker, state), msg);
	DBUG_RETURN;
}

BEH_DECL(say_hello)
{
	CONS* name = map_get(WHAT, ATOM("name"));
	CONS* printer = ACTOR(print_text, NIL);
	CONS* a_text = ATOM("text");

	DBUG_ENTER("say_hello");
	SEND(printer, map_put(NIL, a_text, ATOM("Hello, ")));
	SEND(printer, MINE);
	SEND(printer, map_put(NIL, a_text, ATOM(" from ")));
	SEND(ACTOR(print_line, NIL), map_put(NIL, a_text, name));
	DBUG_RETURN;
}

void
test_sample(CONFIG* cfg)
{
	CONS* actor;
	CONS* state;
	CONS* msg;

	DBUG_ENTER("test_sample");
	TRACE(printf("--test_sample--\n"));
	state = map_put(NIL, ATOM("text"), ATOM("Actor"));
	actor = CFG_ACTOR(cfg, say_hello, state);
	
	msg = map_put(NIL, ATOM("name"), ATOM("World!"));
	CFG_SEND(cfg, actor, msg);
	
	msg = NIL;
	msg = map_put(msg, ATOM("name"), NUMBER(42));
	msg = map_put(msg, ATOM("addr"), actor);
	CFG_SEND(cfg, actor, msg);
	
	report_configuration(cfg);
	DBUG_RETURN;
}
