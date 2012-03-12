/*
 *	dbug.c -- re-implementation of Fred Fish's DBUG package
 *
 *	Copyright 1992-1995,2008 Dale Schumacher, ALL RIGHTS RESERVED.
 */
#include <stdio.h>
#include <stdlib.h>	/* for malloc() and free() */
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <errno.h>	/* for "extern int errno;" */
#include "dbug.h"

/*
 *	dbug mode flags
 */
#define	DBUG_IS_ON	(0x0001)	/* debugging enabled */
#define	DBUG_TRACE_ON	(0x0002)	/* enter/return trace enabled */
#define	DBUG_CALLS_ON	(0x0004)	/* call-stack trace enabled */
#define	DBUG_NUMBER_ON	(0x0100)	/* number output lines */
#define	DBUG_TIME_ON	(0x0200)	/* print timestamp */
#define	DBUG_FILE_ON	(0x0400)	/* print source file name */
#define	DBUG_LINE_ON	(0x0800)	/* print source line number */
#define	DBUG_PROC_ON	(0x1000)	/* print process name */
#define	DBUG_PID_ON	(0x2000)	/* print process id */
#define	DBUG_UNIT_ON	(0x4000)	/* print unit name */
#define	DBUG_DEPTH_ON	(0x8000)	/* print call-depth */

static struct dbug_unit_t base_unit = {	/* file-level information */
	"",
	""
};

static struct dbug_ctx_t base_ctx = {	/* function-level information */
	"",
	0,
	-1,
	"",
	&base_unit,
	(struct dbug_ctx_t *)0
};

static struct dbug_cfg_t base_cfg = {	/* runtime information */
	0,
	"dbug",
	0,
	(FILE*)0/*stderr*/,
	4,
	64,
	0,
	"",
	"",
	"",
	"",
	&base_ctx,
	(struct dbug_cfg_t *)0
};

struct dbug_cfg_t *	dbug_cfg = &base_cfg;	/* current configuration */
int			dbug_on = 0;		/* shadows dbug_cfg->mode */

static void
dbug_fatal(char *fmt, ...)
{
	va_list va;
	FILE *f;
	char *p;

	f = stderr;
	p = "";
	if (dbug_cfg) {
		if (dbug_cfg->output) {
			f = dbug_cfg->output;
		}
		if (dbug_cfg->process) {
			p = dbug_cfg->process;
		}
	}
	fprintf(f, "\n%s(DBUG) FATAL: ", p);
	fflush(f);
	va_start(va, fmt);
	vfprintf(f, fmt, va);
	va_end(va);
	fputc('\n', f);
	fflush(f);
	abort();
}

static int
match_set(char *set, char *key)
{
	int sense = 1;
	int match = 1;
	int klen;
	int len;
	char *p;

	if ((set == NULL) || (*set == '\0') || (*set == ':')) {
		return 1;
	}
	if (key == NULL) {
		return 0;
	}
	match = 0;
	klen = strlen(key);
	if (*set == '^') {
		sense = !sense;
		++set;
	}
	p = set;
	for (;;) {
		while ((*p != ',') && (*p != ':') && (*p != '\0')) {
			++p;
		}
		len = (int) (p - set);
		if ((len == klen) && (strncmp(set, key, klen) == 0)) {
			match = 1;
			break;
		}
		if ((*p == ':') || (*p == '\0')) {
			break;
		}
		set = ++p;
	}
	return (match == sense);
}

static int
selected(void)
{
	return (dbug_on
	     && (dbug_cfg->ctx->depth <= dbug_cfg->maxdepth)
	     && match_set(dbug_cfg->proc_list, dbug_cfg->process)
	     && match_set(dbug_cfg->unit_list, dbug_cfg->ctx->unit->unit)
	     && match_set(dbug_cfg->fn_list, dbug_cfg->ctx->name));
}

int
dbug_keyword(char *key)
{
	return (selected() && match_set(dbug_cfg->kw_list, key));
}

static void
call_trace(struct dbug_ctx_t *ctx)
{
	if (ctx && (ctx != &base_ctx)) {
		if (ctx->caller && (ctx->caller != &base_ctx)) {
			call_trace(ctx->caller);
			fprintf(dbug_cfg->output, ",%.31s", ctx->name);
		} else {
			fprintf(dbug_cfg->output, "%.31s", ctx->name);
		}
	}
	return;
}

