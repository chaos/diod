/*****************************************************************************
 *  Copyright (C) 2010 Lawrence Livermore National Security, LLC.
 *  Written by Jim Garlick <garlick@llnl.gov> LLNL-CODE-423279
 *  All Rights Reserved.
 *
 *  This file is part of the Distributed I/O Daemon (diod).
 *  For details, see <http://code.google.com/p/diod/>.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License (as published by the
 *  Free Software Foundation) version 2, dated June 1991.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY
 *  or FITNESS FOR A PARTICULAR PURPOSE. See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software Foundation,
 *  Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA or see
 *  <http://www.gnu.org/licenses/>.
 *****************************************************************************/

/* ops.c - file ops for diodctl */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#define _BSD_SOURCE         /* daemon */
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#if HAVE_GETOPT_H
#include <getopt.h>
#endif
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/param.h>
#include <string.h>
#include <sys/resource.h>
#include <poll.h>
#include <assert.h>
#include <sys/stat.h>
#include <sys/fcntl.h>

#include "9p.h"
#include "npfs.h"
#include "npfile.h"

#include "list.h"

#include "diod_log.h"
#include "diod_conf.h"
#include "diod_trans.h"
#include "diod_upool.h"
#include "diod_auth.h"
#include "diod_sock.h"

#include "serv.h"

static Npfile       *_ctl_root_create (void);
static Npfcall      *_ctl_attach (Npfid *fid, Npfid *nafid, Npstr *aname);

void
diodctl_register_ops (Npsrv *srv)
{
    npfile_init_srv (srv, _ctl_root_create ());
    srv->debuglevel = diod_conf_get_debuglevel ();
    srv->debugprintf = msg;
    srv->upool = diod_upool;
    srv->auth = diod_auth;
    srv->attach = _ctl_attach;
}

/* Tattach - announce a new user, and associate her fid with the root dir.
 */
static Npfcall*
_ctl_attach (Npfid *fid, Npfid *nafid, Npstr *aname)
{
    Npfile *root = (Npfile *)fid->conn->srv->treeaux;
    Npfcall *ret = NULL;
    Npfilefid *f;

    /* N.B. ignore aname (assume it's '/diodctl') */

    if (!npfile_checkperm (root, fid->user, 4)) {
        np_uerror (EPERM);
        msg ("diodctl_attach: root file mode denies access for user");
        goto done;
    }
    if (!(f = npfile_fidalloc (root, fid))) {
        msg ("diodctl_attach: out of memory");
        np_uerror (ENOMEM);
        goto done;
    }
    if (!(ret = np_create_rattach (&root->qid))) {
        msg ("diodctl_attach: out of memory");
        np_uerror (ENOMEM);
        goto done;
    }
    fid->aux = f;
    np_fid_incref (fid);
done:
    if (np_rerror ())
        npfile_fiddestroy (fid); /* frees fid->aux as Npfilefid* if not NULL */
    return ret;
}

/* Callback for root dir.
 */
static Npfile *
_root_first (Npfile *dir)
{
    if (dir->dirfirst)
        npfile_incref(dir->dirfirst);

    return dir->dirfirst;
}

/* Callback for root dir.
 */
static Npfile *
_root_next (Npfile *dir, Npfile *prevchild)
{
    if (prevchild->next)
        npfile_incref (prevchild->next);

    return prevchild->next;
}

/* Handle a read from the 'exports' file.
 */
static int
_exports_read (Npfilefid *file, u64 offset, u32 count, u8 *data, Npreq *req)
{
    char *s = file->aux;
    int len = s ? strlen (s) : 0;
    int cpylen = len - offset;

    if (cpylen > count)
        cpylen = count;
    if (cpylen < 0)
        cpylen = 0;
    if (cpylen > 0)
        memcpy (data, s + offset, cpylen);
    return cpylen;
}

static void
_exports_closefid (Npfilefid *file)
{
    if (file->aux) {
        free (file->aux);
        file->aux = NULL;
    } 
}

