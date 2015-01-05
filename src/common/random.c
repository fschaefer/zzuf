/*
 *  zzuf - general purpose fuzzer
 *
 *  Copyright © 2002—2015 Sam Hocevar <sam@hocevar.net>
 *
 *  This program is free software. It comes without any warranty, to
 *  the extent permitted by applicable law. You can redistribute it
 *  and/or modify it under the terms of the Do What the Fuck You Want
 *  to Public License, Version 2, as published by the WTFPL Task Force.
 *  See http://www.wtfpl.net/ for more details.
 */

/*
 *  random.c: pseudorandom number generator
 */

#include "config.h"

#if defined HAVE_STDINT_H
#   include <stdint.h>
#elif defined HAVE_INTTYPES_H
#   include <inttypes.h>
#endif
#include <stdlib.h>

#include "random.h"

static unsigned long ctx = 1;

void _zz_srand(uint32_t seed)
{
    ctx = (seed ^ 0x12345678);
}

uint32_t _zz_rand(uint32_t max)
{
    /* Could be better, but do we care? */
    long hi, lo, x;

    hi = ctx / 12773L;
    lo = ctx % 12773L;
    x = 16807L * lo - 2836L * hi;
    if (x <= 0)
        x += 0x7fffffffL;
    return (ctx = x) % (unsigned long)max;
}

