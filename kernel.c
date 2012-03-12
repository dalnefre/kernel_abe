/*
 * kernel.c -- An actor-based implementation of John Shutt's "Kernel" language
 *
 * Copyright 2012 Dale Schumacher.  ALL RIGHTS RESERVED.
 */
static char	_Program[] = "Kernel";
static char	_Version[] = "2012-03-11";
static char	_Copyright[] = "Copyright 2012 Dale Schumacher";

#include <getopt.h>
#include "kernel.h"

#include "dbug.h"
DBUG_UNIT("kernel");

static BEH_PROTO;	/* ==== GLOBAL ACTOR CONFIGURATION ==== */
static FILE* input_file = NULL;
static FILE* output_file = NULL;

/* WARNING! THESE GLOBAL ACTORS MUST BE INITIALIZED IN EACH CONFIGURATION */
static CONS* a_inert;
static CONS* a_true;
static CONS* a_false;
static CONS* a_nil;
static CONS* a_ignore;
static CONS* a_kernel_env;
static CONS* a_ground_env;

static CONS* intern_map;  /* pr(value->const, name->symbol) */

typedef CONS* (*LAMBDA_x)(CONS* x);
typedef CONS* (*LAMBDA_x_y)(CONS* x, CONS* y);
typedef CONS* (*LAMBDA_x_y_z)(CONS* x, CONS* y, CONS* z);

#define ENSURE(invariant) if (!(invariant)) { \
	THROW(pr(ATOM("AT"), pr(ATOM(__FILE__), NUMBER(__LINE__)))); \
	DBUG_RETURN; \
} (void)0

#define	THROW(msg)		SEND(ACTOR(throw_beh, NIL), (msg))

/**
throw_beh = \msg.[
	# report an exception
]
**/
static
BEH_DECL(throw_beh)
{
	char* msg = cons_to_str(WHAT);

	DBUG_ENTER("throw_beh");
	DBUG_PRINT("FAIL!", ("%s", msg));
	fprintf(output_file, "FAIL! %s\n", msg);
	fflush(output_file);
	DBUG_RETURN;
}

/**
abort_beh = \msg.[
	# print message and abort
]
**/
static
BEH_DECL(abort_beh)
{
	char* msg = cons_to_str(WHAT);

	DBUG_ENTER("abort_beh");
	DBUG_PRINT("ABORT!", ("%s", msg));
	fprintf(stderr, "ABORT! %s\n", msg);
	abort();
	DBUG_RETURN;
}

static char*
printable(CONS* p)		/* WARNING! you must free this storage manually */
{
	char* s;
	char* t;
	
	s = cons_to_str(p);  /* pointer to static buffer */
	if (atomp(p)) { ++s; }  /* skip leading '#' */
	t = NEWxN(char, strlen(s) + 1);
	return strcpy(t, s);
}

typedef struct sink_t SINK;
struct sink_t {
	CONS*		context;
	CONS*		(*put)(SINK*, CONS*);  /* transmit value, return a_true on success */
	CONS*		(*put_cstr)(SINK*, char*);  /* transmit C-string, a_true on success */
};

CONS*
file_put(SINK* sink, CONS* value)
{
	FILE* f;
	int c;
	CONS* ok;

	DBUG_ENTER("file_put");
	assert(numberp(value));
	XDBUG_PRINT("stdout", ("%p", stdout));
	XDBUG_PRINT("stderr", ("%p", stderr));
	XDBUG_PRINT("output_file", ("%p", output_file));
	if (sink->context == MK_REF(output_file)) {
		f = output_file;  /* FIXME: WORKAROUND FOR FILE* ENCODING */
	} else {
		f = (FILE*)(MK_PTR(sink->context));
	}
	XDBUG_PRINT("f", ("%p", f));
	c = MK_INT(value);
	DBUG_PRINT("c", ((isprint(c) ? "%d '%c'" : "%d '\\x%X'"), c, c));
	ok = ((fputc(c, f) == EOF) ? a_false : a_true);
	XDBUG_PRINT("ok", ("%s", cons_to_str(ok)));
	DBUG_RETURN ok;
}
CONS*
generic_put_cstr(SINK* sink, char* s)
{
	int c;

	DBUG_ENTER("generic_put_cstr");
	assert(s != NULL);
	DBUG_PRINT("s", ((s ? "\"%s\"" : "NULL"), s));
	while ((c = *s++)) {
		if((sink->put)(sink, NUMBER(c)) == a_false) {
			DBUG_RETURN a_false;
		}
	}
	DBUG_RETURN a_true;
}
SINK*
file_sink(FILE* f)
{
	SINK* sink;

	DBUG_ENTER("file_sink");
	DBUG_PRINT("f", ("%p", f));
	sink = NEW(SINK);
	sink->context = MK_REF(f);
	sink->put = file_put;
	sink->put_cstr = generic_put_cstr;
	DBUG_RETURN sink;
}

static SINK* current_sink;

typedef struct source_t SOURCE;
struct source_t {
	CONS*		context;
	CONS*		(*empty)(SOURCE*);  /* return a_true if empty, else a_false */
	CONS*		(*get)(SOURCE*);  /* return value at current position */
	CONS*		(*next)(SOURCE*);  /* return current and advance position */
};

CONS*
string_empty(SOURCE* src)
{
	char* s;

	DBUG_ENTER("string_empty");
	s = (char*)(MK_PTR(src->context));
	DBUG_PRINT("s", ((s ? "\"%s\"" : "NULL"), s));
	DBUG_RETURN ((s && *s) ? a_false : a_true);
}
CONS*
string_get(SOURCE* src)
{
	char* s;
	int c;

	DBUG_ENTER("string_get");
	if ((src->empty)(src) == a_true) {
		DBUG_PRINT("c", ("EOF"));
		DBUG_RETURN NUMBER(EOF);
	}
	s = (char*)(MK_PTR(src->context));
	c = *s;
	DBUG_PRINT("c", ((isprint(c) ? "%d '%c'" : "%d '\\x%X'"), c, c));
	DBUG_RETURN NUMBER(c);
}
CONS*
string_next(SOURCE* src)
{
	char* s;
	CONS* c;

	DBUG_ENTER("string_next");
	c = (src->get)(src);
	if (c == NUMBER(EOF)) {
		DBUG_RETURN c;
	}
	s = (char*)(MK_PTR(src->context));
	src->context = MK_REF(++s);
	DBUG_PRINT("s", ((s ? "\"%s\"" : "NULL"), s));
	DBUG_RETURN c;
}
SOURCE*
string_source(char* s)
{
	SOURCE* src;

	DBUG_ENTER("string_source");
	DBUG_PRINT("s", ((s ? "\"%s\"" : "NULL"), s));
	src = NEW(SOURCE);
	src->context = MK_REF(s);
	src->empty = string_empty;
	src->get = string_get;
	src->next = string_next;
	DBUG_RETURN src;
}

CONS*
file_empty(SOURCE* src)
{
	CONS* d;

	DBUG_ENTER("file_empty");
	d = (src->get)(src);
	DBUG_RETURN ((d == NUMBER(EOF)) ? a_true : a_false);
}
CONS*
file_get(SOURCE* src)
{
	CONS* d;
	int c;

	DBUG_ENTER("file_get");
	DBUG_PRINT("context", ("%s", cons_to_str(src->context)));
	d = tl(src->context);
	if (nilp(d)) {
		(src->next)(src);
		d = tl(src->context);
	}
	c = MK_INT(d);
	DBUG_PRINT("c", ((isprint(c) ? "%d '%c'" : "%d '\\x%X'"), c, c));
	DBUG_RETURN d;
}
CONS*
file_next(SOURCE* src)
{
	CONS* d;
	FILE* f;

	DBUG_ENTER("file_next");
	d = tl(src->context);
	if (d != NUMBER(EOF)) {
		CONS* r = hd(src->context);
		
		XDBUG_PRINT("stdin", ("%p", stdin));
		XDBUG_PRINT("input_file", ("%p", input_file));
		if (r == MK_REF(input_file)) {
			f = input_file;  /* FIXME: WORKAROUND FOR FILE* ENCODING */
		} else {
			f = (FILE*)(MK_PTR(r));
		}
		XDBUG_PRINT("f", ("%p", f));
		rplacd(src->context, NUMBER(fgetc(f)));
	}
	DBUG_RETURN d;
}
SOURCE*
file_source(FILE* f)
{
	SOURCE* src;

	DBUG_ENTER("file_source");
	assert(f != NULL);
	DBUG_PRINT("f", ("%p", f));
	src = NEW(SOURCE);
	src->context = pr(MK_REF(f), NIL);
	DBUG_PRINT("f'", ("%p", MK_PTR(hd(src->context))));
	src->empty = file_empty;
	src->get = file_get;
	src->next = file_next;
	DBUG_PRINT("context", ("%s", cons_to_str(src->context)));
	DBUG_RETURN src;
}

static SOURCE* current_source;

/**
LET command_beh(msg) = \actor.[
	SEND msg TO actor
]
**/
static
BEH_DECL(command_beh)
{
	CONS* msg = MINE;
	CONS* actor = WHAT;
	
	DBUG_ENTER("command_beh");
	DBUG_PRINT("msg", ("%s", cons_to_str(msg)));
	DBUG_PRINT("actor", ("%s", cons_to_str(actor)));
	ENSURE(actorp(actor));
	SEND(actor, msg);
	DBUG_RETURN;
}

/**
LET join_rest_beh(cust, k_rest, first) = \($k_rest, rest).[
	SEND (first, rest) TO cust
]
**/
static
BEH_DECL(join_rest_beh)
{
	CONS* state = MINE;
	CONS* cust;
	CONS* k_rest;
	CONS* first;
	CONS* msg = WHAT;

	DBUG_ENTER("join_rest_beh");
	assert(is_pr(state));
	cust = hd(state);
	assert(actorp(cust));
	assert(is_pr(tl(state)));
	k_rest = hd(tl(state));
	first = tl(tl(state));

	if (is_pr(msg)
	&& (hd(msg) == k_rest)) {
		CONS* rest = tl(msg);

		DBUG_PRINT("first", ("%s", cons_to_str(first)));
		DBUG_PRINT("rest", ("%s", cons_to_str(rest)));
		SEND(cust, pr(first, rest));
	}
	DBUG_RETURN;
}
/**
LET join_first_beh(cust, k_first, rest) = \($k_first, first).[
	SEND (first, rest) TO cust
]
**/
static
BEH_DECL(join_first_beh)
{
	CONS* state = MINE;
	CONS* cust;
	CONS* k_first;
	CONS* rest;
	CONS* msg = WHAT;

	DBUG_ENTER("join_first_beh");
	assert(is_pr(state));
	cust = hd(state);
	assert(actorp(cust));
	assert(is_pr(tl(state)));
	k_first = hd(tl(state));
	rest = tl(tl(state));

	if (is_pr(msg)
	&& (hd(msg) == k_first)) {
		CONS* first = tl(msg);

		DBUG_PRINT("first", ("%s", cons_to_str(first)));
		DBUG_PRINT("rest", ("%s", cons_to_str(rest)));
		SEND(cust, pr(first, rest));
	}
	DBUG_RETURN;
}
/**
LET join_beh(cust, k_first, k_rest) = \msg.[
	CASE msg OF
	($k_first, first) : [  # join_rest_beh
		BECOME \($k_rest, rest).[ SEND (first, rest) TO cust ]
	]
	($k_rest, rest) : [  # join_first_beh
		BECOME \($k_first, first).[ SEND (first, rest) TO cust ]
	]
	END
]
**/
static
BEH_DECL(join_beh)
{
	CONS* state = MINE;
	CONS* cust;
	CONS* k_first;
	CONS* k_rest;
	CONS* msg = WHAT;

	DBUG_ENTER("join_beh");
	assert(is_pr(state));
	cust = hd(state);
	assert(actorp(cust));
	assert(is_pr(tl(state)));
	k_first = hd(tl(state));
	k_rest = tl(tl(state));

	if (is_pr(msg)) {
		if (hd(msg) == k_first) {
			BECOME(join_rest_beh, pr(cust, pr(k_rest, tl(msg))));
		} else if (hd(msg) == k_rest) {
			BECOME(join_first_beh, pr(cust, pr(k_first, tl(msg))));
		}
	}
	DBUG_RETURN;
}
/**
LET tag_beh(cust) = \msg.[ SEND (SELF, msg) TO cust ]
**/
static
BEH_DECL(tag_beh)
{
	CONS* cust = MINE;
	CONS* msg = WHAT;

	DBUG_ENTER("tag_beh");
	assert(actorp(cust));
	SEND(cust, pr(SELF, msg));
	DBUG_RETURN;
}
/**
LET fork_beh(cust, head, tail) = \(h_req, t_req).[
	CREATE k_head WITH tag_beh(SELF)
	CREATE k_tail WITH tag_beh(SELF)
	SEND (k_head, h_req) TO head
	SEND (k_tail, t_req) TO tail
	BECOME join_beh(cust, k_head, k_tail)
]
**/
static
BEH_DECL(fork_beh)
{
	CONS* state = MINE;
	CONS* cust;
	CONS* head;
	CONS* tail;
	CONS* msg = WHAT;
	CONS* h_req;
	CONS* t_req;
	CONS* k_head;
	CONS* k_tail;

	DBUG_ENTER("fork_beh");
	assert(is_pr(state));
	cust = hd(state);
	assert(actorp(cust));
	assert(is_pr(tl(state)));
	head = hd(tl(state));
	assert(actorp(head));
	tail = tl(tl(state));
	assert(actorp(tail));
	assert(is_pr(msg));
	h_req = hd(msg);
	t_req = tl(msg);

	k_head = ACTOR(tag_beh, SELF);
	k_tail = ACTOR(tag_beh, SELF);
	SEND(head, pr(k_head, h_req));
	SEND(tail, pr(k_tail, t_req));
	BECOME(join_beh, pr(cust, pr(k_head, k_tail)));
	DBUG_RETURN;
}