static char *
_strcpy_exports (void)
{
    List exports;
    ListIterator itr = NULL;
    char *ret = malloc (1);
    int len = 0, elen;
    Export *x;

    if (!ret)
        goto done;
    *ret = '\0';
    if (!(exports = diod_conf_get_exports ()))
        goto done;
    if (!(itr = list_iterator_create (exports)))
        goto done;
    while ((x = list_next (itr))) {
        elen = strlen (x->path) + 1;
        if (!(ret = realloc (ret, len + elen + 1)))
            goto done;
        snprintf (ret + len, elen + 1, "%s\n", x->path);
        len += elen;
    }
    ret[len] = '\0';
done:
    if (itr)
        list_iterator_destroy (itr);
    return ret;
}

static char *
_strcat_mounts (char *ret)
{
    List exports = NULL;
    ListIterator itr = NULL;
    int len, elen;
    Export *x;

    if (!ret)
        goto done;
    len = strlen (ret);
    if (!(exports = diod_conf_get_mounts ()))
        goto done;
    if (!(itr = list_iterator_create (exports)))
        goto done;
    while ((x = list_next (itr))) {
        elen = strlen (x->path) + 1;
        if (!(ret = realloc (ret, len + elen + 1)))
            goto done;
        snprintf (ret + len, elen + 1, "%s\n", x->path);
        len += elen;
    }
    ret[len] = '\0';
done:
    if (itr)
        list_iterator_destroy (itr);
    if (exports)
        list_destroy (exports);
    return ret;
}

static int
_exports_openfid (Npfilefid *file)
{
    assert (file->aux == NULL);
    if (!(file->aux = _strcpy_exports ())) {
        np_uerror (ENOMEM);
        return 0;
    }
    if (diod_conf_get_exportall ()) {
        if (!(file->aux = _strcat_mounts (file->aux))) {
            np_uerror (ENOMEM);
            return 0;
        }
    }
    return 1;
}

/* Handle a read from the 'ctl' file.
 */
static int
_ctl_read (Npfilefid* file, u64 offset, u32 count, u8* data, Npreq *req)
{
    Npfid *fid = file->fid;

    return diodctl_serv_readctl (fid->user, file->aux, offset, count, data);
}

/* Handle a write to the 'ctl' file.  Store the contents of the write
 * in file->aux as a null terminated string.
 */
static int
_ctl_write (Npfilefid* file, u64 offset, u32 count, u8* data, Npreq *req)
{
    char *s = file->aux;
    int ret = -1;

    if (!s && offset == 0) {
        if (!(s = malloc (count + 1))) {
            np_uerror (ENOMEM);
            goto done;
        }
        memcpy (s, data, count);
        s[count] = '\0';
        file->aux = s;
    } else if (s && strlen (s) == offset) {
        if (!(s = realloc (s, offset + count + 1))) {
            np_uerror (ENOMEM);
            goto done;
        }
        memcpy (s + offset, data, count);
        s[offset + count] = '\0';
        file->aux = s;
    } else { 
        np_uerror (EIO);
        goto done;
    }
    ret = count;
done:
    return ret;
}

static void
_ctl_closefid (Npfilefid *file)
{
    if (file->aux) {
        free (file->aux);
        file->aux = NULL;
    } 
}

static Npdirops root_ops = {
        .first = _root_first,
        .next =  _root_next,
};
static Npfileops exports_ops = {
        .read  = _exports_read,
        .closefid = _exports_closefid,
        .openfid = _exports_openfid,
};
static Npfileops ctl_ops = {
        .write = _ctl_write,
        .read  = _ctl_read,
        .closefid = _ctl_closefid,
};

/* Create the file system representation for /diodctl.
 */
static Npfile *
_ctl_root_create (void)
{
    Npfile *root, *exports, *ctl;

    if (!(root = npfile_alloc (NULL, "", 0555|S_IFDIR, 0, &root_ops, NULL)))
        msg_exit ("out of memory");
    root->parent = root;
    npfile_incref(root);

    if (!(exports = npfile_alloc(root, "exports", 0444|S_IFREG, 1,
                                 &exports_ops, NULL)))
        msg_exit ("out of memory");
    npfile_incref(exports);

    if (!(ctl = npfile_alloc(root, "ctl", 0666|S_IFREG, 3, &ctl_ops, NULL)))
        msg_exit ("out of memory");
    npfile_incref(ctl);

    root->dirfirst = exports;
    exports->next = ctl;
    root->dirlast = ctl;

    return root;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
