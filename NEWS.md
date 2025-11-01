diod version 1.0.24 - 2015-03-31
--------------------------------

 * Replace sysv-init script with systemd unit file
 * Move fully to github (including docs)
 * Misc. minor updates, mostly tests and build environment, for rhel7

diod version 1.0.23 - 2014-08-26
--------------------------------

 * Add dist tag to release (TOSS-2662)
 * Minor autotools forward-portability (gcode-124)
 * Doc: AUTHORS, README, license headers updated
 * Stop tracking additional autotools build products.

diod version 1.0.22 - 2014-08-19
--------------------------------

 * Stop tracking autotools build products in git.
 * Fix assertion failure at srv.c:754 (gcode-122)
 * Portability fixes for musl libc (Felix Janda)
 * Change default rdma port to 5640 to match kernel
 * rdmatrans: only register the used buffer to the network
   adapter (Dominique Martinet)
 * fix configure --with-tcmalloc logic
 * libnpfs: fids cloned in xattrwalk should inherit flags
 * rdmatrans: compilation error

diod version 1.0.21 - 2013-05-31
--------------------------------

 * Require that "ctl" be explicitly exported.  This allows export options
   such as "ro", "noauth", and "privport" to apply to the ctl synthetic
   file system.
 * The `auth_required_ctl` config option introduced in 1.0.15 is now
   deprecated.
 * Add test coverage for `trusted.*` and `security.*` xattr namespaces.
 * Add more mmap test coverage.

diod version 1.0.20 - 2013-05-17
--------------------------------

 * Fix diodshowmount segfault on exit with `noauth`.
 * Add diodls `--privport` option.
 * Additional test coverage (atomic create test, pathwalk test)

diod version 1.0.19 - 2013-05-07
--------------------------------

 * Additional test coverage for xattrs: 1) Verify that setxattr flags
   `XATTR_CREATE`, `XATTR_REPLACE` work.  2) Verify that that removal of
   a nonexistent user xattr returns ENODATA
 * Fix failure of tests/user/t17 when SELinux is enabled (bug in the test)

diod version 1.0.18 - 2013-05-16
--------------------------------

 * Man page update.

diod version 1.0.17 - 2013-05-06
--------------------------------

 * RPM: add missing BuildRequires for 'libattr-devel'.

diod version 1.0.16 - 2013-05-03
--------------------------------

 * NEWS: fix typo

diod version 1.0.15 - 2013-05-03
--------------------------------

Add support for dracut bootstrap of 9nbd network block device.

 * Add xattr support (gcode-95)
 * New `dioddate` utility to synchronize system time with server's,
   and `auth_required_ctl` config option which allows MUNGE
   authentication to be disabled for the ctl synthetic file system.
 * Add `noauth` and `privport` export options which disable MUNGE auth,
   and require attaches to originate from a privileged port (512-1024)
   for a specific export.

diod version 1.0.14 - 2012-07-12
--------------------------------

 * Add mount.diod `--attach` and `--detach` options to support 9nbd
   (9P network block device).
 * Allow non-directories to be exported (gcode-113)
 * Return Rlerror(ENOENT) in response to Tauth if auth not required
   instead of Rlerror(0), which confused kernel 9P code.

diod version 1.0.13 - 2012-05-22
--------------------------------

 * Allow `DIOD_MOUNTOPTS` in /etc/sysconfig/auto.diod
 * Normalize `-s,--server NAME` option in diodcat, diodload, diodls,
   and diodshowmount utilities, where NAME can be `IP[:PORT]`, `HOST[:PORT]`,
   or `/path/to/socket`.
 * Add diodshowmount utility (gcode-111)
 * Add diodls utility
 * refactor and clarify error paths in ioctx open/close functions (gcode-108)
 * normalize error messages when ioctx state is not as expected (gcode-108)

diod version 1.0.12 - 2012-05-15
--------------------------------

 * Rework fdtrans somewhat for clarity (gcode-109)
 * Improve misc/t15: don't use munge auth or usleep (caused hang on build farm)
 * Improve misc/t15: update valgrind suppressions for 32b Ubuntu tests

