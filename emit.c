/*
 * emit.c -- Character-stream i/o for cons cells
 *
 * NOTE: THESE PROCEDURES ARE INTENDED FOR DEBUGGING ONLY
 *       THEIR IMPLEMENTATION IS EXPEDIENT AND UGLY!
 *
 * Copyright 2008 Dale Schumacher.  ALL RIGHTS RESERVED.
 */
#include "emit.h"
#include "sbuf.h"
#include "abe.h"

#include "dbug.h"
DBUG_UNIT("emit");

#define	is_reserved(c)	(((c)=='(') || ((c)==')') || ((c)==':') || ((c)=='"') || ((c)=='\''))
/*
#define	is_reserved(c)	(((c)=='(') || ((c)==')') || ((c)==',') || ((c)=='"') || ((c)=='#'))
*/
#define REDUCE_TOKEN_BRKS	" \t\r\n\b():'\""
#define HUMUS_TOKEN_BRKS	" \t\r\n\b(),#\""

static SBUF* cons_sbuf = NULL;

#define	CONS_BUFSZ	1024
#define	CHILD_DEPTH	3
#define	TAIL_LENGTH	6

static void	depth_to_str(int depth, char* buf, CONS* cons);	/* FORWARD */

static void
child_to_str(int depth, char* buf, CONS* cons)
{
	char tmp[CONS_BUFSZ];
	int n;
	
	XDBUG_ENTER("child_to_str");
	n = strlen(buf);
	XDBUG_PRINT("", ("n = %d", n));
	if (n < (CONS_BUFSZ - 4)) {
		depth_to_str(depth, tmp, cons);
		strncpy(buf + n, tmp, (CONS_BUFSZ - n));
/*		buf[CONS_BUFSZ - 1] = '\0'; */
		strcpy((buf + CONS_BUFSZ - 4), ":::");
	}
	XDBUG_RETURN;
}

static void
tail_to_str(int depth, int length, char* buf, CONS* cons)
{
	int n;

	XDBUG_ENTER("tail_to_str");
#if 0
	child_to_str((depth - 1), buf, car(cons));
	n = strlen(buf);
	if (n < (CONS_BUFSZ - 3)) {
		buf[n++] = ',';
		buf[n++] = ' ';
		buf[n] = '\0';
	}
	child_to_str((depth - 1), buf, cdr(cons));
#else
	if (length < 0) {			/* length limit */
		n = strlen(buf);
		if (n < (CONS_BUFSZ - 4)) {
			buf[n++] = '.';
			buf[n++] = '.';
			buf[n++] = '.';
			buf[n] = '\0';
		}
	} else {
		child_to_str((depth - 1), buf, car(cons));
		n = strlen(buf);
		if (n < (CONS_BUFSZ - 3)) {
			buf[n++] = ',';
			buf[n++] = ' ';
			buf[n] = '\0';
		}
		cons = cdr(cons);	/* advance to tail */
		if (nilp(cons) || actorp(cons) || !consp(cons)) {
			child_to_str((depth - 1), buf, cons);
		} else {
			tail_to_str(depth, (length - 1), buf, cons);
		}
	}
#endif
	XDBUG_RETURN;
}

char*
cons_to_str(CONS* cons)		/* warning: returns pointer to static buffer, do not nest calls! */
{
	static char buf[CONS_BUFSZ];

	XDBUG_ENTER("cons_to_str");
	depth_to_str(CHILD_DEPTH, buf, cons);
	XDBUG_RETURN buf;
}