static BEH_DECL(cons_type);  /* forward */
static BEH_DECL(pair_type);  /* forward */

/**
LET dotted_close_beh(cust) = \ok.[
	CASE ok OF
	$True : [ SEND (cust, ")") TO current_sink ]
	_ : [ SEND ok TO cust ]
	END
]
**/
static
BEH_DECL(dotted_close_beh)
{
	CONS* cust = MINE;
	CONS* ok = WHAT;

	DBUG_ENTER("dotted_close_beh");
	ENSURE(actorp(cust));
	
	if (ok == a_true) {
		SINK* sink = current_sink;
		SEND(cust, (sink->put)(sink, NUMBER(')')));
	} else {
		SEND(cust, ok);
	}
	DBUG_RETURN;
}
/**
LET dotted_tail_beh(cust, last) = \ok.[
	CASE ok OF
	$True : [
		SEND (k_close, #write) TO last
		CREATE k_close WITH dotted_close_beh(cust)
	]
	_ : [ SEND ok TO cust ]
	END
]
**/
static
BEH_DECL(dotted_tail_beh)
{
	CONS* state = MINE;
	CONS* cust;
	CONS* last;
	CONS* ok = WHAT;

	DBUG_ENTER("dotted_tail_beh");
	ENSURE(is_pr(state));
	cust = hd(state);
	ENSURE(actorp(cust));
	last = tl(state);
	
	if (ok == a_true) {
		CONS* k_close = ACTOR(dotted_close_beh, cust);
		SEND(last, pr(k_close, ATOM("write")));
	} else {
		SEND(cust, ok);
	}
	DBUG_RETURN;
}
/**
LET object_type = \(cust, req).[	# base object type
	CASE req OF
	(#eval, _) : [ SEND SELF TO cust ]
#	(#eq, $SELF) : [ SEND True TO cust ]
#	(#eq, _) : [ SEND False TO cust ]
	#copy_immutable : [ SEND SELF TO cust ]
	(#write_tail, " ") : [
		SEND (k_tail, " . ") TO current_sink
		CREATE k_tail WITH dotted_tail_beh(cust, SELF)
	]
	_ : THROW (#Not-Understood, SELF, req)
	END
]
**/
static
BEH_DECL(object_type)
{
	CONS* msg = WHAT;
	CONS* cust;
	CONS* req;

	DBUG_ENTER("object_type");
	ENSURE(is_pr(msg));
	cust = hd(msg);
	ENSURE(actorp(cust));
	req = tl(msg);

	if (is_pr(req)
	&& (hd(req) == ATOM("eval"))) {
		SEND(cust, SELF);
/*
	} else if (is_pr(req)
	&& (hd(req) == ATOM("eq"))) {
		SEND(cust, ((tl(req) == SELF) ? a_true : a_false));
*/
	} else if (req == ATOM("copy_immutable")) {
		SEND(cust, SELF);
	} else if (is_pr(req)
	&& (hd(req) == ATOM("write_tail"))
	&& (tl(req) == NUMBER(' '))) {
		SINK* sink = current_sink;
		CONS* k_tail = ACTOR(dotted_tail_beh, pr(cust, SELF));
		SEND(k_tail, (sink->put_cstr)(sink, " . "));
	} else {
		THROW(pr(ATOM("Not-Understood"), pr(SELF, req)));
	}
	DBUG_RETURN;
}

/**
LET unit_type = \(cust, req).[
	CASE req OF
	(#type_eq, $unit_type) : [ SEND True TO cust ]
	(#type_eq, _) : [ SEND False TO cust ]
	#write : [ SEND (cust, "#inert") TO current_sink ]
	_ : object_type(cust, req)
	END
]
**/
static
BEH_DECL(unit_type)
{
	CONS* msg = WHAT;
	CONS* cust;
	CONS* req;

	DBUG_ENTER("unit_type");
	ENSURE(is_pr(msg));
	cust = hd(msg);
	ENSURE(actorp(cust));
	req = tl(msg);

	DBUG_PRINT("cust", ("%s", cons_to_str(cust)));
	DBUG_PRINT("req", ("%s", cons_to_str(req)));
	if (is_pr(req)
	&& (hd(req) == ATOM("type_eq"))) {
		SEND(cust, ((tl(req) == MK_REF(unit_type)) ? a_true : a_false));
	} else if (req == ATOM("write")) {
		SINK* sink = current_sink;
		SEND(cust, (sink->put_cstr)(sink, "#inert"));
	} else {
		object_type(CFG);  /* DELEGATE BEHAVIOR */
	}
	DBUG_RETURN;
}

/**
LET oper_type = \(cust, req).[
	CASE req OF
	(#type_eq, $oper_type) : [ SEND True TO cust ]
	(#type_eq, _) : [ SEND False TO cust ]
	#write : [ SEND (cust, "#operative") TO current_sink ]
	_ : object_type(cust, req)
	END
]
**/
static
BEH_DECL(oper_type)
{
	CONS* msg = WHAT;
	CONS* cust;
	CONS* req;

	DBUG_ENTER("oper_type");
	ENSURE(is_pr(msg));
	cust = hd(msg);
	ENSURE(actorp(cust));
	req = tl(msg);

	DBUG_PRINT("cust", ("%s", cons_to_str(cust)));
	DBUG_PRINT("req", ("%s", cons_to_str(req)));
	if (is_pr(req)
	&& (hd(req) == ATOM("type_eq"))) {
		SEND(cust, ((tl(req) == MK_REF(oper_type)) ? a_true : a_false));
	} else if (req == ATOM("write")) {
		SINK* sink = current_sink;
		SEND(cust, (sink->put_cstr)(sink, "#operative"));
	} else {
		object_type(CFG);  /* DELEGATE BEHAVIOR */
	}
	DBUG_RETURN;
}
/**
LET args_oper(args_beh) = \(cust, req).[
	CASE req OF
	(#comb, opnds, env) : [
		CREATE k_args WITH args_beh(cust, env)
		SEND (k_args, #as_tuple) TO opnds
	]
	_ : oper_type(cust, req)
	END
]
**/
static
BEH_DECL(args_oper)
{
	CONS* args_beh = MINE;
	CONS* msg = WHAT;
	CONS* cust;
	CONS* req;

	DBUG_ENTER("args_oper");
	ENSURE(funcp(args_beh));
	ENSURE(is_pr(msg));
	cust = hd(msg);
	ENSURE(actorp(cust));
	req = tl(msg);

	DBUG_PRINT("cust", ("%s", cons_to_str(cust)));
	DBUG_PRINT("req", ("%s", cons_to_str(req)));
	if (is_pr(req) && is_pr(tl(req))
	&& (hd(req) == ATOM("comb"))) {
		CONS* opnds = hd(tl(req));
		CONS* env = tl(tl(req));
		CONS* k_args;

		k_args = ACTOR((MK_BEH(args_beh)), pr(cust, env));
		SEND(opnds, pr(k_args, ATOM("as_tuple")));
	} else {
		oper_type(CFG);  /* DELEGATE BEHAVIOR */
	}
	DBUG_RETURN;
}

/**
LET appl_args_beh(cust, comb, env) = \args.[
	CREATE expr WITH Pair(comb, args)
	SEND (cust, #eval, env) TO expr
]
**/
static
BEH_DECL(appl_args_beh)
{
	CONS* state = MINE;
	CONS* cust;
	CONS* comb;
	CONS* env;
	CONS* args = WHAT;
	CONS* expr;

	DBUG_ENTER("appl_args_beh");
	ENSURE(is_pr(state));
	cust = hd(state);
	ENSURE(actorp(cust));
	comb = hd(tl(state));
	env = tl(tl(state));
	expr = ACTOR(pair_type, pr(comb, args));
	SEND(expr, pr(cust, pr(ATOM("eval"), env)));
	DBUG_RETURN;
}
/**
LET appl_type(comb) = \(cust, req).[
	CASE req OF
	(#type_eq, $appl_type) : [ SEND True TO cust ]
	(#type_eq, _) : [ SEND False TO cust ]
	(#comb, opnds, env) : [
		SEND (k_args, #map, #eval, env) TO opnds
		CREATE k_args WITH \args.[  # appl_args_beh
			CREATE expr WITH Pair(comb, args)
			SEND (cust, #eval, env) TO expr
		]
	]
	#unwrap : [ SEND comb TO cust ]
	#write : [ SEND (cust, "#applicative") TO current_sink ]
	_ : object_type(cust, req)
	END
]
**/
static
BEH_DECL(appl_type)
{
	CONS* comb = MINE;
	CONS* msg = WHAT;
	CONS* cust;
	CONS* req;

	DBUG_ENTER("appl_type");
	ENSURE(actorp(comb));
	ENSURE(is_pr(msg));
	cust = hd(msg);
	ENSURE(actorp(cust));
	req = tl(msg);

	DBUG_PRINT("cust", ("%s", cons_to_str(cust)));
	DBUG_PRINT("req", ("%s", cons_to_str(req)));
	if (is_pr(req)
	&& (hd(req) == ATOM("type_eq"))) {
		SEND(cust, ((tl(req) == MK_REF(appl_type)) ? a_true : a_false));
	} else if (is_pr(req) && is_pr(tl(req))
	&& (hd(req) == ATOM("comb"))) {
		CONS* opnds = hd(tl(req));
		CONS* env = tl(tl(req));
		CONS* k_args = ACTOR(appl_args_beh, pr(cust, pr(comb, env)));

		SEND(opnds, pr(k_args, pr(ATOM("map"), pr(ATOM("eval"), env))));
	} else if (req == ATOM("unwrap")) {
		SEND(cust, comb);
	} else if (req == ATOM("write")) {
		SINK* sink = current_sink;
		SEND(cust, (sink->put_cstr)(sink, "#applicative"));
	} else {
		object_type(CFG);  /* DELEGATE BEHAVIOR */
	}
	DBUG_RETURN;
}
/* FIXME: (eq? (wrap op) (wrap op)) ==> #t */

/**
LET const_type(value) = \(cust, req).[
	CASE req OF
	(#type_eq, $const_type) : [ SEND True TO cust ]
	(#type_eq, _) : [ SEND False TO cust ]
	#value : [ SEND value TO cust ]
	#write : [ SEND (cust, printable(value)) TO current_sink ]
	_ : object_type(cust, req)
	END
]
**/
static
BEH_DECL(const_type)  /* FIXME: extend to implement encapsulation types */
{
	CONS* value = MINE;
	CONS* msg = WHAT;
	CONS* cust;
	CONS* req;

	DBUG_ENTER("const_type");
	ENSURE(is_pr(msg));
	cust = hd(msg);
	ENSURE(actorp(cust));
	req = tl(msg);

	DBUG_PRINT("value", ("%s", cons_to_str(value)));
	DBUG_PRINT("cust", ("%s", cons_to_str(cust)));
	DBUG_PRINT("req", ("%s", cons_to_str(req)));
	if (is_pr(req)
	&& (hd(req) == ATOM("type_eq"))) {
		SEND(cust, ((tl(req) == MK_REF(const_type)) ? a_true : a_false));
	} else if (req == ATOM("value")) {
		SEND(cust, value);
	} else if (req == ATOM("write")) {
		SINK* sink = current_sink;
		char* s = printable(value);
		SEND(cust, (sink->put_cstr)(sink, s));
		FREE(s);
	} else {
		object_type(CFG);  /* DELEGATE BEHAVIOR */
	}
	DBUG_RETURN;
}

static CONS*
get_const(CONS* value)  /* USE FACTORY TO INTERN INSTANCES */
{
	CONS* constant;
	CONS* const_map = car(intern_map);

	DBUG_ENTER("get_const");
	DBUG_PRINT("value", ("%s", cons_to_str(value)));
	constant = map_get_def(const_map, value, NIL);
	if (nilp(constant)) {
		constant = ACTOR(const_type, value);
		const_map = map_put(const_map, value, constant);
		rplaca(intern_map, const_map);
	}
	DBUG_PRINT("const", ("%s", cons_to_str(constant)));
	DBUG_RETURN constant;
}

