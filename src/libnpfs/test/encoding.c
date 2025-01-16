/*************************************************************\
 * Copyright (C) 2010 by Lawrence Livermore National Security, LLC.
 *
 * This file is part of npfs, a framework for 9P synthetic file systems.
 * For details see https://sourceforge.net/projects/npfs.
 *
 * SPDX-License-Identifier: MIT
 *************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <string.h>

#include "npfs.h"

#include "src/libtap/tap.h"

#define TEST_MSIZE 4096

/* Allocate a new Npfcall to "receive" raw data from fc->pkt.
 */
static Npfcall *
_rcv_buf (Npfcall *fc, int type)
{
    Npfcall *fc2;
    char s[256];

    np_set_tag (fc, 42);

    /* see conn.c:np_conn_new_incall () */
    if (!(fc2 = malloc (sizeof (*fc2) + TEST_MSIZE)))
        BAIL_OUT ("malloc failed");
    fc2->pkt = (u8 *)fc2 + sizeof (*fc2);

    /* see conn.c::np_conn_read_proc */
    memcpy (fc2->pkt, fc->pkt, fc->size);
    if (!np_deserialize (fc2)) {
        diag ("np_deserialize error");
        goto error;
    }

    /* check a few things */
    if (fc->type != type) {
        diag ("incorrect type");
        goto error;
    }
    if (fc->size != fc2->size) {
        diag ("size mismatch");
        goto error;
    }
    if (fc->type != fc2->type) {
        diag ("type mismatch");
        goto error;
    }

    np_snprintfcall (s, sizeof (s), fc);
    diag ("%s", s);

    return fc2;
error:
    free (fc2);
    return NULL;
}

static void
test_rlerror (void)
{
    Npfcall *fc, *fc2;
    char buf[STATIC_RLERROR_SIZE];

    fc = np_create_rlerror (42);
    ok (fc != NULL, "Rlerror encode ecode=42 works");
    fc2 = _rcv_buf (fc, Rlerror);
    ok (fc2 != NULL && fc->u.rlerror.ecode == fc2->u.rlerror.ecode,
        "Rlerror decode works");
    free (fc);
    free (fc2);

    fc = np_create_rlerror_static (42, buf, sizeof(buf));
    ok (fc != NULL, "Rlerror encode (static) ecode=42 works");
    fc2 = _rcv_buf (fc, Rlerror);
    ok (fc2 != NULL && fc->u.rlerror.ecode == fc2->u.rlerror.ecode,
        "Rlerror decode (from static) works");
    // fc is static memory
    free (fc2);
}

static void test_statfs (void)
{
    Npfcall *fc, *fc2;

    fc = np_create_tstatfs (42);
    ok (fc != NULL, "Tstatfs encode fid=42 works");
    fc2 = _rcv_buf (fc, Tstatfs);
    ok (fc2 != NULL && fc->u.tstatfs.fid == fc2->u.tstatfs.fid,
        "Tstatfs decode works");
    free (fc);
    free (fc2);

    fc = np_create_rstatfs (1, 2, 3, 4, 5, 6, 7, 8, 9);
    ok (fc != NULL, "Rstatfs type=1 bsize=2 blocks=3 bfree=4 bavail=5... works");
    fc2 = _rcv_buf (fc, Rstatfs);
    ok (fc2 != NULL
        && fc->u.rstatfs.type == fc2->u.rstatfs.type
        && fc->u.rstatfs.bsize == fc2->u.rstatfs.bsize
        && fc->u.rstatfs.blocks == fc2->u.rstatfs.blocks
        && fc->u.rstatfs.bfree == fc2->u.rstatfs.bfree
        && fc->u.rstatfs.bavail== fc2->u.rstatfs.bavail
        && fc->u.rstatfs.files == fc2->u.rstatfs.files
        && fc->u.rstatfs.ffree == fc2->u.rstatfs.ffree
        && fc->u.rstatfs.fsid == fc2->u.rstatfs.fsid
        && fc->u.rstatfs.namelen == fc2->u.rstatfs.namelen,
        "Rstatfs decode works");
    free (fc);
    free (fc2);
}

static void test_lopen (void)
{
    Npfcall *fc, *fc2;
    Npqid qid = { 1, 2, 3 };

    fc = np_create_tlopen (1, 2);
    ok (fc != NULL, "Tlopen encode fid=1 flags=2 works");
    fc2 = _rcv_buf (fc, Tlopen);
    ok (fc2 != NULL
        && fc->u.tlopen.fid == fc2->u.tlopen.fid
        && fc->u.tlopen.flags == fc2->u.tlopen.flags,
        "Tlopen decode works");
    free (fc);
    free (fc2);

    fc = np_create_rlopen (&qid, 2);
    ok (fc != NULL,
        "Rlopen encode qid.type=1 qid.version=2 qid.path=3 iounit=2 works");
    fc2 = _rcv_buf (fc, Rlopen);
    ok (fc2 != NULL
        && fc->u.rlopen.qid.type == fc2->u.rlopen.qid.type
        && fc->u.rlopen.qid.version == fc2->u.rlopen.qid.version
        && fc->u.rlopen.qid.path == fc2->u.rlopen.qid.path
        && fc->u.rlopen.iounit == fc2->u.rlopen.iounit,
        "Rlopen decode works");
    free (fc);
    free (fc2);
}

