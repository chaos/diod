/* tsetfsuid.c - check that pthreads can independently setfsuid */

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <string.h>

#define TEST_UID 100
#define TEST_GID 100

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


static int
check_fsid (char *msg, uid_t uid, gid_t gid)
{
    int fd;
    char path[] = "/tmp/testfsuid.XXXXXX";

    struct stat sb;

    if ((fd = mkstemp (path)) < 0)
        _errx ("mkstemp", errno);
    if (fstat (fd, &sb) < 0)
        _errx ("fstat", errno);
    if (unlink (path) < 0)
        _errx ("unlink", errno);

    printf ("%s: %d:%d\n", msg, sb.st_uid, sb.st_gid);

    return (sb.st_uid == uid  && sb.st_gid == gid);
}

static void
change_fsid (char *msg, uid_t uid, gid_t gid)
{
    printf ("%s: changing to %d:%d\n", msg, uid, gid);
    if (setfsuid (uid) == uid)
        _errx ("setfsuid", 0);
    if (setfsgid (gid) == gid)
        _errx ("setfsgid", 0);
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
    assert (check_fsid ("task1", 0, 0));
    change_state (S1);
    wait_state (S2);
    assert (check_fsid ("task1", 0, 0));
    change_fsid ("task1", TEST_UID, TEST_GID);
    assert (check_fsid ("task1", TEST_UID, TEST_GID));
    change_state (S3);
    wait_state (S4);
    assert (check_fsid ("task1", TEST_UID, TEST_GID));
}

static void *proc2 (void *a)
{
    assert (check_fsid ("task2", 0, 0));
    wait_state (S1);
    change_fsid ("task2", TEST_UID, TEST_GID);
    assert (check_fsid ("task2", TEST_UID, TEST_GID));
    change_state (S2);
    wait_state (S3);
    change_fsid ("task2", 0, 0);
    assert (check_fsid ("task2", 0, 0));
    change_state (S4);
}

int main(int argc, char *argv[])
{
    pthread_t t1, t2;
    int err;

    assert (check_fsid ("task0", 0, 0));

    if ((err = pthread_create (&t1, NULL, proc1, NULL)))
        _errx ("pthread_create", err);
    if ((err = pthread_create (&t2, NULL, proc2, NULL)))
        _errx ("pthread_create", err);

    if ((err = pthread_join (t2, NULL)))
        _errx ("pthread_join", err);
    if ((err = pthread_join (t1, NULL)))
        _errx ("pthread_join", err);

    assert (check_fsid ("task0", 0, 0));
    
    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
