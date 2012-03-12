/*
 * sbuf.h -- String buffer routines
 *
 * Copyright 2008 Dale Schumacher.  ALL RIGHTS RESERVED.
 */
#ifndef SBUF_H
#define SBUF_H

#include <stddef.h>

typedef struct string_buffer SBUF;

struct string_buffer {
	size_t		limit;
	size_t		offset;
	char*		buf;
};

SBUF*	new_sbuf(size_t size);
void	clear_sbuf(SBUF* sbuf);
SBUF*	free_sbuf(SBUF* sbuf);
void	sbuf_emit(char c, void* ctx);
void	sbuf_emits(char* s, size_t n, void* ctx);

#endif /* SBUF_H */
