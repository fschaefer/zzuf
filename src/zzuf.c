/*
 *  zzuf - general purpose fuzzer
 *
 *  Copyright © 2002—2015 Sam Hocevar <sam@hocevar.net>
 *              2012 Kévin Szkudłapski <kszkudlapski@quarkslab.com>
 *
 *  This program is free software. It comes without any warranty, to
 *  the extent permitted by applicable law. You can redistribute it
 *  and/or modify it under the terms of the Do What the Fuck You Want
 *  to Public License, Version 2, as published by the WTFPL Task Force.
 *  See http://www.wtfpl.net/ for more details.
 */

/*
 *  main.c: main program
 */

#include "config.h"

#define _INCLUDE_POSIX_SOURCE /* for STDERR_FILENO on HP-UX */

#if defined HAVE_STDINT_H
#   include <stdint.h>
#elif defined HAVE_INTTYPES_H
#   include <inttypes.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#if defined HAVE_UNISTD_H
#   include <unistd.h> /* for read(), write(), close() */
#endif
#if defined HAVE_REGEX_H
#   if _WIN32
#       include "com_regexp.hpp"
#   else
#       include <regex.h>
#   endif
#endif
#if defined HAVE_WINSOCK2_H
#   include <winsock2.h> /* for fd_set */
#endif
#if defined HAVE_IO_H
#   include <io.h>
#endif
#include <string.h>
#include <errno.h>
#include <signal.h>
#if defined HAVE_SYS_TIME_H
#   include <sys/time.h>
#endif
#if defined HAVE_SYS_WAIT_H
#   include <sys/wait.h>
#endif
#if defined HAVE_SYS_RESOURCE_H
#   include <sys/resource.h> /* for RLIMIT_AS */
#endif

#include "common.h"
#include "opts.h"
#include "random.h"
#include "fd.h"
#include "fuzz.h"
#include "myfork.h"
#include "md5.h"
#include "timer.h"
#include "caca_getopt.h"

#if !defined SIGKILL
#   define SIGKILL 9
#endif

#if defined RLIMIT_AS
#   define ZZUF_RLIMIT_MEM RLIMIT_AS
#elif defined RLIMIT_VMEM
#   define ZZUF_RLIMIT_MEM RLIMIT_VMEM
#elif defined RLIMIT_DATA
#   define ZZUF_RLIMIT_MEM RLIMIT_DATA
#else
#   undef ZZUF_RLIMIT_MEM
#endif

#if defined RLIMIT_CPU
#   define ZZUF_RLIMIT_CPU RLIMIT_CPU
#else
#   undef ZZUF_RLIMIT_CPU
#endif

static void loop_stdin(struct opts *);

static void spawn_children(struct opts *);
static void clean_children(struct opts *);
static void read_children(struct opts *);

#if !defined HAVE_SETENV
static void setenv(char const *, char const *, int);
#endif
#if defined HAVE_WAITPID
static char const *sig2name(int);
#endif
static void finfo(FILE *, struct opts *, uint32_t);
#if defined HAVE_REGEX_H
static char *merge_regex(char *, char *);
static char *merge_file(char *, char *);
#endif
static void version(void);
static void usage(void);

#define ZZUF_FD_SET(fd, p_fdset, maxfd) \
    if (fd >= 0) \
    { \
        FD_SET((unsigned int)fd, p_fdset); \
        if (fd > maxfd) \
            maxfd = fd; \
    }

#define ZZUF_FD_ISSET(fd, p_fdset) \
    ((fd >= 0) && (FD_ISSET(fd, p_fdset)))

#if defined _WIN32
#   include <Windows.h>
#   include <fcntl.h> /* _O_RDWR */
#   include <io.h> /* _open */
static CRITICAL_SECTION _zz_pipe_cs;
#endif