static void test_lcreate (void)
{
    Npfcall *fc, *fc2;
    Npqid qid = { 1, 2, 3 };

    fc = np_create_tlcreate (1, "xyz", 3, 4, 5);
    ok (fc != NULL, "Tlcreate encode fid=1 name=xyz flags=3 mode=4 gid=5 works");
    fc2 = _rcv_buf (fc, Tlcreate);
    ok (fc2 != NULL
        && fc->u.tlcreate.fid == fc2->u.tlcreate.fid
        && np_str9cmp (&fc->u.tlcreate.name, &fc2->u.tlcreate.name) == 0
        && fc->u.tlcreate.flags == fc2->u.tlcreate.flags
        && fc->u.tlcreate.mode == fc2->u.tlcreate.mode
        && fc->u.tlcreate.gid == fc2->u.tlcreate.gid,
        "Tlcreate decode works");
    free (fc);
    free (fc2);

    fc = np_create_rlcreate (&qid, 2);
    ok (fc != NULL, "Rlcreate encode qid.type=1 qid.version=2 qid.path=3 works");
    fc2 = _rcv_buf (fc, Rlcreate);
    ok (fc2 != NULL
        && fc->u.rlcreate.qid.type == fc2->u.rlcreate.qid.type
        && fc->u.rlcreate.qid.version == fc2->u.rlcreate.qid.version
        && fc->u.rlcreate.qid.path == fc2->u.rlcreate.qid.path
        && fc->u.rlcreate.iounit == fc2->u.rlcreate.iounit,
        "Rlcreate decode works");
    free (fc);
    free (fc2);
}

static void test_symlink (void)
{
    Npfcall *fc, *fc2;
    Npqid qid = { 1, 2, 3 };

    fc = np_create_tsymlink (1, "xyz", "abc", 4);
    ok (fc != NULL, "Tsymlink encode fid=1 name=xyz symtgt=abc gid=4 works");
    fc2 = _rcv_buf (fc, Tsymlink);
    ok (fc2 != NULL
        && fc->u.tsymlink.fid == fc2->u.tsymlink.fid
        && np_str9cmp (&fc->u.tsymlink.name, &fc2->u.tsymlink.name) == 0
        && np_str9cmp (&fc->u.tsymlink.symtgt, &fc2->u.tsymlink.symtgt) == 0
        && fc->u.tsymlink.gid == fc2->u.tsymlink.gid,
        "Tsymlink decode works");
    free (fc);
    free (fc2);

    fc = np_create_rsymlink (&qid);
    ok (fc != NULL, "Rsymlink encode qid.type=1 qid.version=2 qid.path=3 works");
    fc2 = _rcv_buf (fc, Rsymlink);
    ok (fc2 != NULL
        && fc->u.rsymlink.qid.type == fc2->u.rsymlink.qid.type
        && fc->u.rsymlink.qid.version == fc2->u.rsymlink.qid.version
        && fc->u.rsymlink.qid.path == fc2->u.rsymlink.qid.path,
        "Rsymlink decode works");
    free (fc);
    free (fc2);
}

static void test_mknod (void)
{
    Npfcall *fc, *fc2;
    Npqid qid = { 1, 2, 3 };

    fc = np_create_tmknod (1, "xyz", 3, 4, 5, 6);
    ok (fc != NULL,
        "Tmknod encode fid=1 name=xyz mode=3 major=4 minor=5 gid=6 works");
    fc2 = _rcv_buf (fc, Tmknod);
    ok (fc2 != NULL
        && fc->u.tmknod.fid == fc2->u.tmknod.fid
        && np_str9cmp (&fc->u.tmknod.name, &fc2->u.tmknod.name) == 0
        && fc->u.tmknod.mode == fc2->u.tmknod.mode
        && fc->u.tmknod.major == fc2->u.tmknod.major
        && fc->u.tmknod.minor == fc2->u.tmknod.minor
        && fc->u.tmknod.gid == fc2->u.tmknod.gid,
        "Tmknod decode works");
    free (fc);
    free (fc2);

    fc = np_create_rmknod (&qid);
    ok (fc != NULL, "Rmknod encode qid.type=1 qid.version=2 qid.path=3 works");
    fc2 = _rcv_buf (fc, Rmknod);
    ok (fc2 != NULL
        && fc->u.rmknod.qid.type == fc2->u.rmknod.qid.type
        && fc->u.rmknod.qid.version == fc2->u.rmknod.qid.version
        && fc->u.rmknod.qid.path == fc2->u.rmknod.qid.path,
        "Rmknod decode works");
    free (fc);
    free (fc2);
}

static void test_rename (void)
{
    Npfcall *fc, *fc2;

    fc = np_create_trename (1, 2, "xyz");
    ok (fc != NULL, "Trename encode fid=1 dfid=2 name=xyz works");
    fc2 = _rcv_buf (fc, Trename);
    ok (fc2 != NULL
        && fc->u.trename.fid == fc2->u.trename.fid
        && fc->u.trename.dfid == fc2->u.trename.dfid
        && np_str9cmp (&fc->u.trename.name, &fc2->u.trename.name) == 0,
        "Trename decode works");
    free (fc);
    free (fc2);

    fc = np_create_rrename ();
    ok (fc != NULL, "Rrename encode works");
    fc2 = _rcv_buf (fc, Rrename);
    ok (fc2 != NULL, "Rrename decode works");
    free (fc);
    free (fc2);
}

