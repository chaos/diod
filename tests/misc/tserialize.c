/* tserialize.c - exercise serialize/deserialize libnpfs functions */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
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
static void test_txattrwalk (void);     static void test_rxattrwalk (void);
static void test_txattrcreate (void);   static void test_rxattrcreate (void);
static void test_treaddir (void);       static void test_rreaddir (void);
static void test_tfsync (void);         static void test_rfsync (void);
static void test_tlock (void);          static void test_rlock (void);
static void test_tgetlock (void);       static void test_rgetlock (void);
static void test_tlink (void);          static void test_rlink (void);
static void test_tmkdir (void);         static void test_rmkdir (void);
static void test_trenameat (void);      static void test_rrenameat (void);
static void test_tunlinkat (void);      static void test_runlinkat (void);

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
    test_txattrwalk (); test_rxattrwalk ();
    test_txattrcreate (); test_rxattrcreate ();
    test_treaddir ();   test_rreaddir ();
    test_tfsync ();     test_rfsync ();
    test_tlock ();      test_rlock ();
    test_tgetlock ();   test_rgetlock ();
    test_tlink ();      test_rlink ();
    test_tmkdir ();     test_rmkdir ();
    test_trenameat ();  test_rrenameat ();
    test_tunlinkat ();  test_runlinkat ();

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
    char s[256];

    printf ("%s(%d): %d\n", fun, type, fc->size);
    np_set_tag (fc, 42);

    /* see conn.c:np_conn_new_incall () */
    if (!(fc2 = malloc (sizeof (*fc2) + TEST_MSIZE)))
        msg_exit ("out of memory");
    fc2->pkt = (u8 *)fc2 + sizeof (*fc2);

    /* see conn.c::np_conn_read_proc */
    memcpy (fc2->pkt, fc->pkt, fc->size);
    if (!np_deserialize (fc2))
        msg_exit ("np_deserialize error in %s", fun);

    /* check a few things */
    if (fc->type != type)
        msg_exit ("incorrect type in %s", fun);
    if (fc->size != fc2->size)
        msg_exit ("size mismatch in %s", fun);
    if (fc->type != fc2->type)
        msg_exit ("type mismatch in %s", fun);

    np_snprintfcall (s, sizeof (s), fc);
    printf ("%s\n", s);

    return fc2;
}