int main(int argc, char *argv[])
{
    struct opts _opts, *opts = &_opts;
    char *tmp;
#if defined HAVE_REGEX_H
    char *include = NULL, *exclude = NULL;
    int has_cmdline = 0;
#endif
    int debug = 0, has_network = 0;

#if defined _WIN32
    InitializeCriticalSection(&_zz_pipe_cs);
#endif

    _zz_opts_init(opts);

    for (;;)
    {
#if defined HAVE_REGEX_H
#   define OPTSTR_REGEX "cE:I:"
#else
#   define OPTSTR_REGEX ""
#endif
#if defined HAVE_SETRLIMIT && defined ZZUF_RLIMIT_MEM
#   define OPTSTR_RLIMIT_MEM "M:"
#else
#   define OPTSTR_RLIMIT_MEM ""
#endif
#if defined HAVE_SETRLIMIT && defined ZZUF_RLIMIT_CPU
#   define OPTSTR_RLIMIT_CPU "T:"
#else
#   define OPTSTR_RLIMIT_CPU ""
#endif
#define OPTSTR "+" OPTSTR_REGEX OPTSTR_RLIMIT_MEM OPTSTR_RLIMIT_CPU \
                "a:Ab:B:C:dD:e:f:F:ij:l:mnO:p:P:qr:R:s:St:U:vxhV"
#define MOREINFO "Try `%s --help' for more information.\n"
        int option_index = 0;
        static struct caca_option long_options[] =
        {
            /* Long option, needs arg, flag, short option */
            { "allow",        1, NULL, 'a' },
            { "autoinc",      0, NULL, 'A' },
            { "bytes",        1, NULL, 'b' },
            { "max-bytes",    1, NULL, 'B' },
#if defined HAVE_REGEX_H
            { "cmdline",      0, NULL, 'c' },
#endif
            { "max-crashes",  1, NULL, 'C' },
            { "debug",        0, NULL, 'd' },
            { "delay",        1, NULL, 'D' },
#if defined HAVE_REGEX_H
            { "exclude",      1, NULL, 'E' },
#endif
            { "fuzzing",      1, NULL, 'f' },
            { "stdin",        0, NULL, 'i' },
#if defined HAVE_REGEX_H
            { "include",      1, NULL, 'I' },
#endif
            { "jobs",         1, NULL, 'j' },
            { "list",         1, NULL, 'l' },
            { "md5",          0, NULL, 'm' },
            { "max-memory",   1, NULL, 'M' },
            { "network",      0, NULL, 'n' },
            { "opmode",       1, NULL, 'O' },
            { "ports",        1, NULL, 'p' },
            { "protect",      1, NULL, 'P' },
            { "quiet",        0, NULL, 'q' },
            { "ratio",        1, NULL, 'r' },
            { "refuse",       1, NULL, 'R' },
            { "seed",         1, NULL, 's' },
            { "signal",       0, NULL, 'S' },
            { "max-time",     1, NULL, 't' },
            { "max-cputime",  1, NULL, 'T' },
            { "max-usertime", 1, NULL, 'U' },
            { "verbose",      0, NULL, 'v' },
            { "check-exit",   0, NULL, 'x' },
            { "help",         0, NULL, 'h' },
            { "version",      0, NULL, 'V' },
            { NULL,           0, NULL,  0  }
        };
        int c = caca_getopt(argc, argv, OPTSTR, long_options, &option_index);

        if (c == -1)
            break;

        switch (c)
        {
        case 'a': /* --allow */
            opts->allow = caca_optarg;
            break;
        case 'A': /* --autoinc */
            setenv("ZZUF_AUTOINC", "1", 1);
            break;
        case 'b': /* --bytes */
            opts->bytes = caca_optarg;
            break;
        case 'B': /* --max-bytes */
            if (caca_optarg[0] == '=')
                caca_optarg++;
            opts->maxbytes = atoi(caca_optarg);
            break;
#if defined HAVE_REGEX_H
        case 'c': /* --cmdline */
            has_cmdline = 1;
            break;
#endif
        case 'C': /* --max-crashes */
            if (caca_optarg[0] == '=')
                caca_optarg++;
            opts->maxcrashes = atoi(caca_optarg);
            if (opts->maxcrashes <= 0)
                opts->maxcrashes = 0;
            break;
        case 'd': /* --debug */
            debug++;
            break;
        case 'D': /* --delay */
            if (caca_optarg[0] == '=')
                caca_optarg++;
            opts->delay = (int64_t)(atof(caca_optarg) * 1000000.0);
            break;
#if defined HAVE_REGEX_H
        case 'E': /* --exclude */
            exclude = merge_regex(exclude, caca_optarg);
            if (!exclude)
            {
                fprintf(stderr, "%s: invalid regex -- `%s'\n",
                        argv[0], caca_optarg);
                _zz_opts_fini(opts);
                return EXIT_FAILURE;
            }
            break;
#endif
        case 'f': /* --fuzzing */
            opts->fuzzing = caca_optarg;
            break;
        case 'F':
            fprintf(stderr, "%s: `-F' is deprecated, use `-j'\n", argv[0]);
            _zz_opts_fini(opts);
            return EXIT_FAILURE;
        case 'i': /* --stdin */
            setenv("ZZUF_STDIN", "1", 1);
            break;
#if defined HAVE_REGEX_H
        case 'I': /* --include */
            include = merge_regex(include, caca_optarg);
            if (!include)
            {
                fprintf(stderr, "%s: invalid regex -- `%s'\n",
                        argv[0], caca_optarg);
                _zz_opts_fini(opts);
                return EXIT_FAILURE;
            }
            break;
#endif
        case 'j': /* --jobs */
            if (caca_optarg[0] == '=')
                caca_optarg++;
            opts->maxchild = atoi(caca_optarg) > 1 ? atoi(caca_optarg) : 1;
            break;
        case 'l': /* --list */
            opts->list = caca_optarg;
            break;
        case 'm': /* --md5 */
            opts->md5 = 1;
            break;
#if defined HAVE_SETRLIMIT && defined ZZUF_RLIMIT_MEM
        case 'M': /* --max-memory */
            if (caca_optarg[0] == '=')
                caca_optarg++;
            opts->maxmem = atoi(caca_optarg);
            break;
#endif
        case 'n': /* --network */
            setenv("ZZUF_NETWORK", "1", 1);
            has_network = 1;
            break;
        case 'O': /* --opmode */
            if (caca_optarg[0] == '=')
                caca_optarg++;
            if (!strcmp(caca_optarg, "preload"))
                opts->opmode = OPMODE_PRELOAD;
            else if (!strcmp(caca_optarg, "copy"))
                opts->opmode = OPMODE_COPY;
            else
            {
                fprintf(stderr, "%s: invalid operating mode -- `%s'\n",
                        argv[0], caca_optarg);
                _zz_opts_fini(opts);
                return EXIT_FAILURE;
            }
            break;
        case 'p': /* --ports */
            opts->ports = caca_optarg;
            break;
        case 'P': /* --protect */
            opts->protect = caca_optarg;
            break;
        case 'q': /* --quiet */
            opts->quiet = 1;
            break;
        case 'r': /* --ratio */
            if (caca_optarg[0] == '=')
                caca_optarg++;
            tmp = strchr(caca_optarg, ':');
            opts->minratio = atof(caca_optarg);
            opts->maxratio = tmp ? atof(tmp + 1) : opts->minratio;
            break;
        case 'R': /* --refuse */
            opts->refuse = caca_optarg;
            break;
        case 's': /* --seed */
            if (caca_optarg[0] == '=')
                caca_optarg++;
            tmp = strchr(caca_optarg, ':');
            opts->seed = atol(caca_optarg);
            opts->endseed = tmp ? tmp[1] ? (uint32_t)atol(tmp + 1)
                                         : (uint32_t)-1L
                                : opts->seed + 1;
            break;
        case 'S': /* --signal */
            setenv("ZZUF_SIGNAL", "1", 1);
            break;
        case 't': /* --max-time */
            if (caca_optarg[0] == '=')
                caca_optarg++;
            opts->maxtime = (int64_t)atoi(caca_optarg) * 1000000;
            break;
#if defined HAVE_SETRLIMIT && defined ZZUF_RLIMIT_CPU
        case 'T': /* --max-cputime */
            if (caca_optarg[0] == '=')
                caca_optarg++;
            opts->maxcpu = (int)(atof(caca_optarg) + 0.5);
            break;
#endif
        case 'U': /* --max-usertime */
            if (caca_optarg[0] == '=')
                caca_optarg++;
            opts->maxusertime = (int64_t)(atof(caca_optarg) * 1000000.0);
            break;
        case 'x': /* --check-exit */
            opts->checkexit = 1;
            break;
        case 'v': /* --verbose */
            opts->verbose = 1;
            break;
        case 'h': /* --help */
            usage();
            _zz_opts_fini(opts);
            return 0;
        case 'V': /* --version */
            version();
            _zz_opts_fini(opts);
            return 0;
        default:
            fprintf(stderr, "%s: invalid option -- %c\n", argv[0], c);
            printf(MOREINFO, argv[0]);
            _zz_opts_fini(opts);
            return EXIT_FAILURE;
        }
    }

    if (opts->ports && !has_network)
    {
        fprintf(stderr, "%s: port option (-p) requires network fuzzing (-n)\n",
                argv[0]);
        printf(MOREINFO, argv[0]);
        _zz_opts_fini(opts);
        return EXIT_FAILURE;
    }

    if (opts->allow && !has_network)
    {
        fprintf(stderr, "%s: allow option (-a) requires network fuzzing (-n)\n",
                argv[0]);
        printf(MOREINFO, argv[0]);
        _zz_opts_fini(opts);
        return EXIT_FAILURE;
    }

    _zz_setratio(opts->minratio, opts->maxratio);
    _zz_setseed(opts->seed);

    if (opts->fuzzing)
        _zz_fuzzing(opts->fuzzing);
    if (opts->bytes)
        _zz_bytes(opts->bytes);
    if (opts->list)
        _zz_list(opts->list);
    if (opts->protect)
        _zz_protect(opts->protect);
    if (opts->refuse)
        _zz_refuse(opts->refuse);

    /* Needed for stdin mode and for copy opmode. */
    _zz_fd_init();

    /*
     * Mode 1: asked to read from the standard input
     */
    if (caca_optind >= argc)
    {
        if (opts->verbose)
        {
            finfo(stderr, opts, opts->seed);
            fprintf(stderr, "reading from stdin\n");
        }

        if (opts->endseed != opts->seed + 1)
        {
            fprintf(stderr, "%s: seed ranges are incompatible with "
                            "stdin fuzzing\n", argv[0]);
            printf(MOREINFO, argv[0]);
            _zz_opts_fini(opts);
            return EXIT_FAILURE;
        }

        loop_stdin(opts);
    }
    /*
     * Mode 2: asked to launch programs
     */
    else
    {
#if defined HAVE_REGEX_H
        if (has_cmdline)
        {
            int dashdash = 0;

            for (int i = caca_optind + 1; i < argc; ++i)
            {
                if (dashdash)
                    include = merge_file(include, argv[i]);
                else if (!strcmp("--", argv[i]))
                    dashdash = 1;
                else if (argv[i][0] != '-')
                    include = merge_file(include, argv[i]);
            }
        }

        if (include)
            setenv("ZZUF_INCLUDE", include, 1);
        if (exclude)
            setenv("ZZUF_EXCLUDE", exclude, 1);
#endif

        setenv("ZZUF_DEBUG", debug ? debug > 1 ? "2" : "1" : "0", 1);

        if (opts->fuzzing)
            setenv("ZZUF_FUZZING", opts->fuzzing, 1);
        if (opts->bytes)
            setenv("ZZUF_BYTES", opts->bytes, 1);
        if (opts->list)
            setenv("ZZUF_LIST", opts->list, 1);
        if (opts->ports)
            setenv("ZZUF_PORTS", opts->ports, 1);
        if (opts->allow && opts->allow[0] == '!')
            setenv("ZZUF_DENY", opts->allow, 1);
        else if (opts->allow)
            setenv("ZZUF_ALLOW", opts->allow, 1);
        if (opts->protect)
            setenv("ZZUF_PROTECT", opts->protect, 1);
        if (opts->refuse)
            setenv("ZZUF_REFUSE", opts->refuse, 1);
#if defined HAVE_SETRLIMIT && defined ZZUF_RLIMIT_MEM
        if (opts->maxmem >= 0)
        {
            char buf[32];
            snprintf(buf, 32, "%i", opts->maxmem);
            setenv("ZZUF_MEMORY", buf, 1);
        }
#endif

        /* Allocate memory for children handling */
        opts->child = malloc(opts->maxchild * sizeof(struct child));
        for (int i = 0; i < opts->maxchild; ++i)
        {
            opts->child[i].status = STATUS_FREE;
            memset(opts->child[i].fd, -1, sizeof(opts->child->fd));
        }
        opts->nchild = 0;

        /* Create new argv */
        opts->oldargc = argc;
        opts->oldargv = argv;
        for (int i = 0; i < opts->maxchild; ++i)
        {
            int len = argc - caca_optind;
            opts->child[i].newargv = malloc((len + 1) * sizeof(char *));
            memcpy(opts->child[i].newargv, argv + caca_optind,
                   len * sizeof(char *));
            opts->child[i].newargv[len] = (char *)NULL;
        }

        /* Main loop */
        while (opts->nchild || opts->seed < opts->endseed)
        {
            /* Spawn new children, if necessary */
            spawn_children(opts);

            /* Cleanup dead or dying children */
            clean_children(opts);

            /* Read data from children */
            read_children(opts);

            if (opts->maxcrashes && opts->crashes >= opts->maxcrashes
                 && opts->nchild == 0)
            {
                if (opts->verbose)
                    fprintf(stderr,
                            "zzuf: maximum crash count reached, exiting\n");
                break;
            }

            if (opts->maxtime && _zz_time() - opts->starttime >= opts->maxtime
                 && opts->nchild == 0)
            {
                if (opts->verbose)
                    fprintf(stderr,
                            "zzuf: maximum running time reached, exiting\n");
                break;
            }
        }
    }

    /* Clean up */
    _zz_fd_fini();
    _zz_opts_fini(opts);

#if defined _WIN32
    DeleteCriticalSection(&_zz_pipe_cs);
#endif

    return opts->crashes ? EXIT_FAILURE : EXIT_SUCCESS;
}

