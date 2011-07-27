/* tsuppgrp.c - check that pthreads can independently setgroups */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <grp.h>

#include "diod_log.h"

#include "test.h"

#define TEST_GID 100
#define TEST_GID2 101

typedef enum { S0, S1, S2, S3, S4, S5 } state_t;

static state_t         state = S0;
static pthread_mutex_t state_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  state_cond = PTHREAD_COND_INITIALIZER;

static void
show_groups (char *s)
{
    gid_t g[32];
    int n, i;
    char buf[256];

    snprintf (buf, sizeof(buf), "getgroups ");
    n = _getgroups (1, g);
    for (i = 0; i < n; i++)
        snprintf (buf+strlen(buf), sizeof(buf)-strlen(buf), "%d ", g[i]);
    msg ("%s: %s", s, buf);
}

static void
change_state (state_t s)
{
    _lock (&state_lock);
    state = s;
    _condsig (&state_cond);
    _unlock (&state_lock);
}

static void
wait_state (state_t s)
{
    _lock (&state_lock);
    while ((state != s))
        _condwait (&state_cond, &state_lock);
    _unlock (&state_lock);
}

static void *proc1 (void *a)
{
    gid_t g[] = { TEST_GID2 };

    show_groups ("task1");
    change_state (S1);

    wait_state (S2);
    show_groups ("task1");

    msg ("task1: setgroups %d", TEST_GID2);
    _setgroups (1, g);
    show_groups ("task1");
    change_state (S3);

    wait_state (S4);
    return NULL;
}

static void *proc2 (void *a)
{
    gid_t g[] = { TEST_GID };

    wait_state (S1);
    show_groups ("task2");

    msg ("task2: setgroups %d", TEST_GID);
    _setgroups (1, g);
    show_groups ("task2");
    change_state (S2);

    wait_state (S3);
    show_groups ("task2");
    change_state (S4);
    return NULL;
}

int main(int argc, char *argv[])
{
    pthread_t t1, t2;

    diod_log_init (argv[0]);

    assert (geteuid () == 0);

    msg ("task0: setgroups (NULL)");
    _setgroups (0, NULL);
    show_groups ("task0");

    _create (&t1, proc1, NULL);
    _create (&t2, proc2, NULL);

    _join (t2, NULL);
    _join (t1, NULL);

    show_groups ("task0");
    
    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
