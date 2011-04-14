/* tserialize.c - exercise serialize/deserialize libnpfs functions */

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

static void test_rlerror (void);
static void test_tstatfs (void);        static void test_rstatfs (void);
static void test_tlopen (void);         static void test_rlopen (void);
static void test_tlcreate (void);       static void test_rlcreate (void);
static void test_tsymlink (void);       static void test_rsymlink (void);
static void test_tmknod (void);         static void test_rmknod (void);
static void test_trename (void);        static void test_rrename(void);
static void test_treadlink (void);      static void test_rreadlink (void);
static void test_tgetattr (void);       static void test_rgetattr (void);
static void test_tsetattr (void);       static void test_rsetattr (void);

static void test_treaddir (void);       static void test_rreaddir (void);
static void test_tfsync (void);         static void test_rfsync (void);
static void test_tlock (void);          static void test_rlock (void);
static void test_tgetlock (void);       static void test_rgetlock (void);
static void test_tlink (void);          static void test_rlink (void);
static void test_tmkdir (void);         static void test_rmkdir (void);

static void test_tversion (void);       static void test_rversion (void);
static void test_tauth (void);          static void test_rauth (void);
static void test_tflush (void);         static void test_rflush (void);
static void test_tattach (void);        static void test_rattach (void);
static void test_twalk (void);          static void test_rwalk (void);
static void test_tread (void);          static void test_rread (void);
static void test_twrite (void);         static void test_rwrite (void);

static void test_tclunk (void);         static void test_rclunk (void);
static void test_tremove (void);        static void test_rremove (void);

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

    test_rlerror ();
    test_tstatfs ();    test_rstatfs ();
    test_tlopen ();     test_rlopen ();
    test_tlcreate ();   test_rlcreate ();
    test_tsymlink ();   test_rsymlink ();
    test_tmknod ();     test_rmknod ();
    test_trename ();    test_rrename ();
    test_treadlink();   test_rreadlink ();
    test_tgetattr ();   test_rgetattr ();
    test_tsetattr ();   test_rsetattr ();
#if 0
    test_txattrwalk (); test_rxattrwalk ();
    test_txattrcreate (); test_rxattrcreate ();
#endif
    test_treaddir ();   test_rreaddir ();
    test_tfsync ();     test_rfsync ();
    test_tlock ();      test_rlock ();
    test_tgetlock ();   test_rgetlock ();
    test_tlink ();      test_rlink ();
    test_tmkdir ();     test_rmkdir ();
#if HAVE_LARGEIO
    //test_tawrite ();    test_rawrite ();
    //test_taread ();     test_raread ();
#endif
    test_tversion ();   test_rversion ();
    test_tauth ();      test_rauth ();
    test_tflush ();     test_rflush ();
    test_tattach ();    test_rattach ();
    test_twalk ();      test_rwalk ();
    test_tread ();      test_rread ();
    test_twrite ();     test_rwrite ();
    test_tclunk ();     test_rclunk ();
    test_tremove ();    test_rremove ();

    exit (0);
}

/* Allocate a new Npfcall to "receive" raw data from fc->pkt.
 */
static Npfcall *
_rcv_buf (Npfcall *fc, int type, const char *fun)
{
    Npfcall *fc2;

    printf ("%s(%d): %d\n", fun, type, fc->size);

    /* see conn.c:np_conn_new_incall () */
    if (!(fc2 = malloc (sizeof (*fc2) + TEST_MSIZE)))
        msg_exit ("out of memory");
    fc2->pkt = (u8 *)fc2 + sizeof (*fc2);

    /* see conn.c::np_conn_read_proc */
    memcpy (fc2->pkt, fc->pkt, fc->size);
    if (!np_deserialize (fc2, fc2->pkt))
        msg_exit ("np_deserialize error in %s", fun);

    /* check a few things */
    if (fc->type != type)
        msg_exit ("incorrect type in %s", fun);
    if (fc->size != fc2->size)
        msg_exit ("size mismatch in %s", fun);
    if (fc->type != fc2->type)
        msg_exit ("type mismatch in %s", fun);

    return fc2;
}