static void loop_stdin(struct opts *opts)
{
    struct md5 *ctx = NULL;

    if (opts->md5)
        ctx = _zz_md5_init();

    _zz_register(0);

    for (int total = 0; ; )
    {
        uint8_t buf[BUFSIZ];
        int toread = BUFSIZ, off = 0;

        if (opts->maxbytes >= 0)
        {
            if (total >= opts->maxbytes)
                break;
            if (total + BUFSIZ >= opts->maxbytes)
                toread = opts->maxbytes - total;
        }

        int ret = read(0, buf, toread);
        if (ret <= 0)
            break;

        total += ret;

        _zz_fuzz(0, buf, ret);
        _zz_addpos(0, ret);

        if (opts->md5)
            _zz_md5_add(ctx, buf, ret);
        else while (ret)
        {
            int nw = 0;
            if ((nw = write(1, buf + off, (unsigned int)ret)) < 0)
                break;
            ret -= nw;
            off += nw;
        }
    }

    if (opts->md5)
    {
        uint8_t md5sum[16];
        _zz_md5_fini(md5sum, ctx);
        finfo(stdout, opts, opts->seed);
        fprintf(stdout, "%.02x%.02x%.02x%.02x%.02x%.02x%.02x%.02x%.02x%.02x"
                "%.02x%.02x%.02x%.02x%.02x%.02x\n", md5sum[0], md5sum[1],
                md5sum[2], md5sum[3], md5sum[4], md5sum[5], md5sum[6],
                md5sum[7], md5sum[8], md5sum[9], md5sum[10], md5sum[11],
                md5sum[12], md5sum[13], md5sum[14], md5sum[15]);
        fflush(stdout);
    }

    _zz_unregister(0);
}

