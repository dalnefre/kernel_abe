/*
 * kernel.h -- An actor-based implementation of John Shutt's "Kernel" language
 *
 * Copyright 2012 Dale Schumacher.  ALL RIGHTS RESERVED.
 */
#ifndef KERNEL_H
#define KERNEL_H

#include "abe.h"

#define	pr(h,t)			cons((h), (t))
#define	is_pr(p)		(consp(p) && !nilp(p) && !actorp(p))
#define	hd(p)			car(p)
#define	tl(p)			cdr(p)

CONS*	str_to_seq(char* s);	/* create sequence of numbers from string */
char*	seq_to_buf(char* s, int n, CONS* q);
char*	seq_to_str(CONS* q);	/* WARNING! must free this storage manually */

void	run_test_config(CONFIG* cfg, int limit);	/* test dispatch loop */

#endif /* KERNEL_H */