static void test_readlink (void)
{
    Npfcall *fc, *fc2;

    fc = np_create_treadlink (1);
    ok (fc != NULL, "Treadlink encode fid=1 works");
    fc2 = _rcv_buf (fc, Treadlink);
    ok (fc2 != NULL && fc->u.treadlink.fid == fc2->u.treadlink.fid,
        "Treadlink decode works");
    free (fc);
    free (fc2);

    fc = np_create_rreadlink ("xyz");
    ok (fc != NULL, "Rreadlink encode target=xyz works");
    fc2 = _rcv_buf (fc, Rreadlink);
    ok (fc2 != NULL
        && np_str9cmp (&fc->u.rreadlink.target, &fc2->u.rreadlink.target) == 0,
        "Rreadlink decode works");
    free (fc);
    free (fc2);
}

static void test_getattr (void)
{
    Npfcall *fc, *fc2;
    Npqid qid = { 1, 2, 3 };

    fc = np_create_tgetattr (42, 5000);
    ok (fc != NULL, "Tgetattr encode fid=42 request_mask=5000 works");
    fc2 = _rcv_buf (fc, Tgetattr);
    ok (fc2 != NULL
        && fc->u.tgetattr.fid == fc2->u.tgetattr.fid
        && fc->u.tgetattr.request_mask == fc2->u.tgetattr.request_mask,
        "Tgetattr decode works");
    free (fc);
    free (fc2);

    fc = np_create_rgetattr (1, &qid, 4, 5, 6, 7, 8, 9, 10, 11,
                             12, 13, 14, 15, 16, 17, 18, 19, 20, 21);
    ok (fc != NULL,
        "Rgetattr encode valid=1 qid.type=1 qid.version=2 qid.path=3... works");
    fc2 = _rcv_buf (fc, Rgetattr);
    ok (fc2 != NULL
        && fc->u.rgetattr.valid == fc2->u.rgetattr.valid
        && fc->u.rgetattr.qid.type == fc2->u.rgetattr.qid.type
        && fc->u.rgetattr.qid.version == fc2->u.rgetattr.qid.version
        && fc->u.rgetattr.qid.path == fc2->u.rgetattr.qid.path
        && fc->u.rgetattr.mode == fc2->u.rgetattr.mode
        && fc->u.rgetattr.uid == fc2->u.rgetattr.uid
        && fc->u.rgetattr.gid == fc2->u.rgetattr.gid
        && fc->u.rgetattr.nlink == fc2->u.rgetattr.nlink
        && fc->u.rgetattr.rdev == fc2->u.rgetattr.rdev
        && fc->u.rgetattr.size == fc2->u.rgetattr.size
        && fc->u.rgetattr.blksize == fc2->u.rgetattr.blksize
        && fc->u.rgetattr.blocks == fc2->u.rgetattr.blocks
        && fc->u.rgetattr.atime_sec == fc2->u.rgetattr.atime_sec
        && fc->u.rgetattr.atime_nsec == fc2->u.rgetattr.atime_nsec
        && fc->u.rgetattr.mtime_sec == fc2->u.rgetattr.mtime_sec
        && fc->u.rgetattr.mtime_nsec == fc2->u.rgetattr.mtime_nsec
        && fc->u.rgetattr.ctime_sec == fc2->u.rgetattr.ctime_sec
        && fc->u.rgetattr.ctime_nsec == fc2->u.rgetattr.ctime_nsec
        && fc->u.rgetattr.btime_sec == fc2->u.rgetattr.btime_sec
        && fc->u.rgetattr.btime_nsec == fc2->u.rgetattr.btime_nsec
        && fc->u.rgetattr.gen == fc2->u.rgetattr.gen
        && fc->u.rgetattr.data_version == fc2->u.rgetattr.data_version,
        "Rgetattr decode works");
    free (fc);
    free (fc2);
}

static void test_setattr (void)
{
    Npfcall *fc, *fc2;

    fc = np_create_tsetattr (1,2,3,4,5,6,7,8,9,10);
    ok (fc != NULL,
        "Tsetattr encode fid=1 valid=2 mode=3 uid=4 gid=5 size=6... works");
    fc2 = _rcv_buf (fc, Tsetattr);
    ok (fc2 != NULL
        && fc->u.tsetattr.fid == fc2->u.tsetattr.fid
        && fc->u.tsetattr.valid == fc2->u.tsetattr.valid
        && fc->u.tsetattr.mode == fc2->u.tsetattr.mode
        && fc->u.tsetattr.uid == fc2->u.tsetattr.uid
        && fc->u.tsetattr.gid == fc2->u.tsetattr.gid
        && fc->u.tsetattr.size == fc2->u.tsetattr.size
        && fc->u.tsetattr.atime_sec == fc2->u.tsetattr.atime_sec
        && fc->u.tsetattr.atime_nsec == fc2->u.tsetattr.atime_nsec
        && fc->u.tsetattr.mtime_sec == fc2->u.tsetattr.mtime_sec
        && fc->u.tsetattr.mtime_nsec == fc2->u.tsetattr.mtime_nsec,
        "Tsetattr decode works");
    free (fc);
    free (fc2);

    fc = np_create_rsetattr ();
    ok (fc != NULL, "Rsetattr encode works");
    fc2 = _rcv_buf (fc, Rsetattr);
    ok (fc2 != NULL, "Rsetattr decode works");
    free (fc);
    free (fc2);
}

