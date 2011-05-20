/* 
   dbench version 2
   Copyright (C) Andrew Tridgell 1999
   
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


#ifndef SHM_W
#define SHM_W 0000200
#endif

#ifndef SHM_R
#define SHM_R 0000400
#endif

/* return a pointer to a anonymous shared memory segment of size "size"
   which will persist across fork() but will disappear when all processes
   exit 

   The memory is zeroed 

   This function uses system5 shared memory. It takes advantage of a property
   that the memory is not destroyed if it is attached when the id is removed
   */
void *shm_setup(int size)
{
	int shmid;
	void *ret;

	shmid = shmget(IPC_PRIVATE, size, SHM_R | SHM_W);
	if (shmid == -1) {
		printf("can't get private shared memory of %d bytes: %s\n",
		       size, 
		       strerror(errno));
		exit(1);
	}
	ret = (void *)shmat(shmid, 0, 0);
	if (!ret || ret == (void *)-1) {
		printf("can't attach to shared memory\n");
		return NULL;
	}
	/* the following releases the ipc, but note that this process
	   and all its children will still have access to the memory, its
	   just that the shmid is no longer valid for other shm calls. This
	   means we don't leave behind lots of shm segments after we exit 

	   See Stevens "advanced programming in unix env" for details
	   */
	shmctl(shmid, IPC_RMID, 0);

	memset(ret, 0, size);
	
	return ret;
}

/****************************************************************************
similar to string_sub() but allows for any character to be substituted. 
Use with caution!
****************************************************************************/
void all_string_sub(char *s,const char *pattern,const char *insert)
{
	char *p;
	size_t ls,lp,li;

	if (!insert || !pattern || !s) return;

	ls = strlen(s);
	lp = strlen(pattern);
	li = strlen(insert);

	if (!*pattern) return;
	
	while (lp <= ls && (p = strstr(s,pattern))) {
		memmove(p+li,p+lp,ls + 1 - (((int)(p-s)) + lp));
		memcpy(p, insert, li);
		s = p + li;
		ls += (li-lp);
	}
}


/****************************************************************************
  Get the next token from a string, return False if none found
  handles double-quotes. 
Based on a routine by GJC@VILLAGE.COM. 
Extensively modified by Andrew.Tridgell@anu.edu.au
****************************************************************************/
BOOL next_token(char **ptr,char *buff,char *sep)
{
	static char *last_ptr=NULL;
	char *s;
	BOOL quoted;
	
	if (!ptr) ptr = &last_ptr;
	if (!ptr) return(False);
	
	s = *ptr;
	
	/* default to simple separators */
	if (!sep) sep = " \t\n\r";
	
	/* find the first non sep char */
	while(*s && strchr(sep,*s)) s++;
	
	/* nothing left? */
	if (! *s) return(False);
	
	/* copy over the token */
	for (quoted = False; *s && (quoted || !strchr(sep,*s)); s++) {
		if (*s == '\"') 
			quoted = !quoted;
		else
			*buff++ = *s;
	}
	
	*ptr = (*s) ? s+1 : s;  
	*buff = 0;
	last_ptr = *ptr;
	
	return(True);
}

/*
  return a timeval for the current time
*/
struct timeval timeval_current(void)
{
        struct timeval tv;
        gettimeofday(&tv, NULL);
        return tv;
}

/*
  return the number of seconds elapsed since a given time
*/
double timeval_elapsed(struct timeval *tv)
{
        struct timeval tv2 = timeval_current();
        return (tv2.tv_sec - tv->tv_sec) + 
               (tv2.tv_usec - tv->tv_usec)*1.0e-6;
}

/*
  return the number of seconds elapsed since a given time
*/
double timeval_elapsed2(struct timeval *tv1, struct timeval *tv2)
{
        return (tv2->tv_sec - tv1->tv_sec) + 
               (tv2->tv_usec - tv1->tv_usec)*1.0e-6;
}



/**
 Sleep for a specified number of milliseconds.
**/
void msleep(unsigned int t)
{
	struct timeval tval;  

	tval.tv_sec = t/1000;
	tval.tv_usec = 1000*(t%1000);
	/* this should be the real select - do NOT replace
	   with sys_select() */
	select(0,NULL,NULL,NULL,&tval);
}