static void finfo(FILE *fp, struct opts *opts, uint32_t seed)
{
    if (opts->minratio == opts->maxratio)
        fprintf(fp, "zzuf[s=%i,r=%g]: ", seed, opts->minratio);
    else
        fprintf(fp, "zzuf[s=%i,r=%g:%g]: ", seed,
                opts->minratio, opts->maxratio);
}

#if defined HAVE_REGEX_H
static char *merge_file(char *regex, char *file)
{
    char *newfile = malloc(5 + 2 * strlen(file) + 1 + 1);
    char *tmp = newfile;

    *tmp++ = '(';
    *tmp++ = '^';
    *tmp++ = '|';
    *tmp++ = '/';
    *tmp++ = ')';
    while (*file)
    {
        if (strchr("^.[$()|*+?{\\", *file))
            *tmp++ = '\\';
        *tmp++ = *file++;
    }
    *tmp++ = '$';
    *tmp++ = '\0';

    tmp = merge_regex(regex, newfile);
    free(newfile);
    return tmp;
}

static char *merge_regex(char *regex, char *string)
{
    if (regex)
    {
        regex = realloc(regex, strlen(regex) + strlen(string) + 1 + 1);
        sprintf(regex + strlen(regex) - 1, "|%s)", string);
    }
    else
    {
        regex = malloc(1 + strlen(string) + 1 + 1);
        sprintf(regex, "(%s)", string);
    }

    regex_t optre;
    if (regcomp(&optre, regex, REG_EXTENDED) != 0)
    {
        free(regex);
        return NULL;
    }
    regfree(&optre);

    return regex;
}
#endif