static void test_xattrwalk (void)
{
    Npfcall *fc, *fc2;

    fc = np_create_txattrwalk(1, 2, "abc");
    ok (fc != NULL, "Txattrwalk encode fid=1 newfid=2 name=abc works");
    fc2 = _rcv_buf (fc, Txattrwalk);
    ok (fc2 != NULL
        && fc->u.txattrwalk.fid == fc2->u.txattrwalk.fid
        && fc->u.txattrwalk.attrfid == fc2->u.txattrwalk.attrfid
        && np_str9cmp (&fc->u.txattrwalk.name, &fc2->u.txattrwalk.name) == 0,
        "Txattrwalk decode works");
    free (fc);
    free (fc2);

    fc = np_create_rxattrwalk(1);
    ok (fc != NULL, "Rxattrwalk encode size=1 works");
    fc2 = _rcv_buf (fc, Rxattrwalk);
    ok (fc2 != NULL && fc->u.rxattrwalk.size == fc2->u.rxattrwalk.size,
        "Rxattrwalk decode works");
    free (fc);
    free (fc2);
}

static void test_xattrcreate (void)
{
    Npfcall *fc, *fc2;

    fc = np_create_txattrcreate(1, "abc", 3, 4);
    ok (fc != NULL,
        "Txattrcreate encode fid=1 name=abc attr_size=3 flags=4 works");
    fc2 = _rcv_buf (fc, Txattrcreate);
    ok (fc2 != NULL
        && fc->u.txattrcreate.fid == fc2->u.txattrcreate.fid
        && np_str9cmp (&fc->u.txattrcreate.name, &fc2->u.txattrcreate.name) == 0
        && fc->u.txattrcreate.size == fc2->u.txattrcreate.size
        && fc->u.txattrcreate.flag == fc2->u.txattrcreate.flag,
        "Txattrcreate decode works");
    free (fc);
    free (fc2);

    fc = np_create_rxattrcreate();
    ok (fc != NULL, "Rxattrcreate encode works");
    fc2 = _rcv_buf (fc, Rxattrcreate);
    ok (fc2 != NULL, "Rxattrcreate decode works");
    free (fc);
    free (fc2);
}

static void test_readdir (void)
{
    Npfcall *fc, *fc2;
    int n = 0, len = 256;
    Npqid qid[3] = { { 1, 2, 3 }, { 4, 5, 6 }, { 7, 8, 9 } }, qid2[3];
    char *name[3] = { "abc", "def", "ghi" }, name2[3][128];
    u64 offset;
    u8 type;

    fc = np_create_treaddir(1, 2, 3);
    ok (fc != NULL, "Treaddir encode fid=1 offset=2 count=3 works");
    fc2 = _rcv_buf (fc, Treaddir);
    ok (fc2 != NULL
        && fc->u.treaddir.fid == fc2->u.treaddir.fid
        && fc->u.treaddir.offset == fc2->u.treaddir.offset
        && fc->u.treaddir.count == fc2->u.treaddir.count,
        "Treaddir decode works");
    free (fc);
    free (fc2);

    fc = np_create_rreaddir (len);
    ok (fc != NULL,
        "Rreaddir encode len=256 works");
    n += np_serialize_p9dirent (&qid[0], 0, 1, name[0],
                                fc->u.rreaddir.data + n, len - n);
    n += np_serialize_p9dirent (&qid[1], 50, 2, name[1],
                                fc->u.rreaddir.data + n, len - n);
    n += np_serialize_p9dirent (&qid[2], 100, 3, name[2],
                                fc->u.rreaddir.data + n, len - n);
    ok (n < len, "Rreaddir encode three dirents didn't overflow");
    np_finalize_rreaddir (fc, n);
    fc2 = _rcv_buf (fc, Rreaddir);
    ok (fc2 != NULL && fc->u.rreaddir.count == fc2->u.rreaddir.count,
        "Rreaddir decode works");
    n = 0;
    n += np_deserialize_p9dirent (&qid2[0], &offset, &type, name2[0], 128,
                           fc2->u.rreaddir.data + n, fc2->u.rreaddir.count - n);
    ok (offset == 0 && type == 1 && strcmp (name2[0], name[0]) == 0,
        "Rreaddir decode dirent 1 works");
    n += np_deserialize_p9dirent (&qid2[1], &offset, &type, name2[1], 128,
                           fc2->u.rreaddir.data + n, fc2->u.rreaddir.count - n);
    ok (offset == 50 && type == 2 && strcmp (name2[1], name[1]) == 0,
        "Rreaddir decode dirent 2 works");
    n += np_deserialize_p9dirent (&qid2[2], &offset, &type, name2[2], 128,
                           fc2->u.rreaddir.data + n, fc2->u.rreaddir.count - n);
    ok (offset == 100 && type == 3 && strcmp (name2[2], name[2]) == 0,
        "Rreaddir decode dirent 3 works");
    ok (n == fc2->u.rreaddir.count, "Rreaddir message is fully consumed");
    free (fc);
    free (fc2);
}