static int
_str9cmp (Npstr *s1, Npstr *s2)
{
    if (s1->len != s2->len)
        return 1;
    return memcmp (s1->str, s2->str, s1->len);
}

static void
test_rlerror (void)
{
    Npfcall *fc, *fc2;

    if (!(fc = np_create_rlerror (42)))
        msg_exit ("out of memory");
    fc2 = _rcv_buf (fc, P9_RLERROR,  __FUNCTION__);

    assert (fc->u.rlerror.ecode == fc2->u.rlerror.ecode);

    free (fc);
    free (fc2);
}

static void
test_tstatfs (void)
{
    Npfcall *fc, *fc2;

    if (!(fc = np_create_tstatfs (42)))
        msg_exit ("out of memory");
    fc2 = _rcv_buf (fc, P9_TSTATFS,  __FUNCTION__);

    assert (fc->u.tstatfs.fid == fc2->u.tstatfs.fid);

    free (fc);
    free (fc2);
}

static void
test_rstatfs(void)
{
    Npfcall *fc, *fc2;

    if (!(fc = np_create_rstatfs (1, 2, 3, 4, 5, 6, 7, 8, 9)))
        msg_exit ("out of memory");
    fc2 = _rcv_buf (fc, P9_RSTATFS,  __FUNCTION__);

    assert (fc->u.rstatfs.type == fc2->u.rstatfs.type);
    assert (fc->u.rstatfs.bsize == fc2->u.rstatfs.bsize);
    assert (fc->u.rstatfs.blocks == fc2->u.rstatfs.blocks);
    assert (fc->u.rstatfs.bfree == fc2->u.rstatfs.bfree);
    assert (fc->u.rstatfs.bavail== fc2->u.rstatfs.bavail);
    assert (fc->u.rstatfs.files == fc2->u.rstatfs.files);
    assert (fc->u.rstatfs.ffree == fc2->u.rstatfs.ffree);
    assert (fc->u.rstatfs.fsid == fc2->u.rstatfs.fsid);
    assert (fc->u.rstatfs.namelen == fc2->u.rstatfs.namelen);

    free (fc);
    free (fc2);
}

static void
test_tlopen (void)
{
    Npfcall *fc, *fc2;

    if (!(fc = np_create_tlopen (1, 2)))
        msg_exit ("out of memory");
    fc2 = _rcv_buf (fc, P9_TLOPEN,  __FUNCTION__);

    /* FILL IN */

    free (fc);
    free (fc2);
}

static void
test_rlopen (void)
{
    Npfcall *fc, *fc2;
    struct p9_qid qid = { 1, 2, 3 };

    if (!(fc = np_create_rlopen (&qid, 2)))
        msg_exit ("out of memory");
    fc2 = _rcv_buf (fc, P9_RLOPEN,  __FUNCTION__);

    /* FILL IN */

    free (fc);
    free (fc2);
}

static void
test_tlcreate (void)
{
    Npfcall *fc, *fc2;

    if (!(fc = np_create_tlcreate (1, "xyz", 3, 4, 5)))
        msg_exit ("out of memory");
    fc2 = _rcv_buf (fc, P9_TLCREATE,  __FUNCTION__);

    /* FILL IN */

    free (fc);
    free (fc2);
}

static void
test_rlcreate (void)
{
    Npfcall *fc, *fc2;
    struct p9_qid qid = { 1, 2, 3 };

    if (!(fc = np_create_rlcreate (&qid, 2)))
        msg_exit ("out of memory");
    fc2 = _rcv_buf (fc, P9_RLCREATE,  __FUNCTION__);

    /* FILL IN */

    free (fc);
    free (fc2);
}

static void
test_tsymlink (void)
{
    Npfcall *fc, *fc2;

    if (!(fc = np_create_tsymlink (1, "xyz", "abc", 4)))
        msg_exit ("out of memory");
    fc2 = _rcv_buf (fc, P9_TSYMLINK,  __FUNCTION__);

    /* FILL IN */

    free (fc);
    free (fc2);
}

