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
 *  debug.c: debugging support
 */

#include "config.h"

#if defined HAVE_STDINT_H
#   include <stdint.h>
#elif defined HAVE_INTTYPES_H
#   include <inttypes.h>
#endif
#include <stdio.h>
#include <string.h>
#if defined HAVE_UNISTD_H
#   include <unistd.h>
#endif
#if defined HAVE_IO_H
#   include <io.h>
#endif
#include <errno.h>
#include <stdarg.h>

#include "debug.h"
#include "libzzuf.h"

static void mydebug(char const *format, va_list args);

/**
 * Helper macro to write an integer value to a given file descriptor,
 * either in base 10 or in hexadecimal.
 */
#define WRITE_INT(i, base) \
    do \
    { \
        char buf[128], *b = buf + 127; \
        if (i <= 0) \
            append((i = 1 + ~i) ? "-" : "0", 1); /* XXX: hack here */ \
        if (i + 1 <= 0) \
        { \
            i = 1 + ~(i + base); /* XXX: special case for INT_MIN */ \
            *b-- = hex2char[i % base]; \
            i = i / base + 1; \
        } \
        while (i) \
        { \
            *b-- = hex2char[i % base]; \
            i /= base; \
        } \
        append(b + 1, (int)(buf + 127 - b)); \
    } while (0)

/* Temporary buffer for deferred output */
char debugbuffer[BUFSIZ];
size_t debugcount = 1;

#ifdef _WIN32

CRITICAL_SECTION _zz_pipe_cs; /* Initialized in DllMain */

void _zz_debug(char const *format, ...)
{
    va_list args;
    char buf[0x100];
    DWORD written;
    va_start(args, format);
    //if (_zz_debuglevel >= 1) // LATER:
    {
        HANDLE dbg_hdl = (HANDLE)_get_osfhandle(_zz_debugfd);
        int ret = _vsnprintf(buf, sizeof(buf), format, args);

        if (ret <= 0)
            return; /* if _snprintf failed, we send nothing */
        if (buf[0] == '\0')
            return; /* if buf is empty, we don't bother to send it to zzuf */

        /* FIXME: if len >= count, no null-terminator is appended, so we may erased the last character */
        if (ret >= sizeof(buf))
            buf[ret - 1] = '\n';
        else
            buf[ret++] = '\n';

        EnterCriticalSection(&_zz_pipe_cs);
        WriteFile(dbg_hdl, buf, ret, &written, NULL);
        LeaveCriticalSection(&_zz_pipe_cs);
    }
    va_end(args);
    fflush(NULL); /* flush all streams to make sure zzuf gotta catch 'em all */
}

void _zz_debug2(char const *format, ...)
{
    va_list args;
    char buf[0x100];
    DWORD written;
    va_start(args, format);
    //if (_zz_debuglevel >= 1) // LATER:
    {
        HANDLE dbg_hdl = (HANDLE)_get_osfhandle(_zz_debugfd);
        int ret = _vsnprintf(buf, sizeof(buf), format, args);

        if (ret <= 0)
            return; /* if _snprintf failed, we send nothing */
        if (buf[0] == '\0')
            return; /* if buf is empty, we don't bother to send it to zzuf */

        /* FIXME: if len >= count, no null-terminator is appended, so we may erased the last character */
        if (ret >= sizeof(buf)) buf[ret - 1] = '\n';
        else                    buf[ret++]   = '\n';

        EnterCriticalSection(&_zz_pipe_cs);
        WriteFile(dbg_hdl, buf, ret, &written, NULL);
        LeaveCriticalSection(&_zz_pipe_cs);
    }
    va_end(args);
    fflush(NULL); /* flush all streams to make sure zzuf gotta catch 'em all */
}
#else
void _zz_debug(char const *format, ...)
{
    va_list args;
    va_start(args, format);
    if (_zz_debuglevel >= 1)
        mydebug(format, args);
    va_end(args);
}

void _zz_debug2(char const *format, ...)
{
    va_list args;
    va_start(args, format);
    if (_zz_debuglevel >= 2)
        mydebug(format, args);
    va_end(args);
}
#endif

/**
 * Format a string, printf-like, and write the resulting data to zzuf's
 * debug file descriptor _zz_debugfd. If the debug file descriptor is
 * still -1, this function does nothing.
 *
 * This function's code is roughly equivalent to the following *printf
 * calls, except it only uses signal-safe functions:
 *  - fprintf(stderr, "** zzuf debug ** ");
 *  - vfprintf(stderr, format, args);
 *  - fprintf(stderr, "\n");
 */
