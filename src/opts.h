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
 *  opts.h: configuration handling
 */

#ifdef _WIN32
#   include <Windows.h>
#endif

struct opts
{
    enum opmode
    {
        OPMODE_PRELOAD,
        OPMODE_COPY,
    } opmode;
    char **oldargv;
    int oldargc;
    char *fuzzing, *bytes, *list, *ports, *protect, *refuse, *allow;
    uint32_t seed;
    uint32_t endseed;
    double minratio;
    double maxratio;
    int quiet;
    int maxbytes;
    int maxcpu;
    int md5;
    int checkexit;
    int verbose;
    int maxmem;
    int64_t starttime;
    int64_t maxtime;
    int64_t maxusertime;
    int64_t delay;
    int64_t lastlaunch;

    int maxchild, nchild, maxcrashes, crashes;
    struct child
    {
        enum status
        {
            STATUS_FREE,
            STATUS_RUNNING,
            STATUS_SIGTERM,
            STATUS_SIGKILL,
            STATUS_EOF,
        } status;

        pid_t pid;
#ifdef _WIN32
        HANDLE process_handle;
#endif
        int fd[3]; /* 0 is debug, 1 is stderr, 2 is stdout */
        int bytes, seed;
        double ratio;
        int64_t date;
        struct md5 *ctx;
        char **newargv;
    } *child;
};

void _zz_opts_init(struct opts *);
void _zz_opts_fini(struct opts *);

