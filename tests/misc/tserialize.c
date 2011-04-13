/* tserialize.c - exercise serialize/deserialize libnpfs funcitons */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <assert.h>

#include "9p.h"
#include "npfs.h"

#include "diod_log.h"

#define TEST_MSIZE 4096

static void test_tversion (void);
static void test_rversion (void);
static void test_tgetattr (void);
static void test_rgetattr (void);

static void
usage (void)
{
    fprintf (stderr, "Usage: tserialize\n");
    exit (1);
}


int
main (int argc, char *argv[])
{
    diod_log_init (argv[0]);

    if (argc != 1)
        usage ();

    test_tversion ();
    test_rversion ();
    test_tgetattr ();
    test_rgetattr ();

    exit (0);
}

/* Simulate a packet receive:
 * - allocate and initialize Npfcall as in conn.c::np_conn_new_incall ()
 * - copy raw data into fc->pkt as in conn.c::np_conn_read_proc ()
 * - deserialize as in conn.c::np_conn_read_proc ()
 */
static Npfcall *
_rcv_buf (void *buf, int len, const char *fun)
{
    Npfcall *fc;

    printf ("%s: %d\n", fun, len);

    if (!(fc = malloc (sizeof (*fc) + TEST_MSIZE)))
        msg_exit ("out of memory");
    fc->pkt = (u8 *)fc + sizeof (*fc);

    memcpy (fc->pkt, buf, len);

    if (!np_deserialize (fc, fc->pkt))
        msg_exit ("np_deserialize error in %s", fun);

    return fc;
}

/* Check that original and deserialized packet match.
 * Unioned structure comparison is deferred to calling function.
 */
static void
_chk_rcv (int type, Npfcall *fc, Npfcall *fc2, const char *fun)
{
    if (fc->type != type)
        msg_exit ("incorrect type in %s", fun);
    if (fc->size != fc2->size)
        msg_exit ("size mismatch in %s", fun);
    if (fc->type != fc2->type)
        msg_exit ("type mismatch in %s", fun);
    if (memcmp (fc->pkt, fc2->pkt, fc->size) != 0)
        msg_exit ("pkt mismatch in %s", fun);
}

static int
_str9cmp (Npstr *s1, Npstr *s2)
{
    if (s1->len != s2->len)
        return 1;
    return memcmp (s1->str, s2->str, s1->len);
}

static void
test_tversion (void)
{
    Npfcall *fc, *fc2;

    if (!(fc = np_create_tversion (TEST_MSIZE, "9p2000.L")))
        msg_exit ("out of memory");
    fc2 = _rcv_buf (fc->pkt, fc->size, __FUNCTION__);
    _chk_rcv (P9_TVERSION, fc, fc2, __FUNCTION__);

    assert (fc->u.tversion.msize == fc2->u.tversion.msize);
    assert (_str9cmp (&fc->u.tversion.version, &fc2->u.tversion.version) == 0);

    free (fc);
    free (fc2);
}

static void
test_rversion (void)
{
    Npfcall *fc, *fc2;

    if (!(fc = np_create_rversion (TEST_MSIZE, "9p2000.L")))
        msg_exit ("out of memory");
    fc2 = _rcv_buf (fc->pkt, fc->size, __FUNCTION__);
    _chk_rcv (P9_RVERSION, fc, fc2, __FUNCTION__);

    assert (fc->u.rversion.msize == fc2->u.rversion.msize);
    assert (_str9cmp (&fc->u.rversion.version, &fc2->u.rversion.version) == 0);

    free (fc);
    free (fc2);
}

static void
test_tgetattr (void)
{
    Npfcall *fc, *fc2;

    if (!(fc = np_create_tgetattr (42, 5000)))
        msg_exit ("out of memory");
    fc2 = _rcv_buf (fc->pkt, fc->size, __FUNCTION__);
    _chk_rcv (P9_TGETATTR, fc, fc2, __FUNCTION__);

    assert (fc->u.tgetattr.fid == fc2->u.tgetattr.fid);
    assert (fc->u.tgetattr.request_mask == fc2->u.tgetattr.request_mask);

    free (fc);
    free (fc2);
}

static void
test_rgetattr (void)
{
    Npfcall *fc, *fc2;
    struct p9_qid qid = { 1, 2, 3 };

    if (!(fc = np_create_rgetattr (1, &qid, 4, 5, 6, 7, 8, 9, 10, 11,
                                   12, 13, 14, 15, 16, 17, 18, 19, 20, 21)))
        msg_exit ("out of memory");

    fc2 = _rcv_buf (fc->pkt, fc->size, __FUNCTION__);
    _chk_rcv (P9_RGETATTR, fc, fc2, __FUNCTION__);

    assert (fc->u.rgetattr.valid == fc2->u.rgetattr.valid);
    assert (fc->u.rgetattr.qid.type == fc2->u.rgetattr.qid.type);
    assert (fc->u.rgetattr.qid.version == fc2->u.rgetattr.qid.version);
    assert (fc->u.rgetattr.qid.path == fc2->u.rgetattr.qid.path);
    assert (fc->u.rgetattr.mode == fc2->u.rgetattr.mode);
    assert (fc->u.rgetattr.uid == fc2->u.rgetattr.uid);
    assert (fc->u.rgetattr.gid == fc2->u.rgetattr.gid);
    assert (fc->u.rgetattr.nlink == fc2->u.rgetattr.nlink);
    assert (fc->u.rgetattr.rdev == fc2->u.rgetattr.rdev);
    assert (fc->u.rgetattr.size == fc2->u.rgetattr.size);
    assert (fc->u.rgetattr.blksize == fc2->u.rgetattr.blksize);
    assert (fc->u.rgetattr.blocks == fc2->u.rgetattr.blocks);
    assert (fc->u.rgetattr.atime_sec == fc2->u.rgetattr.atime_sec);
    assert (fc->u.rgetattr.atime_nsec == fc2->u.rgetattr.atime_nsec);
    assert (fc->u.rgetattr.mtime_sec == fc2->u.rgetattr.mtime_sec);
    assert (fc->u.rgetattr.mtime_nsec == fc2->u.rgetattr.mtime_nsec);
    assert (fc->u.rgetattr.ctime_sec == fc2->u.rgetattr.ctime_sec);
    assert (fc->u.rgetattr.ctime_nsec == fc2->u.rgetattr.ctime_nsec);
    assert (fc->u.rgetattr.btime_sec == fc2->u.rgetattr.btime_sec);
    assert (fc->u.rgetattr.btime_nsec == fc2->u.rgetattr.btime_nsec);
    assert (fc->u.rgetattr.gen == fc2->u.rgetattr.gen);
    assert (fc->u.rgetattr.data_version == fc2->u.rgetattr.data_version);

    free (fc);
    free (fc2);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
