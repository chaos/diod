/*****************************************************************************
 *  $Id: hash.c 2950 2005-01-18 20:09:32Z dun $
 *****************************************************************************
 *  Copyright (C) 2003-2005 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Chris Dunlap <cdunlap@llnl.gov>.
 *  
 *  This file is from LSD-Tools, the LLNL Software Development Toolbox.
 *
 *  This is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License;
 *  if not, write to the Free Software Foundation, Inc., 59 Temple Place,
 *  Suite 330, Boston, MA  02111-1307  USA.
 *****************************************************************************
 *  Refer to "hash.h" for documentation on public functions.
 *****************************************************************************/


#if HAVE_CONFIG_H
#  include "config.h"
#endif /* HAVE_CONFIG_H */

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "thread.h"
#include "hash.h"


/*****************************************************************************
 *  Constants
 *****************************************************************************/

#define HASH_ALLOC      1024
#define HASH_DEF_SIZE   1213
#define HASH_MAGIC      0xDEADBEEF


/*****************************************************************************
 *  Data Types
 *****************************************************************************/

struct hash_node {
    struct hash_node   *next;           /* next node in list                 */
    void               *data;           /* ptr to hashed item                */
    const void         *hkey;           /* ptr to hashed item's key          */
};

struct hash {
    int                 count;          /* number of items in hash table     */
    int                 size;           /* num slots allocated in hash table */
    struct hash_node  **table;          /* hash table array of node ptrs     */
    hash_cmp_f          cmp_f;          /* key comparison function           */
    hash_del_f          del_f;          /* item deletion function            */
    hash_key_f          key_f;          /* key hash function                 */
#if WITH_PTHREADS
    pthread_mutex_t     mutex;          /* mutex to protect access to hash   */
#endif /* WITH_PTHREADS */
#ifndef NDEBUG
    unsigned int        magic;          /* sentinel for asserting validity   */
#endif /* NDEBUG */
};


/*****************************************************************************
 *  Prototypes
 *****************************************************************************/

static struct hash_node * hash_node_alloc (void);

static void hash_node_free (struct hash_node *node);


/*****************************************************************************
 *  Variables
 *****************************************************************************/

static struct hash_node *hash_free_list = NULL;

#if WITH_PTHREADS
static pthread_mutex_t hash_free_lock = PTHREAD_MUTEX_INITIALIZER;
#endif /* WITH_PTHREADS */


/*****************************************************************************
 *  Macros
 *****************************************************************************/

#ifdef WITH_LSD_FATAL_ERROR_FUNC
#  undef lsd_fatal_error
   extern void lsd_fatal_error (char *file, int line, char *mesg);
#else /* !WITH_LSD_FATAL_ERROR_FUNC */
#  ifndef lsd_fatal_error
#    define lsd_fatal_error(file, line, mesg) (abort ())
#  endif /* !lsd_fatal_error */
#endif /* !WITH_LSD_FATAL_ERROR_FUNC */

#ifdef WITH_LSD_NOMEM_ERROR_FUNC
#  undef lsd_nomem_error
   extern void * lsd_nomem_error (char *file, int line, char *mesg);
#else /* !WITH_LSD_NOMEM_ERROR_FUNC */
#  ifndef lsd_nomem_error
#    define lsd_nomem_error(file, line, mesg) (NULL)
#  endif /* !lsd_nomem_error */
#endif /* !WITH_LSD_NOMEM_ERROR_FUNC */


/*****************************************************************************
 *  Functions
 *****************************************************************************/

hash_t
hash_create (int size, hash_key_f key_f, hash_cmp_f cmp_f, hash_del_f del_f)
{
    hash_t h;

    if (!cmp_f || !key_f) {
        errno = EINVAL;
        return (NULL);
    }
    if (size <= 0) {
        size = HASH_DEF_SIZE;
    }
    if (!(h = malloc (sizeof (*h)))) {
        return (lsd_nomem_error (__FILE__, __LINE__, "hash_create"));
    }
    if (!(h->table = calloc (size, sizeof (struct hash_node *)))) {
        free (h);
        return (lsd_nomem_error (__FILE__, __LINE__, "hash_create"));
    }
    h->count = 0;
    h->size = size;
    h->cmp_f = cmp_f;
    h->del_f = del_f;
    h->key_f = key_f;
    lsd_mutex_init (&h->mutex);
    assert (h->magic = HASH_MAGIC);     /* set magic via assert abuse */
    return (h);
}


void
hash_destroy (hash_t h)
{
    int i;
    struct hash_node *p, *q;

    if (!h) {
        errno = EINVAL;
        return;
    }
    lsd_mutex_lock (&h->mutex);
    assert (h->magic == HASH_MAGIC);
    for (i = 0; i < h->size; i++) {
        for (p = h->table[i]; p != NULL; p = q) {
            q = p->next;
            if (h->del_f)
                h->del_f (p->data);
            hash_node_free (p);
        }
    }
    assert (h->magic = ~HASH_MAGIC);    /* clear magic via assert abuse */
    lsd_mutex_unlock (&h->mutex);
    lsd_mutex_destroy (&h->mutex);
    free (h->table);
    free (h);
    return;
}


int
hash_is_empty (hash_t h)
{
    int n;

    if (!h) {
        errno = EINVAL;
        return (0);
    }
    lsd_mutex_lock (&h->mutex);
    assert (h->magic == HASH_MAGIC);
    n = h->count;
    lsd_mutex_unlock (&h->mutex);
    return (n == 0);
}


int
hash_count (hash_t h)
{
    int n;

    if (!h) {
        errno = EINVAL;
        return (0);
    }
    lsd_mutex_lock (&h->mutex);
    assert (h->magic == HASH_MAGIC);
    n = h->count;
    lsd_mutex_unlock (&h->mutex);
    return (n);
}


