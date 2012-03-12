/*
 * abe.h -- experimental ACTOR-Based Environment
 *
 * Copyright 2008-2009 Dale Schumacher.  ALL RIGHTS RESERVED.
 */
#ifndef ABE_H
#define ABE_H

#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <time.h>
#include <assert.h>
#include "types.h"
#include "gc.h"
#include "cons.h"
#include "atom.h"
#include "emit.h"
#include "actor.h"

#define	TRACE(x)	x		/* enable/disable trace statements */
#define	DEBUG(x)			/* enable/disable debug statements */

#define	NEW(T)		((T *)calloc(sizeof(T), 1))
#define	NEWxN(T,N)	((T *)calloc(sizeof(T), (N)))
#define	FREE(p)		((p) = (free(p), NULL))

CONS*			system_info();	/* return a map of system information data */

#endif /* ABE_H */
