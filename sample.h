/*
 *  sample.h -- Sample hand-coded Actors
 *
 * Copyright 2008 Dale Schumacher.  ALL RIGHTS RESERVED.
 */
#ifndef SAMPLE_H
#define SAMPLE_H

#include "actor.h"

extern BOOL	sample_done;

void	tick_init();
void	start_ticker(CONFIG* cfg, int count);

void	test_sample(CONFIG* cfg);

#endif /* SAMPLE_H */