static void spawn_children(struct opts *opts)
{
    int64_t now = _zz_time();

    if (opts->nchild == opts->maxchild)
        return; /* no slot */

    if (opts->seed == opts->endseed)
        return; /* job finished */

    if (opts->maxcrashes && opts->crashes >= opts->maxcrashes)
        return; /* all jobs crashed */

    if (opts->maxtime && now - opts->starttime >= opts->maxtime)
        return; /* run time exceeded */

    if (opts->delay > 0 && opts->lastlaunch + opts->delay > now)
        return; /* too early */

    /* Find the empty slot */
    int slot = 0;
    while (slot < opts->maxchild && opts->child[slot].status != STATUS_FREE)
        ++slot;

    /* Prepare required files, if necessary */
    if (opts->opmode == OPMODE_COPY)
    {
        char tmpname[4096];
        char *tmpdir;
        tmpdir = getenv("TEMP");
        if (!tmpdir || !*tmpdir)
            tmpdir = "/tmp";

        int k = 0;

        for (int j = caca_optind + 1; j < opts->oldargc; ++j)
        {
            FILE *fpin = fopen(opts->oldargv[j], "r");
            if (!fpin)
                continue;

#ifdef _WIN32
            sprintf(tmpname, "%s/zzuf.$i.XXXXXX", tmpdir, GetCurrentProcessId());
            int fdout = _open(mktemp(tmpname), _O_RDWR, 0600);
#else
            sprintf(tmpname, "%s/zzuf.%i.XXXXXX", tmpdir, (int)getpid());
            int fdout = mkstemp(tmpname);
#endif
            if (fdout < 0)
            {
                fclose(fpin);
                continue;
            }

            opts->child[slot].newargv[j - caca_optind] = strdup(tmpname);

            _zz_register(k);
            while (!feof(fpin))
            {
                uint8_t buf[BUFSIZ];
                size_t n = fread(buf, 1, BUFSIZ, fpin);
                if (n <= 0)
                    break;
                _zz_fuzz(k, buf, n);
                _zz_addpos(k, n);
                write(fdout, buf, n);
            }
            _zz_unregister(k);

            fclose(fpin);
            close(fdout);

            ++k;
        }
    }

    /* Launch process */
    if (myfork(&opts->child[slot], opts) < 0)
    {
        fprintf(stderr, "error launching `%s'\n", opts->child[slot].newargv[0]);
        opts->seed++;
        /* FIXME: clean up OPMODE_COPY files here */
        return;
    }

    /* We’re the parent, acknowledge spawn */
    opts->child[slot].date = now;
    opts->child[slot].bytes = 0;
    opts->child[slot].seed = opts->seed;
    opts->child[slot].ratio = _zz_getratio();
    opts->child[slot].status = STATUS_RUNNING;
    if (opts->md5)
        opts->child[slot].ctx = _zz_md5_init();

    if (opts->verbose)
    {
        finfo(stderr, opts, opts->child[slot].seed);
        fprintf(stderr, "launched `%s'\n", opts->child[slot].newargv[0]);
    }

    opts->lastlaunch = now;
    opts->nchild++;
    opts->seed++;

    _zz_setseed(opts->seed);
}

