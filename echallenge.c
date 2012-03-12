/*
 * echallenge.c -- Joe Armstrong's Erlang challenge (v2):
 *		Create a ring of N processes
 *		Send M simple messages around the ring
 *		Increase N until out of resources
 *
 * Copyright 2009 Dale Schumacher.  ALL RIGHTS RESERVED.
 */
static char	_Program[] = "EChallenge";
static char	_Version[] = "2009-09-04";
static char	_Copyright[] = "Copyright 2009 Dale Schumacher";

#include <getopt.h>
#include "abe.h"

#include "dbug.h"
DBUG_UNIT("echallenge");

static CONS* timing_actor;

BEH_DECL(timing_beh)
{
	char* s;

	DBUG_ENTER("timing_beh");
	s = cons_to_str(NOW);
	DBUG_PRINT("", ("[timestamp] %s", s));
	fprintf(stderr, "[timestamp] %s\n", s);
	DBUG_RETURN;
}

/**
countdown_ring_0_beh(first) = \m.[
	IF $m = 0 [
		BECOME \_.[]
	] ELSE [
		SEND dec(m) TO first
	]
]
**/
BEH_DECL(countdown_ring_0_beh)
{
	CONS* m = WHAT;
	CONS* first = MINE;
	int m_int = MK_INT(m);

	DBUG_ENTER("countdown_ring_0_beh");
	if (m_int <= 0) {
		BECOME(sink_beh, NIL);
		SEND(timing_actor, NIL);
	} else {
		SEND(first, NUMBER(m_int - 1));
	}
	DBUG_RETURN;
}

/**
countdown_ring_beh(next) = \m.[
	SEND m TO next
]
**/
BEH_DECL(countdown_ring_beh)
{
	CONS* m = WHAT;
	CONS* next = MINE;

	DBUG_ENTER("countdown_ring_beh");
	SEND(next, m);
	DBUG_RETURN;
}

/**
countdown_builder_beh(n) = \(first, m).[
	IF $n = 0 [
		BECOME countdown_ring_0_beh(first)
		SEND m TO first  # start message passing phase
	] ELSE [
		CREATE next WITH countdown_builder_beh(dec(n))
		BECOME countdown_ring_beh(next)
		SEND (first, m) TO next
	]
]
**/
BEH_DECL(countdown_builder_beh)
{
	CONS* n;
	CONS* first;
	CONS* m;
	int n_int;
/*
	CONS* n = MINE;
	CONS* first = car(WHAT);
	CONS* m = cdr(WHAT);
	int n_int = MK_INT(n);
*/
	DBUG_ENTER("countdown_builder_beh");
	n = MINE;
	assert(numberp(n));
	assert(consp(WHAT));
	first = car(WHAT);
	assert(actorp(first));
	m = cdr(WHAT);
	assert(numberp(m));
	n_int = MK_INT(n);
	DBUG_PRINT("", ("n=%d m=%d", n_int, MK_INT(m)));
	if (n_int <= 0) {
		BECOME(countdown_ring_0_beh, first);
		SEND(timing_actor, NIL);
		SEND(first, m);			/* NOTE: disable this line for process creation only */
	} else {
		CONS* next;

		next = ACTOR(countdown_builder_beh, NUMBER(n_int - 1));
		BECOME(countdown_ring_beh, next);
		SEND(next, WHAT);
	}
	DBUG_RETURN;
}

/**
ring_link_beh(next) = \first.[
	SEND first TO next
]
**/
BEH_DECL(ring_link_beh)
{
	CONS* next = MINE;
	CONS* first = WHAT;

	DBUG_ENTER("countdown_ring_beh");
	SEND(next, first);
	DBUG_RETURN;
}

/**
ring_seed_beh(n) = \first.[
	CREATE next WITH ring_seed_beh(inc(n))
	BECOME ring_link_beh(next)
	SEND first TO first
]
**/
BEH_DECL(ring_seed_beh)
{
	static int thresh = 1 << 10;
	CONS* n = MINE;
	CONS* first = WHAT;
	CONS* next;
	int n_int = MK_INT(n);

	DBUG_ENTER("ring_seed_beh");
	if (n_int >= thresh) {
		thresh <<= 1;
		TRACE(fprintf(stderr, "%d:", n_int));
		SEND(timing_actor, NIL);
	}
	next = ACTOR(ring_seed_beh, NUMBER(n_int + 1));
	BECOME(ring_link_beh, next);
	SEND(first, first);
	DBUG_RETURN;
}

/**
IF $n = 0 [
	CREATE ring WITH ring_seed_beh(0)
	SEND ring TO ring
] ELSE [
	CREATE countdown WITH countdown_builder_beh(n)
	SEND (countdown, m) TO countdown
]
**/
void
erlang_challenge(int n, int m)
{
	CONFIG* cfg;
	int cnt;

	DBUG_ENTER("erlang_challenge");
	cfg = new_configuration(10);
	timing_actor = CFG_ACTOR(cfg, timing_beh, NIL);
	if (n <= 0) {
		CONS* ring;

		TRACE(printf("Seeding unbounded growth message ring.\n"));
		ring = CFG_ACTOR(cfg, ring_seed_beh, NUMBER(0));
		CFG_SEND(cfg, ring, ring);
	} else {
		CONS* countdown;

		TRACE(printf("Sending %d messages around a ring of %d processes.\n", m, n));
		countdown = CFG_ACTOR(cfg, countdown_builder_beh, NUMBER(n));
		CFG_SEND(cfg, countdown, cons(countdown, NUMBER(m)));
	}
	CFG_SEND(cfg, timing_actor, NIL);
	for (cnt = 1; cnt < 1000000; ++cnt) {
		int max = 1000000;
		int rem;

		rem = run_configuration(cfg, max);
/*		TRACE(printf("+ %d messages...\n", (max - rem))); */
		TRACE(fputc('.', stderr));
		if (cfg->q_count <= 0) {
			/* queue empty */
			break;
		}
	}
	TRACE(printf("< %dM messages delivered.\n", cnt));
	report_actor_usage(cfg);
	DBUG_RETURN;
}

void
report_cons_stats()
{
	report_atom_usage();
	report_cons_usage();
}

void
usage(void)
{
	fprintf(stderr, "\
usage: %s [-n processes] [-m messages] [-# dbug]\n",
		_Program);
	exit(EXIT_FAILURE);
}

void
banner(void)
{
	printf("%s v%s -- %s\n", _Program, _Version, _Copyright);
}

int
main(int argc, char** argv)
{
	int c;
	int n = 0;
	int m = 100;

	DBUG_ENTER("main");
	DBUG_PROCESS(argv[0]);
	while ((c = getopt(argc, argv, "n:m:#:V")) != EOF) {
		switch(c) {
		case 'n':	n = atoi(optarg);		break;
		case 'm':	m = atoi(optarg);		break;
		case '#':	DBUG_PUSH(optarg);		break;
		case 'V':	banner();				exit(EXIT_SUCCESS);
		case '?':							usage();
		default:							usage();
		}
	}
	banner();

	erlang_challenge(n, m);	/* this test involves running the dispatch loop */

	report_cons_stats();
	DBUG_RETURN (exit(EXIT_SUCCESS), 0);
}
