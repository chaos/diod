/* diod_mount.c - mount all the file systems served by diod */

/* Usage: diodmount [-u] hostname
 *
 * What we do:
 * - mount (as user) hostname 9pfs port, aname="ctl" on /mnt/diod
 * - read list of exports from /exports, port of server from /server
 *   (this triggers spawn of server for user if non exists)
 * - create mount points
 * - issue mounts for each export (as user), don't create mtab entries
 * Normally this would be in a private namespace or a non-shared compute node.
 * If -u, same thing only unmount all the file systems.
 */

#include <stdlib.h>
#include <stdio.h>

int
main (int argc, char *argv[])
{
    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
