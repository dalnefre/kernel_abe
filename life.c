/*
 *  life.c -- Conway's "Game of Life" built on the Actor model
 *
 * Copyright 2008 Dale Schumacher.  ALL RIGHTS RESERVED.
 */
static char	_Program[] = "Life";
static char	_Version[] = "2008-05-12";
static char	_Copyright[] = "Copyright 2008 Dale Schumacher";

#include <getopt.h>
#include "abe.h"

#include "dbug.h"
DBUG_UNIT("life");

#define	EMPTY	'.'
#define	FULL	'o'
#define	DEAD	'x'

#define	_		EMPTY
#define	O		FULL

#define	X_MAX	8
#define	Y_MAX	8

#if 0 /* glider */
static int grid[Y_MAX][X_MAX] = {
	{_,_,_,_,_,_,_,_,},
	{_,_,_,_,_,_,_,_,},
	{_,_,_,_,_,_,_,_,},
	{_,_,_,_,_,_,_,_,},
	{_,_,O,_,_,_,_,_,},
	{_,_,O,O,_,_,_,_,},
	{_,O,_,O,_,_,_,_,},
	{_,_,_,_,_,_,_,_,}
};
#endif

#if 1 /* R-pentomino */
static int grid[Y_MAX][X_MAX] = {
	{_,_,_,_,_,_,_,_,},
	{_,_,_,_,_,_,_,_,},
	{_,_,_,_,_,_,_,_,},
	{_,_,_,_,O,O,_,_,},
	{_,_,_,O,O,_,_,_,},
	{_,_,_,_,O,_,_,_,},
	{_,_,_,_,_,_,_,_,},
	{_,_,_,_,_,_,_,_,}
};
#endif

#if 0 /* a complex test pattern */
static int grid[Y_MAX][X_MAX] = {
	{_,_,_,_,_,_,_,_},
	{_,_,_,_,_,_,O,_},
	{_,_,_,_,O,_,O,O},
	{_,_,_,_,O,_,O,_},
	{_,_,_,_,O,_,_,_},
	{_,_,O,_,_,_,_,_},
	{O,_,O,_,_,_,_,_},
	{_,_,_,_,_,_,_,_}
};
#endif

int
clamp_grid_x(int x)
{
	while (x < 0) {
		x += X_MAX;
	}
	while (x >= X_MAX) {
		x -= X_MAX;
	}
	return x;
}

int
clamp_grid_y(int y)
{
	while (y < 0) {
		y += Y_MAX;
	}
	while (y >= Y_MAX) {
		y -= Y_MAX;
	}
	return y;
}

int
get_grid_value(int x, int y) {
	x = clamp_grid_x(x);
	y = clamp_grid_y(y);
	return grid[y][x];
}

void
set_grid_value(int x, int y, int value) {
	x = clamp_grid_x(x);
	y = clamp_grid_y(y);
	grid[y][x] = value;
}

void
print_grid()
{
	int x, y;
	
	for (y = 0; y < Y_MAX; ++y) {
		for (x = 0; x < X_MAX; ++x) {
			printf(" %c", get_grid_value(x, y));
		}
		printf("\n");
	}
}