static inline void append(void const *data, size_t count)
{
    if (debugcount + count <= sizeof(debugbuffer))
    {
        memcpy(debugbuffer + debugcount, data, count);
        debugcount += count;
    }
}

static void mydebug(char const *format, va_list args)
{
    static char const *hex2char = "0123456789abcdef";

    int saved_errno = errno;

    /* If there is spare data and the debug fd is open, we send the data */
    if (debugcount && _zz_debugfd >= 0)
    {
        write(_zz_debugfd, debugbuffer, debugcount);
        debugcount = 0;
    }

    append("** zzuf debug ** ", 17);
    for (char const *f = format; *f; ++f)
    {
        if (*f != '%')
        {
            append(f, 1);
            continue;
        }

        f++;
        if (!*f)
            break;

        if (*f == 'c')
        {
            char i = (char)(unsigned char)va_arg(args, int);
            if (i >= 0x20 && i < 0x7f)
                append(&i, 1);
            else if (i == '\n')
                append("\\n", 2);
            else if (i == '\t')
                append("\\t", 2);
            else if (i == '\r')
                append("\\r", 2);
            else
            {
                append("\\x", 2);
                append(hex2char + ((i & 0xf0) >> 4), 1);
                append(hex2char + (i & 0x0f), 1);
            }
        }
        else if (*f == 'i' || *f == 'd')
        {
            int i = va_arg(args, int);
            WRITE_INT(i, 10);
        }
        else if (*f == 'x')
        {
            unsigned int i = va_arg(args, unsigned int);
            WRITE_INT(i, 16);
        }
        else if (f[0] == 'l' && (f[1] == 'i' || f[1] == 'd'))
        {
            long int i = va_arg(args, long int);
            WRITE_INT(i, 10);
            f++;
        }
        else if (f[0] == 'l' && f[1] == 'l' && (f[2] == 'i' || f[1] == 'd'))
        {
            long long int i = va_arg(args, long long int);
            WRITE_INT(i, 10);
            f += 2;
        }
        else if (f[0] == 'g')
        {
            double g = va_arg(args, double), h = 0.0000001;
            int i = (int)g;
            WRITE_INT(i, 10);
            for (i = 0; i < 7; ++i)
            {
                g = (g - (int)g) * 10;
                h *= 10;
                if (g < h)
                    break;
                if (i == 0)
                    append(".", 1);
                append(hex2char + (int)g, 1);
            }
        }
        else if (f[0] == 'p')
        {
            uintptr_t i = va_arg(args, uintptr_t);
            if (!i)
                append("NULL", 4);
            else
            {
                append("0x", 2);
                WRITE_INT(i, 16);
            }
        }
        else if (f[0] == 's')
        {
            char *s = va_arg(args, char *);
            if (!s)
                append("(nil)", 5);
            else
            {
                int l = 0;
                while (s[l])
                    l++;
                append(s, l);
            }
        }
        else if (f[0] == 'S')
        {
            uint16_t *s = va_arg(args, uint16_t *);
            if (!s)
                append("(nil)", 5);
            else
            {
                int l = 0;
                while (s[l])
                {
                    if (s[l] < 128)
                    {
                        char tmp = (char)s[l];
                        append(&tmp, 1);
                    }
                    else
                    {
                        append("\\u", 2);
                        append(hex2char + ((s[l] & 0xf000) >> 12), 1);
                        append(hex2char + ((s[l] & 0xf00) >> 8), 1);
                        append(hex2char + ((s[l] & 0xf0) >> 4), 1);
                        append(hex2char + (s[l] & 0xf), 1);
                    }
                    l++;
                }
            }
        }
        else if (f[0] == '0' && f[1] == '2' && f[2] == 'x')
        {
            int i = va_arg(args, int);
            append(hex2char + ((i & 0xf0) >> 4), 1);
            append(hex2char + (i & 0x0f), 1);
            f += 2;
        }
        else
        {
            append(f - 1, 2);
        }
    }
    append("\n", 1);

    /* If the debug fd is open, we send the data */
    if (_zz_debugfd >= 0)
    {
        write(_zz_debugfd, debugbuffer, debugcount);
        debugcount = 0;
    }

    errno = saved_errno;
}