/**
LET bool_type(value) = \(cust, req).[
	CASE req OF
	(#type_eq, $bool_type) : [ SEND True TO cust ]
	(#type_eq, _) : [ SEND False TO cust ]
	(#if, cnsq, altn, env) : [
		CASE value OF
		TRUE : [ SEND (cust, #eval, env) TO cnsq ]
		FALSE : [ SEND (cust, #eval, env) TO altn ]
		END
	]
	#write : [
		CASE value OF
		TRUE : [ SEND (cust, "#t") TO current_sink ]
		FALSE : [ SEND (cust, "#f") TO current_sink ]
		END
	]
	_ : object_type(cust, req)
	END
]
**/
static
BEH_DECL(bool_type)
{
	CONS* value = MINE;
	CONS* msg = WHAT;
	CONS* cust;
	CONS* req;

	DBUG_ENTER("bool_type");
	ENSURE(is_pr(msg));
	cust = hd(msg);
	ENSURE(actorp(cust));
	req = tl(msg);

	DBUG_PRINT("value", ("%s", cons_to_str(value)));
	DBUG_PRINT("cust", ("%s", cons_to_str(cust)));
	DBUG_PRINT("req", ("%s", cons_to_str(req)));
	if (is_pr(req)
	&& (hd(req) == ATOM("type_eq"))) {
		SEND(cust, ((tl(req) == MK_REF(bool_type)) ? a_true : a_false));
	} else if (is_pr(req) && is_pr(tl(req)) && is_pr(tl(tl(req)))
	&& (hd(req) == ATOM("if"))) {
		CONS* cnsq = hd(tl(req));
		CONS* altn = hd(tl(tl(req)));
		CONS* env = tl(tl(tl(req)));

		SEND((value ? cnsq : altn), pr(cust, pr(ATOM("eval"), env)));
	} else if (req == ATOM("write")) {
		SINK* sink = current_sink;
		SEND(cust, (sink->put_cstr)(sink, (value ? "#t" : "#f")));
	} else {
		object_type(CFG);  /* DELEGATE BEHAVIOR */
	}
	DBUG_RETURN;
}

/**
LET boolean_and = \(p, q).(IF $p = $True (q) ELSE (False))
**/
static CONS*
boolean_and(CONS* p, CONS* q)
{
	return ((p == a_true) ? q : a_false);
}
/**
LET type_pred_oper(type) = \(cust, req).[
	CASE req OF
	(#comb, opnds, env) : [
		SEND (cust, #foldl, True, boolean_and, #type_eq, type) TO opnds
	]
	_ : oper_type(cust, req)
	END
]
**/
static
BEH_DECL(type_pred_oper)
{
	CONS* type = MINE;
	CONS* msg = WHAT;
	CONS* cust;
	CONS* req;

	DBUG_ENTER("type_pred_oper");
	ENSURE(funcp(type));
	ENSURE(is_pr(msg));
	cust = hd(msg);
	ENSURE(actorp(cust));
	req = tl(msg);

	DBUG_PRINT("cust", ("%s", cons_to_str(cust)));
	DBUG_PRINT("req", ("%s", cons_to_str(req)));
	if (is_pr(req) && is_pr(tl(req))
	&& (hd(req) == ATOM("comb"))) {
		CONS* opnds = hd(tl(req));
		/* CONS* env = tl(tl(req)); */

		SEND(opnds, pr(cust, pr(ATOM("foldl"),
			pr(a_true, pr(MK_FUNC(boolean_and),
			pr(ATOM("type_eq"), type))))));
	} else {
		oper_type(CFG);  /* DELEGATE BEHAVIOR */
	}
	DBUG_RETURN;
}

/**
LET null_type = \(cust, req).[
	CASE req OF
	(#type_eq, $null_type) : [ SEND True TO cust ]
	(#type_eq, _) : [ SEND False TO cust ]
	(#eval, _) : [ SEND SELF TO cust ]
	#as_pair : [ SEND NIL TO cust ]
	#as_tuple : [ SEND NIL TO cust ]
	(#match, $Nil, env) : [ SEND Inert TO cust ]
	#copy_immutable : [ SEND SELF TO cust ]
	(#map, req') : [ SEND (cust, req') TO SELF ]
	(#foldl, zero, oplus, req') : [ SEND zero TO cust ]
	#write : [ SEND (cust, "()") TO current_sink ]
	(#write_tail, " ") : [ SEND (cust, ")") TO current_sink ]
	_ : THROW (#Not-Understood, SELF, req)
	END
]
**/
static
BEH_DECL(null_type)
{
	CONS* msg = WHAT;
	CONS* cust;
	CONS* req;

	DBUG_ENTER("null_type");
	ENSURE(is_pr(msg));
	cust = hd(msg);
	ENSURE(actorp(cust));
	req = tl(msg);

	DBUG_PRINT("cust", ("%s", cons_to_str(cust)));
	DBUG_PRINT("req", ("%s", cons_to_str(req)));
	if (is_pr(req)
	&& (hd(req) == ATOM("type_eq"))) {
		SEND(cust, ((tl(req) == MK_REF(null_type)) ? a_true : a_false));
	} else if (is_pr(req)
	&& (hd(req) == ATOM("eval"))) {
		SEND(cust, SELF);
	} else if (req == ATOM("as_pair")) {
		SEND(cust, NIL);
	} else if (req == ATOM("as_tuple")) {
		SEND(cust, NIL);
	} else if (is_pr(req) && is_pr(tl(req))
	&& (hd(req) == ATOM("match"))
	&& (hd(tl(req)) == a_nil)) {
		SEND(cust, a_inert);
	} else if (req == ATOM("copy_immutable")) {
		SEND(cust, SELF);
	} else if (is_pr(req)
	&& (hd(req) == ATOM("map"))) {
		CONS* req_ = tl(req);

		SEND(SELF, pr(cust, req_));
	} else if (is_pr(req) && is_pr(tl(req))
	&& (hd(req) == ATOM("foldl"))) {
		CONS* zero = hd(tl(req));

		SEND(cust, zero);
	} else if (req == ATOM("write")) {
		SINK* sink = current_sink;
		SEND(cust, (sink->put_cstr)(sink, "()"));
	} else if (is_pr(req)
	&& (hd(req) == ATOM("write_tail"))
	&& (tl(req) == NUMBER(' '))) {
		SINK* sink = current_sink;
		SEND(cust, (sink->put)(sink, NUMBER(')')));
	} else {
		THROW(pr(ATOM("Not-Understood"), pr(SELF, req)));
	}
	DBUG_RETURN;
}

