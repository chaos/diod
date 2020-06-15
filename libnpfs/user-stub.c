/*****************************************************************************
 *  Copyright (C) 2010-14 Lawrence Livermore National Security, LLC.
 *  Written by Jim Garlick <garlick@llnl.gov> LLNL-CODE-423279
 *  All Rights Reserved.
 *
 *  This file is part of the Distributed I/O Daemon (diod).
 *  For details, see http://code.google.com/p/diod.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 *  or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation,
 *  Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also: http://www.gnu.org/licenses
 *****************************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdint.h>

#include "9p.h"
#include "npfs.h"
#include "xpthread.h"
#include "npfsimpl.h"

int
np_setfsid (Npreq *req, Npuser *u, u32 gid_override)
{
	return 0;
}
