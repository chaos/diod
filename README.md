### diod

`diod` is a multi-threaded, user space file server that speaks
[9P2000.L protocol](protocol.md).

### Building

#### On Debian
```
sudo apt-get install build-essential libpopt-dev ncurses-dev automake autoconf git pkgconf
sudo apt-get install lua5.1 liblua5.1-dev libmunge-dev libwrap0-dev libcap-dev libattr1-dev
./autogen.sh
./configure
make
make check
```

#### On Red Hat

```
sudo yum install epel-release gperftools-devel ncurses-devel automake autoconf libattr-devel
sudo yum install lua lua-devel munge-devel tcp_wrappers-devel libcap-devel pkgconf
./autogen.sh
./configure
make
make check
```

#### On FreeBSD

```
portmaster security/munge
portmaster lang/lua53
./autogen.sh
./configure
make
```

See also the remarks below if you want a server that supports impersonation
(access=user in v9fs).

### Kernel Client

The kernel 9P client, sometimes referred to as "v9fs", consists
of the `9p.ko` file system module, and its network transport module,
`9pnet.ko`.

Although the kernel client supports several 9P variants, diod only supports
9P2000.L, and only in its feature-complete form, as it appeared in 2.6.38.

Earlier versions of the kernel that do not support 9P2000.L will fail
at mount time when version negotiation fails.  Some pre-2.6.38 versions
of the kernel that have 9P2000.L but still send some 9P2000.u ops may
fail in less obvious ways.  Use a 2.6.38 or later kernel.

### Quick Start

Start the diod server in foreground, with protocol debugging to stderr,
no authentication, and one export:
```
sudo ./diod -f -d 1 -n -e /tmp/9
```

Mount it using the raw mount command:
```
sudo mount -t 9p -n 127.0.0.1 /mnt \
    -oaname=/tmp/9,version=9p2000.L,uname=root,access=user
```

Or (simpler), mount it using diodmount:
```
sudo ./diodmount -n localhost:/tmp/9 /mnt
```
Or (even simpler) if diodmount is installed as `/sbin/mount.diod`:
```
sudo mount -t diod -n localhost:/tmp/9 /mnt
```

### I/O forwarding on clusters:

On I/O node, set up `/etc/diod.conf` according to diod.conf(5), then:
```
chkconfig diod on
service diod start
```

On compute node, if I/O node is `fritz42`, add entries like this to
`/etc/fstab`:
```
fritz42:/g/g0  /g/g0         diod  default 0 0
```

Alternatively, use "zero-config" automounter method:
* set `DIOD_SERVERS="fritz42"` in `/etc/sysconfig/auto.diod1`
* add `/d /etc/auto.diod` to `/etc/auto.master`
Then:
```
mkdir /d
chkconfig autofs on
service autofs start
ln -s /d/g.g0 /g/g0
```

Note that at this point diod is only being tested with NFS file systems.
Use it with Lustre or GPFS at your own peril - but if you do, please
report issues!

### Impersonation on FreeBSD

FreeBSD does not support per-thread credentials.  If you want a diod server 
that supports v9fs' access=user, you can:
 * build diod with `--enable-impersonation` (disabled by default on FreeBSD)
 * install `net/nfs-ganesha-kmod` from ports (or at least the modules
   `setthreadgid`, `setthreadgroups` and `setthreaduid`) or from
   [source](https://github.com/nfs-ganesha/nfs-ganesha)
 * before stating diod, load the modules providing additional syscalls:
```
kldload /path/to/nfs-ganesha-kmod/setthreaduid/setthreaduid.ko
kldload /path/to/nfs-ganesha-kmod/setthreadgid/setthreadgid.ko
kldload /path/to/nfs-ganesha-kmod/setthreadgroups/setthreadgroups.ko
```

Please read nfs-ganesha-kmod's README first, and use at your own risk.

# Support

Use GitHub!

### Release

SPDX-License-Identifier: GPL-2.0-or-later