/**
LET pair_comb_beh(cust, right, env) = \comb.[
	SEND (cust, #comb, right, env) TO comb
]
**/
static
BEH_DECL(pair_comb_beh)
{
	CONS* state = MINE;
	CONS* cust;
/*
	CONS* right;
	CONS* env;
*/
	CONS* comb = WHAT;

	DBUG_ENTER("pair_comb_beh");
	ENSURE(is_pr(state));
	cust = hd(state);
/*
	ENSURE(is_pr(tl(state)));
	right = hd(tl(state));
	env = tl(tl(state));
	SEND(cust, pr(cust, pr(ATOM("comb"), pr(right, env))));
*/
	SEND(comb, pr(cust, pr(ATOM("comb"), tl(state))));
	DBUG_RETURN;
}
/**
LET pair_tuple_beh(cust, left) = \tuple.[
	SEND (left, tuple) TO cust
]
**/
static
BEH_DECL(pair_tuple_beh)
{
	CONS* state = MINE;
	CONS* cust;
	CONS* left;
	CONS* tuple = WHAT;

	DBUG_ENTER("pair_tuple_beh");
	ENSURE(is_pr(state));
	cust = hd(state);
	ENSURE(actorp(cust));
	left = tl(state);
	DBUG_PRINT("cust", ("%s", cons_to_str(cust)));
	DBUG_PRINT("left", ("%s", cons_to_str(left)));
	DBUG_PRINT("tuple", ("%s", cons_to_str(tuple)));
	SEND(cust, pr(left, tuple));
	DBUG_RETURN;
}
/**
LET pair_match_beh(cust) = \($Inert, $Inert).[
	SEND Inert TO cust
]
**/
static
BEH_DECL(pair_match_beh)
{
	CONS* cust = MINE;
	CONS* msg = WHAT;

	DBUG_ENTER("pair_match_beh");
	ENSURE(actorp(cust));
	if (is_pr(msg)
	&& (hd(msg) == a_inert)
	&& (tl(msg) == a_inert)) {
		SEND(cust, a_inert);
	}
	DBUG_RETURN;
}
/**
LET pair_copy_beh(cust) = \(head, tail).[
	CREATE pair WITH pair_type(head, tail)
	SEND pair TO cust
]
**/
static
BEH_DECL(pair_copy_beh)
{
	CONS* cust = MINE;
	CONS* head_tail = WHAT;

	DBUG_ENTER("pair_copy_beh");
	ENSURE(actorp(cust));
	if (is_pr(head_tail)) {
		SEND(cust, ACTOR(pair_type, head_tail));  /* immutable */
	}
	DBUG_RETURN;
}
/**
LET pair_map_beh(cust) = \(head, tail).[
	CREATE pair WITH cons_type(head, tail)
	SEND pair TO cust
]
**/
static
BEH_DECL(pair_map_beh)
{
	CONS* cust = MINE;
	CONS* head_tail = WHAT;

	DBUG_ENTER("pair_map_beh");
	ENSURE(actorp(cust));
	if (is_pr(head_tail)) {
		SEND(cust, ACTOR(cons_type, head_tail));  /* mutable */
	}
	DBUG_RETURN;
}
/**
LET pair_foldl_beh(cust, right, zero, oplus, req') = \one.[
	SEND (cust, #foldl, oplus(zero, one), oplus, req')
	TO right
]
**/
static
BEH_DECL(pair_foldl_beh)
{
	CONS* state = MINE;
	CONS* cust;
	CONS* right;
	CONS* zero;
	CONS* oplus;
	CONS* req_;
	CONS* one = WHAT;
	CONS* value;

	DBUG_ENTER("pair_foldl_beh");
	ENSURE(is_pr(state));
	cust = hd(state);
	ENSURE(actorp(cust));
	ENSURE(is_pr(tl(state)));
	right = hd(tl(state));
	ENSURE(is_pr(tl(tl(state))));
	zero = hd(tl(tl(state)));
	ENSURE(is_pr(tl(tl(tl(state)))));
	oplus = hd(tl(tl(tl(state))));
	ENSURE(funcp(oplus));
	req_ = tl(tl(tl(tl(state))));

	value = ((LAMBDA_x_y)MK_BEH(oplus))(zero, one);
	SEND(right, pr(cust, pr(ATOM("foldl"), pr(value, pr(oplus, req_)))));
	DBUG_RETURN;
}
/**
LET pair_write_tail_beh(cust, right) = \ok.[
	CASE ok OF
	$True : [ SEND (cust, #write_tail, " ") TO right ]
	_ : [ SEND ok TO cust ]  # propagate failure
	END
]
**/
static
BEH_DECL(pair_write_tail_beh)
{
	CONS* state = MINE;
	CONS* cust;
	CONS* right;
	CONS* ok = WHAT;

	DBUG_ENTER("pair_write_tail_beh");
	ENSURE(is_pr(state));
	cust = hd(state);
	ENSURE(actorp(cust));
	right = tl(state);
	ENSURE(actorp(right));

	if (ok == a_true) {
		SEND(right, pr(cust, pr(ATOM("write_tail"), NUMBER(' '))));
	} else {
		SEND(cust, ok);  /* failure */
	}
	DBUG_RETURN;
}
/**
LET cons_type(left, right) = \(cust, req).[		# mutable cons cell
	CASE req OF
	(#type_eq, $pair_type) : [ SEND True TO cust ]
	(#type_eq, _) : [ SEND False TO cust ]
	(#eval, env) : [
		SEND (k_comb, #eval, env) TO left
		CREATE k_comb WITH \comb.[  # pair_comb_beh
			SEND (cust, #comb, right, env) TO comb
		]
	]
	#as_pair : [ SEND (left, right) TO cust ]
	#as_tuple : [
		SEND (k_tuple, #as_tuple) TO right
		CREATE k_tuple WITH \tuple.[  # pair_tuple_beh
			SEND (left, tuple) TO cust
		]
	]
	(#match, value, env) : [
		CREATE fork WITH fork_beh(k_pair, value, value)
		SEND (
			(#left_match, left, env),
			(#right_match, right, env)
		) TO fork
		CREATE k_pair WITH \($Inert, $Inert).[  # pair_match_beh
			SEND Inert TO cust
		]
	]
	(#left_match, ptree, env) : [
		SEND (cust, #match, left, env) TO ptree
	]
	(#right_match, ptree, env) : [
		SEND (cust, #match, right, env) TO ptree
	]
	#copy_immutable : [
		CREATE fork WITH fork_beh(k_pair, left, right)
		SEND (req, req) TO fork
		CREATE k_pair WITH \(head, tail).[  # pair_copy_beh
			CREATE pair WITH pair_type(head, tail)
			SEND pair TO cust
		]
	]
	(#map, req') : [
		CREATE fork WITH fork_beh(k_pair, left, right)
		SEND (req', req) TO fork
		CREATE k_pair WITH \(head, tail).[  # pair_map_beh
			CREATE pair WITH cons_type(head, tail)
			SEND pair TO cust
		]
	]
	(#foldl, zero, oplus, req') : [
		SEND (k_one, req') TO left
		CREATE k_one WITH \one.[  # pair_foldl_beh
			SEND (cust, #foldl,
				oplus(zero, one), oplus, req')
			TO right
		]
	]
	(#set_car, a) : [
		BECOME cons_type(a, right)
		SEND Inert TO cust
	]
	(#set_cdr, d) : [
		BECOME cons_type(left, d)
		SEND Inert TO cust
	]
	#write : [
		SEND (cust, #write_tail, "(") TO SELF
	]
	(#write_tail, prefix) : [
		SEND (_, prefix) TO current_sink
		SEND (k_write, #write) TO left
		CREATE k_write WITH \ok.[
			CASE ok OF
			TRUE : [ SEND (cust, #write_tail, " ") TO right ]
			_ : [ SEND ok TO cust ]  # propagate failure
			END
		]
	]
	_ : THROW (#Not-Understood, SELF, req)
	END
]
**/
static
BEH_DECL(cons_type)
{
	CONS* state = MINE;
	CONS* left;
	CONS* right;
	CONS* msg = WHAT;
	CONS* cust;
	CONS* req;

	DBUG_ENTER("cons_type");
	ENSURE(is_pr(state));
	left = hd(state);
	right = tl(state);
	ENSURE(is_pr(msg));
	cust = hd(msg);
	ENSURE(actorp(cust));
	req = tl(msg);

	DBUG_PRINT("left", ("%s", cons_to_str(left)));
	DBUG_PRINT("right", ("%s", cons_to_str(right)));
	DBUG_PRINT("cust", ("%s", cons_to_str(cust)));
	DBUG_PRINT("req", ("%s", cons_to_str(req)));
	if (is_pr(req)
	&& (hd(req) == ATOM("type_eq"))) {
		SEND(cust, ((tl(req) == MK_REF(pair_type)) ? a_true : a_false));
	} else if (is_pr(req)
	&& (hd(req) == ATOM("eval"))) {
		CONS* env = tl(req);
		CONS* k_comb = ACTOR(pair_comb_beh, pr(cust, pr(right, env)));

		SEND(left, pr(k_comb, pr(ATOM("eval"), env)));
	} else if (req == ATOM("as_pair")) {
		SEND(cust, state);
	} else if (req == ATOM("as_tuple")) {
		CONS* k_tuple = ACTOR(pair_tuple_beh, pr(cust, left));

		SEND(right, pr(k_tuple, ATOM("as_tuple")));
	} else if (is_pr(req) && is_pr(tl(req))
	&& (hd(req) == ATOM("match"))) {
		CONS* value = hd(tl(req));
		CONS* env = tl(tl(req));
		CONS* k_pair = ACTOR(pair_match_beh, cust);
		CONS* fork = ACTOR(fork_beh, pr(k_pair, pr(value, value)));

		SEND(fork, pr(
			pr(ATOM("left_match"), pr(left, env)),
			pr(ATOM("right_match"), pr(right, env))));
	} else if (is_pr(req) && is_pr(tl(req))
	&& (hd(req) == ATOM("left_match"))) {
		CONS* ptree = hd(tl(req));
		CONS* env = tl(tl(req));

		SEND(ptree, pr(cust, pr(ATOM("match"), pr(left, env))));
	} else if (is_pr(req) && is_pr(tl(req))
	&& (hd(req) == ATOM("right_match"))) {
		CONS* ptree = hd(tl(req));
		CONS* env = tl(tl(req));

		SEND(ptree, pr(cust, pr(ATOM("match"), pr(right, env))));
	} else if (req == ATOM("copy_immutable")) {
		CONS* k_pair = ACTOR(pair_copy_beh, cust);
		CONS* fork = ACTOR(fork_beh, pr(k_pair, pr(left, right)));

		SEND(fork, pr(req, req));
	} else if (is_pr(req)
	&& (hd(req) == ATOM("map"))) {
		CONS* req_ = tl(req);
		CONS* k_pair = ACTOR(pair_map_beh, cust);
		CONS* fork = ACTOR(fork_beh, pr(k_pair, pr(left, right)));

		SEND(fork, pr(req_, req));
	} else if (is_pr(req) && is_pr(tl(req)) && is_pr(tl(tl(req)))
	&& (hd(req) == ATOM("foldl"))) {
		CONS* req_ = tl(tl(tl(req)));
		CONS* k_one = ACTOR(pair_foldl_beh, pr(cust, pr(right, tl(req))));

		SEND(left, pr(k_one, req_));
	} else if (is_pr(req)
	&& (hd(req) == ATOM("set_car"))) {
		BECOME(THIS, pr(tl(req), right));
		SEND(cust, a_inert);
	} else if (is_pr(req)
	&& (hd(req) == ATOM("set_cdr"))) {
		BECOME(THIS, pr(left, tl(req)));
		SEND(cust, a_inert);
	} else if (req == ATOM("write")) {
		SEND(SELF, pr(cust, pr(ATOM("write_tail"), NUMBER('('))));
	} else if (is_pr(req)
	&& (hd(req) == ATOM("write_tail"))) {
		CONS* prefix = tl(req);
		CONS* k_write;
		SINK* sink = current_sink;

		if ((sink->put)(sink, prefix) == a_true) {
			k_write = ACTOR(pair_write_tail_beh, pr(cust, right));
			SEND(left, pr(k_write, ATOM("write")));
		} else {
			SEND(cust, a_false);
		}
	} else {
		THROW(pr(ATOM("Not-Understood"), pr(SELF, req)));
	}
	DBUG_RETURN;
}
/**
LET pair_type(left, right) = \(cust, req).[		# immutable cons cell
	CASE req OF
	(#set_car, a) : [ THROW (#Immutable, SELF) ]
	(#set_cdr, d) : [ THROW (#Immutable, SELF) ]
	#copy_immutable : [ SEND SELF TO cust ]
	_ : (cons_type(left, right))(cust, req)
	END
]
**/
static
BEH_DECL(pair_type)
{
	CONS* msg = WHAT;
	CONS* cust;
	CONS* req;

	DBUG_ENTER("pair_type");
	ENSURE(is_pr(msg));
	cust = hd(msg);
	ENSURE(actorp(cust));
	req = tl(msg);

	DBUG_PRINT("cust", ("%s", cons_to_str(cust)));
	DBUG_PRINT("req", ("%s", cons_to_str(req)));
	if (is_pr(req)
	&& (hd(req) == ATOM("set_car"))) {
		THROW(pr(ATOM("Immutable"), SELF));
	} else if (is_pr(req)
	&& (hd(req) == ATOM("set_cdr"))) {
		THROW(pr(ATOM("Immutable"), SELF));
	} else if (req == ATOM("copy_immutable")) {
		SEND(cust, SELF);
	} else {
		cons_type(CFG);  /* DELEGATE BEHAVIOR */
	}
	DBUG_RETURN;
}

/**
LET symbol_type(name) = \(cust, req).[
	CASE req OF
	(#type_eq, $symbol_type) : [ SEND True TO cust ]
	(#type_eq, _) : [ SEND False TO cust ]
	(#eval, env) : [ SEND (cust, #lookup, name) TO env ]
	(#match, value, env) : [ SEND (cust, #bind, name, value) TO env ]
	#write : [ SEND (cust, printable(name)) TO current_sink ]
	_ : object_type(cust, req)
	END
]
**/
static
BEH_DECL(symbol_type)
{
	CONS* name = MINE;
	CONS* msg = WHAT;
	CONS* cust;
	CONS* req;

	DBUG_ENTER("symbol_type");
	ENSURE(is_pr(msg));
	cust = hd(msg);
	ENSURE(actorp(cust));
	req = tl(msg);

	DBUG_PRINT("name", ("%s", cons_to_str(name)));
	DBUG_PRINT("cust", ("%s", cons_to_str(cust)));
	DBUG_PRINT("req", ("%s", cons_to_str(req)));
	if (is_pr(req)
	&& (hd(req) == ATOM("type_eq"))) {
		SEND(cust, ((tl(req) == MK_REF(symbol_type)) ? a_true : a_false));
	} else if (is_pr(req)
	&& (hd(req) == ATOM("eval"))) {
		CONS* env = tl(req);

		SEND(env, pr(cust, pr(ATOM("lookup"), name)));
	} else if (is_pr(req) && is_pr(tl(req))
	&& (hd(req) == ATOM("match"))) {
		CONS* value = hd(tl(req));
		CONS* env = tl(tl(req));

		SEND(env, pr(cust, pr(ATOM("bind"), pr(name, value))));
	} else if (req == ATOM("write")) {
		SINK* sink = current_sink;
		char* s = printable(name);
		SEND(cust, (sink->put_cstr)(sink, s));
		FREE(s);
	} else {
		object_type(CFG);  /* DELEGATE BEHAVIOR */
	}
	DBUG_RETURN;
}

static CONS*
get_symbol(CONS* name)  /* USE FACTORY TO INTERN INSTANCES */
{
	CONS* symbol;
	CONS* symbol_map = cdr(intern_map);

	DBUG_ENTER("get_symbol");
	DBUG_PRINT("name", ("%s", cons_to_str(name)));
	symbol = map_get_def(symbol_map, name, NIL);
	if (nilp(symbol)) {
		symbol = ACTOR(symbol_type, name);
		symbol_map = map_put(symbol_map, name, symbol);
		rplacd(intern_map, symbol_map);
	}
	DBUG_PRINT("symbol", ("%s", cons_to_str(symbol)));
	DBUG_RETURN symbol;
}

/**
LET any_type = \(cust, req).[
	CASE req OF
	(#type_eq, $any_type) : [ SEND True TO cust ]
	(#type_eq, _) : [ SEND False TO cust ]
	(#match, _) : [ SEND Inert TO cust ]
	#write : [ SEND (cust, "#ignore") TO current_sink ]
	_ : object_type(cust, req)
	END
]
**/
static
BEH_DECL(any_type)
{
	CONS* msg = WHAT;
	CONS* cust;
	CONS* req;

	DBUG_ENTER("any_type");
	ENSURE(is_pr(msg));
	cust = hd(msg);
	ENSURE(actorp(cust));
	req = tl(msg);

	DBUG_PRINT("cust", ("%s", cons_to_str(cust)));
	DBUG_PRINT("req", ("%s", cons_to_str(req)));
	if (is_pr(req)
	&& (hd(req) == ATOM("type_eq"))) {
		SEND(cust, ((tl(req) == MK_REF(any_type)) ? a_true : a_false));
	} else if (is_pr(req)
	&& (hd(req) == ATOM("match"))) {
		SEND(cust, a_inert);
	} else if (req == ATOM("write")) {
		SINK* sink = current_sink;
		SEND(cust, (sink->put_cstr)(sink, "#ignore"));
	} else {
		object_type(CFG);  /* DELEGATE BEHAVIOR */
	}
	DBUG_RETURN;
}

