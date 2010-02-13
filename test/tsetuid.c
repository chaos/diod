/* tsetuid.c - check that pthreads can independently setreuid */

/* N.B. This old linuxthreads behavior is not POSIX.
 * The assumption does not hold on modern Linux systems (e.g. RHEL5).
 */
 
#include <unistd.h>
#include <sys/types.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <string.h>

#define TEST_UID 100

typedef enum { S0, S1, S2, S3, S4, S5 } state_t;

static state_t         state = S0;
static pthread_mutex_t state_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  state_cond = PTHREAD_COND_INITIALIZER;

static void _errx (char *msg, int err)
{
    fprintf (stderr, "%s%s%s\n", msg,
        (err > 0) ? ": " : "",
        (err > 0) ? strerror (err) : "");
    exit (1);
}   

static void
change_state (state_t s)
{
    int err;

    if ((err = pthread_mutex_lock (&state_lock)))
        _errx ("pthread_mutex_lock", err);
    state = s;
    if ((err = pthread_cond_signal (&state_cond)))
        _errx ("pthread_cond_signal", err);
    if ((err = pthread_mutex_unlock (&state_lock)))
        _errx ("pthread_mutex_unlock", err);
}

static void
wait_state (state_t s)
{
    int err;

    if ((err = pthread_mutex_lock (&state_lock)))
        _errx ("pthread_mutex_lock", err);
    while ((state != s))
        if ((err = pthread_cond_wait(&state_cond, &state_lock)))
            _errx ("pthread_cond_wait", err);
    if ((err = pthread_mutex_unlock (&state_lock)))
        _errx ("pthread_mutex_unlock", err);
}

static void *proc1 (void *a)
{
    assert (geteuid () == 0);
    change_state (S1);
    wait_state (S2);
    assert (geteuid () == 0);
    if (setreuid (-1, TEST_UID) < 0)
        _errx ("setreuid", errno);
    assert (geteuid () == TEST_UID);
    change_state (S3);
    wait_state (S4);
    assert (geteuid () == TEST_UID);
}

static void *proc2 (void *a)
{
    assert (geteuid () == 0);
    wait_state (S1);
    if (setreuid (-1, TEST_UID) < 0)
        _errx ("setreuid", errno);
    assert (geteuid () == TEST_UID);
    change_state (S2);
    wait_state (S3);
    if (setreuid (0, 0) < 0)
        _errx ("setreuid", errno);
    assert (geteuid () == 0);
    change_state (S4);
}

int main(int argc, char *argv[])
{
    pthread_t t1, t2;
    int err;

    assert (geteuid () == 0);

    if ((err = pthread_create (&t1, NULL, proc1, NULL)))
        _errx ("pthread_create", err);
    if ((err = pthread_create (&t2, NULL, proc2, NULL)))
        _errx ("pthread_create", err);

    if ((err = pthread_join (t2, NULL)))
        _errx ("pthread_join", err);
    if ((err = pthread_join (t1, NULL)))
        _errx ("pthread_join", err);

    assert (geteuid () == 0);
    
    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