static void
depth_to_str(int depth, char* buf, CONS* cons)
{
	XDBUG_ENTER("depth_to_str");
	XDBUG_PRINT("", ("depth = %d", depth));
	strncpy(buf, "", CONS_BUFSZ);	/* zero buffer */
	if (nilp(cons)) {
		strcpy(buf, "NIL");
	} else if (boolp(cons)) {
		strcpy(buf, (cons ? "TRUE" : "FALSE"));
	} else if (atomp(cons)) {
		char *q = atom_str(cons);
		int n = 0;
		int qtd = q[strcspn(q, HUMUS_TOKEN_BRKS)]; /* look for special chars */

		buf[n++] = '#';
		if (qtd) {
			buf[n++] = '"';
		}
		if (strlen(q) > 249) {
			sprintf((buf + n), "%.249s...", q);
		} else {
			strcpy((buf + n), q);
		}
		if (qtd) {
			n = strlen(buf);
			buf[n++] = '"';
			buf[n] = '\0';
		}
	} else if (numberp(cons)) {
		sprintf(buf, "%d", MK_INT(cons));
#if NUMBER_IS_FUNC
#else
	} else if (funcp(cons)) {
		sprintf(buf, "^%lx", (ulint)MK_PTR(cons));
#endif
	} else if (depth < 0) {			/* depth limit */
		strcpy(buf, "...");
	} else if (actorp(cons)) {
		int n = 0;

		XDBUG_PRINT("", ("actor = @%lx[^%lx, 16#%08lx]", 
			(ulint)cons, (ulint)_THIS(cons), (ulint)_MINE(cons)));
		sprintf(buf, "@%lx[^%lx, ", (ulint)cons, (ulint)_THIS(cons));
		child_to_str((depth - 1), buf, _MINE(cons));
		n = strlen(buf);
		if (n < (CONS_BUFSZ - 2)) {
			buf[n++] = ']';
			buf[n] = '\0';
		}
	} else if (consp(cons)) {
		int n = 0;

		XDBUG_PRINT("", ("cons = (16#%08lx, 16#%08lx)", 
			(ulint)car(cons), (ulint)cdr(cons)));
		buf[n++] = '(';
		buf[n] = '\0';
		tail_to_str(depth, TAIL_LENGTH, buf, cons);
		n = strlen(buf);
		if (n < (CONS_BUFSZ - 2)) {
			buf[n++] = ')';
			buf[n] = '\0';
		}
	} else {
		sprintf(buf, "16#%08lx", (ulint)cons);
	}
	XDBUG_PRINT("", ("buf = %s", buf));
	XDBUG_RETURN;
}

#define	ASSERT_CONS_TO_STR(value, expect, actual) \
	actual = cons_to_str(value); \
	DBUG_PRINT("", ("%s = %s", expect, actual)); \
	assert(strcmp(expect, actual) == 0)

void
test_cons_to_str()
{
	CONS* value;
	char* actual;
	char expect[256];

	DBUG_ENTER("test_cons_to_str");
	TRACE(printf("--test_cons_to_str--\n"));

	ASSERT_CONS_TO_STR(NIL, "NIL", actual);

	ASSERT_CONS_TO_STR(TRUE, "TRUE", actual);

	ASSERT_CONS_TO_STR(FALSE, "FALSE", actual);

	ASSERT_CONS_TO_STR(ATOM("TRUE"), "#TRUE", actual);

	ASSERT_CONS_TO_STR(ATOM("#x,y"), "#\"#x,y\"", actual);

	ASSERT_CONS_TO_STR(NUMBER(0), "0", actual);

	ASSERT_CONS_TO_STR(NUMBER(1), "1", actual);

	ASSERT_CONS_TO_STR(NUMBER(-1), "-1", actual);

	value = MK_FUNC(test_cons_to_str);
#if NUMBER_IS_FUNC
	sprintf(expect, "%d", (int)MK_PTR(value));
#else
	sprintf(expect, "^%lx", (ulint)MK_PTR(value));
#endif
	ASSERT_CONS_TO_STR(value, expect, actual);

	value = CFG_ACTOR(NULL, sink_beh, NIL);
	sprintf(expect, "@%lx[^%lx, NIL]", (ulint)value, (ulint)sink_beh);
	ASSERT_CONS_TO_STR(value, expect, actual);
#if 0
	rplacd(value, value);  /* FIXME: CIRCULAR LINK FORCED FOR TESTING */
	ASSERT_CONS_TO_STR(value, expect, actual);
#endif

	ASSERT_CONS_TO_STR(cons(NUMBER(0), NIL), "(0, NIL)", actual);

	value = cons(
		FALSE, 
		cons(
			cons(
				ATOM("x"),
				NUMBER(0)
			),
			NIL
		)
	);
	ASSERT_CONS_TO_STR(value, "(FALSE, (#x, 0), NIL)", actual);

	DBUG_RETURN;
}

