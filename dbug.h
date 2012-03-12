/*
 *	dbug.h -- definitions for re-implementation of Fred Fish's DBUG package
 *
 *	Copyright 1992-1995,2008 Dale Schumacher, ALL RIGHTS RESERVED.
 */
#ifndef DBUG_H_
#define	DBUG_H_

#include <stdio.h>	/* for FILE, stderr, etc... */
#include <unistd.h>	/* for getpid() */

struct dbug_cfg_t {	/* runtime information */
	int			mode;		/* dbug mode settings */
	char *			process;	/* process name */
	int			proc_id;	/* process id */
	FILE *			output;		/* output stream */
	int			indent;		/* indent depth */
	int			maxdepth;	/* trace depth limit */
	int			line_cnt;	/* output line counter */
	char *			proc_list;	/* enabled process list */
	char *			unit_list;	/* enabled unit list */
	char *			fn_list;	/* enabled function list */
	char *			kw_list;	/* enabled keyword list */
	struct dbug_ctx_t *	ctx;		/* current context pointer */
	struct dbug_cfg_t *	prev;		/* previous configuration */
};

struct dbug_unit_t {	/* file-level information */
	char *			name;		/* source file name */
	char *			unit;		/* unit name */
};

struct dbug_ctx_t {	/* function-level information */
	char *			name;		/* function name */
	int			depth;		/* call-nesting depth */
	int			line;		/* source line number */
	char *			keyword;	/* activation keyword */
	struct dbug_unit_t *	unit;		/* current unit pointer */
	struct dbug_ctx_t *	caller;		/* calling context pointer */
};

extern int dbug_on;		/* TRUE if debug currently enabled */

#ifdef DBUG_OFF

#define DBUG_ENTER(NAME)
#define DBUG_LEAVE
#define DBUG_RETURN return
#define DBUG_EXECUTE(KW,CODE)
#define DBUG_PRINT(KW,ARGS)
#define DBUG_PUSH(CFG)
#define DBUG_POP()
#define DBUG_PROCESS(NAME)
#define DBUG_UNIT(NAME)
#define DBUG_FILE (stderr)
#define DBUG_HEXDUMP(PTR,CNT)

#else /* DBUG_OFF */

extern struct dbug_cfg_t *	dbug_cfg;	/* current configuration */

int	dbug_keyword(char *);			/* accept/reject keyword */
void	dbug_push(char *);			/* push state, set new state */
void	dbug_pop(void);				/* pop previous debug state */
void	dbug_enter(struct dbug_ctx_t *ctx);  	/* user function entered */
void	dbug_return(struct dbug_ctx_t *ctx);	/* user function return */
void	dbug_print(char *, ...);		/* print debug output */
void	dbug_hexdump(void *p, int n);		/* hexdump buffer contents */

#define DBUG_ENTER(NAME) struct dbug_ctx_t dbug_ctx;\
	dbug_ctx.name = (NAME);\
	dbug_ctx.depth = dbug_cfg->ctx->depth;\
	dbug_ctx.line = __LINE__;\
	dbug_ctx.keyword = "";\
	dbug_ctx.unit = &dbug_unit;\
	dbug_ctx.caller = dbug_cfg->ctx;\
	dbug_enter (&dbug_ctx)
#define DBUG_RETURN \
	dbug_ctx.line = __LINE__;\
	dbug_return (&dbug_ctx);\
	return
#define DBUG_EXECUTE(KW,CODE) do{\
	if(dbug_on && dbug_keyword (KW)){\
		CODE;\
	}}while(0)
#define DBUG_PRINT(KW,ARGS) do{\
	if(dbug_on && dbug_keyword (dbug_cfg->ctx->keyword = (KW))){\
		dbug_cfg->ctx->line = __LINE__;\
		dbug_print ARGS;\
	}}while(0)
#define DBUG_PUSH(CFG) do{\
	dbug_cfg->ctx->line = __LINE__;\
	dbug_push (CFG);\
	}while(0)
#define DBUG_POP() do{\
	dbug_cfg->ctx->line = __LINE__;\
	dbug_pop ();\
	}while(0)
#define DBUG_PROCESS(NAME) (\
	dbug_cfg->output = (dbug_cfg->output ? dbug_cfg->output : stderr),\
	dbug_cfg->proc_id = (int)getpid(),\
	dbug_cfg->process = (NAME))
#define	DBUG_UNIT(NAME) static struct dbug_unit_t dbug_unit =\
	{__FILE__, (NAME)}
#define DBUG_FILE (dbug_cfg->output)
#define	DBUG_HEXDUMP(PTR,CNT) do{\
	if(dbug_on){\
		dbug_hexdump (PTR,CNT);\
	}}while(0)

#endif /* DBUG_OFF */

#ifdef XDBUG
#define XDBUG_ENTER(NAME)	DBUG_ENTER(NAME)
#define XDBUG_PRINT(KW,ARGS)	DBUG_PRINT(KW,ARGS)
#define XDBUG_RETURN		DBUG_RETURN
#else /* XDBUG */
#define XDBUG_ENTER(NAME)
#define XDBUG_PRINT(KW,ARGS)
#define XDBUG_RETURN		return
#endif /* XDBUG */

#endif /* DBUG_H_ */
