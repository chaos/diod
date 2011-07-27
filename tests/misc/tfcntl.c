/* tfcntl.c - pthreads contending for fcntl locks are like one process */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <fcntl.h>
#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <grp.h>

#include "diod_log.h"

#include "test.h"

#define TEST_UID 100
#define TEST_GID 100

typedef enum { S0, S1, S2, S3, S4, S5 } state_t;

static state_t         state = S0;
static pthread_mutex_t state_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  state_cond = PTHREAD_COND_INITIALIZER;
static char path[] = "/tmp/test.fcntl.XXXXXX";
static int fd = -1;

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

static void
mkfile (void)
{
    char buf[1024];

    memset (buf, 0, sizeof (buf));
    fd = _mkstemp (path);
    if (write (fd, buf, sizeof (buf)) < 0)
        err_exit ("write");
    if (close (fd) < 0)
        err_exit ("close");
    if ((fd = open (path, O_RDWR)) < 0)
        err_exit ("open %s", path);
}

static void *proc1 (void *a)
{
    struct flock f;

    msg ("proc1: locking bytes 0-64");
    f.l_type = F_WRLCK;
    f.l_whence = SEEK_SET;
    f.l_start = 0;
    f.l_len = 64;
    if (fcntl (fd, F_SETLK, &f) < 0)
        err_exit ("proc1: fcntl F_SETLK");
    change_state (S1);

    wait_state (S2);
    change_state (S3);
    wait_state (S4);
    return NULL;
}

static void *proc2 (void *a)
{
    struct flock f;

    wait_state (S1);
    msg ("proc2: locking bytes 32-64");
    f.l_type = F_WRLCK;
    f.l_whence = SEEK_SET;
    f.l_start = 0;
    f.l_len = 64;
    if (fcntl (fd, F_SETLK, &f) < 0)
        err_exit ("proc2: fcntl F_SETLK");
    change_state (S2);

    wait_state (S3);
    change_state (S4);
    return NULL;
}

int
main (int arg, char *argv[])
{
    pthread_t t1, t2;
    struct flock f;
    pid_t pid;
    int status;

    diod_log_init (argv[0]);

    mkfile ();

    /* same task/thread contending for write lock */
    msg ("proc0: locking bytes 0-64");
    f.l_type = F_WRLCK;
    f.l_whence = SEEK_SET;
    f.l_start = 0;
    f.l_len = 64;
    if (fcntl (fd, F_SETLK, &f) < 0)
        err_exit ("proc0: fcntl F_SETLK");
    msg ("proc0: locking bytes 32-64");
    f.l_type = F_WRLCK;
    f.l_whence = SEEK_SET;
    f.l_start = 32;
    f.l_len = 64;
    if (fcntl (fd, F_SETLK, &f) < 0)
        err_exit ("proc0: fcntl F_SETLK");

    /* two threads contending for write lock */
    _create (&t1, proc1, NULL);
    _create (&t2, proc2, NULL);

    _join (t2, NULL);
    _join (t1, NULL);

    fflush (stderr);

    /* two processes contending for write lock - inherited fd */
    switch (pid = fork ()) {
        case -1:
            err_exit ("fork");
        case 0: /* child */
            msg ("child0: locking bytes 32-64");
            f.l_type = F_WRLCK;
            f.l_whence = SEEK_SET;
            f.l_start = 32;
            f.l_len = 64;
            if (fcntl (fd, F_SETLK, &f) < 0)
                err_exit ("child0: fcntl F_SETLK");
            msg ("child0: unexpected success");
            break;
        default: /* parent */
            if (waitpid (pid, &status, 0) < 0)
                err_exit ("wait");
            break;
    }

    /* two processes contending for write lock - seperate fd's */
    switch (pid = fork ()) {
        case -1:
            err_exit ("fork");
        case 0: /* child */
            if (close (fd) < 0)
                err_exit ("child1: close");
            if ((fd = open (path, O_RDWR)) < 0)
                err_exit ("open %s (child)", path);
            msg ("child1: locking bytes 32-64");
            f.l_type = F_WRLCK;
            f.l_whence = SEEK_SET;
            f.l_start = 32;
            f.l_len = 64;
            if (fcntl (fd, F_SETLK, &f) < 0)
                err_exit ("child1: fcntl F_SETLK");
            msg ("child1: unexpected success");
            close (fd);
            break;
        default: /* parent */
            if (waitpid (pid, &status, 0) < 0)
                err_exit ("wait");
            break;
    }

    if (close (fd) < 0)
        err_exit ("close");
    if (unlink (path) < 0)
        err_exit ("unlink %s", path);
    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