char*
xcons_to_str(CONS* cons)		/* warning: returns pointer to static buffer, do not nest calls! */
{
	XDBUG_ENTER("xcons_to_str");
	if (cons == NULL) {
		XDBUG_RETURN "<NULL>";
	}
	if (cons_sbuf == NULL) {
		cons_sbuf = new_sbuf(1024);		/* allocate 1K string buffer */
	}
	clear_sbuf(cons_sbuf);
	emit_cons(cons, 0, sbuf_emit, cons_sbuf);
	XDBUG_RETURN cons_sbuf->buf;
}

void
file_emit(char c, void* ctx)
{
	FILE* f = (FILE*)ctx;
	int e = fputc(c, f);
	assert(c == e);
}

static int emit_depth = 0;
#define	EMIT_DEPTH_LIMIT	6
#define	EMIT_LENGTH_LIMIT	9

void
emit_cons(CONS* cons, int indent, void (*emit)(char c, void* ctx), void* ctx)
{
	char buf[256];
	int emit_length = 0;
	int i;
	char c;
	char *p;

	XDBUG_ENTER("emit_cons");
	++emit_depth;
	if (boolp(cons)) {
		sprintf(buf, cons ? "-T-" : "-F-");
	} else if (nilp(cons)) {
		sprintf(buf, "NIL");
	} else if (atomp(cons)) {
		char *s = atom_str(cons);
		int qtd = s[strcspn(s, REDUCE_TOKEN_BRKS)]; /* look for special chars */

		if (qtd) {
			(*emit)('"', ctx);
		}
		if (strlen(s) > 250) {
			sprintf(buf, "%.250s...", s);
		} else {
			strncpy(buf, s, 255);
		}
		if (qtd) {
			int n = strlen(buf);

			buf[n++] = '"';
			buf[n] = '\0';
		}
	} else if (numberp(cons)) {
		sprintf(buf, "%d", MK_INT(cons));
#if NUMBER_IS_FUNC
#else
	} else if (funcp(cons)) {
		sprintf(buf, "@%p", MK_PTR(cons));
#endif
	} else if (consp(cons)) {
		if (emit_depth > EMIT_DEPTH_LIMIT) {
			sprintf(buf, "[%p;%p]", (void*)car(cons), (void*)cdr(cons));
		} else {
			(*emit)('(', ctx);
			for (emit_length = 0; emit_length < EMIT_LENGTH_LIMIT; ++emit_length) {
				emit_cons(car(cons), indent, emit, ctx);
				cons = cdr(cons);
				if (nilp(cons)) {
					break;
				} else if (!consp(cons)) {
					(*emit)(' ', ctx);
					(*emit)(':', ctx);
					(*emit)(' ', ctx);
					emit_cons(cons, indent, emit, ctx);
					break;
				}
				if (indent) {
					(*emit)('\n', ctx);
					for (i = 0; i < emit_depth; ++i) {
						(*emit)(' ', ctx);
					}
				} else {
					(*emit)(' ', ctx);
				}
			}
			sprintf(buf, "%s)", (emit_length >= EMIT_LENGTH_LIMIT) ? "... " : "");
		}
	} else {
		sprintf(buf, "#%p", (void*)cons);
	}
	/* tie off the buffer and safely stream it out one character at a time */
	buf[255] = '\0';
	p = buf;
	while ((c = *p++) != '\0') {
		(*emit)(c, ctx);
	}
	--emit_depth;
	XDBUG_RETURN;
}

/*
static void
no_op()
{
}
*/

void	test_str_to_cons();		/* FORWARD */

