/*
 * Very simple but very effective user-space memory tester.
 * Originally by Simon Kirby <sim@stormix.com> <sim@neato.org>
 * Version 2 by Charles Cazabon <charlesc-memtester@pyropus.ca>
 * Version 3 not publicly released.
 * Version 4 rewrite:
 * Copyright (C) 2004-2020 Charles Cazabon <charlesc-memtester@pyropus.ca>
 * Licensed under the terms of the GNU General Public License version 2 (only).
 * See the file COPYING for details.
 *
 * This file contains typedefs, structure, and union definitions.
 *
 */

#include "sizes.h"

#ifdef __VMS
typedef unsigned long long ul;
#else
typedef unsigned long ul;
#endif
typedef unsigned long long ull;
#ifdef __VMS
typedef unsigned long long volatile ulv;
#else
typedef unsigned long volatile ulv;
#endif
typedef unsigned char volatile u8v;
typedef unsigned short volatile u16v;

struct test {
    char *name;
    int (*fp)();
};