static void
test_rlerror (void)
{
    Npfcall *fc, *fc2;
    char buf[STATIC_RLERROR_SIZE];

    if (!(fc = np_create_rlerror (42)))
        msg_exit ("out of memory");
    fc2 = _rcv_buf (fc, P9_RLERROR,  __FUNCTION__);

    assert (fc->u.rlerror.ecode == fc2->u.rlerror.ecode);

    free (fc);
    free (fc2);

    fc = np_create_rlerror_static (42, buf, sizeof(buf));
    fc2 = _rcv_buf (fc, P9_RLERROR,  __FUNCTION__);

    assert (fc->u.rlerror.ecode == fc2->u.rlerror.ecode);

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

    assert (fc->u.tlopen.fid == fc2->u.tlopen.fid);
    assert (fc->u.tlopen.flags == fc2->u.tlopen.flags);

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

    assert (fc->u.rlopen.qid.type == fc2->u.rlopen.qid.type);
    assert (fc->u.rlopen.qid.version == fc2->u.rlopen.qid.version);
    assert (fc->u.rlopen.qid.path == fc2->u.rlopen.qid.path);
    assert (fc->u.rlopen.iounit == fc2->u.rlopen.iounit);

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

    assert (fc->u.tlcreate.fid == fc2->u.tlcreate.fid);
    assert (np_str9cmp (&fc->u.tlcreate.name, &fc2->u.tlcreate.name) == 0);
    assert (fc->u.tlcreate.flags == fc2->u.tlcreate.flags);
    assert (fc->u.tlcreate.mode == fc2->u.tlcreate.mode);
    assert (fc->u.tlcreate.gid == fc2->u.tlcreate.gid);

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

    assert (fc->u.rlcreate.qid.type == fc2->u.rlcreate.qid.type);
    assert (fc->u.rlcreate.qid.version == fc2->u.rlcreate.qid.version);
    assert (fc->u.rlcreate.qid.path == fc2->u.rlcreate.qid.path);
    assert (fc->u.rlcreate.iounit == fc2->u.rlcreate.iounit);

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

    assert (fc->u.tsymlink.fid == fc2->u.tsymlink.fid);
    assert (np_str9cmp (&fc->u.tsymlink.name, &fc2->u.tsymlink.name) == 0);
    assert (np_str9cmp (&fc->u.tsymlink.symtgt, &fc2->u.tsymlink.symtgt) == 0);
    assert (fc->u.tsymlink.gid == fc2->u.tsymlink.gid);

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

    assert (fc->u.rsymlink.qid.type == fc2->u.rsymlink.qid.type);
    assert (fc->u.rsymlink.qid.version == fc2->u.rsymlink.qid.version);
    assert (fc->u.rsymlink.qid.path == fc2->u.rsymlink.qid.path);

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

    assert (fc->u.tmknod.fid == fc2->u.tmknod.fid);
    assert (np_str9cmp (&fc->u.tmknod.name, &fc2->u.tmknod.name) == 0);
    assert (fc->u.tmknod.mode == fc2->u.tmknod.mode);
    assert (fc->u.tmknod.major == fc2->u.tmknod.major);
    assert (fc->u.tmknod.minor == fc2->u.tmknod.minor);
    assert (fc->u.tmknod.gid == fc2->u.tmknod.gid);

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

    assert (fc->u.rmknod.qid.type == fc2->u.rmknod.qid.type);
    assert (fc->u.rmknod.qid.version == fc2->u.rmknod.qid.version);
    assert (fc->u.rmknod.qid.path == fc2->u.rmknod.qid.path);

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

    assert (fc->u.trename.fid == fc2->u.trename.fid);
    assert (fc->u.trename.dfid == fc2->u.trename.dfid);
    assert (np_str9cmp (&fc->u.trename.name, &fc2->u.trename.name) == 0);

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

    assert (fc->u.treadlink.fid == fc2->u.treadlink.fid);

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

    assert (np_str9cmp (&fc->u.rreadlink.target, &fc2->u.rreadlink.target) == 0);

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

    assert (fc->u.tsetattr.fid == fc2->u.tsetattr.fid);
    assert (fc->u.tsetattr.valid == fc2->u.tsetattr.valid);
    assert (fc->u.tsetattr.mode == fc2->u.tsetattr.mode);
    assert (fc->u.tsetattr.uid == fc2->u.tsetattr.uid);
    assert (fc->u.tsetattr.gid == fc2->u.tsetattr.gid);
    assert (fc->u.tsetattr.size == fc2->u.tsetattr.size);
    assert (fc->u.tsetattr.atime_sec == fc2->u.tsetattr.atime_sec);
    assert (fc->u.tsetattr.atime_nsec == fc2->u.tsetattr.atime_nsec);
    assert (fc->u.tsetattr.mtime_sec == fc2->u.tsetattr.mtime_sec);
    assert (fc->u.tsetattr.mtime_nsec == fc2->u.tsetattr.mtime_nsec);

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

    free (fc);
    free (fc2);
}

static void
test_txattrwalk (void)
{
    Npfcall *fc, *fc2;

    if (!(fc = np_create_txattrwalk(1, 2, "abc")))
        msg_exit ("out of memory");
    fc2 = _rcv_buf (fc, P9_TXATTRWALK,  __FUNCTION__);

    assert (fc->u.txattrwalk.fid == fc2->u.txattrwalk.fid);
    assert (fc->u.txattrwalk.attrfid == fc2->u.txattrwalk.attrfid);
    assert (np_str9cmp (&fc->u.txattrwalk.name, &fc2->u.txattrwalk.name) == 0);

    free (fc);
    free (fc2);
}

static void
test_rxattrwalk (void)
{
    Npfcall *fc, *fc2;

    if (!(fc = np_create_rxattrwalk(1)))
        msg_exit ("out of memory");
    fc2 = _rcv_buf (fc, P9_RXATTRWALK,  __FUNCTION__);

    assert (fc->u.rxattrwalk.size == fc2->u.rxattrwalk.size);

    free (fc);
    free (fc2);
}

static void
test_txattrcreate (void)
{
    Npfcall *fc, *fc2;

    if (!(fc = np_create_txattrcreate(1, "abc", 3, 4)))
        msg_exit ("out of memory");
    fc2 = _rcv_buf (fc, P9_TXATTRCREATE,  __FUNCTION__);

    assert (fc->u.txattrcreate.fid == fc2->u.txattrcreate.fid);
    assert (np_str9cmp (&fc->u.txattrcreate.name, &fc2->u.txattrcreate.name) == 0);
    assert (fc->u.txattrcreate.size == fc2->u.txattrcreate.size);
    assert (fc->u.txattrcreate.flag == fc2->u.txattrcreate.flag);

    free (fc);
    free (fc2);
}