static void
prefix(void)
{
	if (!dbug_on) {
		return;
	}
	if (dbug_on & DBUG_NUMBER_ON) {
		fprintf(dbug_cfg->output, "%05d:", ++dbug_cfg->line_cnt);
	}
	if (dbug_on & DBUG_PROC_ON) {
		fprintf(dbug_cfg->output, "%.8s:", dbug_cfg->process);
	}
	if (dbug_on & DBUG_PID_ON) {
		fprintf(dbug_cfg->output, "%05u:", dbug_cfg->proc_id);
	}
	if (dbug_on & DBUG_TIME_ON) {
		time_t t;
		struct tm *tm;

		time(&t);
		tm = localtime(&t);
		fprintf(dbug_cfg->output, "%02d/%02d %02d.%02d.%02d:",
			tm->tm_mon + 1, tm->tm_mday,
			tm->tm_hour, tm->tm_min, tm->tm_sec);
	}
	if (dbug_on & DBUG_FILE_ON) {
		fprintf(dbug_cfg->output, "%14.14s:",
			dbug_cfg->ctx->unit->name);
	}
	if (dbug_on & DBUG_LINE_ON) {
		fprintf(dbug_cfg->output, "%-5d ", dbug_cfg->ctx->line);
	}
	if (dbug_on & DBUG_UNIT_ON) {
		fprintf(dbug_cfg->output, "%8.8s:",
			dbug_cfg->ctx->unit->unit);
	}
	if (dbug_on & DBUG_DEPTH_ON) {
		fprintf(dbug_cfg->output, "%02d ", dbug_cfg->ctx->depth);
	}
	if (dbug_on & DBUG_CALLS_ON) {
		call_trace(dbug_cfg->ctx);
		fputc(' ', dbug_cfg->output);
	} else if (dbug_on & DBUG_TRACE_ON) {
		int i;

		for (i = dbug_cfg->ctx->depth; i > 0; --i) {
			fprintf(dbug_cfg->output, "%-*s",
				dbug_cfg->indent, "|");
		}
	} else {
		fprintf(dbug_cfg->output, "%.31s ", dbug_cfg->ctx->name);
	}
	return;
}

static char *
mk_string(char *p)
{
	static char buf[256];
	char *q;
	int n;

	if (*p == ',') {			/* skip leading comma, if any */
		++p;
	}
	for (q = p; *q && (*q != ':'); ++q)	/* find the end of the token */
		;
	n = (int)(q - p);
	if (n > (sizeof(buf) - 1)) {		/* safegaurd the copy size */
		n = (sizeof(buf) - 1);
	}
	memset(buf, '\0', sizeof(buf));
	strncpy(buf, p, n);
	return buf;
}

static FILE *
open_output(char *p)
{
	FILE *f;

	f = (FILE *)0;
	p = mk_string(p);
	if (p[0] == '\0') {
		f = stderr;
	} else if ((p[0] == '-') && (p[1] == '\0')) {
		f = stdout;
	} else {
		if (p[0] == '!') {
			f = fopen(++p, "w");
		} else {
			f = fopen(p, "a");
		}
		if (f == NULL) {
			dbug_fatal("can't open output file '%s'", p);
		}
	}
	return f;
}

static int
close_output(FILE *f)
{
	int rv;

	rv = 0;
	if (f && (f != stderr) && (f != stdout)) {
		rv = fclose(f);
	}
	return rv;
}

static int
parse(char *p)
{
	int c;
#define	GET()		((*p)?(*p++):'\0')
#define	UNGET(c)	((c)?(*--p = c):'\0')
	char *q;

	if (*p == '-') {		/* command line option prefix */
		++p;
	}
	while (*p == '#') {		/* DBUG option letter */
		++p;
	}
	if (*p == '+') {		/* add to existing modes */
		++p;
	} else {
		dbug_cfg->mode = 0;
	}
	for (;;) {
		c = GET();
		switch (c) {
		case 'd':
			dbug_cfg->mode |= DBUG_IS_ON;
			dbug_cfg->kw_list = p;
			break;
		case 'o':
			fflush(dbug_cfg->output);
			dbug_cfg->output = open_output(p);
			break;
		case 't':
			dbug_cfg->mode |= DBUG_TRACE_ON;
			q = mk_string(p);
			if (*q) {
				dbug_cfg->maxdepth = atoi(q);
			}
			break;
		case 'c':
			dbug_cfg->mode |= DBUG_CALLS_ON;
			q = mk_string(p);
			if (*q) {
				dbug_cfg->maxdepth = atoi(q);
			}
			break;
		case 'p':	dbug_cfg->proc_list = p;		break;
		case 'u':	dbug_cfg->unit_list = p;		break;
		case 'f':	dbug_cfg->fn_list = p;			break;
		case 'N':	dbug_cfg->mode |= DBUG_NUMBER_ON;	break;
		case 'T':	dbug_cfg->mode |= DBUG_TIME_ON;		break;
		case 'F':	dbug_cfg->mode |= DBUG_FILE_ON;		break;
		case 'L':	dbug_cfg->mode |= DBUG_LINE_ON;		break;
		case 'P':	dbug_cfg->mode |= DBUG_PROC_ON;		break;
		case 'I':	dbug_cfg->mode |= DBUG_PID_ON;		break;
		case 'U':	dbug_cfg->mode |= DBUG_UNIT_ON;		break;
		case 'n': /* --obsolete-- */
		case 'D':	dbug_cfg->mode |= DBUG_DEPTH_ON;	break;
		case 'r':
			dbug_cfg->ctx->depth = atoi(mk_string(p));
			break;
		case 'i':
			dbug_cfg->indent = atoi(mk_string(p));
			if (dbug_cfg->indent < 1) {
				dbug_cfg->indent = 1;
			}
			break;
		case ':':
			UNGET(':');
			break;
		case '\0':
			return 0;	/* success */
		default:
			/* unrecognized option */
			dbug_fatal("unrecognized option '%c' (0x%02x)",
				c, c);
			break;
		}
		while (((c = GET()) != '\0') && (c != ':'))
			;
	}
	/* NOTREACHED */
	return 0;		/* success */
#undef GET
#undef UNGET
}