/**
LET env_type(parent, map) = \(cust, req).[
	CASE req OF
	(#type_eq, $unit_type) : [ SEND True TO cust ]
	(#type_eq, _) : [ SEND False TO cust ]
	(#lookup, key) : [
		CASE map_find(map, key) OF
		NIL : [
			CASE parent OF
			NIL : [ THROW (#Undefined, key) ]
			_ : [ SEND (cust, req) TO parent ]
			END
		]
		(_, value) : [ SEND value TO cust ]
		END
	]
	(#bind, key, value) : [
		CASE map_find(map, key) OF
		NIL : [ BECOME env_type(parent, map_put(map, key, value)) ]
		binding : rplacd(binding, value)
		END
		SEND Inert TO cust  # new binding
	]
	#write : [ SEND (cust, "#environment") TO current_sink ]
	_ : object_type(cust, req)
	END
]
**/
static
BEH_DECL(env_type)
{
	CONS* state = MINE;
	CONS* parent;
	CONS* map;
	CONS* msg = WHAT;
	CONS* cust;
	CONS* req;

	DBUG_ENTER("env_type");
	ENSURE(is_pr(state));
	parent = hd(state);
	map = tl(state);
	ENSURE(is_pr(msg));
	cust = hd(msg);
	ENSURE(actorp(cust));
	req = tl(msg);

	DBUG_PRINT("parent", ("%s", cons_to_str(parent)));
	DBUG_PRINT("map", ("%s", cons_to_str(map)));
	DBUG_PRINT("cust", ("%s", cons_to_str(cust)));
	DBUG_PRINT("req", ("%s", cons_to_str(req)));
	if (is_pr(req)
	&& (hd(req) == ATOM("type_eq"))) {
		SEND(cust, ((tl(req) == MK_REF(env_type)) ? a_true : a_false));
	} else if (is_pr(req)
	&& (hd(req) == ATOM("lookup"))) {
		CONS* key = tl(req);
		CONS* binding = map_find(map, key);

		DBUG_PRINT("key", ("%s", cons_to_str(key)));
		DBUG_PRINT("binding", ("%s", cons_to_str(binding)));
		if (nilp(binding)) {
			if (nilp(parent)) {
				THROW(pr(ATOM("Undefined"), key));
			} else {
				SEND(parent, msg);
			}
		} else {
			SEND(cust, tl(binding));
		}
	} else if (is_pr(req) && is_pr(tl(req))
	&& (hd(req) == ATOM("bind"))) {
		CONS* key = hd(tl(req));
		CONS* value = tl(tl(req));
		CONS* binding = map_find(map, key);

		DBUG_PRINT("key", ("%s", cons_to_str(key)));
		DBUG_PRINT("value", ("%s", cons_to_str(value)));
		DBUG_PRINT("binding", ("%s", cons_to_str(binding)));
		if (nilp(binding)) {
			BECOME(env_type, pr(parent, map_put(map, key, value)));
		} else {
			rplacd(binding, value);
		}
		SEND(cust, a_inert);
	} else if (req == ATOM("write")) {
		SINK* sink = current_sink;
		SEND(cust, (sink->put_cstr)(sink, "#environment"));
	} else {
		object_type(CFG);  /* DELEGATE BEHAVIOR */
	}
	DBUG_RETURN;
}

/**
LET list_oper = \(cust, req).[
	CASE req OF
	(#comb, opnds, env) : [
		SEND opnds TO cust
	]
	_ : oper_type(cust, req)
	END
]
**/
static
BEH_DECL(list_oper)
{
	CONS* msg = WHAT;
	CONS* cust;
	CONS* req;

	DBUG_ENTER("list_oper");
	ENSURE(is_pr(msg));
	cust = hd(msg);
	ENSURE(actorp(cust));
	req = tl(msg);

	DBUG_PRINT("cust", ("%s", cons_to_str(cust)));
	DBUG_PRINT("req", ("%s", cons_to_str(req)));
	if (is_pr(req) && is_pr(tl(req))
	&& (hd(req) == ATOM("comb"))) {
		CONS* opnds = hd(tl(req));
		/* CONS* env = tl(tl(req)); */

		SEND(cust, opnds);
	} else {
		oper_type(CFG);  /* DELEGATE BEHAVIOR */
	}
	DBUG_RETURN;
}

/**
LET pair_tail = \(x,y).y
**/
static CONS*
pair_tail(CONS* p, CONS* q)
{
	return q;
}
/**
LET sequence_oper = \(cust, req).[
	CASE req OF
	(#comb, opnds, env) : [
		SEND (cust, #foldl, Inert, (\(x,y).y), #eval, env) TO opnds
	]
	_ : oper_type(cust, req)
	END
]
**/
static
BEH_DECL(sequence_oper)
{
	CONS* msg = WHAT;
	CONS* cust;
	CONS* req;

	DBUG_ENTER("sequence_oper");
	ENSURE(is_pr(msg));
	cust = hd(msg);
	ENSURE(actorp(cust));
	req = tl(msg);

	DBUG_PRINT("cust", ("%s", cons_to_str(cust)));
	DBUG_PRINT("req", ("%s", cons_to_str(req)));
	if (is_pr(req) && is_pr(tl(req))
	&& (hd(req) == ATOM("comb"))) {
		CONS* opnds = hd(tl(req));
		CONS* env = tl(tl(req));

		SEND(opnds, pr(cust, pr(ATOM("foldl"),
			pr(a_inert, pr(MK_FUNC(pair_tail), pr(ATOM("eval"), env))))));
	} else {
		oper_type(CFG);  /* DELEGATE BEHAVIOR */
	}
	DBUG_RETURN;
}

/**
LET define_match_beh(cust, ptree, env) = \value.[
	SEND (cust, #match, value, env) TO ptree
]
**/
static
BEH_DECL(define_match_beh)
{
	CONS* state = MINE;
	CONS* cust;
	CONS* ptree;
	CONS* env;
	CONS* value = WHAT;

	DBUG_ENTER("define_match_beh");
	ENSURE(is_pr(state));
	cust = hd(state);
	ENSURE(actorp(cust));
	ENSURE(is_pr(tl(state)));
	ptree = hd(tl(state));
	env = tl(tl(state));

	SEND(ptree, pr(cust, pr(ATOM("match"), pr(value, env))));
	DBUG_RETURN;
}
/**
LET define_args_beh(cust, env) = \(ptree, expr, NIL).[
	CREATE k_value WITH define_match_beh(cust, ptree, env)
	SEND (k_value, #eval, env) TO expr
]
**/
static
BEH_DECL(define_args_beh)
{
	CONS* state = MINE;
	CONS* cust;
	CONS* env;
	CONS* msg = WHAT;
	CONS* ptree;
	CONS* expr;
	CONS* k_value;

	DBUG_ENTER("define_args_beh");
	ENSURE(is_pr(state));
	cust = hd(state);
	ENSURE(actorp(cust));
	env = tl(state);
	ENSURE(is_pr(msg));
	ptree = hd(msg);
	ENSURE(is_pr(tl(msg)));
	expr = hd(tl(msg));
	ENSURE(nilp(tl(tl(msg))));

	DBUG_PRINT("cust", ("%s", cons_to_str(cust)));
	DBUG_PRINT("env", ("%s", cons_to_str(env)));
	DBUG_PRINT("ptree", ("%s", cons_to_str(ptree)));
	DBUG_PRINT("expr", ("%s", cons_to_str(expr)));
	k_value = ACTOR(define_match_beh, pr(cust, pr(ptree, env)));
	SEND(expr, pr(k_value, pr(ATOM("eval"), env)));
	DBUG_RETURN;
}

/**
LET eval_sequence_beh(cust, body, env) = \$Inert.[  # eval_sequence_beh
	SEND (cust, #foldl, Inert, (\(x,y).y), #eval, env) TO body
]
**/
static
BEH_DECL(eval_sequence_beh)
{
	CONS* state = MINE;
	CONS* cust;
	CONS* body;
	CONS* env;

	DBUG_ENTER("eval_sequence_beh");
	ENSURE(is_pr(state));
	cust = hd(state);
	ENSURE(actorp(cust));
	ENSURE(is_pr(tl(state)));
	body = hd(tl(state));
	env = tl(tl(state));
	ENSURE(WHAT == a_inert);

	SEND(body, pr(cust, pr(ATOM("foldl"),
		pr(a_inert, pr(MK_FUNC(pair_tail), pr(ATOM("eval"), env))))));
	DBUG_RETURN;
}
/**
LET vau_type(ptree, body, s_env) = \(cust, req).[
	CASE req OF
	(#comb, opnds, d_env) : [
		CREATE local WITH Env(s_env)
		CREATE formal WITH Pair(opnds, d_env)
		SEND (k_eval, #match, formal, local) TO ptree
		CREATE k_eval WITH \$Inert.[  # eval_sequence_beh
			SEND (cust, #foldl, Inert, (\(x,y).y), #eval, local) TO body
		]
	]
	_ : oper_type(cust, req)
	END
])
**/
static
BEH_DECL(vau_type)
{
	CONS* state = MINE;
	CONS* ptree;
	CONS* body;
	CONS* s_env;
	CONS* msg = WHAT;
	CONS* cust;
	CONS* req;

	DBUG_ENTER("vau_type");
	ENSURE(is_pr(state));
	ptree = hd(state);
	ENSURE(is_pr(tl(state)));
	body = hd(tl(state));
	s_env = tl(tl(state));
	ENSURE(is_pr(msg));
	cust = hd(msg);
	ENSURE(actorp(cust));
	req = tl(msg);

	DBUG_PRINT("ptree", ("%s", cons_to_str(ptree)));
	DBUG_PRINT("body", ("%s", cons_to_str(body)));
	DBUG_PRINT("s_env", ("%s", cons_to_str(s_env)));
	DBUG_PRINT("cust", ("%s", cons_to_str(cust)));
	DBUG_PRINT("req", ("%s", cons_to_str(req)));
	if (is_pr(req) && is_pr(tl(req))
	&& (hd(req) == ATOM("comb"))) {
		CONS* opnds = hd(tl(req));
		CONS* d_env = tl(tl(req));
		CONS* local = ACTOR(env_type, pr(s_env, NIL));
		CONS* formal = ACTOR(pair_type, pr(opnds, d_env));
		CONS* k_eval = ACTOR(eval_sequence_beh, pr(cust, pr(body, local)));

		DBUG_PRINT("opnds", ("%s", cons_to_str(opnds)));
		DBUG_PRINT("d_env", ("%s", cons_to_str(d_env)));
		SEND(ptree, pr(k_eval, pr(ATOM("match"), pr(formal, local))));
	} else {
		oper_type(CFG);  /* DELEGATE BEHAVIOR */
	}
	DBUG_RETURN;
}
/**
LET vau_evar_beh(cust, vars, env) = \(evar, body).[
	CREATE actual WITH pair_type(vars, evar)
	CREATE comb WITH vau_type(actual, body, env)
	SEND comb TO cust
]
**/
static
BEH_DECL(vau_evar_beh)
{
	CONS* state = MINE;
	CONS* cust;
	CONS* vars;
	CONS* env;
	CONS* msg = WHAT;
	CONS* evar;
	CONS* body;
	CONS* actual;
	CONS* comb;

	DBUG_ENTER("vau_evar_beh");
	ENSURE(is_pr(state));
	cust = hd(state);
	ENSURE(actorp(cust));
	ENSURE(is_pr(tl(state)));
	vars = hd(tl(state));
	env = tl(tl(state));
	ENSURE(is_pr(msg));
	evar = hd(msg);
	body = tl(msg);

	actual = ACTOR(pair_type, pr(vars, evar));
	comb = ACTOR(vau_type, pr(actual, pr(body, env)));
	SEND(cust, comb);
	DBUG_RETURN;
}
/**
LET vau_vars_beh(cust, env) = \(vars, opnds).[
	SEND (SELF, #as_pair) TO opnds
	BECOME vau_evar_beh(cust, vars, env)
]
**/
static
BEH_DECL(vau_vars_beh)
{
	CONS* state = MINE;
	CONS* cust;
	CONS* env;
	CONS* msg = WHAT;
	CONS* vars;
	CONS* opnds;

	DBUG_ENTER("vau_vars_beh");
	ENSURE(is_pr(state));
	cust = hd(state);
	ENSURE(actorp(cust));
	env = tl(state);
	ENSURE(is_pr(msg));
	vars = hd(msg);
	opnds = tl(msg);

	SEND(opnds, pr(SELF, ATOM("as_pair")));
	BECOME(vau_evar_beh, pr(cust, pr(vars, env)));
	DBUG_RETURN;
}
/**
LET vau_oper = \(cust, req).[
	CASE req OF
	(#comb, opnds, env) : [
		SEND (k_copy, #copy_immutable) TO opnds
		CREATE k_copy WITH command_beh(k_pair, #as_pair)		
		CREATE k_pair WITH \(vars, opnds').[  # vau_vars_beh
			SEND (SELF, #as_pair) TO opnds'
			BECOME \(evar, body).[  # vau_evar_beh
				CREATE actual WITH pair_type(vars, evar)
				CREATE comb WITH vau_type(actual, body, env)
				SEND comb TO cust
			]
		]
	]
	_ : oper_type(cust, req)
	END
]
**/
static
BEH_DECL(vau_oper)
{
	CONS* msg = WHAT;
	CONS* cust;
	CONS* req;

	DBUG_ENTER("vau_oper");
	ENSURE(is_pr(msg));
	cust = hd(msg);
	ENSURE(actorp(cust));
	req = tl(msg);

	DBUG_PRINT("cust", ("%s", cons_to_str(cust)));
	DBUG_PRINT("req", ("%s", cons_to_str(req)));
	if (is_pr(req) && is_pr(tl(req))
	&& (hd(req) == ATOM("comb"))) {
		CONS* opnds = hd(tl(req));
		CONS* env = tl(tl(req));
		CONS* k_copy;
		CONS* k_pair;

		k_pair = ACTOR(vau_vars_beh, pr(cust, env));
		k_copy = ACTOR(command_beh, pr(k_pair, ATOM("as_pair")));
		SEND(opnds, pr(k_copy, ATOM("copy_immutable")));
	} else {
		oper_type(CFG);  /* DELEGATE BEHAVIOR */
	}
	DBUG_RETURN;
}