static void
test_rxattrcreate (void)
{
    Npfcall *fc, *fc2;

    if (!(fc = np_create_rxattrcreate()))
        msg_exit ("out of memory");
    fc2 = _rcv_buf (fc, P9_RXATTRCREATE,  __FUNCTION__);

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

    assert (fc->u.treaddir.fid == fc2->u.treaddir.fid);
    assert (fc->u.treaddir.offset == fc2->u.treaddir.offset);
    assert (fc->u.treaddir.count == fc2->u.treaddir.count);

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

    assert (fc->u.rreaddir.count == fc2->u.rreaddir.count);

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

    assert (fc->u.tfsync.fid == fc2->u.treaddir.fid);

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

    if (!(fc = np_create_tlock (1, P9_LOCK_TYPE_UNLCK, 3, 4, 5, 6, "xyz")))
        msg_exit ("out of memory");
    fc2 = _rcv_buf (fc, P9_TLOCK,  __FUNCTION__);

    assert (fc->u.tlock.fid == fc2->u.tlock.fid);
    assert (fc->u.tlock.type == fc2->u.tlock.type);
    assert (fc->u.tlock.flags == fc2->u.tlock.flags);
    assert (fc->u.tlock.start == fc2->u.tlock.start);
    assert (fc->u.tlock.length == fc2->u.tlock.length);
    assert (np_str9cmp (&fc->u.tlock.client_id, &fc2->u.tlock.client_id) == 0);

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

    assert (fc->u.rlock.status == fc2->u.rlock.status);

    free (fc);
    free (fc2);
}

static void
test_tgetlock (void)
{
    Npfcall *fc, *fc2;

    if (!(fc = np_create_tgetlock (1, P9_LOCK_TYPE_UNLCK, 3, 4, 5, "xyz")))
        msg_exit ("out of memory");
    fc2 = _rcv_buf (fc, P9_TGETLOCK,  __FUNCTION__);

    assert (fc->u.tgetlock.fid == fc2->u.tgetlock.fid);
    assert (fc->u.tgetlock.type == fc2->u.tgetlock.type);
    assert (fc->u.tgetlock.start == fc2->u.tgetlock.start);
    assert (fc->u.tgetlock.length == fc2->u.tgetlock.length);
    assert (fc->u.tgetlock.proc_id == fc2->u.tgetlock.proc_id);
    assert (np_str9cmp (&fc->u.tgetlock.client_id, &fc2->u.tgetlock.client_id) == 0);

    free (fc);
    free (fc2);
}

static void
test_rgetlock (void)
{
    Npfcall *fc, *fc2;

    if (!(fc = np_create_rgetlock (P9_LOCK_TYPE_WRLCK, 2, 3, 4, "xyz")))
        msg_exit ("out of memory");
    fc2 = _rcv_buf (fc, P9_RGETLOCK,  __FUNCTION__);

    assert (fc->u.rgetlock.type == fc2->u.rgetlock.type);
    assert (fc->u.rgetlock.start == fc2->u.rgetlock.start);
    assert (fc->u.rgetlock.length == fc2->u.rgetlock.length);
    assert (fc->u.rgetlock.proc_id == fc2->u.rgetlock.proc_id);
    assert (np_str9cmp (&fc->u.rgetlock.client_id, &fc2->u.rgetlock.client_id) == 0);

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

    assert (fc->u.tlink.dfid == fc2->u.tlink.dfid);
    assert (fc->u.tlink.fid == fc2->u.tlink.fid);
    assert (np_str9cmp (&fc->u.tlink.name, &fc2->u.tlink.name) == 0);

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

    assert (fc->u.tmkdir.fid == fc2->u.tmkdir.fid);
    assert (np_str9cmp (&fc->u.tmkdir.name, &fc2->u.tmkdir.name) == 0);
    assert (fc->u.tmkdir.mode == fc2->u.tmkdir.mode);
    assert (fc->u.tmkdir.gid == fc2->u.tmkdir.gid);

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

    assert (fc->u.rmkdir.qid.type == fc2->u.rmkdir.qid.type);
    assert (fc->u.rmkdir.qid.version == fc2->u.rmkdir.qid.version);
    assert (fc->u.rmkdir.qid.path == fc2->u.rmkdir.qid.path);

    free (fc);
    free (fc2);
}