void *
hash_find (hash_t h, const void *key)
{
    unsigned int slot;
    struct hash_node *p;
    void *data = NULL;

    if (!h || !key) {
        errno = EINVAL;
        return (NULL);
    }
    errno = 0;
    lsd_mutex_lock (&h->mutex);
    assert (h->magic == HASH_MAGIC);
    slot = h->key_f (key) % h->size;
    for (p = h->table[slot]; p != NULL; p = p->next) {
        if (!h->cmp_f (p->hkey, key)) {
            data = p->data;
            break;
        }
    }
    lsd_mutex_unlock (&h->mutex);
    return (data);
}


void *
hash_insert (hash_t h, const void *key, void *data)
{
    struct hash_node *p;
    unsigned int slot;

    if (!h || !key || !data) {
        errno = EINVAL;
        return (NULL);
    }
    lsd_mutex_lock (&h->mutex);
    assert (h->magic == HASH_MAGIC);
    slot = h->key_f (key) % h->size;
    for (p = h->table[slot]; p != NULL; p = p->next) {
        if (!h->cmp_f (p->hkey, key)) {
            errno = EEXIST;
            data = NULL;
            goto end;
        }
    }
    if (!(p = hash_node_alloc ())) {
        data = lsd_nomem_error (__FILE__, __LINE__, "hash_insert");
        goto end;
    }
    p->hkey = key;
    p->data = data;
    p->next = h->table[slot];
    h->table[slot] = p;
    h->count++;

end:
    lsd_mutex_unlock (&h->mutex);
    return (data);
}


void *
hash_remove (hash_t h, const void *key)
{
    struct hash_node **pp;
    struct hash_node *p;
    unsigned int slot;
    void *data = NULL;

    if (!h || !key) {
        errno = EINVAL;
        return (NULL);
    }
    errno = 0;
    lsd_mutex_lock (&h->mutex);
    assert (h->magic == HASH_MAGIC);
    slot = h->key_f (key) % h->size;
    for (pp = &(h->table[slot]); (p = *pp) != NULL; pp = &((*pp)->next)) {
        if (!h->cmp_f (p->hkey, key)) {
            data = p->data;
            *pp = p->next;
            hash_node_free (p);
            h->count--;
            break;
        }
    }
    lsd_mutex_unlock (&h->mutex);
    return (data);
}


int
hash_delete_if (hash_t h, hash_arg_f arg_f, void *arg)
{
    int i;
    struct hash_node **pp;
    struct hash_node *p;
    int n = 0;

    if (!h || !arg_f) {
        errno = EINVAL;
        return (-1);
    }
    lsd_mutex_lock (&h->mutex);
    assert (h->magic == HASH_MAGIC);
    for (i = 0; i < h->size; i++) {
        pp = &(h->table[i]);
        while ((p = *pp) != NULL) {
            if (arg_f (p->data, p->hkey, arg) > 0) {
                if (h->del_f)
                    h->del_f (p->data);
                *pp = p->next;
                hash_node_free (p);
                h->count--;
                n++;
            }
            else {
                pp = &(p->next);
            }
        }
    }
    lsd_mutex_unlock (&h->mutex);
    return (n);
}


int
hash_for_each (hash_t h, hash_arg_f arg_f, void *arg)
{
    int i;
    struct hash_node *p;
    int n = 0;

    if (!h || !arg_f) {
        errno = EINVAL;
        return (-1);
    }
    lsd_mutex_lock (&h->mutex);
    assert (h->magic == HASH_MAGIC);
    for (i = 0; i < h->size; i++) {
        for (p = h->table[i]; p != NULL; p = p->next) {
            if (arg_f (p->data, p->hkey, arg) > 0) {
                n++;
            }
        }
    }
    lsd_mutex_unlock (&h->mutex);
    return (n);
}


/*****************************************************************************
 *  Hash Functions
 *****************************************************************************/

unsigned int
hash_key_string (const char *str)
{
    unsigned char *p;
    unsigned int hval = 0;
    const unsigned int multiplier = 31;

    for (p = (unsigned char *) str; *p != '\0'; p++) {
        hval += (multiplier * hval) + *p;
    }
    return (hval);
}


/*****************************************************************************
 *  Internal Functions
 *****************************************************************************/

static struct hash_node *
hash_node_alloc (void)
{
/*  Allocates a hash node from the freelist.
 *  Memory is allocated in chunks of HASH_ALLOC.
 *  Returns a ptr to the object, or NULL if memory allocation fails.
 */
    int i;
    struct hash_node *p = NULL;

    assert (HASH_ALLOC > 0);
    lsd_mutex_lock (&hash_free_lock);
    if (!hash_free_list) {
        if ((hash_free_list = malloc (HASH_ALLOC * sizeof (*p)))) {
            for (i = 0; i < HASH_ALLOC - 1; i++)
                hash_free_list[i].next = &hash_free_list[i+1];
            hash_free_list[i].next = NULL;
        }
    }
    if (hash_free_list) {
        p = hash_free_list;
        hash_free_list = p->next;
    }
    else {
        errno = ENOMEM;
    }
    lsd_mutex_unlock (&hash_free_lock);
    return (p);
}


static void
hash_node_free (struct hash_node *node)
{
/*  De-allocates the object [node], returning it to the freelist.
 */
    assert (node != NULL);
    memset (node, 0, sizeof (*node));
    lsd_mutex_lock (&hash_free_lock);
    node->next = hash_free_list;
    hash_free_list = node;
    lsd_mutex_unlock (&hash_free_lock);
    return;
}
