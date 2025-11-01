### diod

`diod` is a file server that speaks [9P2000.L protocol](protocol.md).

### Building and Installing

The basic installation instructions are:
```
$ ./configure
$ make
$ make check
$ sudo make install
```

Or for the minimalists:
```
$ ./configure --disable-diodmount --disable-config \
  --disable-auth --disable-multiuser
```

To build from the git tree, first do:
```
$ git clone https://github.com/chaos/diod.git
$ cd diod
$ ./autogen.sh
```

To build and install a test deb package (debian-based systems only):
```
$ sudo scripts/install-deps-deb.sh deb test
$ ./configure
$ make deb
$ cd debbuild
$ sudo dpkg -i *deb
```

NOTE:  Don't run `make check` as root.  Tests that require root will
attempt to use passwordless sudo.  If that is unavailable, those tests
are simply skipped.

### Quick Start: single-user

start the server:
```
$ diod --listen=127.0.0.1:9000 --no-auth --export=/home/bob
Listening on 127.0.0.1:9000
Only bob can attach and access files as bob
No authentication is required
```

mount the file system:
```
$ sudo mount -t 9p -n -o version=9p2000.L,trans=tcp \
  -o aname=/home/bob,uname=bob,access=1001,port=9000 127.0.0.1 /mnt/bob
```

### Quick Start: multi-user

start the server:
```
$ sudo diod --no-auth --export=/home
Listening on 0.0.0.0:564
Anyone can attach and access files as themselves
No authentication is required
```

mount the file system:
```
$ sudo mount.diod localhost:/home /mnt/home
```
or equivalently
```
$ sudo mount -t 9p -n -o version=9p2000.L,trans=tcp \
  -o aname=/home,uname=root,access=client 127.0.0.1 /mnt/home
```

### Should I use 9P instead of NFS?

If you are asking this here, you must mean `diod` with the Linux
[v9fs kernel client](https://docs.kernel.org/filesystems/9p.html).
Roughly speaking, `diod` in multi-user mode coupled with v9fs
`access=client,uname=root` approximates NFS, but be aware of
the following caveats:

 * If the server restarts or the client loses its connection, the
   client will need to be remounted.  There is no automatic recovery.
 * By default, there is no caching so interactive performance is poor.
   Several v9fs client caching options are available, with different
   trade-offs, however there is no mechanism in 9P to allow the server
   to invalidate a cached object.
 * Even when securely authenticated with MUNGE, connections are not
   protected against eavesdropping or other attacks.
 * File locking may not work as expected (test your use case!)

9P is compelling for its simplicity.

### License

SPDX-License-Identifier: GPL-2.0-or-later