static void
test_trenameat (void)
{
    Npfcall *fc, *fc2;

    if (!(fc = np_create_trenameat (1, "abc", 2, "zyx")))
        msg_exit ("out of memory");
    fc2 = _rcv_buf (fc, P9_TRENAMEAT,  __FUNCTION__);

    assert (fc->u.trenameat.olddirfid == fc2->u.trenameat.olddirfid);
    assert (np_str9cmp (&fc->u.trenameat.oldname, &fc2->u.trenameat.oldname) == 0);
    assert (fc->u.trenameat.newdirfid == fc2->u.trenameat.newdirfid);
    assert (np_str9cmp (&fc->u.trenameat.newname, &fc2->u.trenameat.newname) == 0);

    free (fc);
    free (fc2);
}

static void
test_rrenameat (void)
{
    Npfcall *fc, *fc2;

    if (!(fc = np_create_rrenameat ()))
        msg_exit ("out of memory");
    fc2 = _rcv_buf (fc, P9_RRENAMEAT,  __FUNCTION__);

    free (fc);
    free (fc2);
}

static void
test_tunlinkat (void)
{
    Npfcall *fc, *fc2;

    if (!(fc = np_create_tunlinkat(1, "abc", 2)))
        msg_exit ("out of memory");
    fc2 = _rcv_buf (fc, P9_TUNLINKAT,  __FUNCTION__);

    assert (fc->u.tunlinkat.dirfid == fc2->u.tunlinkat.dirfid);
    assert (np_str9cmp (&fc->u.tunlinkat.name, &fc2->u.tunlinkat.name) == 0);
    assert (fc->u.tunlinkat.flags == fc2->u.tunlinkat.flags);

    free (fc);
    free (fc2);
}

static void
test_runlinkat (void)
{
    Npfcall *fc, *fc2;

    if (!(fc = np_create_runlinkat ()))
        msg_exit ("out of memory");
    fc2 = _rcv_buf (fc, P9_RUNLINKAT,  __FUNCTION__);

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
    assert (np_str9cmp (&fc->u.tversion.version, &fc2->u.tversion.version) == 0);

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
    assert (np_str9cmp (&fc->u.rversion.version, &fc2->u.rversion.version) == 0);

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

    assert (fc->u.tauth.afid == fc2->u.tauth.afid);
    assert (np_str9cmp (&fc->u.tauth.uname, &fc2->u.tauth.uname) == 0);
    assert (np_str9cmp (&fc->u.tauth.aname, &fc2->u.tauth.aname) == 0);
    assert (fc->u.tauth.n_uname == fc2->u.tauth.n_uname);

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

    assert (fc->u.rauth.qid.type == fc2->u.rauth.qid.type);
    assert (fc->u.rauth.qid.version == fc2->u.rauth.qid.version);
    assert (fc->u.rauth.qid.path == fc2->u.rauth.qid.path);

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

    assert (fc->u.tflush.oldtag == fc2->u.tflush.oldtag);

    free (fc);
    free (fc2);
}

static void
test_rflush (void)
{
    Npfcall *fc, *fc2;
    char buf[STATIC_RFLUSH_SIZE];

    if (!(fc = np_create_rflush ()))
        msg_exit ("out of memory in %s", __FUNCTION__); 
    fc2 = _rcv_buf (fc, P9_RFLUSH, __FUNCTION__);

    free (fc);
    free (fc2);

    if (!(fc = np_create_rflush_static (buf, sizeof(buf))))
        msg_exit ("out of memory in %s", __FUNCTION__); 
    fc2 = _rcv_buf (fc, P9_RFLUSH, __FUNCTION__);

    free (fc2);
}

static void
test_tattach (void)
{
    Npfcall *fc, *fc2;

    if (!(fc = np_create_tattach (1, 2, "abc", "xyz", 5)))
        msg_exit ("out of memory in %s", __FUNCTION__); 
    fc2 = _rcv_buf (fc, P9_TATTACH, __FUNCTION__);

    assert (fc->u.tattach.fid == fc2->u.tattach.fid);
    assert (fc->u.tattach.afid == fc2->u.tattach.afid);
    assert (np_str9cmp (&fc->u.tattach.uname, &fc2->u.tattach.uname) == 0);
    assert (np_str9cmp (&fc->u.tattach.aname, &fc2->u.tattach.aname) == 0);
    assert (fc->u.tattach.n_uname == fc2->u.tattach.n_uname);

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

    assert (fc->u.rattach.qid.type == fc2->u.rattach.qid.type);
    assert (fc->u.rattach.qid.version == fc2->u.rattach.qid.version);
    assert (fc->u.rattach.qid.path == fc2->u.rattach.qid.path);

    free (fc);
    free (fc2);
}