/**
LET wrap_args_beh(cust, env) = \(comb, NIL).[
	CREATE appl WITH Appl(comb)
	SEND appl TO cust
]
**/
static
BEH_DECL(wrap_args_beh)
{
	CONS* state = MINE;
	CONS* cust;
	CONS* msg = WHAT;
	CONS* comb;
	CONS* appl;

	DBUG_ENTER("wrap_args_beh");
	ENSURE(is_pr(state));
	cust = hd(state);
	ENSURE(actorp(cust));
	ENSURE(is_pr(msg));
	comb = hd(msg);
	ENSURE(nilp(tl(msg)));

	appl = ACTOR(appl_type, comb);
	SEND(cust, appl);
	DBUG_RETURN;
}

/**
LET unwrap_args_beh(cust, env) = \(appl, NIL).[
	SEND (cust, #unwrap) TO appl
]
**/
static
BEH_DECL(unwrap_args_beh)
{
	CONS* state = MINE;
	CONS* cust;
	CONS* msg = WHAT;
	CONS* appl;

	DBUG_ENTER("unwrap_args_beh");
	ENSURE(is_pr(state));
	cust = hd(state);
	ENSURE(actorp(cust));
	ENSURE(is_pr(msg));
	appl = hd(msg);
	ENSURE(nilp(tl(msg)));

	SEND(appl, pr(cust, ATOM("unwrap")));
	DBUG_RETURN;
}

/**
LET lambda_type(ptree, body, env) = \(cust, req).[
	CASE req OF
	(#comb, opnds, _) : [
		CREATE local WITH env_type(env)
		SEND (k_eval, #match, opnds, local) TO ptree
		CREATE k_eval WITH \$Inert.[  # eval_sequence_beh
			SEND (cust, #foldl, Inert, (\(x,y).y), #eval, local) TO body
		]
	]
	_ : oper_type(cust, req)
	END
]
**/
static
BEH_DECL(lambda_type)
{
	CONS* state = MINE;
	CONS* ptree;
	CONS* body;
	CONS* env;
	CONS* msg = WHAT;
	CONS* cust;
	CONS* req;

	DBUG_ENTER("lambda_type");
	ENSURE(is_pr(state));
	ptree = hd(state);
	ENSURE(is_pr(tl(state)));
	body = hd(tl(state));
	env = tl(tl(state));
	ENSURE(is_pr(msg));
	cust = hd(msg);
	ENSURE(actorp(cust));
	req = tl(msg);

	DBUG_PRINT("ptree", ("%s", cons_to_str(ptree)));
	DBUG_PRINT("body", ("%s", cons_to_str(body)));
	DBUG_PRINT("env", ("%s", cons_to_str(env)));
	DBUG_PRINT("cust", ("%s", cons_to_str(cust)));
	DBUG_PRINT("req", ("%s", cons_to_str(req)));
	if (is_pr(req) && is_pr(tl(req))
	&& (hd(req) == ATOM("comb"))) {
		CONS* opnds = hd(tl(req));
		/* CONS* env = tl(tl(req)); -- dynamic environment ignored */
		CONS* local = ACTOR(env_type, pr(env, NIL));
		CONS* k_eval = ACTOR(eval_sequence_beh, pr(cust, pr(body, local)));

		DBUG_PRINT("opnds", ("%s", cons_to_str(opnds)));
		SEND(ptree, pr(k_eval, pr(ATOM("match"), pr(opnds, local))));
	} else {
		oper_type(CFG);  /* DELEGATE BEHAVIOR */
	}
	DBUG_RETURN;
}
/**
LET lambda_vars_beh(cust, env) = \(ptree, body).[
	CREATE oper WITH lambda_type(ptree, body, env)
	SEND NEW appl_type(oper) TO cust
]
**/
static
BEH_DECL(lambda_vars_beh)
{
	CONS* state = MINE;
	CONS* cust;
	CONS* env;
	CONS* msg = WHAT;
	CONS* ptree;
	CONS* body;
	CONS* oper;

	DBUG_ENTER("lambda_vars_beh");
	ENSURE(is_pr(state));
	cust = hd(state);
	ENSURE(actorp(cust));
	env = tl(state);
	ENSURE(is_pr(msg));
	ptree = hd(msg);
	body = tl(msg);

	oper = ACTOR(lambda_type, pr(ptree, pr(body, env)));
	SEND(cust, ACTOR(appl_type, oper));
	DBUG_RETURN;
}
/**
LET lambda_oper = \(cust, req).[
	CASE req OF
	(#comb, opnds, env) : [
		SEND (k_copy, #copy_immutable) TO opnds
		CREATE k_copy WITH command_beh(k_pair, #as_pair)		
		CREATE k_pair WITH \(ptree, body).[  # lambda_vars_beh
			CREATE oper WITH lambda_type(ptree, body, env)
			SEND NEW appl_type(oper) TO cust
		]
	]
	_ : oper_type(cust, req)
	END
]
**/
static
BEH_DECL(lambda_oper)
{
	CONS* msg = WHAT;
	CONS* cust;
	CONS* req;

	DBUG_ENTER("list_oper");
	ENSURE(is_pr(msg));
	cust = hd(msg);
	ENSURE(actorp(cust));
	req = tl(msg);

	DBUG_PRINT("cust", ("%s", cons_to_str(cust)));
	DBUG_PRINT("req", ("%s", cons_to_str(req)));
	if (is_pr(req) && is_pr(tl(req))
	&& (hd(req) == ATOM("comb"))) {
		CONS* opnds = hd(tl(req));
		CONS* env = tl(tl(req));
		CONS* k_copy;
		CONS* k_pair;

		k_pair = ACTOR(lambda_vars_beh, pr(cust, env));
		k_copy = ACTOR(command_beh, pr(k_pair, ATOM("as_pair")));
		SEND(opnds, pr(k_copy, ATOM("copy_immutable")));
	} else {
		oper_type(CFG);  /* DELEGATE BEHAVIOR */
	}
	DBUG_RETURN;
}

/**
LET eq_args_beh(cust, env) = \args.[
	LET eq* = \rest.(
		CASE rest OF
		NIL : True
		(h, t) : (
			CASE h OF
			$first : eq*(t)
			_ : False
			END
		)
		END
	)
	CASE args OF
	(first, rest) : [ SEND eq*(rest) TO cust ]
	_ : True
	END
]
**/
static
BEH_DECL(eq_args_beh)
{
	CONS* state = MINE;
	CONS* cust;
	CONS* args = WHAT;
	CONS* first;
	CONS* rest;
	CONS* result;

	DBUG_ENTER("eq_args_beh");
	ENSURE(is_pr(state));
	cust = hd(state);
	ENSURE(actorp(cust));

	result = a_true;
	if (is_pr(args)) {
		first = hd(args);
		DBUG_PRINT("first", ("%s", cons_to_str(first)));
		rest = tl(args);
		while (is_pr(rest)) {
			DBUG_PRINT("rest", ("%s", cons_to_str(rest)));
			if (!equal(first, hd(rest))) {
				result = a_false;
				break;
			}
			rest = tl(rest);
		}
	}
	DBUG_PRINT("result", ("%s", cons_to_str(result)));
	SEND(cust, result);
	DBUG_RETURN;
}

/**
LET if_test_beh(cust, cnsq, altn, env) = \bool.[
	SEND (cust, #if, cnsq, altn, env) TO bool
]
**/
static
BEH_DECL(if_test_beh)
{
	CONS* state = MINE;
	CONS* cust;
	CONS* bool = WHAT;

	DBUG_ENTER("if_test_beh");
	ENSURE(is_pr(state));
	cust = hd(state);
	ENSURE(actorp(cust));
	ENSURE(actorp(bool));

	SEND(bool, pr(cust, pr(ATOM("if"), tl(state))));
	DBUG_RETURN;
}
/**
LET if_args_beh(cust, env) = \(test, cnsq, altn, NIL).[
	SEND (k_test, #eval, env) TO test
	CREATE k_test WITH \bool.[  # if_test_beh
		SEND (cust, #if, cnsq, altn, env) TO bool
	]
]
**/
static
BEH_DECL(if_args_beh)
{
	CONS* state = MINE;
	CONS* cust;
	CONS* env;
	CONS* msg = WHAT;
	CONS* test;
	CONS* cnsq;
	CONS* altn;
	CONS* k_test;

	DBUG_ENTER("if_args_beh");
	ENSURE(is_pr(state));
	cust = hd(state);
	ENSURE(actorp(cust));
	env = tl(state);
	ENSURE(is_pr(msg));
	test = hd(msg);
	ENSURE(is_pr(tl(msg)));
	cnsq = hd(tl(msg));
	ENSURE(is_pr(tl(tl(msg))));
	altn = hd(tl(tl(msg)));
	ENSURE(nilp(tl(tl(tl(msg)))));

	DBUG_PRINT("cust", ("%s", cons_to_str(cust)));
	DBUG_PRINT("env", ("%s", cons_to_str(env)));
	DBUG_PRINT("test", ("%s", cons_to_str(test)));
	DBUG_PRINT("cnsq", ("%s", cons_to_str(cnsq)));
	DBUG_PRINT("altn", ("%s", cons_to_str(altn)));
	k_test = ACTOR(if_test_beh, pr(cust, pr(cnsq, pr(altn, env))));
	SEND(test, pr(k_test, pr(ATOM("eval"), env)));
	DBUG_RETURN;
}

/**
LET cons_args_beh(cust, env) = \(a, d, NIL).[
	CREATE pair WITH Pair(a, d)
	SEND pair TO cust
]
**/
static
BEH_DECL(cons_args_beh)
{
	CONS* state = MINE;
	CONS* cust;
	CONS* msg = WHAT;
	CONS* a;
	CONS* d;

	DBUG_ENTER("cons_args_beh");
	ENSURE(is_pr(state));
	cust = hd(state);
	ENSURE(actorp(cust));
	ENSURE(is_pr(msg));
	a = hd(msg);
	ENSURE(is_pr(tl(msg)));
	d = hd(tl(msg));
	ENSURE(nilp(tl(tl(msg))));

	DBUG_PRINT("a", ("%s", cons_to_str(a)));
	DBUG_PRINT("d", ("%s", cons_to_str(d)));
	SEND(cust, ACTOR(cons_type, pr(a, d)));  /* cons produces mutable pairs */
	DBUG_RETURN;
}

/**
LET write_args_beh(cust, env) = \(sexpr, NIL).[
	SEND (cust, #write) TO sexpr
]
**/
static
BEH_DECL(write_args_beh)
{
	CONS* state = MINE;
	CONS* cust;
	CONS* msg = WHAT;
	CONS* sexpr;

	DBUG_ENTER("write_args_beh");
	ENSURE(is_pr(state));
	cust = hd(state);
	ENSURE(actorp(cust));
	ENSURE(is_pr(msg));
	sexpr = hd(msg);
	ENSURE(nilp(tl(msg)));

	SEND(sexpr, pr(cust, ATOM("write")));
	DBUG_RETURN;
}

/**
LET newline_args_beh(cust, env) = \NIL.[
	SEND (cust, "\n") TO current_sink
]
**/
static
BEH_DECL(newline_args_beh)
{
	CONS* state = MINE;
	CONS* cust;
	SINK* sink = current_sink;

	DBUG_ENTER("newline_args_beh");
	ENSURE(is_pr(state));
	cust = hd(state);
	ENSURE(actorp(cust));
	ENSURE(nilp(WHAT));

	SEND(cust, (sink->put)(sink, NUMBER('\n')));
	DBUG_RETURN;
}

/**
LET set_car_args_beh(cust, env) = \(p, a, NIL).[
	SEND (cust, #set_car, a) TO p
]
**/
static
BEH_DECL(set_car_args_beh)
{
	CONS* state = MINE;
	CONS* cust;
	CONS* msg = WHAT;
	CONS* p;
	CONS* a;

	DBUG_ENTER("set_car_args_beh");
	ENSURE(is_pr(state));
	cust = hd(state);
	ENSURE(actorp(cust));
	ENSURE(is_pr(msg));
	p = hd(msg);
	ENSURE(is_pr(tl(msg)));
	a = hd(tl(msg));
	ENSURE(nilp(tl(tl(msg))));

	DBUG_PRINT("p", ("%s", cons_to_str(p)));
	DBUG_PRINT("a", ("%s", cons_to_str(a)));
	SEND(p, pr(cust, pr(ATOM("set_car"), a)));
	DBUG_RETURN;
}

