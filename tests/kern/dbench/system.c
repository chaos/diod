/* 
   dbench version 3

   Copyright (c) Timur I. Bakeyev 2004
   Copyright (C) Andrew Tridgell 1999-2004
   
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.
   
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
   
   You should have received a copy of the GNU General Public License
   along with this program; if not, see <http://www.gnu.org/licenses/>.
*/

#include "dbench.h"

/**************************************************************************
 Wrappers for extented attribute calls. Based on the Linux package with
 support for IRIX also. Expand as other systems have them.
****************************************************************************/

ssize_t sys_getxattr (const char *path, const char *name, void *value, size_t size)
{
#if defined(HAVE_GETXATTR)
	return getxattr(path, name, value, size);
#elif defined(HAVE_EXTATTR_GET_FILE)
	char *s;
	int attrnamespace = (strncmp(name, "system", 6) == 0) ? 
		EXTATTR_NAMESPACE_SYSTEM : EXTATTR_NAMESPACE_USER;
	const char *attrname = ((s=strchr(name, '.')) == NULL) ? name : s + 1;

	return extattr_get_file(path, attrnamespace, attrname, value, size);
#elif defined(HAVE_ATTR_GET)
	int retval, flags = 0;
	int valuelength = (int)size;
	char *attrname = strchr(name,'.') +1;
	
	if (strncmp(name, "system", 6) == 0) flags |= ATTR_ROOT;

	retval = attr_get(path, attrname, (char *)value, &valuelength, flags);

	return retval ? retval : valuelength;
#else
	errno = ENOSYS;
	return -1;
#endif
}

ssize_t sys_fgetxattr (int filedes, const char *name, void *value, size_t size)
{
#if defined(HAVE_FGETXATTR)
	return fgetxattr(filedes, name, value, size);
#elif defined(HAVE_EXTATTR_GET_FD)
	char *s;
	int attrnamespace = (strncmp(name, "system", 6) == 0) ? 
		EXTATTR_NAMESPACE_SYSTEM : EXTATTR_NAMESPACE_USER;
	const char *attrname = ((s=strchr(name, '.')) == NULL) ? name : s + 1;

	return extattr_get_fd(filedes, attrnamespace, attrname, value, size);
#elif defined(HAVE_ATTR_GETF)
	int retval, flags = 0;
	int valuelength = (int)size;
	char *attrname = strchr(name,'.') +1;
	
	if (strncmp(name, "system", 6) == 0) flags |= ATTR_ROOT;

	retval = attr_getf(filedes, attrname, (char *)value, &valuelength, flags);

	return retval ? retval : valuelength;
#else
	errno = ENOSYS;
	return -1;
#endif
}


#if !defined(HAVE_SETXATTR)
#define XATTR_CREATE  0x1       /* set value, fail if attr already exists */
#define XATTR_REPLACE 0x2       /* set value, fail if attr does not exist */
#endif

int sys_fsetxattr (int filedes, const char *name, const void *value, size_t size, int flags)
{
#if defined(HAVE_FSETXATTR)
	return fsetxattr(filedes, name, value, size, flags);
#elif defined(HAVE_EXTATTR_SET_FD)
	char *s;
	int retval = 0;
	int attrnamespace = (strncmp(name, "system", 6) == 0) ? 
		EXTATTR_NAMESPACE_SYSTEM : EXTATTR_NAMESPACE_USER;
	const char *attrname = ((s=strchr(name, '.')) == NULL) ? name : s + 1;

	retval = extattr_set_fd(filedes, attrnamespace, attrname, value, size);
	return (retval < 0) ? -1 : 0;
#elif defined(HAVE_ATTR_SETF)
	int myflags = 0;
	char *attrname = strchr(name,'.') +1;
	
	if (strncmp(name, "system", 6) == 0) myflags |= ATTR_ROOT;
	if (flags & XATTR_CREATE) myflags |= ATTR_CREATE;
	if (flags & XATTR_REPLACE) myflags |= ATTR_REPLACE;

	return attr_setf(filedes, attrname, (const char *)value, size, myflags);
#else
	errno = ENOSYS;
	return -1;
#endif
}