diod version 1.0.11 - 2012-05-14
--------------------------------

 * Return EINVAL when a file is opened with `O_DIRECT` flag (gcode-110).
 * Drop unused maxmmap feature.
 * Fix possible race on dumpable flag (gcode-105)
 * Allow connections on UNIX domain sockets
 * Add test kern/t40: scrub a file (split off from kern/t35)
 * Add test misc/t15: run full server under valgrind

diod version 1.0.10 - 2012-03-30
--------------------------------

 * Fix `statfs_passthru` option which had no effect
 * Add tests/kern/t12 to verify creat modes
 * eliminate a false failure in tests/misc/t10
 * Update RPM BuildRequires for tcmalloc's package rename in EPEL.

diod version 1.0.9 - 2012-03-12
-------------------------------

* Additional changes to ensure cores and asserts are logged (gcode-105)

diod version 1.0.8 - 2012-03-09
-------------------------------

 * Enable core dumps for diod (gcode-105)
 * Add new assert macro that ensures assertions are captured in logs.
 * Interpret 9P2000.L open flags (gcode-101)
 * Avoid asserting in `ppool_fini` on SIGTERM (gcode-99)
 * Don't close ioctx if already open in `diod_lopen()`.

diod version 1.0.7 - 2012-03-07
-------------------------------

 * Change statfs to return `f_type == V9FS_MAGIC` by default, and add
  `statfs_passthru = 1` config to get old behavior (gcode-80)
 * Relax kern/t14 to work around 32 bit `f_fsid issue` (high byte -1)
   and allow `f_type` to be `V9FS_MAGIC` (gcode-54)
 * Allow `DAC_OVERRIDE` to work in --no-auth mode, which
   was causing kern/t31 to intermittently fail (gcode-94)
 * Build with -g so debuginfo package contains symbols (gcode-96)
 * Fix problem writing to files with mode g+w (gcode-98)
 * Expanded valgrind test coverage.
 * Fix `make dist` to include all source materials.

diod version 1.0.6 - 2012-03-02
-------------------------------

 * Set `maxmmap=0` by default as no performance benefit has been
   demonstrated and it increases our VM footprint.
 * Don't try to mmap zero length files (generates log noise).
 * Do not return clunk error in the event of munmap failure.

diod version 1.0.5 - 2012-02-29
-------------------------------

 * Add `maxmmap` tunable which will mmap the first `maxmmap` bytes of
   a shared file as a performance optimization for parallel reads
   (default 4194304).  This option only affects file systems exported
   with the `sharefd` attribute.

diod version 1.0.4 - 2012-02-28
-------------------------------

 * Fix locking problem in new ioctx code (gcode-97)
   This was causing the server to assert occasionally under load.

diod version 1.0.3 - 2012-02-24
-------------------------------

 * Fix bug in `exportopts` parsing.
 * Add diod option to override exportopts in config file:
   `-o,--export-opts opt[,opt]`

diod version 1.0.2 - 2012-02-24
-------------------------------

 * Add `exportopts` diod.conf config option.  This allows the new
   `sharefd` option to be enabled globally in conjunction with `exportall=1`.

diod version 1.0.1 - 2012-02-24
-------------------------------

 * Add `sharefd` export option which enables limited server-side
   sharing of file descriptors for files opened read-only.  The default
   is (still) for every client open to trigger a server open.

diod version 1.0.0 - 2012-02-17
-------------------------------

 * Disable verbose fidpool and flush debugging
 * Drop ctl:requests synthetic file
 * Don't cache request structs.

Pre-release Notes
-----------------