/**
LET set_cdr_args_beh(cust, env) = \(p, d, NIL).[
	SEND (cust, #set_cdr, d) TO p
]
**/
static
BEH_DECL(set_cdr_args_beh)
{
	CONS* state = MINE;
	CONS* cust;
	CONS* msg = WHAT;
	CONS* p;
	CONS* d;

	DBUG_ENTER("set_cdr_args_beh");
	ENSURE(is_pr(state));
	cust = hd(state);
	ENSURE(actorp(cust));
	ENSURE(is_pr(msg));
	p = hd(msg);
	ENSURE(is_pr(tl(msg)));
	d = hd(tl(msg));
	ENSURE(nilp(tl(tl(msg))));

	DBUG_PRINT("p", ("%s", cons_to_str(p)));
	DBUG_PRINT("d", ("%s", cons_to_str(d)));
	SEND(p, pr(cust, pr(ATOM("set_cdr"), d)));
	DBUG_RETURN;
}

/**
LET copy_es_immutable_args_beh(cust, env) = \(sexpr, NIL).[
	SEND (cust, #copy_immutable) TO sexpr
]
**/
static
BEH_DECL(copy_es_immutable_args_beh)
{
	CONS* state = MINE;
	CONS* cust;
	CONS* msg = WHAT;
	CONS* sexpr;

	DBUG_ENTER("copy_es_immutable_args_beh");
	ENSURE(is_pr(state));
	cust = hd(state);
	ENSURE(actorp(cust));
	ENSURE(is_pr(msg));
	sexpr = hd(msg);
	ENSURE(nilp(tl(msg)));

	SEND(sexpr, pr(cust, ATOM("copy_immutable")));
	DBUG_RETURN;
}

/**
CREATE Inert WITH unit_type()
CREATE True WITH bool_type(TRUE)
CREATE False WITH bool_type(FALSE)
CREATE Nil WITH null_type()
CREATE Ignore WITH any_type()

ground_env("copy-es-immutable") = NEW appl_type(NEW args_oper(copy_es_immutable_args_beh))
ground_env("set-car!") = NEW appl_type(NEW args_oper(set_car_args_beh))
ground_env("set-cdr!") = NEW appl_type(NEW args_oper(set_cdr_args_beh))
ground_env("newline") = NEW appl_type(NEW args_oper(newline_args_beh))
ground_env("write") = NEW appl_type(NEW args_oper(write_args_beh))
ground_env("cons") = NEW appl_type(NEW args_oper(cons_args_beh))
ground_env("$if") = NEW args_oper(if_args_beh)
ground_env("eq?") = NEW appl_type(NEW args_oper(eq_args_beh))
ground_env("$lambda") = NEW lambda_oper
ground_env("unwrap") = NEW appl_type(NEW args_oper(unwrap_args_beh))
ground_env("wrap") = NEW appl_type(NEW args_oper(wrap_args_beh))
ground_env("$vau") = NEW vau_oper
ground_env("$define!") = NEW args_oper(define_args_beh)
ground_env("$sequence") = NEW sequence_oper
ground_env("list") = NEW appl_type(NEW list_oper)

ground_env("environment?") = NEW appl_type(NEW type_pred_oper(env_type))
ground_env("operative?") = NEW appl_type(NEW type_pred_oper(oper_type))
ground_env("applicative?") = NEW appl_type(NEW type_pred_oper(appl_type))
ground_env("symbol?") = NEW appl_type(NEW type_pred_oper(symbol_type))
ground_env("ignore?") = NEW appl_type(NEW type_pred_oper(any_type))
ground_env("inert?") = NEW appl_type(NEW type_pred_oper(unit_type))
ground_env("boolean?") = NEW appl_type(NEW type_pred_oper(bool_type))
ground_env("pair?") = NEW appl_type(NEW type_pred_oper(pair_type))
ground_env("null?") = NEW appl_type(NEW type_pred_oper(null_type))
**/
static void
init_kernel()
{
	CONS* ground_map = NIL;

	DBUG_ENTER("init_kernel");
	intern_map = pr(NIL, NIL);
	cfg_add_gc_root(CFG, intern_map);	/* protect from gc */

	input_file = stdin;
	output_file = stdout;
	current_source = file_source(input_file);
	current_sink = file_sink(output_file);

	a_inert = ACTOR(unit_type, NIL);
	cfg_add_gc_root(CFG, a_inert);		/* protect from gc */
	a_true = ACTOR(bool_type, TRUE);
	cfg_add_gc_root(CFG, a_true);		/* protect from gc */
	a_false = ACTOR(bool_type, FALSE);
	cfg_add_gc_root(CFG, a_false);		/* protect from gc */
	a_nil = ACTOR(null_type, NIL);
	cfg_add_gc_root(CFG, a_nil);		/* protect from gc */
	a_ignore = ACTOR(any_type, NIL);
	cfg_add_gc_root(CFG, a_ignore);		/* protect from gc */

	ground_map = map_put(ground_map, ATOM("copy-es-immutable"),
		ACTOR(appl_type,
			ACTOR(args_oper, MK_FUNC(copy_es_immutable_args_beh))));
	ground_map = map_put(ground_map, ATOM("set-car!"),
		ACTOR(appl_type,
			ACTOR(args_oper, MK_FUNC(set_car_args_beh))));
	ground_map = map_put(ground_map, ATOM("set-cdr!"),
		ACTOR(appl_type,
			ACTOR(args_oper, MK_FUNC(set_cdr_args_beh))));
	ground_map = map_put(ground_map, ATOM("newline"),
		ACTOR(appl_type,
			ACTOR(args_oper, MK_FUNC(newline_args_beh))));
	ground_map = map_put(ground_map, ATOM("write"),
		ACTOR(appl_type,
			ACTOR(args_oper, MK_FUNC(write_args_beh))));
	ground_map = map_put(ground_map, ATOM("cons"),
		ACTOR(appl_type,
			ACTOR(args_oper, MK_FUNC(cons_args_beh))));
	ground_map = map_put(ground_map, ATOM("$if"),
		ACTOR(args_oper, MK_FUNC(if_args_beh)));
	ground_map = map_put(ground_map, ATOM("eq?"),
		ACTOR(appl_type,
			ACTOR(args_oper, MK_FUNC(eq_args_beh))));
	ground_map = map_put(ground_map, ATOM("$lambda"),
		ACTOR(lambda_oper, NIL));
	ground_map = map_put(ground_map, ATOM("unwrap"),
		ACTOR(appl_type,
			ACTOR(args_oper, MK_FUNC(unwrap_args_beh))));
	ground_map = map_put(ground_map, ATOM("wrap"),
		ACTOR(appl_type,
			ACTOR(args_oper, MK_FUNC(wrap_args_beh))));
	ground_map = map_put(ground_map, ATOM("$vau"),
		ACTOR(vau_oper, NIL));
	ground_map = map_put(ground_map, ATOM("$define!"),
		ACTOR(args_oper, MK_FUNC(define_args_beh)));
	ground_map = map_put(ground_map, ATOM("$sequence"),
		ACTOR(sequence_oper, NIL));
	ground_map = map_put(ground_map, ATOM("list"),
		ACTOR(appl_type,
			ACTOR(list_oper, NIL)));

	ground_map = map_put(ground_map, ATOM("environment?"),
		ACTOR(appl_type,
			ACTOR(type_pred_oper, MK_REF(env_type))));
	ground_map = map_put(ground_map, ATOM("operative?"),
		ACTOR(appl_type,
			ACTOR(type_pred_oper, MK_REF(oper_type))));
	ground_map = map_put(ground_map, ATOM("applicative?"),
		ACTOR(appl_type,
			ACTOR(type_pred_oper, MK_REF(appl_type))));
	ground_map = map_put(ground_map, ATOM("symbol?"),
		ACTOR(appl_type,
			ACTOR(type_pred_oper, MK_REF(symbol_type))));
	ground_map = map_put(ground_map, ATOM("ignore?"),
		ACTOR(appl_type,
			ACTOR(type_pred_oper, MK_REF(any_type))));
	ground_map = map_put(ground_map, ATOM("inert?"),
		ACTOR(appl_type,
			ACTOR(type_pred_oper, MK_REF(unit_type))));
	ground_map = map_put(ground_map, ATOM("boolean?"),
		ACTOR(appl_type,
			ACTOR(type_pred_oper, MK_REF(bool_type))));
	ground_map = map_put(ground_map, ATOM("pair?"),
		ACTOR(appl_type,
			ACTOR(type_pred_oper, MK_REF(pair_type))));
	ground_map = map_put(ground_map, ATOM("null?"),
		ACTOR(appl_type,
			ACTOR(type_pred_oper, MK_REF(null_type))));

	a_kernel_env = ACTOR(env_type, pr(NIL, ground_map));
	cfg_add_gc_root(CFG, a_kernel_env);		/* protect from gc */
	a_ground_env = ACTOR(env_type, pr(a_kernel_env, NIL));
	cfg_add_gc_root(CFG, a_ground_env);		/* protect from gc */
	DBUG_RETURN;
}

#define	ONE_OF(c,s)	(((c) && strchr((s),(c))) ? TRUE : FALSE)

CONS*
read_sexpr(SOURCE* src)
{
	CONS* x;
	int c;

	DBUG_ENTER("read_sexpr");
	for (;;) {	/* skip whitespace */
		c = MK_INT((src->get)(src));
		if (c == ';') {
			(src->next)(src);
			for (;;) {  /* skip comment */
				c = MK_INT((src->get)(src));
				if ((c == '\n') || (c == '\r') || (c == EOF)) {
					break;
				}
				(src->next)(src);
			}
		}
		if (!isspace(c)) {
			break;
		}
		(src->next)(src);
	}
	if (c == EOF) {
		x = NUMBER(EOF);
	} else if (c == '(') {
		CONS* y = NIL;

		(src->next)(src);
		for (;;) {
			x = read_sexpr(src);
			if (actorp(x)) {
				DBUG_PRINT("actor", ("x=%s", cons_to_str(x)));
				y = pr(x, y);
				DBUG_PRINT("actor", ("y=%s", cons_to_str(y)));
			} else if (nilp(x) || is_pr(x)) {
				x = (nilp(x) ? a_nil : hd(x));
				DBUG_PRINT("close", ("x=%s", cons_to_str(x)));
				DBUG_PRINT("close", ("y=%s", cons_to_str(y)));
				while (is_pr(y)) {
					x = ACTOR(pair_type, pr(hd(y), x));
					y = tl(y);
				}
				DBUG_PRINT("close", ("x=%s", cons_to_str(x)));
				break;
			} else {
				DBUG_PRINT("error", ("x=%s", cons_to_str(x)));
				break;
			}
		}
	} else if (c == ')') {
		(src->next)(src);
		x = NIL;
	} else if (c == '.') {
		CONS* y;
		
		(src->next)(src);
		x = read_sexpr(src);
		DBUG_PRINT("dot", ("x=%s", cons_to_str(x)));
		y = read_sexpr(src);
		DBUG_PRINT("dot", ("y=%s", cons_to_str(y)));
		if (nilp(y)) {
			x = pr(x, y);
		} else {
			x = NUMBER(')');  /* missing ')' */
		}
	} else if (c == '"') {
		x = NUMBER(c);  /* FIXME: implement string literals */
	} else if (ispunct(c) && ONE_OF(c, "'`,[]{}|")) {
		x = NUMBER(c);  /* illegal lexeme */
	} else if (isdigit(c)) {
		x = NUMBER(0);
		do {
			x = NUMBER((MK_INT(x) * 10) + (c - '0'));
			(src->next)(src);
			c = MK_INT((src->get)(src));
		} while (isdigit(c));
		if ((c == EOF) || isspace(c) || ONE_OF(c, "\"()")) {
			x = get_const(x);
		} else {
			x = NUMBER(c);  /* malformed number */
		}
	} else {
		BOOL sharp = ((c == '#') ? TRUE : FALSE);

		x = NIL;
		do {
			x = ATOM_X(x, tolower(c));  /* forced lowercase */
			(src->next)(src);
			c = MK_INT((src->get)(src));
		} while (isgraph(c) && !ONE_OF(c, "\"()"));
		if (sharp == TRUE) {
			if (x == ATOM("#inert")) {
				x = a_inert;
			} else if (x == ATOM("#t")) {
				x = a_true;
			} else if (x == ATOM("#f")) {
				x = a_false;
			} else if (x == ATOM("#ignore")) {
				x = a_ignore;
			} else {
				x = get_symbol(x);
			}
		} else {
			x = get_symbol(x);
		}
	}
	DBUG_PRINT("x", ("%s", cons_to_str(x)));
	DBUG_RETURN x;
}

void
run_repl(int batch)
{
	CONFIG* cfg = CFG;  /* USE GLOBAL CONFIGURATION */

	for (;;) {
		int remain = run_configuration(cfg, batch);

		if (remain < 0) {
			DBUG_PRINT("", ("%d messages queued (limit %d).", cfg->q_count, cfg->q_limit));
			break;		/* abnormal return */
		}
		DBUG_PRINT("", ("%d messages delivered (batch %d).", (batch - remain), batch));
		if (cfg->t_count > 0) {
			DBUG_PRINT("", ("waiting for timed event..."));
			sleep(1);
		} else {
			return;		/* no more work to do! */
		}
	}
	fprintf(stderr, "\nOutstanding messages exceeded limit of %d\n", cfg->q_limit);
	abort();	/* abnormal termination */
}