static void clean_children(struct opts *opts)
{
#if defined HAVE_KILL || defined HAVE_WINDOWS_H
    int64_t now = _zz_time();
#endif

#if defined HAVE_KILL || defined HAVE_WINDOWS_H
    /* Terminate children if necessary */
    for (int i = 0; i < opts->maxchild; ++i)
    {
        if (opts->child[i].status == STATUS_RUNNING
            && opts->maxbytes >= 0
            && opts->child[i].bytes > opts->maxbytes)
        {
            if (opts->verbose)
            {
                finfo(stderr, opts, opts->child[i].seed);
                fprintf(stderr, "data output exceeded, sending SIGTERM\n");
            }
#if defined HAVE_KILL
            kill(opts->child[i].pid, SIGTERM);
#else
            /* We must invalidate fd */
            memset(opts->child[i].fd, -1, sizeof(opts->child[i].fd));
            TerminateProcess(opts->child[i].process_handle, 0x0);
#endif
            opts->child[i].date = now;
            opts->child[i].status = STATUS_SIGTERM;
        }

        if (opts->child[i].status == STATUS_RUNNING
             && opts->maxusertime >= 0
             && now > opts->child[i].date + opts->maxusertime)
        {
            if (opts->verbose)
            {
                finfo(stderr, opts, opts->child[i].seed);
                fprintf(stderr, "running time exceeded, sending SIGTERM\n");
            }
#if defined HAVE_KILL
            kill(opts->child[i].pid, SIGTERM);
#else
            /* We must invalidate fd */
            memset(opts->child[i].fd, -1, sizeof(opts->child[i].fd));
            TerminateProcess(opts->child[i].process_handle, 0x0);
#endif
            opts->child[i].date = now;
            opts->child[i].status = STATUS_SIGTERM;
        }
    }

    /* Kill children if necessary (still there after 2 seconds) */
    for (int i = 0; i < opts->maxchild; ++i)
    {
        if (opts->child[i].status == STATUS_SIGTERM
            && now > opts->child[i].date + 2000000)
        {
            if (opts->verbose)
            {
                finfo(stderr, opts, opts->child[i].seed);
                fprintf(stderr, "not responding, sending SIGKILL\n");
            }
#if defined HAVE_KILL
            kill(opts->child[i].pid, SIGKILL);
#else
            TerminateProcess(opts->child[i].process_handle, 0x0);
#endif
            opts->child[i].status = STATUS_SIGKILL;
        }
    }
#endif

    /* Collect dead children */
    for (int i = 0; i < opts->maxchild; ++i)
    {
        uint8_t md5sum[16];
#if defined HAVE_WAITPID
        int status;
        pid_t pid;
#endif

        if (opts->child[i].status != STATUS_SIGKILL
            && opts->child[i].status != STATUS_SIGTERM
            && opts->child[i].status != STATUS_EOF)
            continue;

#if defined HAVE_WAITPID
        pid = waitpid(opts->child[i].pid, &status, WNOHANG);
        if (pid <= 0)
            continue;

        if (opts->checkexit && WIFEXITED(status) && WEXITSTATUS(status))
        {
            finfo(stderr, opts, opts->child[i].seed);
            fprintf(stderr, "exit %i\n", WEXITSTATUS(status));
            opts->crashes++;
        }
        else if (WIFSIGNALED(status)
                 && !(WTERMSIG(status) == SIGTERM
                       && opts->child[i].status == STATUS_SIGTERM))
        {
            char const *message = "";

            if (WTERMSIG(status) == SIGKILL && opts->maxmem >= 0)
                message = " (memory exceeded?)";
#   if defined SIGXCPU
            else if (WTERMSIG(status) == SIGXCPU && opts->maxcpu >= 0)
                message = " (CPU time exceeded?)";
#   endif
            else if (WTERMSIG(status) == SIGKILL && opts->maxcpu >= 0)
                message = " (CPU time exceeded?)";

            finfo(stderr, opts, opts->child[i].seed);
            fprintf(stderr, "signal %i%s%s\n",
                    WTERMSIG(status), sig2name(WTERMSIG(status)), message);
            opts->crashes++;
        }
        else if (opts->verbose)
        {
            finfo(stderr, opts, opts->child[i].seed);
            if (WIFSIGNALED(status))
                fprintf(stderr, "signal %i%s\n",
                        WTERMSIG(status), sig2name(WTERMSIG(status)));
            else
                fprintf(stderr, "exit %i\n", WEXITSTATUS(status));
        }
#elif defined _WIN32
        {
            DWORD exit_code;
            if (GetExitCodeProcess(opts->child[i].process_handle, &exit_code))
            {
                if (exit_code == STILL_ACTIVE) continue; /* The process is still active, we don't do anything */

                /*
                 * The main problem with GetExitCodeProcess is it returns either returned parameter value of
                 * ExitProcess/TerminateProcess, or the unhandled exception (which is what we're looking for)
                 */
                switch (exit_code)
                {
                case EXCEPTION_ACCESS_VIOLATION: fprintf(stderr, "child(%d) unhandled exception: Access Violation\n", opts->child[i].pid); break;
                default: fprintf(stderr, "child(%d) exited with code %#08x\n", opts->child[i].pid, exit_code); break;
                }
            }

            if (opts->child[i].status != STATUS_RUNNING)
            {
                TerminateProcess(opts->child[i].process_handle, 0);
            }
        }
#else
        /* waitpid() is not available. Don't kill the process. */
        continue;
#endif

        for (int j = 0; j < 3; ++j)
            if (opts->child[i].fd[j] >= 0)
                close(opts->child[i].fd[j]);

        if (opts->opmode == OPMODE_COPY)
        {
            for (int j = caca_optind + 1; j < opts->oldargc; ++j)
            {
                if (opts->child[i].newargv[j - caca_optind] != opts->oldargv[j])
                {
                    unlink(opts->child[i].newargv[j - caca_optind]);
                    free(opts->child[i].newargv[j - caca_optind]);
                    opts->child[i].newargv[j - caca_optind] = opts->oldargv[j];
                }
            }
        }

        if (opts->md5)
        {
            _zz_md5_fini(md5sum, opts->child[i].ctx);
            finfo(stdout, opts, opts->child[i].seed);
            fprintf(stdout, "%.02x%.02x%.02x%.02x%.02x%.02x%.02x%.02x%.02x"
                    "%.02x%.02x%.02x%.02x%.02x%.02x%.02x\n", md5sum[0],
                    md5sum[1], md5sum[2], md5sum[3], md5sum[4], md5sum[5],
                    md5sum[6], md5sum[7], md5sum[8], md5sum[9], md5sum[10],
                    md5sum[11], md5sum[12], md5sum[13], md5sum[14], md5sum[15]);
            fflush(stdout);
        }
        opts->child[i].status = STATUS_FREE;
        opts->nchild--;
    }
}

#ifdef _WIN32

/* This structure contains useful information about data sent from fuzzed applications */
struct child_overlapped
{
    OVERLAPPED overlapped;
    char buf[BUFSIZ];
    struct opts * opts;
    int child_no;
    int fd_no;
};

/* This callback is called when fuzzed applications write in fd out, err or debug */
static void _stdcall read_child(DWORD err_code, DWORD nbr_of_bytes_transfered, LPOVERLAPPED overlapped)
{
    struct child_overlapped * co = (struct child_overlapped *)overlapped;

    /* TODO: handle more cases like ERROR_MORE_DATA */
    if (err_code != ERROR_SUCCESS)
        return;

    EnterCriticalSection(&_zz_pipe_cs);
    switch (co->fd_no)
    {
    case 0: /* debug fd */
        write(1, "dbg: ", 4);
    case 1: /* out */
        write(1, co->buf, nbr_of_bytes_transfered); break;
    case 2: /* err */
        write(2, co->buf, nbr_of_bytes_transfered); break;
    default: break;
    }
    LeaveCriticalSection(&_zz_pipe_cs);

    if (co->fd_no != 0) /* either out or err fd */
        co->opts->child[co->child_no].bytes += nbr_of_bytes_transfered;

    if (co->opts->md5 && co->fd_no == 2)
        _zz_md5_add(co->opts->child[co->child_no].ctx, co->buf, nbr_of_bytes_transfered);

    free(co); /* clean up allocated data */
}