static void test_fsync (void)
{
    Npfcall *fc, *fc2;

    fc = np_create_tfsync(1, 42);
    ok (fc != NULL, "Tfsync encode datasync=42 works");
    fc2 = _rcv_buf (fc, Tfsync);
    ok (fc2 != NULL
        && fc->u.tfsync.fid == fc2->u.tfsync.fid
        && fc->u.tfsync.datasync == fc2->u.tfsync.datasync,
        "Tfsync decode works");
    free (fc);
    free (fc2);

    fc = np_create_rfsync();
    ok (fc != NULL, "Rfsync encode works");
    fc2 = _rcv_buf (fc, Rfsync);
    ok (fc2 != NULL, "Rfsync decode works");

    free (fc);
    free (fc2);
}

static void test_lock (void)
{
    Npfcall *fc, *fc2;

    fc = np_create_tlock (1, Lunlck, 3, 4, 5, 6, "xyz");
    ok (fc != NULL,
        "Tlock encode fid=1 type=Lunlck flags=3 start=4 length=5 proc_id=6"
        " client_id=xyz works");
    fc2 = _rcv_buf (fc, Tlock);
    ok (fc2 != NULL
        && fc->u.tlock.fid == fc2->u.tlock.fid
        && fc->u.tlock.type == fc2->u.tlock.type
        && fc->u.tlock.flags == fc2->u.tlock.flags
        && fc->u.tlock.start == fc2->u.tlock.start
        && fc->u.tlock.length == fc2->u.tlock.length
        && np_str9cmp (&fc->u.tlock.client_id, &fc2->u.tlock.client_id) == 0,
        "Tlock decode works");
    free (fc);
    free (fc2);

    fc = np_create_rlock (1);
    ok (fc != NULL, "Rlock encode status=1 works");
    fc2 = _rcv_buf (fc, Rlock);
    ok (fc2 != NULL && fc->u.rlock.status == fc2->u.rlock.status,
        "Rlock decode works");
    free (fc);
    free (fc2);
}

static void test_getlock (void)
{
    Npfcall *fc, *fc2;

    fc = np_create_tgetlock (1, Lunlck, 3, 4, 5, "xyz");
    ok (fc != NULL,
        "Tgetlock encode fid=1 type=Lunlck start=3 length=4 proc_id=5"
        " client_id=xyz works");
    fc2 = _rcv_buf (fc, Tgetlock);
    ok (fc2 != NULL
        && fc->u.tgetlock.fid == fc2->u.tgetlock.fid
        && fc->u.tgetlock.type == fc2->u.tgetlock.type
        && fc->u.tgetlock.start == fc2->u.tgetlock.start
        && fc->u.tgetlock.length == fc2->u.tgetlock.length
        && fc->u.tgetlock.proc_id == fc2->u.tgetlock.proc_id
        && np_str9cmp (&fc->u.tgetlock.client_id, &fc2->u.tgetlock.client_id) == 0,
        "Tgetlock decode works");
    free (fc);
    free (fc2);

    fc = np_create_rgetlock (Lwrlck, 2, 3, 4, "xyz");
    ok (fc != NULL,
        "Rgetlock encode type=Lwrlck start=2 length=3 proc_id=4 client_id=xyz"
        " works");
    fc2 = _rcv_buf (fc, Rgetlock);
    ok (fc2 != NULL
        && fc->u.rgetlock.type == fc2->u.rgetlock.type
        && fc->u.rgetlock.start == fc2->u.rgetlock.start
        && fc->u.rgetlock.length == fc2->u.rgetlock.length
        && fc->u.rgetlock.proc_id == fc2->u.rgetlock.proc_id
        && np_str9cmp (&fc->u.rgetlock.client_id, &fc2->u.rgetlock.client_id) == 0,
        "Rgetlock decode works");
    free (fc);
    free (fc2);
}

static void test_link (void)
{
    Npfcall *fc, *fc2;

    fc = np_create_tlink (1, 2, "xyz");
    ok (fc != NULL, "Tlink encode dfid=1 fid=2 name=xyz works");
    fc2 = _rcv_buf (fc, Tlink);
    ok (fc2 != NULL
        && fc->u.tlink.dfid == fc2->u.tlink.dfid
        && fc->u.tlink.fid == fc2->u.tlink.fid
        && np_str9cmp (&fc->u.tlink.name, &fc2->u.tlink.name) == 0,
        "Tlink decode works");
    free (fc);
    free (fc2);

    fc = np_create_rlink ();
    ok (fc != NULL, "Rlink encode works");
    fc2 = _rcv_buf (fc, Rlink);
    ok (fc2 != NULL, "Rlink decodeworks");
    free (fc);
    free (fc2);
}