void
test_emit()
{
	char* s;
	char tmp[256];
	SBUF* sbuf = new_sbuf(1024);	/* allocate 1K string buffer */
	CONS* zero_atom = ATOM("zero");
	CONS* zero_list = cons(NUMBER(0), NIL);
	CONS* list;

	DBUG_ENTER("test_emit");
	TRACE(printf("--test_emit--\n"));

	clear_sbuf(sbuf);
	emit_cons(TRUE, 0, sbuf_emit, sbuf);
	DBUG_PRINT("", ("TRUE = %s", sbuf->buf));
	assert(strcmp("-T-", sbuf->buf) == 0);
	
	clear_sbuf(sbuf);
	emit_cons(FALSE, 0, sbuf_emit, sbuf);
	DBUG_PRINT("", ("FALSE = %s", sbuf->buf));
	assert(strcmp("-F-", sbuf->buf) == 0);
	
	clear_sbuf(sbuf);
	emit_cons(NIL, 0, sbuf_emit, sbuf);
	DBUG_PRINT("", ("NIL = %s", sbuf->buf));
	assert(strcmp("NIL", sbuf->buf) == 0);
	
	clear_sbuf(sbuf);
	emit_cons(zero_atom, 0, sbuf_emit, sbuf);
	DBUG_PRINT("", ("zero = %s", sbuf->buf));
	assert(strcmp("zero", sbuf->buf) == 0);
	
	clear_sbuf(sbuf);
	emit_cons(zero_list, 0, sbuf_emit, sbuf);
	DBUG_PRINT("", ("(0) = %s", sbuf->buf));
	assert(strcmp("(0)", sbuf->buf) == 0);
	
	clear_sbuf(sbuf);
	emit_cons(cons(NIL, NIL), 1, sbuf_emit, sbuf);
	DBUG_PRINT("", ("(NIL) = %s", sbuf->buf));
	assert(strcmp("(NIL)", sbuf->buf) == 0);
	
	clear_sbuf(sbuf);
	emit_cons(cons(zero_atom, NUMBER(0)), 1, sbuf_emit, sbuf);
	DBUG_PRINT("", ("(zero . 0) = %s", sbuf->buf));
	assert(strcmp("(zero : 0)", sbuf->buf) == 0);
	
	clear_sbuf(sbuf);
	emit_cons(cons(zero_atom, zero_list), 1, sbuf_emit, sbuf);
	XDBUG_PRINT("", ("\n(zero\n 0) = \n%s\n", sbuf->buf));
	assert(strcmp("(zero\n 0)", sbuf->buf) == 0);

	clear_sbuf(sbuf);
	emit_cons(cons(zero_atom, zero_list), 0, sbuf_emit, sbuf);
	DBUG_PRINT("", ("(zero 0) = %s", sbuf->buf));
	assert(strcmp("(zero 0)", sbuf->buf) == 0);

	clear_sbuf(sbuf);
	emit_cons(cons(NUMBER(0), MK_FUNC(test_emit)), 0, sbuf_emit, sbuf);
#if NUMBER_IS_FUNC
	sprintf(tmp, "(0 : %d)", (int)test_emit);
#else
	sprintf(tmp, "(0 : @%p)", (void*)(ulint)test_emit);
#endif
	DBUG_PRINT("", ("%s = %s", tmp, sbuf->buf));
	assert(strcmp(tmp, sbuf->buf) == 0);

	list = ATOM("@ (:.)");
	s = xcons_to_str(list);
	DBUG_PRINT("", ("\"@ (:.)\" = %s", s));
	assert(strcmp("\"@ (:.)\"", s) == 0);
	
	list = ATOM("'x:y");
	s = xcons_to_str(list);
	DBUG_PRINT("", ("\"'x:y\" = %s", s));
	assert(strcmp("\"'x:y\"", s) == 0);
	
	list = NIL;
	list = cons(NUMBER(0), list);
	list = cons(NUMBER(1), list);
	list = cons(NUMBER(2), list);
	s = xcons_to_str(list);
	DBUG_PRINT("", ("(2 1 0) = %s", s));
	assert(strcmp("(2 1 0)", s) == 0);
	
	list = cons(zero_list, NIL);
	s = xcons_to_str(list);
	DBUG_PRINT("", ("((0)) = %s", s));
	assert(strcmp("((0))", s) == 0);
	
	list = map_put(NIL, zero_atom, NUMBER(0));
	s = xcons_to_str(list);
	DBUG_PRINT("", ("((zero : 0)) = %s", s));
	assert(strcmp("((zero : 0))", s) == 0);
	
	list = NIL;
	list = cons(cons(zero_atom, NUMBER(0)), list);
	list = cons(cons(ATOM("one"), NUMBER(1)), list);
	s = xcons_to_str(list);
	DBUG_PRINT("", ("((one : 1) (zero : 0)) = %s", s));
	assert(strcmp("((one : 1) (zero : 0))", s) == 0);
	
	list = NIL;
	list = map_put(list, zero_atom, NUMBER(0));
	list = map_put(list, ATOM("one"), NUMBER(1));
	s = xcons_to_str(list);
	DBUG_PRINT("", ("((one : 1) (zero : 0)) = %s", s));
	assert(strcmp("((one : 1) (zero : 0))", s) == 0);

	sbuf = free_sbuf(sbuf);
	test_cons_to_str();		/* chain to new tests */
	test_str_to_cons();		/* chain to reverse-direction tests */
	DBUG_RETURN;
}