/* Since on windows select doesn't support file HANDLE, we use IOCP */
static void read_children(struct opts *opts)
{
    HANDLE *children_handle, * cur_child_handle;
    size_t fd_number = opts->maxchild * 3;

    cur_child_handle = children_handle = malloc(sizeof(*children_handle) * fd_number);

    for (size_t i = 0; i < fd_number; ++i)
        children_handle[i] = INVALID_HANDLE_VALUE;

    for (int i = 0; i < opts->maxchild; ++i)
    for (int j = 0; j < 3; ++j)
    {
        struct child_overlapped * co;
        HANDLE h = (opts->child[i].fd[j] == -1) ? INVALID_HANDLE_VALUE : (HANDLE)_get_osfhandle(opts->child[i].fd[j]);

        if (opts->child[i].status != STATUS_RUNNING
             || opts->child[i].fd[j] == -1
             || h == INVALID_HANDLE_VALUE)
        {
            fd_number--;
            continue;
        }

        co = malloc(sizeof(*co));
        ZeroMemory(co, sizeof(*co));
        *cur_child_handle = co->overlapped.hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
        co->child_no = i;
        co->fd_no    = j;
        co->opts     = opts;

        if (!ReadFileEx(h, co->buf, sizeof(co->buf), (LPOVERLAPPED)co, read_child))
        {
            /* End of file reached */
            close(opts->child[i].fd[j]);
            opts->child[i].fd[j] = -1;

            if (opts->child[i].fd[0] == -1
                && opts->child[i].fd[1] == -1
                && opts->child[i].fd[2] == -1)
                opts->child[i].status = STATUS_EOF;
        }
        cur_child_handle++;
    }

    if (fd_number == 0)
        return;

    /* FIXME: handle error */
    WaitForMultipleObjectsEx(fd_number, children_handle, FALSE, 1000, TRUE);
}

#else
static void read_children(struct opts *opts)
{
    struct timeval tv;
    fd_set fdset;
    int maxfd = 0;

    /* Read data from all sockets */
    FD_ZERO(&fdset);
    for (int i = 0; i < opts->maxchild; ++i)
    {
        if (opts->child[i].status != STATUS_RUNNING)
            continue;

        for (int j = 0; j < 3; ++j)
            ZZUF_FD_SET(opts->child[i].fd[j], &fdset, maxfd);
    }
    tv.tv_sec = 0;
    tv.tv_usec = 1000;

    errno = 0;
    int ret = select(maxfd + 1, &fdset, NULL, NULL, &tv);
    if (ret < 0 && errno)
        perror("select");
    if (ret <= 0)
        return;

    for (int i = 0; i < opts->maxchild; ++i)
    for (int j = 0; j < 3; ++j)
    {
        uint8_t buf[BUFSIZ];

        if (opts->child[i].status != STATUS_RUNNING)
            continue;

        if (!ZZUF_FD_ISSET(opts->child[i].fd[j], &fdset))
            continue;

        ret = read(opts->child[i].fd[j], buf, BUFSIZ - 1);
        if (ret > 0)
        {
            /* We got data */
            if (j != 0)
                opts->child[i].bytes += ret;

            if (opts->md5 && j == 2)
                _zz_md5_add(opts->child[i].ctx, buf, ret);
            else if (!opts->quiet || j == 0)
                write((j < 2) ? STDERR_FILENO : STDOUT_FILENO, buf, ret);
        }
        else if (ret == 0)
        {
            /* End of file reached */
            close(opts->child[i].fd[j]);
            opts->child[i].fd[j] = -1;

            if (opts->child[i].fd[0] == -1
                && opts->child[i].fd[1] == -1
                && opts->child[i].fd[2] == -1)
                opts->child[i].status = STATUS_EOF;
        }
    }
}
#endif

#if !defined HAVE_SETENV
static void setenv(char const *name, char const *value, int overwrite)
{
    if (!overwrite && getenv(name))
        return;

    char *str = malloc(strlen(name) + 1 + strlen(value) + 1);
    sprintf(str, "%s=%s", name, value);
    putenv(str);
}
#endif

#if defined HAVE_WAITPID
static char const *sig2name(int signum)
{
    switch (signum)
    {
#ifdef SIGQUIT
        case SIGQUIT:  return " (SIGQUIT)"; /* 3 */
#endif
        case SIGILL:   return " (SIGILL)";  /* 4 */
#ifdef SIGTRAP
        case SIGTRAP:  return " (SIGTRAP)"; /* 5 */
#endif
        case SIGABRT:  return " (SIGABRT)"; /* 6 */
#ifdef SIGBUS
        case SIGBUS:   return " (SIGBUS)";  /* 7 */
#endif
        case SIGFPE:   return " (SIGFPE)";  /* 8 */
        case SIGSEGV:  return " (SIGSEGV)"; /* 11 */
        case SIGPIPE:  return " (SIGPIPE)"; /* 13 */
#ifdef SIGEMT
        case SIGEMT:   return " (SIGEMT)";  /* ? */
#endif
#ifdef SIGXCPU
        case SIGXCPU:  return " (SIGXCPU)"; /* 24 */
#endif
#ifdef SIGXFSZ
        case SIGXFSZ:  return " (SIGXFSZ)"; /* 25 */
#endif
#ifdef SIGSYS
        case SIGSYS:   return " (SIGSYS)";  /* 31 */
#endif
    }

    return "";
}
#endif