static void test_mkdir (void)
{
    Npfcall *fc, *fc2;
    Npqid qid = { 1, 2, 3 };

    fc = np_create_tmkdir (1, "abc", 2, 3);
    ok (fc != NULL, "Tmkdir encode dfid=1 name=abc mode=2 gid=3 works");
    fc2 = _rcv_buf (fc, Tmkdir);
    ok (fc2 != NULL
        && fc->u.tmkdir.fid == fc2->u.tmkdir.fid
        && np_str9cmp (&fc->u.tmkdir.name, &fc2->u.tmkdir.name) == 0
        && fc->u.tmkdir.mode == fc2->u.tmkdir.mode
        && fc->u.tmkdir.gid == fc2->u.tmkdir.gid,
        "Tmkdir decode works");
    free (fc);
    free (fc2);

    fc = np_create_rmkdir (&qid);
    ok (fc != NULL, "Rmkdir encode qid.type=1 qid.version=2 qid.path=3 works");
    fc2 = _rcv_buf (fc, Rmkdir);
    ok (fc2 != NULL
        && fc->u.rmkdir.qid.type == fc2->u.rmkdir.qid.type
        && fc->u.rmkdir.qid.version == fc2->u.rmkdir.qid.version
        && fc->u.rmkdir.qid.path == fc2->u.rmkdir.qid.path,
        "Rmkdir decode works");
    free (fc);
    free (fc2);
}

static void test_renameat (void)
{
    Npfcall *fc, *fc2;

    fc = np_create_trenameat (1, "abc", 2, "zyx");
    ok (fc != NULL,
        "Trenameat encode olddirfd=1 oldname=abc newdirfd=2 newname=zyx works");
    fc2 = _rcv_buf (fc, Trenameat);
    ok (fc2 != NULL
        && fc->u.trenameat.olddirfid == fc2->u.trenameat.olddirfid
        && np_str9cmp (&fc->u.trenameat.oldname, &fc2->u.trenameat.oldname) == 0
        && fc->u.trenameat.newdirfid == fc2->u.trenameat.newdirfid
        && np_str9cmp (&fc->u.trenameat.newname, &fc2->u.trenameat.newname) == 0,
        "Trenameat decode works");
    free (fc);
    free (fc2);

    fc = np_create_rrenameat ();
    ok (fc != NULL, "Rrenameat encode works");
    fc2 = _rcv_buf (fc, Rrenameat);
    ok (fc2 != NULL, "Rrenameat decode works");
    free (fc);
    free (fc2);
}

static void test_unlinkat (void)
{
    Npfcall *fc, *fc2;

    fc = np_create_tunlinkat(1, "abc", 2);
    ok (fc != NULL, "Tunlinkat encode dirfd=1 name=abc flags=2 works");
    fc2 = _rcv_buf (fc, Tunlinkat);
    ok (fc2 != NULL
        && fc->u.tunlinkat.dirfid == fc2->u.tunlinkat.dirfid
        && np_str9cmp (&fc->u.tunlinkat.name, &fc2->u.tunlinkat.name) == 0
        && fc->u.tunlinkat.flags == fc2->u.tunlinkat.flags,
        "Tunlinkat decode works");
    free (fc);
    free (fc2);

    fc = np_create_runlinkat ();
    ok (fc != NULL, "Runlinkat encode works");
    fc2 = _rcv_buf (fc, Runlinkat);
    ok (fc2 != NULL, "Runlinkat works");

    free (fc);
    free (fc2);
}

static void test_version (void)
{
    Npfcall *fc, *fc2;

    fc = np_create_tversion (TEST_MSIZE, "9p2000.L");
    ok (fc != NULL, "Tversion encode msize=%d version=9p2000.L works", TEST_MSIZE);
    fc2 = _rcv_buf (fc, Tversion);
    ok (fc2 != NULL
        && fc->u.tversion.msize == fc2->u.tversion.msize
        && np_str9cmp (&fc->u.tversion.version, &fc2->u.tversion.version) == 0,
        "Tversion decode works");
    free (fc);
    free (fc2);

    fc = np_create_rversion (TEST_MSIZE, "9p2000.L");
    ok (fc != NULL, "Rversion encode msize=%d version=9p2000.L works", TEST_MSIZE);
    fc2 = _rcv_buf (fc, Rversion);
    ok (fc2 != NULL
        && fc->u.rversion.msize == fc2->u.rversion.msize
        && np_str9cmp (&fc->u.rversion.version, &fc2->u.rversion.version) == 0,
        "Rversion decode works");
    free (fc);
    free (fc2);
}

static void test_auth (void)
{
    Npfcall *fc, *fc2;
    Npqid qid = { 1, 2, 3 };

    fc = np_create_tauth (1, "abc", "xyz", 4);
    ok (fc != NULL, "Tauth encode afid=1 uname=abc aname=xyz n_uname=4 works");
    fc2 = _rcv_buf (fc, Tauth);
    ok (fc2 != NULL
        && fc->u.tauth.afid == fc2->u.tauth.afid
        && np_str9cmp (&fc->u.tauth.uname, &fc2->u.tauth.uname) == 0
        && np_str9cmp (&fc->u.tauth.aname, &fc2->u.tauth.aname) == 0
        && fc->u.tauth.n_uname == fc2->u.tauth.n_uname,
        "Tauth decode works");
    free (fc);
    free (fc2);

    fc = np_create_rauth (&qid);
    ok (fc != NULL, "Rauth encode qid.type=1 qid.version=2 qid.path=3 works");
    fc2 = _rcv_buf (fc, Rauth);
    ok (fc2 != NULL
        && fc->u.rauth.qid.type == fc2->u.rauth.qid.type
        && fc->u.rauth.qid.version == fc2->u.rauth.qid.version
        && fc->u.rauth.qid.path == fc2->u.rauth.qid.path,
        "Rauth decode works");
    free (fc);
    free (fc2);
}