/**
int_generator(next:<next>){step:<step>, limit:<limit>, label:<label>, ctx:<ctx>, send-to:<send-to>} =
	IF preceeds?(0, <step>)
		IF NOT preceeds?(<limit>, <next>)
			map_put(<ctx>, <label>, <next>) => <send-to>
			{next:sum(<next>, <step>)} => self()
	ELSE
		IF NOT preceeds?(<next>, <limit>)
			map_put(<ctx>, <label>, <next>) => <send-to>
			{next:sum(<next>, <step>)} => self()
**/
BEH_DECL(int_generator)
{
	CONS* msg = WHAT;
	CONS* next = map_get(msg, ATOM("next"));
	CONS* state = MINE;
	CONS* step = map_get(state, ATOM("step"));
	CONS* limit = map_get(state, ATOM("limit"));
	CONS* label = map_get(state, ATOM("label"));
	CONS* ctx = map_get_def(state, ATOM("ctx"), NIL);
	CONS* send_to = map_get(state, ATOM("send-to"));
	
	DBUG_ENTER("int_generator");
	DBUG_PRINT("", ("next=%s", cons_to_str(next)));
	assert(atomp(label));
	assert(actorp(send_to));
	if (0 < MK_INT(step)) {
		if (MK_INT(next) <= MK_INT(limit)) {
			SEND(send_to, map_put(ctx, label, next));
			next = NUMBER(MK_INT(next) + MK_INT(step));
			SEND(SELF, map_put(NIL, ATOM("next"), next));
		}
	} else {
		if (MK_INT(next) >= MK_INT(limit)) {
			SEND(send_to, map_put(ctx, label, next));
			next = NUMBER(MK_INT(next) + MK_INT(step));
			SEND(SELF, map_put(NIL, ATOM("next"), next));
		}
	}
	DBUG_RETURN;
}

/**
seq_generator(<msg>){next:<next>, step:<step>, limit:<limit>, label:<label>, ctx:<ctx>, send-to:<send-to>} =
	<ctx> := map_put_all(<ctx>, <msg>)
	{next:<next>} => actor(int_generator, {step:<step>, limit:<limit>, label:<label>, ctx:<ctx>, send-to:<send-to>})
**/
BEH_DECL(seq_generator)
{
	CONS* msg = WHAT;
	CONS* state = MINE;
	CONS* next = map_get(state, ATOM("next"));
	CONS* step = map_get(state, ATOM("step"));
	CONS* limit = map_get(state, ATOM("limit"));
	CONS* label = map_get(state, ATOM("label"));
	CONS* ctx = map_get_def(state, ATOM("ctx"), NIL);
	CONS* send_to = map_get(state, ATOM("send-to"));
	
	DBUG_ENTER("seq_generator");
	DBUG_PRINT("", ("msg=%s", cons_to_str(msg)));
	ctx = map_put_all(ctx, msg);
	state = NIL;
	state = map_put(state, ATOM("send-to"), send_to);
	state = map_put(state, ATOM("ctx"), ctx);
	state = map_put(state, ATOM("label"), label);
	state = map_put(state, ATOM("limit"), limit);
	state = map_put(state, ATOM("step"), step);
/*	actor = ACTOR(int_generator, state); */
	msg = NIL;
	msg = map_put(msg, ATOM("next"), next);
	SEND(ACTOR(int_generator, state), msg);
	DBUG_RETURN;
}

static CONS* cells[Y_MAX][X_MAX];

CONS*
get_cell(int x, int y) {
	x = clamp_grid_x(x);
	y = clamp_grid_y(y);
	return cells[y][x];
}

int
count_neighbors(int x, int y)
{
	int n = 0;
/*	if (get_grid_value(x + 0, y + 0) == FULL) ++n; */
	if (get_grid_value(x + 1, y + 0) == FULL) ++n;
	if (get_grid_value(x + 1, y + 1) == FULL) ++n;
	if (get_grid_value(x + 0, y + 1) == FULL) ++n;
	if (get_grid_value(x - 1, y + 1) == FULL) ++n;
	if (get_grid_value(x - 1, y + 0) == FULL) ++n;
	if (get_grid_value(x - 1, y - 1) == FULL) ++n;
	if (get_grid_value(x + 0, y - 1) == FULL) ++n;
	if (get_grid_value(x + 1, y - 1) == FULL) ++n;
	return n;
}

