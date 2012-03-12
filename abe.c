/*
 * abe.c -- experimental ACTOR-Based Environment
 *
 * Copyright 2008-2009 Dale Schumacher.  ALL RIGHTS RESERVED.
 */
static char	_Program[] = "ABE";
static char	_Version[] = "2009-11-03";
static char	_Copyright[] = "Copyright 2008-2009 Dale Schumacher";

#include <getopt.h>
#include "abe.h"
#include "sample.h"

/*#include <unistd.h>*/
extern int usleep();			/* FIXME: non-portable microsecond delay */

#include "dbug.h"
DBUG_UNIT("abe");

BOOL	test_mode = FALSE;		/* flag to run unit tests */
BOOL	init_sample = FALSE;	/* flag to pre-load sample configuration */

void
test_pre()
{
/*	char tmp_buf[256]; */

	DBUG_ENTER("test_pre");

	DBUG_PRINT("", ("sizeof(int)=%d", sizeof(int)));
	DBUG_PRINT("", ("sizeof(CONS*)=%d", sizeof(CONS*)));
	DBUG_PRINT("", ("sizeof(BEH)=%d", sizeof(BEH)));
	assert(sizeof(int) == sizeof(CONS*));
	assert(sizeof(BEH) == sizeof(CONS*));

	DBUG_PRINT("", ("NIL=16#%08lx (%p)", NIL, NIL));
	assert(nilp(NIL));
	assert(nilp(NIL) == _nilp(NIL));

	DBUG_PRINT("", ("sizeof(BOOL)=%d", sizeof(BOOL)));
	assert(sizeof(BOOL) == sizeof(CONS*));
	DBUG_PRINT("", ("TRUE=16#%08lx (%p)", TRUE, TRUE));
	DBUG_PRINT("", ("FALSE=16#%08lx (%p)", FALSE, FALSE));
	assert(TRUE);
	assert(!FALSE);
	assert(TRUE == ((BOOL)(0 == 0)));
	assert(FALSE == ((BOOL)(0 != 0)));

#if TYPETAG_USES_3MSB
	DBUG_PRINT("", ("Type tag in MSB[31:29]"));
#endif /* TYPETAG_USES_3MSB */
#if TYPETAG_USES_2LSB
	DBUG_PRINT("", ("Type tag in LSB[1:0]"));
#endif /* TYPETAG_USES_2LSB */
#if TYPETAG_USES_3LSB
	DBUG_PRINT("", ("Type tag in LSB[2/1:0]"));
#endif /* TYPETAG_USES_3LSB */
#if TYPETAG_USES_1LSB_2MSB
	DBUG_PRINT("", ("Type tag in LSB[0]+MSB[31:30]"));
#endif /* TYPETAG_USES_1LSB_2MSB */
	DBUG_PRINT("", ("_Program[]=16#%08lx (%p)", _Program, _Program));
	assert(_Program == MK_PTR(MK_REF(_Program)));

/* FIXME: enable this test when stack allocation is allowed
	DBUG_PRINT("", ("tmp_buf[]=%p", tmp_buf));
	assert(tmp_buf == MK_PTR(MK_REF(tmp_buf)));
*/

	DBUG_PRINT("", ("test_pre()=16#%08lx (%p)", test_pre, test_pre));
	assert(test_pre == MK_BEH(MK_FUNC(test_pre)));

	DBUG_RETURN;
}

CONS*
system_info()
{
	CONS* info = NIL;
	
	info = map_put(info, ATOM("Program"), ATOM(_Program));
	info = map_put(info, ATOM("Version"), ATOM(_Version));
	info = map_put(info, ATOM("Copyright"), ATOM(_Copyright));
	return info;
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
usage: %s [-ts] [-n count] [-# dbug] filename ...\n",
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
	int	counter = 5;			/* default 5 second counter for ticker */

	DBUG_ENTER("main");
	DBUG_PROCESS(argv[0]);
	while ((c = getopt(argc, argv, "tsn:#:V")) != EOF) {
		switch(c) {
		case 't':	test_mode = TRUE;		break;
		case 's':	init_sample = TRUE;		break;
		case 'n':	counter = atoi(optarg);	break;
		case '#':	DBUG_PUSH(optarg);		break;
		case 'V':	banner();				exit(EXIT_SUCCESS);
		case '?':							usage();
		default:							usage();
		}
	}
	banner();
	if (test_mode) {
		DBUG_PRINT("", ("_nilp()@%p, main()@%p", _nilp, main));
		test_pre();
		test_number();
		test_gc();
		test_cons();
		test_atom();
		test_emit();
	}
	if (init_sample) {
		int limit = 100;
		int	budget = 1000000;
		int n;
		CONFIG* cfg = new_configuration(limit);

		tick_init();
		test_sample(cfg);
		start_ticker(cfg, counter);
		DBUG_PRINT("", ("--begin--"));
		TRACE(printf("sample running with %d queue limit and %d budget\n", limit, budget));
		sample_done = FALSE;
		do {
			n = run_configuration(cfg, budget);
			if (cfg->q_count > 0) {
				TRACE(printf("queue length %d with %d budget remaining\n", cfg->q_count, n));
			}
			if (cfg->t_count > 0) {
				usleep(TICK_FREQ / 10);	/* delayed messages pending... sleep for a while */
			}
		} while ((cfg->q_count < cfg->q_limit) && !sample_done);
		DBUG_PRINT("", ("n=%d q_count=%d q_limit=%d t_count=%d sample_done=%s",
			n, cfg->q_count, cfg->q_limit, cfg->t_count, (sample_done ? "TRUE" : "FALSE")));
		DBUG_PRINT("", ("--end--"));
		report_actor_usage(cfg);
	}
	report_cons_stats();
	DBUG_RETURN (exit(EXIT_SUCCESS), 0);
}