void
run_test(int limit)
{
	CONFIG* cfg = CFG;  /* USE GLOBAL CONFIGURATION */

	for (;;) {
		int remain = run_configuration(cfg, limit);

		if (remain < 0) {
			DBUG_PRINT("", ("%d messages queued (limit %d).", cfg->q_count, cfg->q_limit));
			break;		/* abnormal return */
		}
		DBUG_PRINT("", ("%d messages delivered (limit %d).", (limit - remain), limit));
		if (cfg->q_count > 0) {
			DBUG_PRINT("", ("%d messages remain in queue.", cfg->q_count));
			break;		/* excessive messaging */
		}
		if (cfg->t_count > 0) {
			DBUG_PRINT("", ("waiting for timed event..."));
			sleep(1);
		} else {
			return;		/* no more work to do! */
		}
	}
	abort();	/* terminate test run */
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

static void
report_cons_stats()
{
	report_atom_usage();
	report_cons_usage();
}

void
prompt()
{
	fprintf(output_file, "\n> ");
	fflush(output_file);
}

/**
newline_beh(state) = \_.[
	# print newline on console
	CASE state OF
	(value, cust) : [ SEND value TO cust ]
	END
]
**/
static
BEH_DECL(newline_beh)
{
	CONS* state = MINE;
	
	DBUG_ENTER("newline_beh");
	fputc('\n', output_file);
	fflush(output_file);
	if (is_pr(state) && actorp(hd(state))) {
		SEND(hd(state), tl(state));
	}
	DBUG_RETURN;
}

/**
report_beh(cust) = \value.[
	CREATE cust' WITH newline_beh(cust, value)
	SEND (cust', #write) TO value
	BECOME abort_beh
]
**/
static
BEH_DECL(report_beh)
{
	CONS* cust = MINE;
	CONS* value = WHAT;
	
	DBUG_ENTER("report_beh");
	cust = ACTOR(newline_beh, pr(cust, value));
	SEND(value, pr(cust, ATOM("write")));
	BECOME(abort_beh, NIL);
	DBUG_RETURN;
}

static CONS*
read_eval_print_loop(FILE* f, BOOL interactive)
{
	CONS* cust;
	CONS* expr;
	
	DBUG_ENTER("read_eval_print_loop");
	input_file = f;
	current_source = file_source(input_file);
	for (;;) {
		if (interactive) {
			prompt();
		}
		expr = read_sexpr(current_source);
		if (expr == NUMBER(EOF)) {
			DBUG_RETURN a_inert;  /* end of input */
		} else if (!actorp(expr)) {
			DBUG_RETURN expr;  /* error */
		}
		cust = ACTOR(sink_beh, NIL);
		if (interactive) {
			cust = ACTOR(report_beh, cust);
		}
		SEND(expr, pr(cust, pr(ATOM("eval"), a_ground_env)));  /* evaluate */
		run_repl(1000);  /* actor dispatch loop */
	}	
}

/**
assert_beh(expect) = \actual.[
	IF $expect = $actual [
		# matched, done
		BECOME abort_beh
	] ELSE [
		# print message and abort
	]
]
**/
static
BEH_DECL(assert_beh)
{
	CONS* expect = MINE;
	CONS* actual = WHAT;

	DBUG_ENTER("assert_beh");
	DBUG_PRINT("", ("expect=%s", cons_to_str(expect)));
	if (equal(expect, actual)) {
		BECOME(abort_beh, NIL);
	} else {
		DBUG_PRINT("", ("actual=%s", cons_to_str(actual)));
		fprintf(stderr, "assert_beh: FAIL!\n");
		abort();
	}
	DBUG_RETURN;
}

static void
assert_eval(CONS* expr, CONS* expect)
{
	CONS* cust;

	prompt();
	cust = ACTOR(newline_beh, NIL);
	SEND(expr, pr(cust, ATOM("write")));  /* echo expr to console */
	cust = ACTOR(assert_beh, expect);
	cust = ACTOR(report_beh, cust);
	SEND(expr, pr(cust, pr(ATOM("eval"), a_ground_env)));
	run_test(1000);
	assert(_THIS(cust) == abort_beh);
}

void
test_kernel()
{
	SOURCE* src;
	CONS* expr;
	CONS* expect;

	DBUG_ENTER("test_kernel");
	TRACE(printf("--test_kernel--\n"));

	/*
	 * test character source
	 */
	src = string_source(NULL);
	assert((src->empty)(src) == a_true);
	assert((src->next)(src) == NUMBER(EOF));

	src = string_source("");
	assert((src->empty)(src) == a_true);
	assert((src->next)(src) == NUMBER(EOF));

	src = string_source(" ");
	assert((src->empty)(src) == a_false);
	expr = (src->next)(src);
	assert(expr == NUMBER(32));
	assert(expr == NUMBER(' '));
	assert((src->empty)(src) == a_true);
	assert((src->next)(src) == NUMBER(EOF));

	src = string_source("()");
	assert((src->empty)(src) == a_false);
	expr = (src->next)(src);
	assert(expr == NUMBER('('));
	assert((src->empty)(src) == a_false);
	expr = (src->next)(src);
	assert(expr == NUMBER(')'));
	assert((src->empty)(src) == a_true);
	assert((src->next)(src) == NUMBER(EOF));

	src = string_source("\r\n");
	expr = read_sexpr(src);
	expect = NUMBER(EOF);
	assert(equal(expect, expr));

	src = string_source("()");
	expr = read_sexpr(src);
	expect = a_nil;
	assert(equal(expect, expr));
/*
	src = string_source("(x)");
	expr = read_sexpr(src);
	expect = pr(ATOM("x"), NIL);
	assert(equal(expect, expr));

	src = string_source("(x y)");
	expr = read_sexpr(src);
	expect = pr(ATOM("x"), pr(ATOM("y"), NIL));
	assert(equal(expect, expr));
*/
	src = string_source("(x (y");
	expr = read_sexpr(src);
	expect = NUMBER(EOF);
	assert(equal(expect, expr));

	/*
	 * #inert
	 * ==> #inert
	 */
	expr = a_inert;
	expect = a_inert;
	assert_eval(expr, expect);

	/*
	 * (boolean? #t #f)
	 * ==> #t
	 */
	expr = ACTOR(pair_type, pr(
		get_symbol(ATOM("boolean?")),
		ACTOR(pair_type, pr(
			a_true,
			ACTOR(pair_type, pr(
				a_false,
				a_nil))))));
	expect = a_true;
	assert_eval(expr, expect);

	/*
	 * (ignore? #ignore #inert)
	 * ==> #f
	 */
	expr = ACTOR(pair_type, pr(
		get_symbol(ATOM("ignore?")),
		ACTOR(pair_type, pr(
			a_ignore,
			ACTOR(pair_type, pr(
				a_inert,
				a_nil))))));
	expect = a_false;
	assert_eval(expr, expect);

	/*
	 * (($vau (x) #ignore x) y)
	 * ==> y
	 */
	expr = read_sexpr(string_source(
		"(($vau (x) #ignore x) y)"));
/*	expr = ACTOR(pair_type, pr(
		ACTOR(pair_type, pr(
			get_symbol(ATOM("$vau")),
			ACTOR(pair_type, pr(
				ACTOR(pair_type, pr(
					get_symbol(ATOM("x")),
					a_nil)),
				ACTOR(pair_type, pr(
					a_ignore,
					ACTOR(pair_type, pr(
						get_symbol(ATOM("x")),
						a_nil)))))))),
		ACTOR(pair_type, pr(
			get_symbol(ATOM("y")),
			a_nil)))); */
	expect = get_symbol(ATOM("y"));
	assert_eval(expr, expect);

	/*
	 * (list #t #f)
	 */
/*	expr = ACTOR(pair_type, pr(
		get_symbol(ATOM("list")),
		ACTOR(pair_type, pr(
			a_true,
			ACTOR(pair_type, pr(
				a_false,
				a_nil))))));
	expect = a_true;
	assert_eval(expr, expect);
*/
	/*
	 * ($sequence
	 *		($define! y #t)
	 *		(($lambda (x) x) y))
	 * ==> #t
	 */
	expr = read_sexpr(string_source(
"($sequence \n\
	($define! y #t) \n\
	(($lambda (x) x) y))\n\
"));
/*	expr = ACTOR(pair_type, pr(
		get_symbol(ATOM("$sequence")),
		ACTOR(pair_type, pr(
			ACTOR(pair_type, pr(
				get_symbol(ATOM("$define!")),
				ACTOR(pair_type, pr(
					get_symbol(ATOM("y")),
					ACTOR(pair_type, pr(
						a_true,
						a_nil)))))),
			ACTOR(pair_type, pr(
				ACTOR(pair_type, pr(
					ACTOR(pair_type, pr(
						get_symbol(ATOM("$lambda")),
						ACTOR(pair_type, pr(
							ACTOR(pair_type, pr(
								get_symbol(ATOM("x")),
								a_nil)),
							ACTOR(pair_type, pr(
								get_symbol(ATOM("x")),
								a_nil)))))),
					ACTOR(pair_type, pr(
						get_symbol(ATOM("y")),
						a_nil)))),
				a_nil)))))); */
	expect = a_true;
	assert_eval(expr, expect);

	/*
	 * (eq? #f (boolean? #t #f))
	 * ==> #f
	 */
	expr = read_sexpr(string_source(
		"(eq? #f (boolean? #t #f))"));
	expect = a_false;
	assert_eval(expr, expect);

	/*
	 * ($if #t
	 *		($if #f
	 *			0
	 *			42)
	 *		314)
	 * ==> 42
	 */
	expr = read_sexpr(string_source(
"($if #t \n\
	($if #f \n\
		0 \n\
		42) \n\
	314)\n\
"));
	expect = get_const(NUMBER(42));
	assert_eval(expr, expect);

	/*
	 * (eq? (cons 0 (cons 1 ())) (list 0 1))
	 * ==> #f  ; equal?, but not eq?
	 */
	expr = read_sexpr(string_source(
		"(eq? (cons 0 (cons 1 ())) (list 0 1))"));
	expect = a_false;
	assert_eval(expr, expect);

	/*
	 * ($sequence
	 *		(write (cons (list #t #f #inert #ignore) (cons 0 1)))
	 *		(newline))
	 * ==> #t
	 */
	expr = read_sexpr(string_source(
"($sequence \n\
	(write (cons (list #t #f #inert #ignore) (cons 0 1))) \n\
	(newline))"
));
	expect = a_true;
	assert_eval(expr, expect);
	
	/*
	 * (($lambda ((#ignore . x)) x) (cons 0 1))
	 * ==> 1
	 */
	expr = read_sexpr(string_source(
		"(($lambda ((#ignore . x)) x) (cons 0 1))"));
	expect = get_const(NUMBER(1));
	assert_eval(expr, expect);

/* ...ADD TESTS HERE... */

#if 1
	cfg_force_gc(CFG);			/* clean up garbage created by tests */
#endif

	DBUG_RETURN;
}

void
usage(void)
{
	fprintf(stderr, "\
usage: %s [-ti] [-# dbug] file...\n",
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
	BOOL test_mode = FALSE;			/* flag to run unit tests */
	BOOL interactive = FALSE;		/* flag to run unit tests */

	DBUG_ENTER("main");
	DBUG_PROCESS(argv[0]);
	while ((c = getopt(argc, argv, "ti#:V")) != EOF) {
		switch(c) {
		case 't':	test_mode = TRUE;		break;
		case 'i':	interactive = TRUE;		break;
		case '#':	DBUG_PUSH(optarg);		break;
		case 'V':	banner();				exit(EXIT_SUCCESS);
		case '?':							usage();
		default:							usage();
		}
	}
	banner();
	CFG = new_configuration(1000);
	init_kernel();  /* ==== INITIALIZE GLOBAL CONFIGURATION ==== */
	if (test_mode) {
		test_kernel();	/* this test involves running the dispatch loop */
		fputc('\n', output_file);
	}
	while (optind < argc) {
		FILE* f;
		char* filename = argv[optind++];

		DBUG_PRINT("", ("filename=%s", filename));
		if ((f = fopen(filename, "r")) == NULL) {
			perror(filename);
			exit(EXIT_FAILURE);
		}
		fprintf(output_file, "Loading %s\n", filename);
		read_eval_print_loop(f, FALSE);
		fclose(f);
	}
	if (interactive) {
		fprintf(output_file, "Entering INTERACTIVE mode.\n");
		read_eval_print_loop(stdin, TRUE);
		fputc('\n', output_file);
		report_actor_usage(CFG);
	}
	report_cons_stats();

	DBUG_RETURN (exit(EXIT_SUCCESS), 0);
}
