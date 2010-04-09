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

/* diod_trans.c - transport code for distributed I/O daemon */

/* Initial code borrowed from fdtrans.c which is 
 *   Copyright (C) 2005 by Latchesar Ionkov <lucho@ionkov.net>
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <pthread.h>

#include "list.h"
#include "npfs.h"
#include "diod_log.h"
#include "diod_conf.h"
#include "diod_trans.h"

typedef struct {
    Nptrans         *trans;
    int              fd;
    char            *host;
    char            *ip;
    char            *svc;
    int              magic;
    int              authenticated;
    uid_t            authuser;
    char            *jobid;
} DTrans;

#define DIOD_TRANS_MAGIC    0xf00fbaaa

static int  diod_trans_read (u8 *data, u32 count, void *a);
static int  diod_trans_write (u8 *data, u32 count, void *a);
void diod_trans_destroy (void *);

Nptrans *
diod_trans_create (int fd, char *host, char *ip, char *svc)
{
    Nptrans *npt;
    DTrans *dt;

    if (!(dt = malloc (sizeof(*dt))))
        return NULL;
    dt->magic = DIOD_TRANS_MAGIC;
    dt->fd = fd;
    dt->authenticated = 0;
    dt->jobid = NULL;
    if (!(dt->host = strdup (host))) {
        diod_trans_destroy (dt);
        return NULL;
    }
    if (!(dt->ip = strdup (ip))) {
        diod_trans_destroy (dt);
        return NULL;
    }
    if (!(dt->svc = strdup (svc))) {
        diod_trans_destroy (dt);
        return NULL;
    }
    npt = np_trans_create (dt, diod_trans_read, diod_trans_write,
                           diod_trans_destroy);
    if (!npt) {
        diod_trans_destroy (dt);
        return NULL;
    }

    dt->trans = npt;
    return npt;
}

void
diod_trans_destroy (void *a)
{
    DTrans *dt = a;

    assert (dt->magic == DIOD_TRANS_MAGIC);
    dt->magic = 0;
    if (dt->fd >= 0)
        close (dt->fd);
    if (dt->host)
        free (dt->host);
    if (dt->ip)
        free (dt->ip);
    if (dt->svc)
        free (dt->svc);
    if (dt->jobid)
        free (dt->jobid);

    free (dt);
}

static int
diod_trans_read (u8 *data, u32 count, void *a)
{
    DTrans *dt = a;

    assert (dt->magic == DIOD_TRANS_MAGIC);

    return read(dt->fd, data, count);
}

static int
diod_trans_write (u8 *data, u32 count, void *a)
{
    DTrans *dt = a;

    assert (dt->magic == DIOD_TRANS_MAGIC);

    return write(dt->fd, data, count);
}    

char *
diod_trans_get_host (Nptrans *trans)
{
    DTrans *dt = trans->aux;

    assert (dt->magic == DIOD_TRANS_MAGIC);

    return dt->host;
}

char *
diod_trans_get_ip (Nptrans *trans)
{
    DTrans *dt = trans->aux;

    assert (dt->magic == DIOD_TRANS_MAGIC);

    return dt->ip;
}

char *
diod_trans_get_svc (Nptrans *trans)
{
    DTrans *dt = trans->aux;

    assert (dt->magic == DIOD_TRANS_MAGIC);

    return dt->svc;
}

void
diod_trans_set_authuser (Nptrans *trans, uid_t uid, char *jobid)
{
    DTrans *dt = trans->aux;

    assert (dt->magic == DIOD_TRANS_MAGIC);

    dt->authuser = uid;
    dt->authenticated = 1;
    dt->jobid = jobid;
            
}

int
diod_trans_get_authuser (Nptrans *trans, uid_t *uidp)
{
    DTrans *dt = trans->aux;
    int ret = -1;

    assert (dt->magic == DIOD_TRANS_MAGIC);

    if (dt->authenticated) {
        *uidp = dt->authuser;
        ret = 0;
    }

    return ret;
}

char *
diod_trans_get_jobid (Nptrans *trans)
{
    DTrans *dt = trans->aux;

    assert (dt->magic == DIOD_TRANS_MAGIC);

    return dt->jobid;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