static void
test_rsymlink (void)
{
    Npfcall *fc, *fc2;
    struct p9_qid qid = { 1, 2, 3 };

    if (!(fc = np_create_rsymlink (&qid)))
        msg_exit ("out of memory");
    fc2 = _rcv_buf (fc, P9_RSYMLINK,  __FUNCTION__);

    /* FILL IN */

    free (fc);
    free (fc2);
}

static void
test_tmknod (void)
{
    Npfcall *fc, *fc2;

    if (!(fc = np_create_tmknod (1, "xyz", 3, 4, 5, 6)))
        msg_exit ("out of memory");
    fc2 = _rcv_buf (fc, P9_TMKNOD,  __FUNCTION__);

    /* FILL IN */

    free (fc);
    free (fc2);
}

static void
test_rmknod (void)
{
    Npfcall *fc, *fc2;
    struct p9_qid qid = { 1, 2, 3 };

    if (!(fc = np_create_rmknod (&qid)))
        msg_exit ("out of memory");
    fc2 = _rcv_buf (fc, P9_RMKNOD,  __FUNCTION__);

    /* FILL IN */

    free (fc);
    free (fc2);
}

static void
test_trename (void)
{
    Npfcall *fc, *fc2;

    if (!(fc = np_create_trename (1, 2, "xyz")))
        msg_exit ("out of memory");
    fc2 = _rcv_buf (fc, P9_TRENAME,  __FUNCTION__);

    /* FILL IN */

    free (fc);
    free (fc2);
}

static void
test_rrename(void)
{
    Npfcall *fc, *fc2;

    if (!(fc = np_create_rrename ()))
        msg_exit ("out of memory");
    fc2 = _rcv_buf (fc, P9_RRENAME,  __FUNCTION__);

    /* FILL IN */

    free (fc);
    free (fc2);
}

static void
test_treadlink (void)
{
    Npfcall *fc, *fc2;

    if (!(fc = np_create_treadlink (1)))
        msg_exit ("out of memory");
    fc2 = _rcv_buf (fc, P9_TREADLINK,  __FUNCTION__);

    /* FILL IN */

    free (fc);
    free (fc2);
}

static void
test_rreadlink (void)
{
    Npfcall *fc, *fc2;

    if (!(fc = np_create_rreadlink ("xyz")))
        msg_exit ("out of memory");
    fc2 = _rcv_buf (fc, P9_RREADLINK,  __FUNCTION__);

    /* FILL IN */

    free (fc);
    free (fc2);
}

