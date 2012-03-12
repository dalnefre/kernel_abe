/*
 * sbuf.c -- String buffer routines
 *
 * Copyright 2008 Dale Schumacher.  ALL RIGHTS RESERVED.
 */
#include "sbuf.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>

SBUF*
new_sbuf(size_t size)
{
	SBUF* sbuf = (SBUF*)malloc(sizeof(SBUF));

	assert(sbuf != NULL);
	sbuf->limit = size - 1;
	sbuf->offset = 0;
	sbuf->buf = (char*)malloc(size);
	return sbuf;
}

void
clear_sbuf(SBUF* sbuf)
{
	sbuf->offset = 0;
	strncpy(sbuf->buf, "", sbuf->limit + 1);
}

SBUF*
free_sbuf(SBUF* sbuf)
{
	free(sbuf->buf);
	free(sbuf);
	return NULL;
}

void
sbuf_emit(char c, void* ctx)
{
	SBUF* sbuf = (SBUF*)ctx;
	
	if (sbuf->offset < sbuf->limit) {
		sbuf->buf[sbuf->offset] = c;
		++sbuf->offset;
		sbuf->buf[sbuf->offset] = '\0';
	}
}

void
sbuf_emits(char* s, size_t n, void* ctx)
{
	char c;
	SBUF* sbuf = (SBUF*)ctx;
	
	if ((sbuf->offset + n) < sbuf->limit) {
		while ((c = *s++) && (n-- > 0)) {
			sbuf->buf[sbuf->offset] = c;
			++sbuf->offset;
		}
		sbuf->buf[sbuf->offset] = '\0';
	}
}
