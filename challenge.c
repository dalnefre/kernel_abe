/*
 *  challenge.c -- Joe Armstrong's Erlang challenge:
 *		Create a ring of M processes
 *		Send N simple messages around the ring
 *		Increase M until out of resources
 *
 * Copyright 2008 Dale Schumacher.  ALL RIGHTS RESERVED.
 */
static char	_Program[] = "Challenge";
static char	_Version[] = "2008-05-12";
static char	_Copyright[] = "Copyright 2008 Dale Schumacher";

#include <getopt.h>
#include "abe.h"

#include "dbug.h"
DBUG_UNIT("challenge");

#if 0
#define	M_CNT	(-1)	/* sending -1 will trigger a countdown based on the highest numbered process */
#else
#define	M_CNT	(100)	/* with a large (1M) number of processes, we want to send fewer messages */
#endif

/**
countdown_ring:
	BEHAVIOR {n:$n, a:$a}
	$m -> [
		IF preceeds($m, 0) [
			SEND $a $n
		] ELIF preceeds(0, $n) [
			SEND $a $m
		] ELIF preceeds(0, $m) [
			SEND $a sum($m, -1)
		]
	]
	DONE
**/
BEH_DECL(countdown_ring)
{
	CONS* m = WHAT;
	CONS* state = MINE;
	CONS* n = map_get(state, ATOM("n"));
	CONS* a = map_get(state, ATOM("a"));

	DBUG_ENTER("countdown_ring");
	DBUG_PRINT("", ("self=%p", SELF));
	DBUG_PRINT("", ("m=%d n=%d a=%p", MK_INT(m), MK_INT(n), a));
	if (MK_INT(m) < 0) {
		SEND(a, n);
	} else if (0 < MK_INT(n)) {
		SEND(a, m);
	} else if (0 < MK_INT(m)) {
		SEND(a, NUMBER(MK_INT(m) - 1));
	}
	DBUG_RETURN;
}

/**
challenge:
	BEHAVIOR {n:$n}
	$m -> [
		IF preceeds(0, $n) [
			a: ACTOR $challenge {n:sum($n, -1)}
			BECOME $countdown_ring {n:$n, a:$a}
			SEND $a $m
		] ELSE [
			BECOME $countdown_ring {n:$n, a:$m}
			SEND $m -1
		]
	]
	DONE
**/
BEH_DECL(challenge)
{
	CONS* msg = WHAT;
	CONS* state = MINE;
	CONS* n = map_get(state, ATOM("n"));
	CONS* a = map_get(state, ATOM("a"));

	DBUG_ENTER("challenge");
	DBUG_PRINT("", ("self=%p", SELF));
	DBUG_PRINT("", ("msg=%p n=%d a=%p", msg, MK_INT(n), a));
	if (0 < MK_INT(n)) {
		a = ACTOR(challenge, map_put(NIL, ATOM("n"), NUMBER(MK_INT(n) - 1)));
		BECOME(countdown_ring, map_put(state, ATOM("a"), a));
		SEND(a, msg);
	} else {
		state = NIL;
		state = map_put(state, ATOM("a"), msg);
		state = map_put(state, ATOM("n"), n);
		BECOME(countdown_ring, state);
		SEND(msg, NUMBER(M_CNT));			/* disable this line for process creation only */
	}
	DBUG_RETURN;
}

/**
	a: ACTOR $challenge {n:$counter}
	SEND $a $a
**/
void
test_erlang_challenge(int counter)
{
	CONFIG* cfg;
	CONS* actor;
	int n;
	int m;

	DBUG_ENTER("test_erlang_challenge");
	cfg = new_configuration(10);
	actor = CFG_ACTOR(cfg, challenge, map_put(NIL, ATOM("n"), NUMBER(counter)));
	CFG_SEND(cfg, actor, actor);
	for (m = 1; m < 1000000; ++m) {
		n = run_configuration(cfg, 1000000);
		if (cfg->q_count <= 0) {
			/* queue empty */
			break;
		}
	}
	TRACE(printf("~%dM messages delivered.\n", m));
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
usage: %s [-n count] [-# dbug]\n",
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
	int counter = 1000;

	DBUG_ENTER("main");
	DBUG_PROCESS(argv[0]);
	while ((c = getopt(argc, argv, "n:#:V")) != EOF) {
		switch(c) {
		case 'n':	counter = atoi(optarg);	break;
		case '#':	DBUG_PUSH(optarg);		break;
		case 'V':	banner();				exit(EXIT_SUCCESS);
		case '?':							usage();
		default:							usage();
		}
	}
	banner();

	test_erlang_challenge(counter);	/* this test involves running the dispatch loop */

	report_cons_stats();
	DBUG_RETURN (exit(EXIT_SUCCESS), 0);
}