/**
cell_actor(request:<request>, reply-to:<reply-to>){value:<value>, x:<x>, y:<y>} =
	IF equal?(<request>, update-grid)
		set_grid_value(<x>, <y>, <value>)
	ELIF equal?(<request>, gen-next)
		<n> := count_neighbors(<x>, <y>)
		IF equal?(<value>, EMPTY)
			IF equal?(<n>, 3)
				become(cell_actor, {value:FULL, x:<x>, y:<y>})
		ELIF equal?(<value>, FULL)
			IF preceed?(<n>, 2) OR preceed?(3, <n>)
				become(cell_actor, {value:EMPTY, x:<x>, y:<y>})
	ELIF equal?(<request>, die)
		set_grid_value(<x>, <y>, DEAD)
**/
BEH_DECL(cell_actor)
{
	CONS* msg = WHAT;
	CONS* request = map_get(msg, ATOM("request"));
/*	CONS* reply_to = map_get(msg, ATOM("reply-to")); */
	CONS* state = MINE;
	CONS* value = map_get(state, ATOM("value"));
	CONS* x = map_get(state, ATOM("x"));
	CONS* y = map_get(state, ATOM("y"));
	
	DBUG_ENTER("cell_actor");
	DBUG_PRINT("", ("x=%d y=%d value=%c", MK_INT(x), MK_INT(y), MK_INT(value)));
	DBUG_PRINT("", ("request=%s", cons_to_str(request)));
	if (request == ATOM("update-grid")) {
		set_grid_value(MK_INT(x), MK_INT(y), MK_INT(value));
	} else if (request == ATOM("gen-next")) {
		int n = count_neighbors(MK_INT(x), MK_INT(y));
		DBUG_PRINT("", ("neighbors=%d", n));
		if (value == NUMBER(EMPTY)) {
			if (n == 3) {
				CONS* state = NIL;
				state = map_put(state, ATOM("y"), y);
				state = map_put(state, ATOM("x"), x);
				state = map_put(state, ATOM("value"), NUMBER(FULL));
				DBUG_PRINT("", ("cell birth"));
				BECOME(THIS, state);
			}
		} else if (value == NUMBER(FULL)) {
			if ((n < 2) || (n > 3)) {
				CONS* state = NIL;
				state = map_put(state, ATOM("y"), y);
				state = map_put(state, ATOM("x"), x);
				state = map_put(state, ATOM("value"), NUMBER(EMPTY));
				DBUG_PRINT("", ("cell death"));
				BECOME(THIS, state);
			}
		}
	} else if (request == ATOM("die")) {
		if (value == NUMBER(FULL)) {
			value = NUMBER('*');
		} else {
			value = NUMBER(DEAD);
		}
		set_grid_value(MK_INT(x), MK_INT(y), MK_INT(value));
	}
	DBUG_RETURN;
}

/**
ask_cell(x:<x>, y:<y>){request:<request>, reply-to:<reply-to>} =
	<cell> := get_cell(<x>, <y>)
	{request:<request>, reply-to:<reply-to>} => <cell>
**/
BEH_DECL(ask_cell)
{
	CONS* msg = WHAT;
	CONS* x = map_get(msg, ATOM("x"));
	CONS* y = map_get(msg, ATOM("y"));
	CONS* state = MINE;
/*	CONS* request = map_get(state, ATOM("request")); */
/*	CONS* reply_to = map_get(state, ATOM("reply-to")); */
	CONS* cell;

	DBUG_ENTER("ask_cell");
	cell = get_cell(MK_INT(x), MK_INT(y));
	assert(actorp(cell));
	msg = state;
	DBUG_PRINT("", ("x=%d y=%d msg=%s", MK_INT(x), MK_INT(y), cons_to_str(msg)));
	SEND(cell, msg);
	DBUG_RETURN;
}