static void
test_tgetattr (void)
{
    Npfcall *fc, *fc2;

    if (!(fc = np_create_tgetattr (42, 5000)))
        msg_exit ("out of memory");
    fc2 = _rcv_buf (fc, P9_TGETATTR, __FUNCTION__);

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
    fc2 = _rcv_buf (fc, P9_RGETATTR, __FUNCTION__);

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

static void
test_tsetattr (void)
{
    Npfcall *fc, *fc2;

    if (!(fc = np_create_tsetattr (1,2,3,4,5,6,7,8,9,10)))
        msg_exit ("out of memory");
    fc2 = _rcv_buf (fc, P9_TSETATTR,  __FUNCTION__);

    /* FILL IN */

    free (fc);
    free (fc2);
}

static void
test_rsetattr (void)
{
    Npfcall *fc, *fc2;

    if (!(fc = np_create_rsetattr ()))
        msg_exit ("out of memory");
    fc2 = _rcv_buf (fc, P9_RSETATTR,  __FUNCTION__);

    /* FILL IN */

    free (fc);
    free (fc2);
}

static void
test_treaddir (void)
{
    Npfcall *fc, *fc2;

    if (!(fc = np_create_treaddir(1, 2, 3)))
        msg_exit ("out of memory");
    fc2 = _rcv_buf (fc, P9_TREADDIR,  __FUNCTION__);

    /* FILL IN */

    free (fc);
    free (fc2);
}

static void
test_rreaddir (void)
{
    Npfcall *fc, *fc2;
    int n = 0, len = 256;
    struct p9_qid qid[3] = { { 1, 2, 3 }, { 4, 5, 6 }, { 7, 8, 9 } }, qid2[3];
    char *name[3] = { "abc", "def", "ghi" }, name2[3][128];
    u64 offset;
    u8 type;

    if (!(fc = np_create_rreaddir (len)))
        msg_exit ("out of memory");
    n += np_serialize_p9dirent (&qid[0], 0, 1, name[0],
                                fc->u.rreaddir.data + n, len - n);
    n += np_serialize_p9dirent (&qid[1], 50, 2, name[1],
                                fc->u.rreaddir.data + n, len - n);
    n += np_serialize_p9dirent (&qid[2], 100, 3, name[2],
                                fc->u.rreaddir.data + n, len - n);
    assert (n < len);
    np_finalize_rreaddir (fc, n);
    fc2 = _rcv_buf (fc, P9_RREADDIR,  __FUNCTION__);

    n = 0;
    n += np_deserialize_p9dirent (&qid2[0], &offset, &type, name2[0], 128,
                           fc2->u.rreaddir.data + n, fc2->u.rreaddir.count - n);
    assert (offset == 0);
    assert (type == 1);
    assert (strcmp (name2[0], name[0]) == 0);
    n += np_deserialize_p9dirent (&qid2[1], &offset, &type, name2[1], 128,
                           fc2->u.rreaddir.data + n, fc2->u.rreaddir.count - n);
    assert (offset == 50);
    assert (type == 2);
    assert (strcmp (name2[1], name[1]) == 0);
    n += np_deserialize_p9dirent (&qid2[2], &offset, &type, name2[2], 128,
                           fc2->u.rreaddir.data + n, fc2->u.rreaddir.count - n);
    assert (offset == 100);
    assert (type == 3);
    assert (strcmp (name2[2], name[2]) == 0);
    assert (n == fc2->u.rreaddir.count);

    free (fc);
    free (fc2);
}

static void
test_tfsync (void)
{
    Npfcall *fc, *fc2;

    if (!(fc = np_create_tfsync(1)))
        msg_exit ("out of memory");
    fc2 = _rcv_buf (fc, P9_TFSYNC,  __FUNCTION__);

    /* FILL IN */

    free (fc);
    free (fc2);
}

static void
test_rfsync (void)
{
    Npfcall *fc, *fc2;

    if (!(fc = np_create_rfsync()))
        msg_exit ("out of memory");
    fc2 = _rcv_buf (fc, P9_RFSYNC,  __FUNCTION__);

    free (fc);
    free (fc2);
}

static void
test_tlock (void)
{
    Npfcall *fc, *fc2;

    if (!(fc = np_create_tlock (1, 2, 3, 4, 5, 6, "xyz")))
        msg_exit ("out of memory");
    fc2 = _rcv_buf (fc, P9_TLOCK,  __FUNCTION__);

    /* FILL IN */

    free (fc);
    free (fc2);
}

static void
test_rlock (void)
{
    Npfcall *fc, *fc2;

    if (!(fc = np_create_rlock (1)))
        msg_exit ("out of memory");
    fc2 = _rcv_buf (fc, P9_RLOCK,  __FUNCTION__);

    /* FILL IN */

    free (fc);
    free (fc2);
}

static void
test_tgetlock (void)
{
    Npfcall *fc, *fc2;

    if (!(fc = np_create_tgetlock (1, 2, 3, 4, 5, "xyz")))
        msg_exit ("out of memory");
    fc2 = _rcv_buf (fc, P9_TGETLOCK,  __FUNCTION__);

    /* FILL IN */

    free (fc);
    free (fc2);
}

static void
test_rgetlock (void)
{
    Npfcall *fc, *fc2;

    if (!(fc = np_create_rgetlock (1, 2, 3, 4, "xyz")))
        msg_exit ("out of memory");
    fc2 = _rcv_buf (fc, P9_RGETLOCK,  __FUNCTION__);

    /* FILL IN */

    free (fc);
    free (fc2);
}

static void
test_tlink (void)
{
    Npfcall *fc, *fc2;

    if (!(fc = np_create_tlink (1, 2, "xyz")))
        msg_exit ("out of memory");
    fc2 = _rcv_buf (fc, P9_TLINK,  __FUNCTION__);

    /* FILL IN */

    free (fc);
    free (fc2);
}

static void
test_rlink (void)
{
    Npfcall *fc, *fc2;

    if (!(fc = np_create_rlink ()))
        msg_exit ("out of memory");
    fc2 = _rcv_buf (fc, P9_RLINK,  __FUNCTION__);

    /* FILL IN */

    free (fc);
    free (fc2);
}

static void
test_tmkdir (void)
{
    Npfcall *fc, *fc2;

    if (!(fc = np_create_tmkdir (1, "abc", 2, 3)))
        msg_exit ("out of memory");
    fc2 = _rcv_buf (fc, P9_TMKDIR,  __FUNCTION__);

    /* FILL IN */

    free (fc);
    free (fc2);
}

static void
test_rmkdir (void)
{
    Npfcall *fc, *fc2;
    struct p9_qid qid = { 1, 2, 3 };

    if (!(fc = np_create_rmkdir (&qid)))
        msg_exit ("out of memory");
    fc2 = _rcv_buf (fc, P9_RMKDIR,  __FUNCTION__);

    /* FILL IN */

    free (fc);
    free (fc2);
}

static void
test_tversion (void)
{
    Npfcall *fc, *fc2;

    if (!(fc = np_create_tversion (TEST_MSIZE, "9p2000.L")))
        msg_exit ("out of memory");
    fc2 = _rcv_buf (fc, P9_TVERSION,  __FUNCTION__);

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
    fc2 = _rcv_buf (fc, P9_RVERSION, __FUNCTION__);

    assert (fc->u.rversion.msize == fc2->u.rversion.msize);
    assert (_str9cmp (&fc->u.rversion.version, &fc2->u.rversion.version) == 0);

    free (fc);
    free (fc2);
}

static void
test_tauth (void)
{
    Npfcall *fc, *fc2;

    if (!(fc = np_create_tauth (1, "abc", "xyz", 4)))
        msg_exit ("out of memory in %s", __FUNCTION__); 
    fc2 = _rcv_buf (fc, P9_TAUTH, __FUNCTION__);

    /* FILL IN */

    free (fc);
    free (fc2);
}

static void
test_rauth (void)
{
    Npfcall *fc, *fc2;
    struct p9_qid qid = { 1, 2, 3 };

    if (!(fc = np_create_rauth (&qid)))
        msg_exit ("out of memory in %s", __FUNCTION__); 
    fc2 = _rcv_buf (fc, P9_RAUTH, __FUNCTION__);

    /* FILL IN */

    free (fc);
    free (fc2);
}

static void
test_tflush (void)
{
    Npfcall *fc, *fc2;

    if (!(fc = np_create_tflush (1)))
        msg_exit ("out of memory in %s", __FUNCTION__); 
    fc2 = _rcv_buf (fc, P9_TFLUSH, __FUNCTION__);

    /* FILL IN */

    free (fc);
    free (fc2);
}

static void
test_rflush (void)
{
    Npfcall *fc, *fc2;

    if (!(fc = np_create_rflush ()))
        msg_exit ("out of memory in %s", __FUNCTION__); 
    fc2 = _rcv_buf (fc, P9_RFLUSH, __FUNCTION__);

    /* FILL IN */

    free (fc);
    free (fc2);
}

static void
test_tattach (void)
{
    Npfcall *fc, *fc2;

    if (!(fc = np_create_tattach (1, 2, "abc", "xyz", 5)))
        msg_exit ("out of memory in %s", __FUNCTION__); 
    fc2 = _rcv_buf (fc, P9_TATTACH, __FUNCTION__);

    /* FILL IN */

    free (fc);
    free (fc2);
}

static void
test_rattach (void)
{
    Npfcall *fc, *fc2;
    struct p9_qid qid = { 1, 2, 3 };

    if (!(fc = np_create_rattach (&qid)))
        msg_exit ("out of memory in %s", __FUNCTION__); 
    fc2 = _rcv_buf (fc, P9_RATTACH, __FUNCTION__);

    /* FILL IN */

    free (fc);
    free (fc2);
}

static void
test_twalk (void)
{
    Npfcall *fc, *fc2;
    char *wnames[4] = {
        "abc", "def", "ghi", "jkl",
    };

    if (!(fc = np_create_twalk (1, 2, 4, wnames)))
        msg_exit ("out of memory in %s", __FUNCTION__); 
    fc2 = _rcv_buf (fc, P9_TWALK, __FUNCTION__);

    /* FILL IN */

    free (fc);
    free (fc2);
}

static void
test_rwalk (void)
{
    Npfcall *fc, *fc2;
    struct p9_qid wqids [4] = { 
        { 1, 2, 3 }, { 4, 5, 6 }, { 7, 8, 9 }, { 10, 11, 12 },
    };

    if (!(fc = np_create_rwalk (4, wqids)))
        msg_exit ("out of memory in %s", __FUNCTION__); 
    fc2 = _rcv_buf (fc, P9_RWALK, __FUNCTION__);

    /* FILL IN */

    free (fc);
    free (fc2);
}

static void
test_tread (void)
{
    Npfcall *fc, *fc2;

    if (!(fc = np_create_tread (1, 2, 3)))
        msg_exit ("out of memory in %s", __FUNCTION__); 
    fc2 = _rcv_buf (fc, P9_TREAD, __FUNCTION__);

    /* FILL IN */

    free (fc);
    free (fc2);
}

static void
test_rread (void)
{
    Npfcall *fc, *fc2;
    u8 buf[128];

    memset (buf, 0xf0, sizeof(buf));

    if (!(fc = np_create_rread (sizeof (buf), buf)))
        msg_exit ("out of memory in %s", __FUNCTION__); 
    np_set_rread_count (fc, sizeof (buf));
    fc2 = _rcv_buf (fc, P9_RREAD, __FUNCTION__);

    /* FILL IN */

    free (fc);
    free (fc2);
}

static void
test_twrite (void)
{
    Npfcall *fc, *fc2;
    u8 buf[128];

    if (!(fc = np_create_twrite (1, 2, sizeof (buf), buf)))
        msg_exit ("out of memory in %s", __FUNCTION__); 
    fc2 = _rcv_buf (fc, P9_TWRITE, __FUNCTION__);

    /* FILL IN */

    free (fc);
    free (fc2);
}

static void
test_rwrite (void)
{
    Npfcall *fc, *fc2;

    if (!(fc = np_create_rwrite (1)))
        msg_exit ("out of memory in %s", __FUNCTION__); 
    fc2 = _rcv_buf (fc, P9_RWRITE, __FUNCTION__);

    /* FILL IN */

    free (fc);
    free (fc2);
}

static void
test_tclunk (void)
{
    Npfcall *fc, *fc2;

    if (!(fc = np_create_tclunk (1)))
        msg_exit ("out of memory in %s", __FUNCTION__); 
    fc2 = _rcv_buf (fc, P9_TCLUNK, __FUNCTION__);

    /* FILL IN */

    free (fc);
    free (fc2);
}

static void
test_rclunk (void)
{
    Npfcall *fc, *fc2;

    if (!(fc = np_create_rclunk ()))
        msg_exit ("out of memory in %s", __FUNCTION__); 
    fc2 = _rcv_buf (fc, P9_RCLUNK, __FUNCTION__);

    /* FILL IN */

    free (fc);
    free (fc2);
}

static void
test_tremove (void)
{
    Npfcall *fc, *fc2;

    if (!(fc = np_create_tremove (1)))
        msg_exit ("out of memory in %s", __FUNCTION__); 
    fc2 = _rcv_buf (fc, P9_TREMOVE, __FUNCTION__);

    /* FILL IN */

    free (fc);
    free (fc2);
}

static void
test_rremove (void)
{
    Npfcall *fc, *fc2;

    if (!(fc = np_create_rremove ()))
        msg_exit ("out of memory in %s", __FUNCTION__); 
    fc2 = _rcv_buf (fc, P9_RREMOVE, __FUNCTION__);

    /* FILL IN */

    free (fc);
    free (fc2);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