void
dbug_push(char *config)
{
	struct dbug_cfg_t *cfg;

	cfg = (struct dbug_cfg_t *)malloc(sizeof(struct dbug_cfg_t));
	if (cfg != (struct dbug_cfg_t *)0) {
		*cfg = *dbug_cfg;
		cfg->prev = dbug_cfg;
		dbug_cfg = cfg;
#if 0
		dbug_cfg->proc_list = "";
		dbug_cfg->unit_list = "";
		dbug_cfg->fn_list = "";
		dbug_cfg->kw_list = "";
#endif /* 0 */
		parse(config);
		dbug_on = dbug_cfg->mode;
		if (dbug_on) {
			prefix();
			fprintf(dbug_cfg->output,
				"DBUG_PUSH(\"%s\")\n", config);
			fflush(dbug_cfg->output);
		}
	}
	return;
}

void
dbug_pop(void)
{
	struct dbug_cfg_t *cfg;

	cfg = dbug_cfg;
	if (cfg == &base_cfg) {
		dbug_fatal("DBUG_POP stack underflow (%s:%d)",
			dbug_cfg->ctx->unit->name,
			dbug_cfg->ctx->line);
	}
	if (dbug_on) {
		prefix();
		fprintf(dbug_cfg->output, "DBUG_POP()\n");
	}
	dbug_cfg = cfg->prev;
	if (dbug_cfg->output != cfg->output) {
		close_output(cfg->output);
	}
	dbug_cfg->line_cnt = cfg->line_cnt;
	dbug_cfg->ctx = cfg->ctx;
	free(cfg);
	dbug_on = dbug_cfg->mode;
	return;
}

static void
io_error(void)
{
	dbug_fatal("i/o error on dbug output file (errno=%d)", errno);
	return;
}

void
dbug_enter(struct dbug_ctx_t *ctx)
{
	dbug_cfg->ctx = ctx;
	if (selected() && (dbug_on & DBUG_TRACE_ON)) {
		prefix();
		fprintf(dbug_cfg->output, ">%.31s\n", ctx->name);
		if (fflush(dbug_cfg->output) == EOF) {
			io_error();
		}
	}
	++ctx->depth;
	return;
}

void
dbug_print(char *fmt, ...)
{
	va_list va;

	prefix();
	fprintf(dbug_cfg->output, "%.31s: ", dbug_cfg->ctx->keyword);
	fflush(dbug_cfg->output);
	va_start(va, fmt);
	vfprintf(dbug_cfg->output, fmt, va);
	va_end(va);
	fputc('\n', dbug_cfg->output);
	if (fflush(dbug_cfg->output) == EOF) {
		io_error();
	}
	return;
}

void
dbug_return(struct dbug_ctx_t *ctx)
{
	if (ctx != dbug_cfg->ctx) {
		dbug_fatal("missing DBUG_RETURN detected in '%s' (%s:%d)",
			dbug_cfg->ctx->name,
			dbug_cfg->ctx->unit->name,
			dbug_cfg->ctx->line);
	}
	--ctx->depth;
	if (selected() && (dbug_on & DBUG_TRACE_ON)) {
		prefix();
		fprintf(dbug_cfg->output, "<%.31s\n", ctx->name);
		if (fflush(dbug_cfg->output) == EOF) {
			io_error();
		}
	}
	dbug_cfg->ctx = ctx->caller;
	return;
}

/*
 *	extra functions...
 */

void
dbug_hexdump(void *p, int n)
{
	unsigned char *bp = p;
	char asc[16 + 1];
	int i;
	int c;

	while (n > 0) {
		fprintf(dbug_cfg->output, "%p  ", bp);
		for (i = 0; i < 16; ++i) {
			if (i < n) {
				c = bp[i];
				if ((c >= ' ') && (c < 0x7F)) {
					asc[i] = c;
				} else {
					asc[i] = '.';
				}
				fprintf(dbug_cfg->output, " %02x", c);
			} else {
				asc[i] = ' ';
				fprintf(dbug_cfg->output, "   ");
			}
		}
		asc[16] = '\0';
		fprintf(dbug_cfg->output, "  |%16.16s|\n", asc);
		bp += 16;
		n -= 16;
	}
	fflush(dbug_cfg->output);
}