/*** FIXME: this out-of-band communication is a MAJOR HACK!! ***/
static char*	nextptr;	/* pointer to next parse location from str_to_cons() */

CONS*
str_to_cons(char* in)
{
	int state = 1;
	int c;
	char* tk;
	int n;
	CONS* out = NIL;
	char buf[256];
	
	XDBUG_ENTER("str_to_cons");
	assert(in != NULL);
	strncpy(buf, "", sizeof(buf));
	tk = in;
	XDBUG_PRINT("", ("in=%s", in));
	while (state) {
		c = *in;			/* get character */
		XDBUG_PRINT("", ("state=%d c=%d", state, c));
		XDBUG_PRINT("", ("in=%s", in));
		XDBUG_PRINT("", ("tk=%s", tk));
		switch (state) {
		case 1:				/* initial state (optional whitespace) */
			if (c == '\0') {
				state = -1;		/* fail */
			} else if (isspace(c)) {
				state = 1;
			} else if (isdigit(c) || (c == '-')) {
				tk = in;
				n = ((c == '-') ? 0 : (c - '0'));
				state = 2;
			} else if (isprint(c) && !is_reserved(c)) {
				tk = in;
				state = 3;
			} else if (c == '"') {
				tk = in;
				state = 4;
			} else if (c == '(') {
				CONS* q = cons(NIL, NIL);
				CONS* p = NIL;

				XDBUG_PRINT("", ("list begin"));
				++in;		/* consume character */
				for (;;) {
					while (isspace(*in)) {
						++in;		/* consume character */
					}
					if (*in == ')') {
						++in;		/* consume character */
						XDBUG_PRINT("", ("list end"));
						break;
					} else if (*in == ':') {
						XDBUG_PRINT("", ("pair split"));
						++in;		/* consume character */
						p = str_to_cons(in);
						if (p == NULL) {
							break;
						}
						CQ_PUT(q, p);
						in = nextptr;
						while (isspace(*in)) {
							++in;		/* consume character */
						}
						if (*in == ')') {
							++in;		/* consume character */
							XDBUG_PRINT("", ("pair end"));
							break;
						} else {
							p = NULL;
							break;
						}
					}
					p = str_to_cons(in);
					if (p == NULL) {
						break;
					}
					CQ_PUT(q, cons(p, NIL));
					in = nextptr;
				}
				out = CQ_PEEK(q);
				state = ((p == NULL) ? -1 : 0);		/* done */
			} else if (c == '\'') {
				CONS* p;

				XDBUG_PRINT("", ("quoted literal"));
				++in;		/* consume character */
				p = str_to_cons(in);
				if (p != NULL) {
					out = cons(ATOM("literal"), cons(p, NIL));
					in = nextptr;
					state = 0;		/* done */
				} else {
					state = -1;		/* fail */
				}
			} else {
				state = -1;		/* fail */
			}
			break;
		case 2:				/* numeric token */
			if ((c == '\0') || isspace(c) || is_reserved(c)) {
				if (*tk == '-') {
					if ((in - tk) > 1) {
						n = -n;
					} else {
						--in;		/* push back charactrer */
						state = 3;	/* re-interpret as an atom */
						break;
					}
				}
				out = NUMBER(n);
				state = 0;		/* done */
			} else if (isdigit(c)) {
				n = (n * 10) + (c - '0');
				state = 2;
			} else if (isprint(c)) {
				state = 3;
			} else {
				state = -1;		/* fail */
			}
			break;
		case 3:				/* atomic token */
			if ((c == '\0') || isspace(c) || is_reserved(c)) {
				if ((in - tk) < sizeof(buf)) {
					strncpy(buf, tk, (in - tk));
					XDBUG_PRINT("", ("ATOM(%s) c=%d", buf, c));
					out = ATOM(buf);
					if (out == ATOM("NIL")) {
						out = NIL;
					}
					state = 0;		/* done */
				} else {
					DBUG_PRINT("", ("token buffer overflow! %d >= %d", (in - tk), sizeof(buf)));
					state = -1;
				}
			} else if (isprint(c)) {
				state = 3;
			} else {
				state = -1;		/* fail */
			}
			break;
		case 4:				/* quoted token */
			if ((c == '\0') || !isprint(c)) {
				state = -1;		/* fail */
			} else if (c == '"') {
				if (*tk != '"') {
					state = -1;
				} else if ((in - tk) < sizeof(buf)) {
					++tk;	/* skip leading quote */
					strncpy(buf, tk, (in - tk));
					XDBUG_PRINT("", ("ATOM(\"%s\") c=%d", buf, c));
					out = ATOM(buf);
					if (out == ATOM("NIL")) {
						out = NIL;
					}
					++in;	/* skip trailing quote */
					state = 0;		/* done */
				} else {
					DBUG_PRINT("", ("token buffer overflow! %d >= %d", (in - tk), sizeof(buf)));
					state = -1;
				}
			} else {
				state = 4;
			}
			break;
		default:
			DBUG_PRINT("", ("PARSE FAIL -->%s", in));
			TRACE(printf("PARSE FAIL -->%s\n", in));
			XDBUG_RETURN NULL;	/* parse error */
		}
		if (state >= 0) {
			nextptr = in;
			++in;				/* move to next character */
		}
	}
	XDBUG_PRINT("", ("out=%s", cons_to_str(out)));
	XDBUG_PRINT("", ("next=%s", nextptr));
	XDBUG_RETURN out;
}