static void test_flush (void)
{
    Npfcall *fc, *fc2;
    char buf[STATIC_RFLUSH_SIZE];

    fc = np_create_tflush (1);
    ok (fc != NULL, "Tflush encode oldtag=1 works");
    fc2 = _rcv_buf (fc, Tflush);
    ok (fc2 != NULL && fc->u.tflush.oldtag == fc2->u.tflush.oldtag,
        "Tflush decode works");
    free (fc);
    free (fc2);

    fc = np_create_rflush ();
    ok (fc != NULL, "Rflush encode works");
    fc2 = _rcv_buf (fc, Rflush);
    ok (fc2 != NULL, "Rflush decode works");
    free (fc);
    free (fc2);

    fc = np_create_rflush_static (buf, sizeof(buf));
    ok (fc != NULL, "Rflush encode (static) works");
    fc2 = _rcv_buf (fc, Rflush);
    ok (fc2 != NULL, "Rflush decode (from static) works");
    // fc is static memory
    free (fc2);
}

static void test_attach (void)
{
    Npfcall *fc, *fc2;
    Npqid qid = { 1, 2, 3 };

    fc = np_create_tattach (1, 2, "abc", "xyz", 5);
    ok (fc != NULL,
        "Tattach encode fid=1 afid=2 uname=abc aname=xyz, n_uname=5 works");
    fc2 = _rcv_buf (fc, Tattach);
    ok (fc2 != NULL
        && fc->u.tattach.fid == fc2->u.tattach.fid
        && fc->u.tattach.afid == fc2->u.tattach.afid
        && np_str9cmp (&fc->u.tattach.uname, &fc2->u.tattach.uname) == 0
        && np_str9cmp (&fc->u.tattach.aname, &fc2->u.tattach.aname) == 0
        && fc->u.tattach.n_uname == fc2->u.tattach.n_uname,
        "Tattach decode works");
    free (fc);
    free (fc2);

    fc = np_create_rattach (&qid);
    ok (fc != NULL, "Rattach encode qid.type=1 qid.version=2 qid.path=3 works");
    fc2 = _rcv_buf (fc, Rattach);
    ok (fc2 != NULL
        && fc->u.rattach.qid.type == fc2->u.rattach.qid.type
        && fc->u.rattach.qid.version == fc2->u.rattach.qid.version
        && fc->u.rattach.qid.path == fc2->u.rattach.qid.path,
        "Rattach decode works");
    free (fc);
    free (fc2);
}

static void test_walk (void)
{
    Npfcall *fc, *fc2;
    char *wnames[MAXWELEM] = {
        "abc", "def", "ghi", "jkl",
        "abc", "def", "ghi", "jkl",
        "abc", "def", "ghi", "jkl",
        "abc", "def", "ghi", "jkl",
    };
    Npqid wqids [MAXWELEM] = {
        { 1, 2, 3 }, { 4, 5, 6 }, { 7, 8, 9 }, { 10, 11, 12 },
        { 1, 2, 3 }, { 4, 5, 6 }, { 7, 8, 9 }, { 10, 11, 12 },
        { 1, 2, 3 }, { 4, 5, 6 }, { 7, 8, 9 }, { 10, 11, 12 },
        { 1, 2, 3 }, { 4, 5, 6 }, { 7, 8, 9 }, { 10, 11, 12 },
    };
    int i;

    if (MAXWELEM != 16)
        BAIL_OUT ("MAXWELEM != 16");
    fc = np_create_twalk (1, 2, MAXWELEM, wnames);
    ok (fc != NULL, "Twalk encode fid=1 newfid=2 nwname=16 works");
    fc2 = _rcv_buf (fc, Twalk);
    ok (fc2 != NULL
        && fc->u.twalk.fid == fc2->u.twalk.fid
        && fc->u.twalk.newfid == fc2->u.twalk.newfid
        && fc->u.twalk.nwname == fc2->u.twalk.nwname
        && fc->u.twalk.nwname == MAXWELEM,
        "Twalk decode works");
    int errors = 0;
    for (i = 0; i < MAXWELEM; i++) {
        if (np_str9cmp (&fc->u.twalk.wnames[i], &fc2->u.twalk.wnames[i]) != 0)
            errors++;
    }
    ok (errors == 0, "Twalk decoded wnames correctly");
    free (fc);
    free (fc2);

    fc = np_create_rwalk (MAXWELEM, wqids);
    ok (fc != NULL, "Rwalk encode nwqid=16 works");
    fc2 = _rcv_buf (fc, Rwalk);
    ok (fc2 != NULL
        && fc->u.rwalk.nwqid == MAXWELEM
        && fc->u.rwalk.nwqid == fc2->u.rwalk.nwqid,
        "Rwalk decode works");
    errors = 0;
    for (i = 0; i < MAXWELEM; i++) {
        if (fc->u.rwalk.wqids[i].type != fc2->u.rwalk.wqids[i].type
            || fc->u.rwalk.wqids[i].version != fc2->u.rwalk.wqids[i].version
            || fc->u.rwalk.wqids[i].path != fc2->u.rwalk.wqids[i].path)
            errors++;
    }
    ok (errors == 0, "Rwalk decoded qids correctly");
    free (fc);
    free (fc2);
}