```
1.0-pre64:
	Set rwdepth=1 mount option until v9fs null deref is resolved.
	Avoid allocating memory in error response path
	Change the way a late flush is handled: don't suppress successful req

1.0-pre63:
	Avoid double close introduced in pre62 (gcode-92)
	Add 'loosefid' flag to work around buggy client (gcode-81)
	Fix fid accounting on flushed Tclunk/Tremove/Twalk (gcode-81)

1.0-pre62:
	Don't leak file descriptor if clunk is flushed.
	Really close down clunk/walk race on server (gcode-81)
	Better fix to server deadlock (gcode-90)

1.0-pre61:
	Drop srv->lock before calling np_req_unref (gcode-90)

1.0-pre60:
	Build with tcmalloc (google-perftools).

1.0-pre59:
	Enable keepalives and tune aggressively (gcode-88)
	[dtop] Server aname count should not count tpools with 0 active fids

1.0-pre58:
	Parameterize timestamps in protocol debug.

1.0-pre57:
	Set rwdepth=32 mount option (gcode-86)
	Turn off Nagle algorithm for streaming write performance.
	Add timestamps to protocol debug.

1.0-pre56:
	Destroy fids if clunk or remove is flushed (gcode-81)
	Include path in fidpool debug message (gcode-81)
	Add flush debugging flag, on by default (temporarily)
	Disable worker thread flush signaling (temporarily)

1.0-pre55:
	Include op in fidpool debug message (gcode-81)

1.0-pre54:
	Fix race that caused server to segfault (gcode-83)
	Improve fidpool debugging messages (gcode-81)

1.0-pre53:
	Rework fidpool code and add debugging for gcode-81
	Fidpool debugging is on by default (temporarily)
	Dtop shows time missing server was last seen.
	Add experimental suppoort for building with tcmalloc

1.0-pre52:
	Fix broken runtime test for supplementary group creds (gcode-53)
	Change dtop to show file system (aname) view by default.
	Fix automake gcode-seen on debian [E. Meshcheryakov]
	Update 'make dist' target to include some missing files.

1.0-pre51:
	Switch to new lock type defintions.
          NOTE: This will break regression tests on some non-x86_64
          architectures until they catch up with upstream kernel fs/9p.
	Return fid to pool before sending rename/clunk reply (gcode-81)

1.0-pre50:
	Fix dtop help window and rwsize counts.
	Various coverity fixes.

1.0-pre49:
	Fix histogram binning in dtop rwsize view.
	Add help command to dtop.
	Lay some groundwork for renameat/unlinkat support.

1.0-pre48:
	Add request age and status to ctl:requests synthetic file.
	Decode new lock/getlock type values (gcode-69).
	Add dtop screen for monitoring I/O read/write sizes.
	Add fsstress and fsx to internal test suite.

1.0-pre47:
	Don't mask close errors (gcode-71)
	Implement diod --rfdno,wfdno options to match v9fs (gcode-68)
	Fix transport read/write error logging.
	Change transport internal interface to message-oriented.
	Don't hardwire .L lock/getlock bits in test (gcode-69)
	Make setgroups() system call directly to kernel (gcode-53).
	Misc. llvm fixes [E. Meshcheryakov]
	Make unit tests work in fakeroot environment [E. Meshcheryakov]
	Prep for Infiniband RDMA transport work.
	Misc. coverity fixes.

1.0-pre46:
	Support hostlist:aname mount syntax (gcode-66)
	Fix transport abstraction to handle whole messages (gcode-67)
	Don't call setgroups if it affects entire process (gcode-53)
	Unmount servers on dtop shutdown (gcode-63)
	Drop an extra stat in setattr implementation and avoid potential race.

1.0-pre45:
	Call setgroups to get supplemental groups in cred (gcode-64)
	Make mount points appear empty (gcode-62)
	Eliminate extra seekdir/telldir in readdir handler
	Don't get fsid from statvfs (gcode-54)
	Misc. cosmetic coverity fixes and other cleanup.

1.0-pre44:
	Add "suppress" export option to filter exportall list (gcode-59)
	Add -p,--port option to dtop (former -p,--poll-period renamed to -P).
	Suppress Ubuntu FORTIFY for postmark compilation (gcode-56)
	Fix kern/t30 to not need nobody group (gcode-55)
	Handle host[:port] in DIOD_SERVERS (gcode-40)
	Handle multiple DIOD_SERVERS entries (gcode-48,57,58)
	Skip capability regression test if libcap not installed

1.0-pre43:
	Disable server DAC check and don't setgroups before handling requests.

1.0-pre42:
	Fix null deref on server conn create failure.

1.0-pre41:
	Add diodload utility for testing.
	Reduce number of read requests needed to handle synthetic file get.
	Careful which thread destroys conn (gcode-52)
	Better test coverage.
	Improved logging of source of EIO errors.

1.0-pre40:
	Handle flush expitiously on working requests (gcode-49)
	Parse new TRENAMEAT and TUNLINKAT ops.
	Set O_NONBLOCK on fd passed to kernel client.
	Show additional information in dtop and fix dtop reconnect logic.
	Add ctl:zero, ctl:null synthetic files for testing.

1.0-pre39:
	Fix a null pointer deref regression introduced in 1.0-pre37.

1.0-pre38:
	Fix a compiler warning that prevented RPM from building.

1.0-pre37:
	Drop per-threadpool locks and support for flushing working requests.
	Fix numerous small coverity problems.

1.0-pre36:
	Undo workaround for gcode-47.
	Fix numerous small coverity problems.

1.0-pre35:
	Never destroy tpools (temporary workaround for gcode-47).
	lcreate and lopen return iounit = 0 instead of st_blksize.
	Fix flush test kern/t35 (gcode-45)

1.0-pre34:
	Fix a missing Buildrequires for tcp_wrappers-devel in the spec file.

1.0-pre33:
	Add dtop monitoring tool.
	Fix deadlock when handling a P9_TFLUSH (gcode-44).

1.0-pre32:
	Fix two places where thread pool lock could be taken twice.
	Rework 'tpools' synthetic file content.
	Test cleanup.

1.0-pre31:
	Remove 'threadmode' option and make threadmode=aname the default.
	Fix ignored open flags in create case (gcode-43).
	Fix fsync returning EBADF on directory (gcode-42).
	Override squashuser when euid != 0 (gcode-41).
	Add --squashuser command line option.
	Improve test coverage.

1.0-pre30:
	Implement per-aname thread pools --threadmode=aname (gcode-26).
	Provide diodcat defaults for host, aname, and file.
	Fix diodcat segfault when file does not exist.
	Normalize logged errors in diod operation handlers.
	Export more performance data via ctl interface.
	Cleanup.

1.0-pre29:
	Fix uninitialized aname in diodmount (regression from pre28).
	Clunk afid if diodmount attach fails
	Mount synthetic file system as 'ctl' rather than nil aname (gcode-38).
	Implement ctl/exports file and have auto.diod script use it.
	On SIGHUP config reload, reset before reading config file value.

1.0-pre28:
	Rewrite npfile code and added some synthetic files for monitoring.
	Don't setfsgid to gid outside of user's sg set.
	Allow stand-alone ctime update in setattr, found by fstest (gcode-37)
	Add --no-userdb option to allow fstest to use random uids (gcode-37)
	Drop usercache on receipt of SIGHUP.
	Cleanup.

1.0-pre27:
        Updating from pre26:
        - Chkconfig diod on.  Service is now called 'diod' not 'diodctl'.
        - In diod.conf, rename 'diodctllisten' to 'listen' and use port 564.

	Now the changes:
	Serialize password/group lookups, log all errors, and use big buffers.
	Minimize password/group lookups by  briefly caching lookup results.
	Perform a late term abortion on diodctl.  Now we just start diod.
	Added diodcat utility and updated auto.diod to use it not diodctl
	Reworked the libnpclient library and used it to improve test coverage.
	Add better error handling for setfsuid/getfsuid.
	Fix a regression in 1.0-pre25 in the gid was never being reset.

1.0-pre26:
	Reopen logs after daemonization.

1.0-pre25:
	Fix the allsquash option and add 'squashuser' config option.
	Avoid unneded calls to setgroups/setfsuid/setfsgid.
	Improved logging for authentication and user handling.
	Cleanup.

1.0-pre24:
	Fix fid refcounting bug in remove handler (gcode-29)
	Add more verbose logging in auth path (gcode-28)
	Drop diod trans plugin and use fdtrans.
	Fix test suite deadlock	in Ubuntu 2.6.35 kernel.
	Drop -Werror from default CFLAGS
	Provide more sophisticated lua configure glue (gcode-21)
	Fix double-free with diod -c and !HAVE_LUA_H
	Fix various memory issues.
	Drop upool module and integrate UNIX user handling into libnpfs.

1.0-pre23:
	Make -s,--stdin option explicit in diod and diodctl.
	Have diod listen by default on the well known 9pfs port 564.
	Don't require explicit confirmation if munge isn't found (gcode-23)
	Terminate diod children when diodctl is terminated with SIGTERM.
	Reconfigure diod children when diodctl is reconfiged with SIGHUP.
	Implement orderly shutdown on SIGTERM in both servers and libnpfs.
	Don't error on missing config file if built without lua (gcode-24).
	Remove vestiges of atomic IO extensions.
	Get rid of superfluous connect errors from diodctl as it starts diod.
	Fix some small memory leaks/problems.
	Remove diod -s,--stats IO stats option.
	Drop DEBUG_9P_ERRORS debug bit.
	Prep libnpfs for extended attributes.
	Cleanup.

1.0-pre22:
	Fixed initialization problem in diodctl introduced in pre21.
	Properly handle msize negotiation.
	Improved cleanup during shutdown.

1.0-pre21: (do not use)
	Fix minor memory problems uncovered by valgrind.
	Improved test coverage.
	Cleanup.

1.0-pre20:
	Add 'allsquash' config option to remap all users to nobody.
	Improve portability to RHEL 5 (glibc-2.5) based systems.

1.0-pre19:
	Build liblsd with thread-safety enabled (gcode-18).
	Allow /d/ROOT as alias for exported root fs (/).

1.0-pre18:
	Allow file systems to be exported read-only with new export format.
	Add 'exportall' diod.conf option to export everything in /proc/mounts.
	Add diod -E,--export-all and diodctl -E,--export-all options.

1.0-pre17:
	Fix (another) typo in diodexp output that broke automounter.
	Optimize np_gets () to avoid duplicate P9_READ calls.
	Fix bug inheriting -n and -c options from diodctl to diod.

1.0-pre16:
	Service diodctl reload works now (without killing diodctl).
	Make it possible to set syslog level in config file.
	Cleanup.

1.0-pre15:
	Fix typo in diodexp output that broke automounter.

1.0-pre14:
	Chkconfig --add diodctl upon RPM installation.
	Fix 60s idle (disconnected) timeout on diod server spawned by diodctl.
	Config file renamed to /etc/diod.conf and install example (noreplace).
	Don't allow 'debuglevel', 'foreground', or 'diodpath' in config file.
	Set listen port with 'diodlisten' and 'diodctllisten' in config file.
	Allow diod to be run in the background again.
	diodexp: if hostlist, compute intersection of results.
	diodexp: allow 'foo-bar', 'foo.bar', and 'bar' as keys for /foo/bar.
	Misc. code cleanup and man page improvement.

1.0-pre13:
	Fix error handling in diod_sock_connect().
	Rework automounter integration.

1.0-pre12:
	Rework mount helper options.

1.0-pre11:
	Resync with upstream 9P2000.L and rhel6.  Require 9P2000.L (2.6.38+).
	Re-integrate MUNGE using proper 9p authentication.
	Integrate diodctl and autofs (new tool: diodexp for program maps).
	Dumb down diodmount and turn it into /sbin/mount.diod mount helper.
	Restructure test framework to use socketpair client-server setups.
	Get libnpclient working and use it for auth and diodctl.
	Temporarily drop aread/awrite support.

1.0-pre10:
	Revert gcc-4.4 changes which were incorrect.

1.0-pre9:
	Fix warnings that prevented compilation with gcc-4.4

1.0-pre8:
	Fix wstat bug that broke chgrp.

1.0-pre7:
        Fix -L parsing bug that prevented diodctl from starting diod when
        not in debug mode.
        Drop diodmount -U option.
        Improved fcntl/flock testing in 'make check'.

1.0-pre6:
	Add diod -s filename option to capture I/O stats.
	Log any I/O errors to syslog, other minor logging changes.
	Incremental improvements to test suite.
	Add -L <log-dest> option to diod and diodctl.
	Fix bugs in diodctl init script and diodctl that broke restart.
	Set proctitle in diod instances to reflect usage.
	Add -j <jobid> to diodmount and support per-job server instances.
	Add -A <atomic-max> option to diod.

1.0-pre5:
        Diod is always started by diodctl.
        Simplify options on all commands.
        Fix some silly bugs.

1.0-pre4:
        Diodmount should always mount with debug=1 (show errors)
        Diodmount should drop datacheck option (it is implied now)
        Diodmount should  mount 9p2000.L not .H
        Add advisory locking implementation to diod.
        Fold 9p2000.H changes into 9p2000.L.

1.0-pre3:
        Add diodmount --verbose option, mount with -o datacheck.
        Drop readahead option

1.0-pre2:
        Fix some silly bugs in 1.0-pre1.
```
