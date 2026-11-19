/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* Functions for encoding/decoding and manipulating RFC 29 Hostlists */

#ifndef FLUX_HOSTLIST_H
#define FLUX_HOSTLIST_H

#ifdef __cplusplus
extern "C" {
#endif

struct hostlist *hostlist_create (void);

void hostlist_destroy (struct hostlist *hl);

/*  Decode string 's' in RFC 29 Hostlist Format.
 *
 *  Returns hostlist on success or NULL on failure with errno set.
 */
struct hostlist *hostlist_decode (const char *s);

/*
 *  Encode a hostlist to a string in RFC 29 Hostlist Format
 */
char *hostlist_encode (struct hostlist *hl);

/*
 *  Copy a hostlist
 */
struct hostlist *hostlist_copy (const struct hostlist *hl);

/*  Add a string representation of a hostlist onto the tail of the
 *   hostlist 'hl'.
 *  Returns the total number of hosts appended to 'hl', or -1 on failure.
 */
int hostlist_append (struct hostlist *hl, const char *hosts);

/*
 *  Append hostlist 'hl2' onto 'hl1'.
 *
 *  Returns number of hosts appended for success, -1 for failure.
 *
 */
int hostlist_append_list (struct hostlist *hl1, struct hostlist *hl2);

/*  Return the nth host in hostlist 'hl' or NULL on failure.
 *
 *  Moves the hostlist cursor to the returned host on success.
 *
 *  The returned value is only valid until the next call that modifies
 *   the cursor, i.e. hostlist_next(), hostlist_first(), hostlist_last(),
 *    hostlist_find(), and hostlist_nth().
 */
const char * hostlist_nth (struct hostlist * hl, int n);

/*
 *  Search hostlist hl for the first host matching hostname
 *   and return position in list if found.
 *
 *  Leaves cursor pointing to the matching host.
 *
 *  Returns -1 if host is not found.
 */
int hostlist_find (struct hostlist * hl, const char *hostname);

/*
 *  Create an opaque hostname object which can be used with the
 *   more efficient hostlist_find_hostname() below. This interface
 *   should be used when matching the same hostname to many hostlists
 *   since it caches processing of the hostname instead of duplicating
 *   the work on each call to hostlist_find().
 *
 *  Caller must call hostlist_hostname_destroy() to free memory associated
 *   with the returns hostlist_hostname object.
 *
 *  Returns NULL on error.
 */
struct hostlist_hostname *hostlist_hostname_create (const char *hostname);

/*
 *  Free resources associated with hostname hn.
 */
void hostlist_hostname_destroy (struct hostlist_hostname *hn);

/*
 *  Search hostlist hl for the first host matching hostlist_hostname `hn`
 *   and return position in list if found.
 *
 *  Leaves cursor pointing to the matching host.
 *
 *  Returns -1 if host is not found.
 */
int hostlist_find_hostname (struct hostlist *hl, struct hostlist_hostname *hn);

/*
 *  Delete all hosts in the list represented by `hosts'
 *
 *  Returns the number of hosts successfully deleted, -1 on failure.
 */
int hostlist_delete (struct hostlist * hl, const char *hosts);

/*
 *  Return the number of hosts in hostlist hl.
 */
int hostlist_count (struct hostlist * hl);

/* hostlist_is_empty(): return true if hostlist is empty. */
#define hostlist_is_empty(__hl) ( hostlist_count(__hl) == 0 )

/*
 *  Sort the hostlist hl.
 */
void hostlist_sort (struct hostlist * hl);

/*
 *  Sort the hostlist hl and remove duplicate entries.
 */
void hostlist_uniq (struct hostlist *hl);

/*
 *  Return the host at the head of hostlist 'hl', or NULL if list is empty.
 *  Leaves internal cursor pointing at the head item.
 *
 *  The returned value is only valid until the next call that modifies
 *   the cursor, i.e. hostlist_next(), hostlist_first(), hostlist_last(),
 *    hostlist_find(), and hostlist_nth().
 */
const char * hostlist_first (struct hostlist *hl);

/*
 *  Return the host at the tail of hostlist 'hl', or NULL if list is empty.
 *  Leaves internal cursor pointing at the last item.
 *
 *  The returned value is only valid until the next call that modifies
 *   the cursor, i.e. hostlist_next(), hostlist_first(), hostlist_last(),
 *    hostlist_find(), and hostlist_nth().
 */
const char * hostlist_last (struct hostlist *hl);

/*
 *  Advance the internal cursor and return the next host in 'hl' or NULL
 *   if list is empty or the end of the list has been reached.
 *
 *  The returned value is only valid until the next call that modifies
 *   the cursor, i.e. hostlist_next(), hostlist_first(), hostlist_last(),
 *    hostlist_find(), and hostlist_nth().
 */
const char * hostlist_next (struct hostlist *hl);

/*  Return the current host to which the cursor is pointing, or NULL if
 *   cursor is pointing to end of list or list is empty.
 *
 *  The returned value is only valid until the next call that modifies
 *   the cursor, i.e. hostlist_next(), hostlist_first(), hostlist_last(),
 *    hostlist_find(), and hostlist_nth().
 */
const char * hostlist_current (struct hostlist *hl);

/* Remove the host at which the current cursor is pointing.
 * Returns 1 on Success, 0 if the cursor doesn't point to a host
 *  or -1 on error.
 */
int hostlist_remove_current (struct hostlist *hl);

#ifdef __cplusplus
}
#endif

#endif /* !_HOSTLIST_H */