static void version(void)
{
    printf("zzuf %s\n", PACKAGE_VERSION);
    printf("Copyright © 2002—2015 Sam Hocevar <sam@hocevar.net>\n");
    printf("This program is free software. It comes without any warranty, to the extent\n");
    printf("permitted by applicable law. You can redistribute it and/or modify it under\n");
    printf("the terms of the Do What the Fuck You Want to Public License, Version 2, as\n");
    printf("published by the WTFPL Task Force. See http://www.wtfpl.net/ for more details.\n");
    printf("\n");
    printf("Written by Sam Hocevar. Report bugs to <sam@hocevar.net>.\n");
}

static void usage(void)
{
#if defined HAVE_REGEX_H
    printf("Usage: zzuf [-aAcdimnqSvx] [-s seed|-s start:stop] [-r ratio|-r min:max]\n");
#else
    printf("Usage: zzuf [-aAdimnqSvx] [-s seed|-s start:stop] [-r ratio|-r min:max]\n");
#endif
    printf("              [-f mode] [-D delay] [-j jobs] [-C crashes] [-B bytes] [-a list]\n");
    printf("              [-t seconds]");
#if defined HAVE_SETRLIMIT && defined ZZUF_RLIMIT_CPU
    printf(                          " [-T seconds]");
#endif
#if defined HAVE_SETRLIMIT && defined ZZUF_RLIMIT_MEM
    printf(                                       " [-M mebibytes]");
#endif
    printf(                                                      " [-b ranges] [-p ports]\n");
    printf("              [-P protect] [-R refuse] [-l list]");
#if defined HAVE_REGEX_H
    printf(                                                " [-I include] [-E exclude]");
#endif
    printf("              [-O mode]\n");
    printf("\n");
    printf("              [PROGRAM [--] [ARGS]...]\n");
    printf("       zzuf -h | --help\n");
    printf("       zzuf -V | --version\n");
    printf("Run PROGRAM with optional arguments ARGS and fuzz its input.\n");
    printf("\n");
    printf("Mandatory arguments to long options are mandatory for short options too.\n");
    printf("  -a, --allow <list>        only fuzz network input for IPs in <list>\n");
    printf("          ... !<list>       do not fuzz network input for IPs in <list>\n");
    printf("  -A, --autoinc             increment seed each time a new file is opened\n");
    printf("  -b, --bytes <ranges>      only fuzz bytes at offsets within <ranges>\n");
    printf("  -B, --max-bytes <n>       kill children that output more than <n> bytes\n");
#if defined HAVE_REGEX_H
    printf("  -c, --cmdline             only fuzz files specified in the command line\n");
#endif
    printf("  -C, --max-crashes <n>     stop after <n> children have crashed (default 1)\n");
    printf("  -d, --debug               print debug messages (twice for more verbosity)\n");
    printf("  -D, --delay               delay between forks\n");
#if defined HAVE_REGEX_H
    printf("  -E, --exclude <regex>     do not fuzz files matching <regex>\n");
#endif
    printf("  -f, --fuzzing <mode>      use fuzzing mode <mode> ([xor] set unset)\n");
    printf("  -i, --stdin               fuzz standard input\n");
#if defined HAVE_REGEX_H
    printf("  -I, --include <regex>     only fuzz files matching <regex>\n");
#endif
    printf("  -j, --jobs <n>            number of simultaneous jobs (default 1)\n");
    printf("  -l, --list <list>         only fuzz Nth descriptor with N in <list>\n");
    printf("  -m, --md5                 compute the output's MD5 hash\n");
#if defined HAVE_SETRLIMIT && defined ZZUF_RLIMIT_MEM
    printf("  -M, --max-memory <n>      maximum child virtual memory in MiB (default %u)\n", DEFAULT_MEM);
#endif
    printf("  -n, --network             fuzz network input\n");
    printf("  -O, --opmode <mode>       use operating mode <mode> ([preload] copy)\n");
    printf("  -p, --ports <list>        only fuzz network destination ports in <list>\n");
    printf("  -P, --protect <list>      protect bytes and characters in <list>\n");
    printf("  -q, --quiet               do not print children's messages\n");
    printf("  -r, --ratio <ratio>       bit fuzzing ratio (default %g)\n", DEFAULT_RATIO);
    printf("          ... <start:stop>  specify a ratio range\n");
    printf("  -R, --refuse <list>       refuse bytes and characters in <list>\n");
    printf("  -s, --seed <seed>         random seed (default %i)\n", DEFAULT_SEED);
    printf("         ... <start:stop>   specify a seed range\n");
    printf("  -S, --signal              prevent children from diverting crashing signals\n");
    printf("  -t, --max-time <n>        stop spawning children after <n> seconds\n");
    printf("  -T, --max-cputime <n>     kill children that use more than <n> CPU seconds\n");
    printf("  -U, --max-usertime <n>    kill children that run for more than <n> seconds\n");
    printf("  -v, --verbose             print information during the run\n");
    printf("  -x, --check-exit          report processes that exit with a non-zero status\n");
    printf("  -h, --help                display this help and exit\n");
    printf("  -V, --version             output version information and exit\n");
    printf("\n");
    printf("Written by Sam Hocevar. Report bugs to <sam@hocevar.net>.\n");
}