static void
test_twalk (void)
{
    Npfcall *fc, *fc2;
    char *wnames[P9_MAXWELEM] = {
        "abc", "def", "ghi", "jkl",
        "abc", "def", "ghi", "jkl",
        "abc", "def", "ghi", "jkl",
        "abc", "def", "ghi", "jkl",
    };
    int i;

    assert (P9_MAXWELEM == 16);
    if (!(fc = np_create_twalk (1, 2, P9_MAXWELEM, wnames)))
        msg_exit ("out of memory in %s", __FUNCTION__); 
    fc2 = _rcv_buf (fc, P9_TWALK, __FUNCTION__);

    assert (fc->u.twalk.fid == fc2->u.twalk.fid);
    assert (fc->u.twalk.newfid == fc2->u.twalk.newfid);
    assert (fc->u.twalk.nwname == fc2->u.twalk.nwname);
    assert (fc->u.twalk.nwname == P9_MAXWELEM);

    for (i = 0; i < P9_MAXWELEM; i++) {
        assert (np_str9cmp (&fc->u.twalk.wnames[i], &fc2->u.twalk.wnames[i]) ==0);
    }

    free (fc);
    free (fc2);
}

static void
test_rwalk (void)
{
    Npfcall *fc, *fc2;
    struct p9_qid wqids [P9_MAXWELEM] = { 
        { 1, 2, 3 }, { 4, 5, 6 }, { 7, 8, 9 }, { 10, 11, 12 },
        { 1, 2, 3 }, { 4, 5, 6 }, { 7, 8, 9 }, { 10, 11, 12 },
        { 1, 2, 3 }, { 4, 5, 6 }, { 7, 8, 9 }, { 10, 11, 12 },
        { 1, 2, 3 }, { 4, 5, 6 }, { 7, 8, 9 }, { 10, 11, 12 },
    };
    int i;

    assert (P9_MAXWELEM == 16);

    if (!(fc = np_create_rwalk (P9_MAXWELEM, wqids)))
        msg_exit ("out of memory in %s", __FUNCTION__); 
    fc2 = _rcv_buf (fc, P9_RWALK, __FUNCTION__);

    assert (fc->u.rwalk.nwqid == P9_MAXWELEM);
    assert (fc->u.rwalk.nwqid == fc2->u.rwalk.nwqid);

    for (i = 0; i < P9_MAXWELEM; i++) {
        assert (fc->u.rwalk.wqids[i].type == fc2->u.rwalk.wqids[i].type);
        assert (fc->u.rwalk.wqids[i].version == fc2->u.rwalk.wqids[i].version);
        assert (fc->u.rwalk.wqids[i].path == fc2->u.rwalk.wqids[i].path);
    }

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

    assert (fc->u.tread.fid == fc2->u.tread.fid);
    assert (fc->u.tread.offset == fc2->u.tread.offset);
    assert (fc->u.tread.count == fc2->u.tread.count);

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

    assert (fc->u.rread.count == fc2->u.rread.count);
    assert (memcmp (fc->u.rread.data, fc2->u.rread.data, fc->u.rread.count) == 0);

    free (fc);
    free (fc2);
}

static void
test_twrite (void)
{
    Npfcall *fc, *fc2;
    u8 buf[128];

    memset (buf, 0x0f, sizeof(buf));

    if (!(fc = np_create_twrite (1, 2, sizeof (buf), buf)))
        msg_exit ("out of memory in %s", __FUNCTION__); 
    fc2 = _rcv_buf (fc, P9_TWRITE, __FUNCTION__);

    assert (fc->u.twrite.fid == fc2->u.twrite.fid);
    assert (fc->u.twrite.offset == fc2->u.twrite.offset);
    assert (fc->u.twrite.count == fc2->u.twrite.count);
    assert (memcmp (fc->u.twrite.data, fc2->u.twrite.data, fc->u.twrite.count) == 0);

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

    assert (fc->u.rwrite.count == fc2->u.rwrite.count);

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

    assert (fc->u.tclunk.fid == fc2->u.tclunk.fid);

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

    assert (fc->u.tremove.fid == fc2->u.tremove.fid);

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

    free (fc);
    free (fc2);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
