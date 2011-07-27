/* tsetuid.c - check that pthreads CANNOT independently setreuid */

/* This was an assumption made in the old npfs code, no longer true
 * post-linuxthreads.  It's here as a demonstration of that fact,
 * and in case anything changes in this area we might want to flag it.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <stdarg.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <grp.h>

#include "diod_log.h"
#include "test.h"

#define TEST_UID 100
#define TEST_UID2 101
#define TEST_UID3 102

typedef enum { S0, S1, S2, S3, S4, S5 } state_t;

static state_t         state = S0;
static pthread_mutex_t state_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  state_cond = PTHREAD_COND_INITIALIZER;

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
    msg ("task1: geteuid %d", geteuid ());
    change_state (S1);

    wait_state (S2);
    msg ("task1: geteuid %d", geteuid ());
    msg ("task1: seteuid %d", TEST_UID2);
    _setreuid (0, 0);
    _setreuid (-1, TEST_UID2);
    msg ("task1: geteuid %d", geteuid ());
    change_state (S3);

    wait_state (S4);
    msg ("task1: geteuid %d", geteuid ());
    return NULL;
}

static void *proc2 (void *a)
{
    wait_state (S1);
    msg ("task2: geteuid %d", geteuid ());
    msg ("task2: seteuid %d", TEST_UID);
    _setreuid (0, 0);
    _setreuid (-1, TEST_UID);
    msg ("task2: geteuid %d", geteuid ());
    change_state (S2);

    wait_state (S3);
    msg ("task2: geteuid %d", geteuid ());
    msg ("task2: seteuid %d", TEST_UID3);
    _setreuid (0, 0);
    _setreuid (-1, TEST_UID3);
    msg ("task2: geteuid %d", geteuid ());
    change_state (S4);
    return NULL;
}

int main(int argc, char *argv[])
{
    pthread_t t1, t2;

    diod_log_init (argv[0]);

    assert (geteuid () == 0);

    msg ("task0: geteuid %d", geteuid ());

    _create (&t1, proc1, NULL);
    _create (&t2, proc2, NULL);

    _join (t2, NULL);
    _join (t1, NULL);

    msg ("task0: geteuid %d", geteuid ());
    
    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