static void test_read (void)
{
    Npfcall *fc, *fc2;
    u8 buf[128];

    fc = np_create_tread (1, 2, 3);
    ok (fc != NULL, "Tread encode fid=1 offset=2 count=3 works");
    fc2 = _rcv_buf (fc, Tread);
    ok (fc2 != NULL
        && fc->u.tread.fid == fc2->u.tread.fid
        && fc->u.tread.offset == fc2->u.tread.offset
        && fc->u.tread.count == fc2->u.tread.count,
        "Tread decode works");
    free (fc);
    free (fc2);

    memset (buf, 0xf0, sizeof(buf));
    fc = np_create_rread (sizeof (buf), buf);
    ok (fc != NULL, "Rread encode count=128 works");
    np_set_rread_count (fc, sizeof (buf));
    fc2 = _rcv_buf (fc, Rread);
    ok (fc2 != NULL
        && fc->u.rread.count == fc2->u.rread.count
        && memcmp (fc->u.rread.data, fc2->u.rread.data, fc->u.rread.count) == 0,
        "Rread decode works");
    free (fc);
    free (fc2);
}

static void test_write (void)
{
    Npfcall *fc, *fc2;
    u8 buf[128];

    memset (buf, 0x0f, sizeof(buf));

    fc = np_create_twrite (1, 2, sizeof (buf), buf);
    ok (fc != NULL, "Twrite encode fid=1 offset=2 count=128 works");
    fc2 = _rcv_buf (fc, Twrite);
    ok (fc2 != NULL
        && fc->u.twrite.fid == fc2->u.twrite.fid
        && fc->u.twrite.offset == fc2->u.twrite.offset
        && fc->u.twrite.count == fc2->u.twrite.count
        && memcmp (fc->u.twrite.data, fc2->u.twrite.data, fc->u.twrite.count) == 0,
        "Twrite decode works");
    free (fc);
    free (fc2);

    fc = np_create_rwrite (1);
    ok (fc != NULL, "Rwrite encode count=1 works");
    fc2 = _rcv_buf (fc, Rwrite);
    ok (fc2 != NULL && fc->u.rwrite.count == fc2->u.rwrite.count,
        "Rwrite decode works");
    free (fc);
    free (fc2);
}

static void test_clunk (void)
{
    Npfcall *fc, *fc2;

    fc = np_create_tclunk (1);
    ok (fc != NULL, "Tclunk encode fid=1 works");
    fc2 = _rcv_buf (fc, Tclunk);
    ok (fc2 != NULL && fc->u.tclunk.fid == fc2->u.tclunk.fid,
        "Tclunk decode works");
    free (fc);
    free (fc2);

    fc = np_create_rclunk ();
    ok (fc != NULL, "Rclunk encode works");
    fc2 = _rcv_buf (fc, Rclunk);
    ok (fc2 != NULL, "Rclunk decode works");

    free (fc);
    free (fc2);
}

static void test_remove (void)
{
    Npfcall *fc, *fc2;

    fc = np_create_tremove (1);
    ok (fc != NULL, "Tremove encode fid=1 works");
    fc2 = _rcv_buf (fc, Tremove);
    ok (fc2 != NULL && fc->u.tremove.fid == fc2->u.tremove.fid,
        "Tremove decode works");
    free (fc);
    free (fc2);

    fc = np_create_rremove ();
    ok (fc != NULL, "Rremove encode works");
    fc2 = _rcv_buf (fc, Rremove);
    ok (fc2 != NULL, "Rremove decode works");
    free (fc);
    free (fc2);
}

static void test_truncated_tlink (void)
{
    Npfcall *fc;

    if (!(fc = np_create_tlink (1, 2, "xyz")))
        BAIL_OUT ("np_create_tlink failed");

    // truncate fc->pkt so entire name is missing
    fc->size -= 3;
    fc->pkt[0] = fc->size;
    fc->pkt[1] = fc->size >> 8;
    fc->pkt[2] = fc->size >> 16;
    fc->pkt[3] = fc->size >> 24;

    ok (np_deserialize (fc) == 0,
        "Tlink decode failed as expected on truncated message (issue#109)");

    free (fc);
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    test_rlerror ();
    test_statfs ();
    test_lopen ();
    test_lcreate ();
    test_symlink ();
    test_mknod ();
    test_rename ();
    test_readlink();
    test_getattr ();
    test_setattr ();
    test_xattrwalk ();
    test_xattrcreate ();
    test_readdir ();
    test_fsync ();
    test_lock ();
    test_getlock ();
    test_link ();
    test_mkdir ();
    test_renameat ();
    test_unlinkat ();
    test_version ();
    test_auth ();
    test_flush ();
    test_attach ();
    test_walk ();
    test_read ();
    test_write ();
    test_clunk ();
    test_remove ();

    test_truncated_tlink ();

    done_testing ();

    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