/**
ask_all_cells(request:<request>, reply-to:<reply-to>){} =
	<cell> := actor(ask_cell, {request:<request>, reply-to:<reply-to>})
	<cols> := actor(seq_generator,
		{next:0, step:1, limit:(<y-max> - 1), label:y, ctx:(), send-to:<cell>})
	<rows> := actor(int_generator,
		{step:1, limit:(<x-max> - 1), label:x, ctx:(), send-to:<cols>})
	{next:0} => <rows>
**/
BEH_DECL(ask_all_cells)
{
	CONS* msg = WHAT;
/*	CONS* request = map_get(msg, ATOM("request")); */
/*	CONS* reply_to = map_get(msg, ATOM("reply-to")); */
	CONS* state = MINE;
	CONS* actor;

	DBUG_ENTER("ask_all_cells");
	state = NIL;
	state = map_put(state, ATOM("send-to"), ACTOR(ask_cell, msg));
	state = map_put(state, ATOM("label"), ATOM("y"));
	state = map_put(state, ATOM("limit"), NUMBER(Y_MAX - 1));
	state = map_put(state, ATOM("step"), NUMBER(1));
	state = map_put(state, ATOM("next"), NUMBER(0));
	actor = ACTOR(seq_generator, state);

	state = NIL;
	state = map_put(state, ATOM("send-to"), actor);
	state = map_put(state, ATOM("label"), ATOM("x"));
	state = map_put(state, ATOM("limit"), NUMBER(X_MAX - 1));
	state = map_put(state, ATOM("step"), NUMBER(1));
	actor = ACTOR(int_generator, state);

	msg = NIL;
	msg = map_put(msg, ATOM("next"), NUMBER(0));
	SEND(actor, msg);
	DBUG_RETURN;
}

CONFIG*
init_life()
{
	static BOOL init_done = FALSE;
	int x, y;
	CONFIG* cfg;
	
	DBUG_ENTER("init_life");
	if (init_done) {
		DBUG_RETURN NULL;
	}
	DBUG_PRINT("", ("init needed"));
	cfg = new_configuration(1000);
	for (y = 0; y < Y_MAX; ++y) {
		for (x = 0; x < X_MAX; ++x) {
			CONS* state = NIL;
			state = map_put(state, ATOM("y"), NUMBER(y));
			state = map_put(state, ATOM("x"), NUMBER(x));
			state = map_put(state, ATOM("value"), NUMBER(get_grid_value(x, y)));
			cells[y][x] = CFG_ACTOR(cfg, cell_actor, state);
			DBUG_PRINT("", ("cell(%d,%d) = %s", x, y, cons_to_str(cells[y][x])));
		}
	}
	init_done = TRUE;
	DBUG_RETURN cfg;
}

void
life_tick(CONFIG* cfg)
{
	CONS* msg;
	CONS* actor;
	int n;

	DBUG_ENTER("life_tick");
	actor = CFG_ACTOR(cfg, ask_all_cells, NIL);

	msg = NIL;
	msg = map_put(msg, ATOM("request"), ATOM("gen-next"));
	CFG_SEND(cfg, actor, msg);

	n = run_configuration(cfg, 1000000);
	if (cfg->q_count > 0) {
		TRACE(printf("gen-next: queue length %d with %d budget remaining\n", cfg->q_count, n));
	}

	msg = NIL;
	msg = map_put(msg, ATOM("request"), ATOM("update-grid"));
	CFG_SEND(cfg, actor, msg);

	n = run_configuration(cfg, 1000000);
	if (cfg->q_count > 0) {
		TRACE(printf("update-grid: queue length %d with %d budget remaining\n", cfg->q_count, n));
	}

	DBUG_RETURN;
}

void
test_life(int counter)
{
	int n;
	CONFIG* cfg;

	DBUG_ENTER("test_life");
	TRACE(printf("--test_life--\n"));
	cfg = init_life();

	TRACE(printf("Initial Grid\n"));
	print_grid();
	
	for (n = 1; n < counter; ++n) {
		life_tick(cfg);
		TRACE(printf("Generation %d\n", n));
		print_grid();
	}

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
	int counter = 35;

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

	test_life(counter);	/* this test involves running the dispatch loop */

	report_cons_stats();
	DBUG_RETURN (exit(EXIT_SUCCESS), 0);
}
