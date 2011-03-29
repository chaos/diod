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

typedef struct opt_struct *Opt;

Opt             opt_create (void);

void            opt_destroy (Opt o);

char           *opt_csv (Opt o);

int             opt_addf (Opt o, const char *fmt, ...)
                          __attribute__ ((format (printf, 2, 3)));

char           *opt_find (Opt o, char *key);

int             opt_delete (Opt o, char *key);

int             opt_scanf (Opt o, const char *fmt, ...)
                          __attribute__ ((format (scanf, 2, 3)));

int             opt_check_allowed_csv (Opt o, const char *s);


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