void
test_str_to_cons()
{
	CONS* p;
	CONS* q;
	char buf[8];

	DBUG_ENTER("test_str_to_cons");
	TRACE(printf("--test_str_to_cons--\n"));

	p = str_to_cons("0");
	assert(p != NULL);
	assert(numberp(p));
	assert(p == NUMBER(0));

	strncpy(buf, "T", sizeof(buf));
	p = str_to_cons(buf);
	assert(p != NULL);
	assert(atomp(p));
	assert(p == ATOM("T"));

	p = str_to_cons("zero");
	assert(p != NULL);
	assert(atomp(p));
	assert(p == ATOM("zero"));
	
	p = str_to_cons("NIL");
	assert(p != NULL);
	assert(nilp(p));

	p = str_to_cons("-");
	assert(p != NULL);
	assert(atomp(p));
	assert(p == ATOM("-"));
	
	p = str_to_cons("-1");
	assert(p != NULL);
	assert(numberp(p));
	assert(p == NUMBER(-1));

	p = str_to_cons("-0_!?");
	assert(p != NULL);
	assert(atomp(p));
	assert(p == ATOM("-0_!?"));
	
	p = str_to_cons("\"@ (:.)\"");
	assert(p != NULL);
	assert(atomp(p));
	assert(p == ATOM("@ (:.)"));
	
	p = str_to_cons("(0)");
	assert(p != NULL);
	assert(consp(p));
	assert(!nilp(p));
	assert(nilp(cdr(p)));
	p = car(p);
	assert(numberp(p));
	assert(p == NUMBER(0));

	p = str_to_cons("(0 -1)");
	assert(p != NULL);
	q = cons(NUMBER(0), cons(NUMBER(-1), NIL));
	assert(equal(p, q));

	p = str_to_cons("(0 (42 -1))");
	DBUG_PRINT("", ("p=%s", cons_to_str(p)));
	assert(p != NULL);
	q = cons(NUMBER(42), cons(NUMBER(-1), NIL));
	q = cons(NUMBER(0), cons(q, NIL));
	assert(equal(p, q));
	
	p = str_to_cons("()");
	assert(p != NULL);
	assert(nilp(p));
	
	p = str_to_cons("(0:1)");
	assert(p != NULL);
	assert(consp(p));
	assert(!nilp(p));
	assert(!nilp(cdr(p)));
	q = car(p);
	assert(numberp(q));
	assert(q == NUMBER(0));
	q = cdr(p);
	assert(numberp(q));
	assert(q == NUMBER(1));

	p = str_to_cons("(NAME :value)");
	assert(p != NULL);
	assert(consp(p));
	assert(!nilp(p));
	assert(!nilp(cdr(p)));
	q = car(p);
	assert(atomp(q));
	assert(q == ATOM("NAME"));
	q = cdr(p);
	assert(atomp(q));
	assert(q == ATOM("value"));

	p = str_to_cons("( actor  state: behavior )");
	DBUG_PRINT("", ("p=%s", cons_to_str(p)));
	assert(p != NULL);
	q = cons(ATOM("actor"), cons(ATOM("state"), ATOM("behavior")));
	assert(equal(p, q));

	q = cons(ATOM("ATOM"), cons(ATOM("T"), NIL));
	q = cons(ATOM("QUOTE"), cons(q, NIL));
	p = str_to_cons("(QUOTE (ATOM T))");
	assert(equal(p, q));
	p = str_to_cons("  ( QUOTE( \r\nATOM  T\t)  )");
	assert(equal(p, q));
	
	q = cons(cons(NIL, NIL), NIL);
	q = cons(cons(ATOM("ACTORS"), NIL), q);
	p = str_to_cons("((ACTORS)(()))");
	DBUG_PRINT("", ("p=%s", cons_to_str(p)));
	assert(p != NULL);
	DBUG_PRINT("", ("q=%s", cons_to_str(q)));
	assert(equal(p, q));
	
	p = str_to_cons("'ATOM");
	DBUG_PRINT("", ("p=%s", cons_to_str(p)));
	assert(p != NULL);
	q = cons(ATOM("ATOM"), NIL);
	q = cons(ATOM("literal"), q);
	assert(equal(p, q));
	
	p = str_to_cons("'(QUOTE ATOM)");
	DBUG_PRINT("", ("p=%s", cons_to_str(p)));
	assert(p != NULL);
	q = cons(ATOM("ATOM"), NIL);
	q = cons(ATOM("QUOTE"), q);
	q = cons(ATOM("literal"), cons(q, NIL));
	assert(equal(p, q));
	
	p = str_to_cons("('x 'y)");
	DBUG_PRINT("", ("p=%s", cons_to_str(p)));
	assert(p != NULL);
	q = cons(cons(ATOM("literal"), cons(ATOM("y"), NIL)), NIL);
	q = cons(cons(ATOM("literal"), cons(ATOM("x"), NIL)), q);
	assert(equal(p, q));
	
	DBUG_RETURN;
}
