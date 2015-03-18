### Overview

The diod server implements the 9P2000.L protocol, a variant of 9P from the
[Plan 9 Operating System](http://en.wikipedia.org/wiki/Plan_9_from_Bell_Labs).
9P2000.L consists of a subset of the canonical 9P2000 operations, the 9P2000.u
`attach` and `auth` messages, plus new operations designed to map to the
Linux VFS in a straightforward way. The v9fs client in linux kernel version
2.6.38 onward includes a more or less complete 9P2000.L implementation.

For a general introduction to 9P see the Plan 9
[intro(5)](http://plan9.bell-labs.com/magic/man2html/5/0intro) manual page.

### Data Structures

1, 2, 4, and 8 byte integers, denoted `name[1]`, `name[2]`, `name[4]`,
and `name[8]` respectively, are represend on the wire in little-endian
format (least significant byte first).

A string, denoted `name[s]`, is sent on the wire as
```
length[2] string[length]
```
with no terminating NULL.

A _qid_, a 13 byte value representing a unique file system object,
is represented on the wire as:
```
type[1] version[4] path[8]
```

### 9P2000 Operations (.L subset)

#### version -- negotiate protocol version
```
size[4] Tversion tag[2] msize[4] version[s]
size[4] Rversion tag[2] msize[4] version[s]
```
*version* establishes the `msize`, which is the maximum message size
inclusive of the size value that can be handled by both client and server.

It also establishes the protocol version. For 9P2000.L version must be the
string `9P2000.L`.

See the Plan 9 manual page for
[version(5)](http://plan9.bell-labs.com/magic/man2html/5/version).

### flush -- abort a message
```
size[4] Tflush tag[2] oldtag[2]
size[4] Rflush tag[2]
```
flush aborts an in-flight request referenced by oldtag, if any.

See the Plan 9 manual page for flush(5).

### walk -- descend a directory hierarchy
```
size[4] Twalk tag[2] fid[4] newfid[4] nwname[2] nwname*(wname[s])
size[4] Rwalk tag[2] nwqid[2] nwqid*(wqid[13])
```
walk is used to descend a directory represented by fid using successive path elements provided in the wname array. If succesful, newfid represents the new path.

fid can be cloned to newfid by calling walk with nwname set to zero.

See the Plan 9 manual page for walk(5).

### read, write -- transfer data from and to a file
```
size[4] Tread tag[2] fid[4] offset[8] count[4]
size[4] Rread tag[2] count[4] data[count]

size[4] Twrite tag[2] fid[4] offset[8] count[4] data[count]
size[4] Rwrite tag[2] count[4]
```
read and write perform I/O on the file represented by fid. Note that in v9fs, a read(2) or write(2) system call for a chunk of the file that wont fit in a single request is broken up into multiple requests.

Under 9P2000.L, read cannot be used on directories. See readdir below.

See the Plan 9 manual page for read(5).

#### clunk -- destroy a fid
```
size[4] Tclunk tag[2] fid[4]
size[4] Rclunk tag[2]
```
clunk signifies that fid is no longer needed by the client.

See the Plan 9 manual page for clunk(5).

### remove -- remove a file system object
```
size[4] Tremove tag[2] fid[4]
size[4] Rremove tag[2]
```
remove removes the file system object represented by fid.

See the Plan 9 manual page for remove(5).

This operation will eventually be replaced by unlinkat (see below).

### 9P2000.u Operations (.L subset)

#### attach, auth -- messages to establish a connection
```
size[4] Tauth tag[2] afid[4] uname[s] aname[s] n_uname[4]
size[4] Rauth tag[2] aqid[13]

size[4] Tattach tag[2] fid[4] afid[4] uname[s] aname[s] n_uname[4]
size[4] Rattach tag[2] qid[13]
```

auth initiates an authentication handshake for n_uname. Rlerror is returned if authentication is not required. If successful, afid is used to read/write the authentication handshake (protocol does not specify what is read/written), and afid is presented in the attach.

attach introduces a new user to the server, and establishes fid as the root for that user on the file tree selected by aname.

afid can be `P9_NOFID` (~0) or the fid from a previous auth handshake. The afid can be clunked immediately after the attach.

n_uname, if not set to `P9_NONUNAME` (~0), is the uid of the user and is used in preference to uname.

v9fs has several modes of access which determine how it uses attach. In the default access=user, an initial attach is sent for the user provided in the uname=name mount option, and for each user that accesses the file system thereafter. For access=<uid>, only the initial attach is sent for <uid> and all other users are denied access by the client.

See the Plan 9 manual page for attach(5) and the 9P2000.u experimetental-draft RFC entry for auth/attach.

### 9P2000.L Operations

#### lerror -- return error code
```
size[4] Rlerror tag[2] ecode[4]
```
lerror replaces the reply message used in a successful call. ecode is a numerical Linux errno.

See the Plan 9 manual page for error(5),

#### statfs -- get file system information
```
size[4] Tstatfs tag[2] fid[4]
size[4] Rstatfs tag[2] type[4] bsize[4] blocks[8] bfree[8] bavail[8]
                       files[8] ffree[8] fsid[8] namelen[4]
```
statfs is used to request file system information of the file system containing fid. The Rstatfs response corresponds to the fields returned by the statfs(2) system call, e.g.:
```
struct statfs {
    long    f_type;     /* type of file system (see below) */
    long    f_bsize;    /* optimal transfer block size */
    long    f_blocks;   /* total data blocks in file system */
    long    f_bfree;    /* free blocks in fs */
    long    f_bavail;   /* free blocks avail to non-superuser */
    long    f_files;    /* total file nodes in file system */
    long    f_ffree;    /* free file nodes in fs */
    fsid_t  f_fsid;     /* file system id */
    long    f_namelen;  /* maximum length of filenames */
};
```

#### lopen -- open a file
```
size[4] Tlopen tag[2] fid[4] flags[4]
size[4] Rlopen tag[2] qid[13] iounit[4]
```
lopen prepares fid for file I/O. flags contains Linux open(2) flags bits, e.g. O_RDONLY, O_RDWR, O_WRONLY.

See the Plan 9 manual page for open(5).

#### lcreate -- create regular file
```
size[4] Tlcreate tag[2] fid[4] name[s] flags[4] mode[4] gid[4]
size[4] Rlcreate tag[2] qid[13] iounit[4]
```
lcreate creates a regular file name in directory fid and prepares it for I/O.

fid initially represents the parent directory of the new file. After the call it represents the new file.

mode contains Linux creat(2) mode bits.

flags is used to pass Linux kernel intent bits (FIXME: diod ignores flags)

gid is the effective gid of the caller.

See the Plan 9 manual page for create(5),

#### symlink -- create symbolic link
```
size[4] Tsymlink tag[2] fid[4] name[s] symtgt[s] gid[4]
size[4] Rsymlink tag[2] qid[13]
```
symlink creates a symbolic link name in directory dfid. The link will point to symtgt.

gid is the effective group id of the caller.

The qid for the new symbolic link is returned in the reply.

#### mknod -- create a device node
```
size[4] Tmknod tag[2] dfid[4] name[s] mode[4] major[4] minor[4] gid[4]
size[4] Rmknod tag[2] qid[13]
```
mknod creates a device node name in directory dfid with major and minor numbers.

mode contains Linux mknod(2) mode bits.

gid is the effective group id of the caller.

The qid for the new device node is returned in the reply.

#### rename -- rename a file
```
size[4] Trename tag[2] fid[4] dfid[4] name[s]
size[4] Rrename tag[2]
```
rename renames a file system object referenced by fid, to name in the directory referenced by dfid.

This operation will eventually be replaced by renameat (see below).

#### readlink -- read value of symbolic link
```
size[4] Treadlink tag[2] fid[4]
size[4] Rreadlink tag[2] target[s]
```
readlink returns the contents of the symbolic link referenced by fid.

#### getattr -- get file attributes
```
size[4] Tgetattr tag[2] fid[4] request_mask[8]
size[4] Rgetattr tag[2] valid[8] qid[13] mode[4] uid[4] gid[4] nlink[8]
                 rdev[8] size[8] blksize[8] blocks[8]
                 atime_sec[8] atime_nsec[8] mtime_sec[8] mtime_nsec[8]
                 ctime_sec[8] ctime_nsec[8] btime_sec[8] btime_nsec[8]
                 gen[8] data_version[8]
```
getattr gets attributes of a file system object referenced by fid.

The response is intended to follow pretty closely the fields returned by the stat(2) system call:
```
struct stat {
    dev_t     st_dev;     /* ID of device containing file */
    ino_t     st_ino;     /* inode number */
    mode_t    st_mode;    /* protection */
    nlink_t   st_nlink;   /* number of hard links */
    uid_t     st_uid;     /* user ID of owner */
    gid_t     st_gid;     /* group ID of owner */
    dev_t     st_rdev;    /* device ID (if special file) */
    off_t     st_size;    /* total size, in bytes */
    blksize_t st_blksize; /* blocksize for file system I/O */
    blkcnt_t  st_blocks;  /* number of 512B blocks allocated */
    time_t    st_atime;   /* time of last access */
    time_t    st_mtime;   /* time of last modification */
    time_t    st_ctime;   /* time of last status change */
};
```
The differences are:

* st_dev is omitted
* st_ino is contained in the path component of qid
* times are nanosecond resolution
* btime, gen and data_version fields are reserved for future use

Not all fields are valid in every call. request_mask is a bitmask indicating which fields are requested. valid is a bitmask indicating which fields are valid in the response. The mask values are as follows:
```
#define P9_GETATTR_MODE         0x00000001ULL
#define P9_GETATTR_NLINK        0x00000002ULL
#define P9_GETATTR_UID          0x00000004ULL
#define P9_GETATTR_GID          0x00000008ULL
#define P9_GETATTR_RDEV         0x00000010ULL
#define P9_GETATTR_ATIME        0x00000020ULL
#define P9_GETATTR_MTIME        0x00000040ULL
#define P9_GETATTR_CTIME        0x00000080ULL
#define P9_GETATTR_INO          0x00000100ULL
#define P9_GETATTR_SIZE         0x00000200ULL
#define P9_GETATTR_BLOCKS       0x00000400ULL

#define P9_GETATTR_BTIME        0x00000800ULL
#define P9_GETATTR_GEN          0x00001000ULL
#define P9_GETATTR_DATA_VERSION 0x00002000ULL

#define P9_GETATTR_BASIC        0x000007ffULL /* Mask for fields up to BLOCKS */
#define P9_GETATTR_ALL          0x00003fffULL /* Mask for All fields above */
```

#### setattr -- set file attributes
```
size[4] Tsetattr tag[2] fid[4] valid[4] mode[4] uid[4] gid[4] size[8]
                 atime_sec[8] atime_nsec[8] mtime_sec[8] mtime_nsec[8]
size[4] Rsetattr tag[2]
```
setattr sets attributes of a file system object referenced by fid. As with getattr, valid is a bitmask selecting which fields to set, which can be any combination of:

mode - Linux chmod(2) mode bits.

uid, gid - New owner, group of the file as described in Linux chown(2).

size - New file size as handled by Linux truncate(2).

atime_sec, atime_nsec - Time of last file access.

mtime_sec, mtime_nsec - Time of last file modification.

The valid bits are defined as follows:
```
#define P9_SETATTR_MODE         0x00000001UL
#define P9_SETATTR_UID          0x00000002UL
#define P9_SETATTR_GID          0x00000004UL
#define P9_SETATTR_SIZE         0x00000008UL
#define P9_SETATTR_ATIME        0x00000010UL
#define P9_SETATTR_MTIME        0x00000020UL
#define P9_SETATTR_CTIME        0x00000040UL
#define P9_SETATTR_ATIME_SET    0x00000080UL
#define P9_SETATTR_MTIME_SET    0x00000100UL
```
If a time bit is set without the corresponding SET bit, the current system time on the server is used instead of the value sent in the request.

#### xattrwalk - prepare to read/list extended attributes
```
size[4] Txattrwalk tag[2] fid[4] newfid[4] name[s]
size[4] Rxattrwalk tag[2] size[8]
```
xattrwalk gets a newfid pointing to xattr name. This fid can later be used to read the xattr value. If name is NULL newfid can be used to get the list of extended attributes associated with the file system object.

#### xattrcreate -- prepare to set extended attribute
```
size[4] Txattrcreate tag[2] fid[4] name[s] attr_size[8] flags[4]
size[4] Rxattrcreate tag[2]
```
xattrcreate gets a fid pointing to the xattr name. This fid can later be used to set the xattr value.

flag is derived from set Linux setxattr. The manpage says 

> The flags parameter can be used to refine the semantics of the operation. XATTR_CREATE specifies a pure create, which fails if the named attribute exists already. XATTR_REPLACE specifies a pure replace operation, which fails if the named attribute does not already exist. By default (no flags), the extended attribute will be created if need be, or will simply replace the value if the attribute exists.

The actual setxattr operation happens when the fid is clunked. At that point the written byte count and the attr_size specified in TXATTRCREATE should be same otherwise an error will be returned.

#### readdir - read a directory
```
size[4] Treaddir tag[2] fid[4] offset[8] count[4]
size[4] Rreaddir tag[2] count[4] data[count]
```
readdir requests that the server return directory entries from the directory represented by fid, previously opened with lopen. offset is zero on the first call.

Directory entries are represented as variable-length records:
```
qid[13] offset[8] type[1] name[s]
```
At most count bytes will be returned in data. If count is not zero in the response, more data is available. On subsequent calls, offset is the offset returned in the last directory entry of the previous call.

#### fsync - flush any cached data to disk
```
size[4] Tfsync tag[2] fid[4]
size[4] Rfsync tag[2]
```
fsync tells the server to flush any cached data associated with fid, previously opened with lopen.

#### lock - acquire or release a POSIX record lock
```
size[4] Tlock tag[2] fid[4] type[1] flags[4] start[8] length[8] proc_id[4] client_id[s]
size[4] Rlock tag[2] status[1]
```
lock is used to acquire or release a POSIX record lock on fid and has semantics similar to Linux fcntl(F_SETLK).

type has one of the values:
```
#define P9_LOCK_TYPE_RDLCK 0
#define P9_LOCK_TYPE_WRLCK 1
#define P9_LOCK_TYPE_UNLCK 2
````
start, length, and proc_id correspond to the analagous fields passed to Linux fcntl(F_SETLK):
```
struct flock {
    short l_type;  /* Type of lock: F_RDLCK, F_WRLCK, F_UNLCK */
    short l_whence;/* How to interpret l_start: SEEK_SET, SEEK_CUR, SEEK_END */
    off_t l_start; /* Starting offset for lock */
    off_t l_len;   /* Number of bytes to lock */
    pid_t l_pid;   /* PID of process blocking our lock (F_GETLK only) */
};
```
flags bits are:
```
#define P9_LOCK_FLAGS_BLOCK 1    /* blocking request */
#define P9_LOCK_FLAGS_RECLAIM 2  /* reserved for future use */
```
client_id is an additional mechanism for uniquely identifying the lock requester and is set to the nodename by the Linux v9fs client.

status can be:
```
#define P9_LOCK_SUCCESS 0
#define P9_LOCK_BLOCKED 1
#define P9_LOCK_ERROR   2
#define P9_LOCK_GRACE   3
```
The Linux v9fs client implements the fcntl(F_SETLKW) (blocking) lock request by calling lock with P9_LOCK_FLAGS_BLOCK set. If the response is P9_LOCK_BLOCKED, it retries the lock request in an interruptible loop until status is no longer P9_LOCK_BLOCKED.

The Linux v9fs client translates BSD advisory locks (flock) to whole-file POSIX record locks. v9fs does not implement mandatory locks and will return ENOLCK if use is attempted.

Because of POSIX record lock inheritance and upgrade properties, pass-through servers must be implemented carefully.

#### getlock - test for the existence of a POSIX record lock
```
size[4] Tgetlock tag[2] fid[4] type[1] start[8] length[8] proc_id[4] client_id[s]
size[4] Rgetlock tag[2] type[1] start[8] length[8] proc_id[4] client_id[s]
```
getlock tests for the existence of a POSIX record lock and has semantics similar to Linux fcntl(F_GETLK).

As with lock, type has one of the values defined above, and start, length, and proc_id correspond to the analagous fields in struct flock passed to Linux fcntl(F_GETLK), and client_Id is an additional mechanism for uniquely identifying the lock requester and is set to the nodename by the Linux v9fs client.

#### link - create hard link
```
size[4] Tlink tag[2] dfid[4] fid[4] name[s]
size[4] Rlink tag[2]
```
link creates a hard link name in directory dfid. The link target is referenced by fid.

### mkdir - create directory
```
size[4] Tmkdir tag[2] dfid[4] name[s] mode[4] gid[4]
size[4] Rmkdir tag[2] qid[13]
```
mkdir creates a new directory name in parent directory dfid.

mode contains Linux mkdir(2) mode bits.

gid is the effective group ID of the caller.

The qid of the new directory is returned in the response.

#### renameat - rename a file or directory
```
size[4] Trenameat tag[2] olddirfid[4] oldname[s] newdirfid[4] newname[s]
size[4] Rrenameat tag[2]
```
Change the name of a file from oldname to newname, possible moving it from old directory represented by olddirfid to new directory represented by newdirfid.

If the server returns ENOTSUPP, the client should fall back to the rename operation.

#### unlinkat - unlink a file or directory
```
size[4] Tunlinkat tag[2] dirfd[4] name[s] flags[4]
size[4] Runlinkat tag[2]
```
Unlink name from directory represented by dirfd. If the file is represented by a fid, that fid is not clunked. If the server returns ENOTSUPP, the client should fall back to the remove operation.

### Sample Session
diod server is started:
```
/usr/sbin/diod -e /tmp/9 -d3 -l 0.0.0.0:1942
```
diod client (v9fs) is mounted:
```
mount -t 9p 192.168.50.135 /tmp/9 -ouname=root,port=1942,aname=/tmp/9,msize=65560,version=9p2000.L,debug=0x0,user=access
```
The client negotiates the protocol version and msize with version, then introduces itself with an attach as root. This attach is for the uname=root mount option. The client then runs a getattr on the mount point.
```
P9_TVERSION tag 65535 msize 65512 version '9P2000.L'
P9_RVERSION tag 65535 msize 65512 version '9P2000.L'
P9_TATTACH tag 1 fid 0 afid -1 uname 'root' aname '/tmp/9' n_uname P9_NONUNAME
P9_RATTACH tag 1 qid (00000000002c1fac 0 'd')
P9_TGETATTR tag 1 fid 0 request_mask 0x7ff
P9_RGETATTR tag 1 valid 0x7ff qid (00000000002c1fac 0 'd') mode 040755 uid 500 gid 500 nlink 56 rdev 0 size 0 blksize 4096 blocks 2664 atime Fri Feb  4 17:57:18 2011 mtime Fri Feb  4 17:45:07 2011 ctime Mon Feb  7 03:07:04 2011 btime X gen X data_version X
```
User uid=500 runs /bin/ls on the mount point. This triggers another attach on behalf of uid=500 (due to v9fs user=access default mode). The new user has a different fid (1) for the mount point, which it getattrs:
```
P9_TATTACH tag 1 fid 1 afid -1 uname '' aname '/tmp/9' n_uname 500
P9_RATTACH tag 1 qid (00000000002c1fac 0 'd')
P9_TGETATTR tag 1 fid 1 request_mask 0x3fff
P9_RGETATTR tag 1 valid 0x3fff qid (00000000002c1fac 0 'd') mode 040755 uid 500 gid 500 nlink 56 rdev 0 size 0 blksize 4096 blocks 2664 atime Fri Feb  4 17:57:18 2011 mtime Fri Feb  4 17:45:07 2011 ctime Mon Feb  7 03:07:04 2011 btime 0 gen 0 data_version 0
```
Next the users opendir triggers a clone (walk with nwnames=0) of fid (1) to fid (2), which is used in lopen to open the directory.
```
P9_TWALK tag 1 fid 1 newfid 2 nwname 0
P9_RWALK tag 1 nwqid 0
P9_TLOPEN tag 1 fid 2 mode 02304000
P9_RLOPEN tag 1 qid (00000000002c1fac 0 'd') iounit 0
```
This is followed by a readdir which transfers the entire directory in one request (4457 bytes). A second readdir returning no data signifies the end of the directory, which is then closed, resulting in a clunk of fid 2.
```
P9_TREADDIR tag 1 fid 2 offset 0 count 65488
P9_RREADDIR tag 1 count 4457
80000000 00ac1f2c 00000000 00010000 00000000 00040100 2e800000 0000ffff 
25000000 00002658 ea000000 00000402 002e2e00 00000000 d51d2c00 00000000 
P9_TREADDIR tag 1 fid 2 offset 2147483647 count 65488
P9_RREADDIR tag 1 count 0
P9_TCLUNK tag 1 fid 2
P9_RCLUNK tag 1
```
Now user uid=500 stats a nonexistent file with /bin/ls /tmp/9/foo. This triggers an error response (ecode=2, no such file or directory):
```
P9_TWALK tag 1 fid 1 newfid 2 nwname 1 'foo'
P9_RLERROR tag 1 ecode 2
```
Next, the file is created with echo hello >/tmp/9/foo:
```
P9_TWALK tag 1 fid 1 newfid 2 nwname 0
P9_RWALK tag 1 nwqid 0
P9_TLCREATE tag 1 fid 2 name 'foo' flags 0x8241 mode 0100644 gid 500
P9_RLCREATE tag 1 qid (00000000002c1bd1 0 '') iounit 4096
P9_TWALK tag 1 fid 1 newfid 3 nwname 1 'foo'
P9_RWALK tag 1 nwqid 1(00000000002c1bd1 0 '')
P9_TGETATTR tag 1 fid 3 request_mask 0x7ff
P9_RGETATTR tag 1 valid 0x7ff qid (00000000002c1bd1 0 '') mode 0100644 uid 500 gid 500 nlink 1 rdev 0 size 0 blksize 4096 blocks 0 atime Mon Feb  7 08:58:35 2011 mtime Mon Feb  7 08:58:35 2011 ctime Mon Feb  7 08:58:35 2011 btime X gen X data_version X
P9_TWRITE tag 1 fid 2 offset 0 count 6
68656c6c 6f0a
P9_RWRITE tag 1 count 6
P9_TCLUNK tag 1 fid 2
P9_RCLUNK tag 1
```
The file is unlinked with unlink /tmp/9/foo:
```
P9_TWALK tag 1 fid 3 newfid 2 nwname 0
P9_RWALK tag 1 nwqid 0
P9_TREMOVE tag 1 fid 2
P9_RREMOVE tag 1
```
A new directory is created with mkdir /tmp/9/newdir:
```
P9_TWALK tag 1 fid 1 newfid 2 nwname 1 'newdir'
P9_RLERROR tag 1 ecode 2
P9_TMKDIR tag 1 fid 1 name 'newdir' mode 040755 gid 500
P9_RMKDIR tag 1 qid (00000000002c1bd1 0 'd')
```
A symlink is created with ln -s /tmp/9/foo /tmp/9/newsymlink:
```
P9_TWALK tag 1 fid 1 newfid 4 nwname 1 'newsymlink'
P9_RLERROR tag 1 ecode 2
P9_TSYMLINK tag 1 fid 1 name 'newsymlink' symtgt '/tmp/9/newdir' gid 500
P9_RSYMLINK tag 1 qid (00000000002c1de3 0 'L')
```
The link contents are read with readlink /tmp/9/newsymlink:
```
P9_TWALK tag 1 fid 1 newfid 4 nwname 1 'newsymlink'
P9_RWALK tag 1 nwqid 1(00000000002c1de3 0 'L')
P9_TREADLINK tag 1 fid 4
P9_RREADLINK tag 1 target '/tmp/9/newdir'
```
Directory mode is changed with chmod 0 /tmp/9/newdir:
```
P9_TGETATTR tag 1 fid 2 request_mask 0x3fff
P9_RGETATTR tag 1 valid 0x3fff qid (00000000002c1bd1 0 'd') mode 040755 uid 500 gid 500 nlink 2 rdev 0 size 0 blksize 4096 blocks 8 atime Mon Feb  7 09:01:10 2011 mtime Mon Feb  7 09:01:10 2011 ctime Mon Feb  7 09:01:10 2011 btime 0 gen 0 data_version 0
P9_TSETATTR tag 1 fid 2 valid 0x41 mode 040000 uid X gid X size X atime X mtime X
P9_RSETATTR tag 1
```
A file is read with cat /tmp/9/foo2:
```
P9_TWALK tag 1 fid 6 newfid 5 nwname 0
P9_RWALK tag 1 nwqid 0
P9_TLOPEN tag 1 fid 5 mode 0100000
P9_RLOPEN tag 1 qid (00000000002c1de4 0 '') iounit 0
P9_TGETATTR tag 1 fid 6 request_mask 0x3fff
P9_RGETATTR tag 1 valid 0x3fff qid (00000000002c1de4 0 '') mode 0100644 uid 500 gid 500 nlink 1 rdev 0 size 6 blksize 4096 blocks 8 atime Mon Feb  7 09:07:20 2011 mtime Mon Feb  7 09:07:20 2011 ctime Mon Feb  7 09:07:20 2011 btime 0 gen 0 data_version 0
P9_TREAD tag 1 fid 5 offset 0 count 32768
P9_RREAD tag 1 count 6
68656c6c 6f0a
P9_TREAD tag 1 fid 5 offset 6 count 32768
P9_RREAD tag 1 count 0
P9_TCLUNK tag 1 fid 5
P9_RCLUNK tag 1
```
### References

VirtFS -- A virtualization aware File System pass-through, Jujjuri et al, 2010.

v9fs linux/Documentation/filesystems/9p.txt

Plan 9 Remote Resource Protocol Unix Extension experimental-draft-9P2000-unix-extension, Van Hensbergen, 2009.

Grave Robbers from Outer Space: Using 9P2000 Under Linux, Van Hensbergen et al, 2005.

Plan 9 Remote Resource Protocol 9p2000, Van Hensbergen, 2005.

Plan 9 from Bell Labs - Section 5 - Plan 9 File Protocol, 9P, Plan 9 Manual, 4nd edition, 2002.

The Use of Name Spaces in Plan 9 Pike et al, 1993.
