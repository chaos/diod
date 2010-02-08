/*****************************************************************************
 *  $Id: thread.h 2938 2003-11-26 23:01:18Z dun $
 *****************************************************************************
 *  Copyright (C) 2003 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Chris Dunlap <cdunlap@llnl.gov>.
 *  
 *  This file is from LSD-Tools, the LLNL Software Development Toolbox.
 *
 *  LSD-Tools is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  LSD-Tools is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 *  more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with LSD-Tools; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 *****************************************************************************/


#ifndef LSD_THREAD_H
#define LSD_THREAD_H

#if WITH_PTHREADS
#  include <errno.h>
#  include <pthread.h>
#  include <stdlib.h>
#endif /* WITH_PTHREADS */


/*****************************************************************************
 *  Macros
 *****************************************************************************/

#if WITH_PTHREADS

#  ifdef WITH_LSD_FATAL_ERROR_FUNC
#    undef lsd_fatal_error
     extern void lsd_fatal_error (char *file, int line, char *mesg);
#  else /* !WITH_LSD_FATAL_ERROR_FUNC */
#    ifndef lsd_fatal_error
#      define lsd_fatal_error(file, line, mesg) (abort ())
#    endif /* !lsd_fatal_error */
#  endif /* !WITH_LSD_FATAL_ERROR_FUNC */

#  define lsd_mutex_init(pmutex)                                              \
     do {                                                                     \
         int e = pthread_mutex_init (pmutex, NULL);                           \
         if (e != 0) {                                                        \
             errno = e;                                                       \
             lsd_fatal_error (__FILE__, __LINE__, "mutex_init");              \
             abort ();                                                        \
         }                                                                    \
     } while (0)

#  define lsd_mutex_lock(pmutex)                                              \
     do {                                                                     \
         int e = pthread_mutex_lock (pmutex);                                 \
         if (e != 0) {                                                        \
             errno = e;                                                       \
             lsd_fatal_error (__FILE__, __LINE__, "mutex_lock");              \
             abort ();                                                        \
         }                                                                    \
     } while (0)

#  define lsd_mutex_unlock(pmutex)                                            \
     do {                                                                     \
         int e = pthread_mutex_unlock (pmutex);                               \
         if (e != 0) {                                                        \
             errno = e;                                                       \
             lsd_fatal_error (__FILE__, __LINE__, "mutex_unlock");            \
             abort ();                                                        \
         }                                                                    \
     } while (0)

#  define lsd_mutex_destroy(pmutex)                                           \
     do {                                                                     \
         int e = pthread_mutex_destroy (pmutex);                              \
         if (e != 0) {                                                        \
             errno = e;                                                       \
             lsd_fatal_error (__FILE__, __LINE__, "mutex_destroy");           \
             abort ();                                                        \
         }                                                                    \
     } while (0)

#  ifndef NDEBUG
     int lsd_mutex_is_locked (pthread_mutex_t *pmutex);
#  endif /* !NDEBUG */

#else /* !WITH_PTHREADS */

#  define lsd_mutex_init(mutex)
#  define lsd_mutex_lock(mutex)
#  define lsd_mutex_unlock(mutex)
#  define lsd_mutex_destroy(mutex)
#  define lsd_mutex_is_locked(mutex) (1)

#endif /* !WITH_PTHREADS */


#endif /* !LSD_THREAD_H */
