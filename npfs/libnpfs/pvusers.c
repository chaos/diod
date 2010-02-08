/*
 * Copyright (C) 2006 by Latchesar Ionkov <lucho@ionkov.net>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * LATCHESAR IONKOV AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "npfs.h"
#include "npfsimpl.h"

typedef struct Upool Upool;

struct Upool {
	pthread_mutex_t	lock;
	Npuser*		users;
	Npgroup*	groups;
};

static char *Euserexists = "user exists";
static char *Egroupexists = "group exists";

static Npuser *np_priv_uname2user(Npuserpool *up, char *uname);
static Npuser *np_priv_uid2user(Npuserpool *up, u32 uid);
static Npgroup *np_priv_gname2group(Npuserpool *up, char *gname);
static Npgroup *np_priv_gid2group(Npuserpool *up, u32 gid);
static int np_priv_ismember(Npuserpool *up, Npuser *u, Npgroup *g);
static void np_priv_udestroy(Npuserpool *up, Npuser *);
static void np_priv_gdestroy(Npuserpool *up, Npgroup *);

Npuserpool *
np_priv_userpool_create()
{
	Npuserpool *up;
	Upool *upp;

	up = np_malloc(sizeof(Npuserpool) + sizeof(Upool));
	if (!up)
		return NULL;

	upp = (Upool *) ((char *)up + sizeof(Npuserpool));
	pthread_mutex_init(&upp->lock, NULL);
	upp->users = NULL;
	upp->groups = NULL;

	up->aux = upp;
	up->uname2user = np_priv_uname2user;
	up->uid2user = np_priv_uid2user;
	up->gname2group = np_priv_gname2group;
	up->gid2group = np_priv_gid2group;
	up->ismember = np_priv_ismember;
	up->udestroy = np_priv_udestroy;
	up->gdestroy = np_priv_gdestroy;

	return up;
}

Npuser *
np_priv_user_add(Npuserpool *up, char *uname, u32 uid, void *aux)
{
	Npuser *u;
	Upool *upp;

	upp = up->aux;
	u = up->uname2user(up, uname);
	if (u) {
		np_user_decref(u);
		np_werror("%s:%s", EIO, uname, Euserexists);
		return NULL;
	}

	u = up->uid2user(up, uid);
	if (u) {
		np_user_decref(u);
		np_werror("%d:%s", EIO, uid, Euserexists);
		return NULL;
	}

	u = np_malloc(sizeof(*u) + strlen(uname) + 1);
	if (!u)
		return NULL;

	u->refcount = 1;
	u->upool = up;
	u->uid = uid;
	u->uname = (char *)u + sizeof(*u);
	strcpy(u->uname, uname);
	u->dfltgroup = NULL;
	u->ngroups = 0;
	u->groups = NULL;
	u->aux = aux;
	pthread_mutex_init(&u->lock, NULL);
	pthread_mutex_lock(&upp->lock);
	u->next = upp->users;
	upp->users = u;
	pthread_mutex_unlock(&upp->lock);

	np_user_incref(u);
	return u;
}

void np_priv_user_del(Npuser *u)
{
	Npuser *tu, *pu;
	Upool *upp;

	upp = u->upool->aux;

	if (!upp->users)
		return;

	pthread_mutex_lock(&upp->lock);
	for(pu = NULL, tu = upp->users; tu != NULL; pu = tu, tu = tu->next)
		if (tu == u)
			break;

	if (!pu)
		upp->users = u->next;
	else
		pu->next = u->next;
	pthread_mutex_unlock(&upp->lock);

	np_user_decref(u);
}

int
np_priv_user_setdfltgroup(Npuser *u, Npgroup *g)
{
	if (u->dfltgroup)
		np_group_decref(u->dfltgroup);

	u->dfltgroup = g;	/* refcount should be adjusted already */
	return 0;
}

Npgroup *
np_priv_group_add(Npuserpool *up, char *gname, u32 gid)
{
	Npgroup *g;
	Upool *upp;

	upp = up->aux;
	g = up->gname2group(up, gname);
	if (g) {
		np_group_decref(g);
		np_werror("%s:%s", EIO, gname, Egroupexists);
		return NULL;
	}

	g = up->gid2group(up, gid);
	if (g) {
		np_group_decref(g);
		np_werror("%d:%s", EIO, gid, Egroupexists);
		return NULL;
	}

	g = np_malloc(sizeof(*g) + strlen(gname) + 1);
	if (!g)
		return NULL;

	g->refcount = 1;
	g->upool = up;
	g->gid = gid;
	g->gname = (char *)g + sizeof(*g);
	if (pthread_mutex_init(&g->lock, 0)){
		fprintf(stderr, "%s: pthread_mutex_init failed\n", __FUNCTION__);
		exit(1);
	}
	strcpy(g->gname, gname);

	pthread_mutex_lock(&upp->lock);
	g->next = upp->groups;
	upp->groups = g;
	pthread_mutex_unlock(&upp->lock);

	np_group_incref(g);
	return g;
}

void
np_priv_group_del(Npgroup *g)
{
	Npgroup *tg, *pg;
	Upool *upp;

	upp = g->upool->aux;

	if (!upp->groups)
		return;

	pthread_mutex_lock(&upp->lock);
	for(pg = NULL, tg = upp->groups; tg != NULL; pg = tg, tg = tg->next)
		if (tg == g)
			break;

	if (!pg)
		upp->groups = g->next;
	else
		pg->next = g->next;

	pthread_mutex_unlock(&upp->lock);
	np_group_decref(g);
}

int
np_priv_group_adduser(Npgroup *g, Npuser *u)
{
	Npgroup **grps;

	if (u->upool->ismember(u->upool, u, g))
		return 0;

	pthread_mutex_lock(&g->lock);
	grps = realloc(u->groups, sizeof(Npgroup *) * (u->ngroups + 1));
	if (!grps) {
		np_werror(Ennomem, ENOMEM);
		pthread_mutex_unlock(&g->lock);
		return -1;
	}

	grps[u->ngroups] = g;	/* refcount should be updated already */
	u->ngroups++;
	u->groups = grps;
	pthread_mutex_unlock(&g->lock);
	return 0;
}

int
np_priv_group_deluser(Npgroup *g, Npuser *u)
{
	int i;

	pthread_mutex_lock(&g->lock);
	for(i = 0; i < u->ngroups; i++)
		if (u->groups[i] == g) {
			memmove(&u->groups[i], &u->groups[i+1], 
				sizeof(Npgroup*) * (u->ngroups - i - 1));

			u->ngroups--;
			break;
		}
	pthread_mutex_lock(&g->lock);

	return 0;
}

static Npuser *
np_priv_uname2user(Npuserpool *up, char *uname)
{
	Npuser *u;
	Upool *upp;

	upp = up->aux;
	pthread_mutex_lock(&upp->lock);
	for(u = upp->users; u != NULL; u = u->next)
		if (strcmp(u->uname, uname) == 0) {
			np_user_incref(u);
			break;
		}
	pthread_mutex_unlock(&upp->lock);

	return u;
}

static Npuser *
np_priv_uid2user(Npuserpool *up, u32 uid)
{
	Npuser *u;
	Upool *upp;

	upp = up->aux;
	pthread_mutex_lock(&upp->lock);
	for(u = upp->users; u != NULL; u = u->next)
		if (u->uid == uid) {
			np_user_incref(u);
			break;
		}
	pthread_mutex_unlock(&upp->lock);

	return u;
}

static Npgroup *
np_priv_gname2group(Npuserpool *up, char *gname)
{
	Npgroup *g;
	Upool *upp;

	upp = up->aux;
	pthread_mutex_lock(&upp->lock);
	for(g = upp->groups; g != NULL; g = g->next)
		if (strcmp(g->gname, gname) == 0) {
			np_group_incref(g);
			break;
		}
	pthread_mutex_unlock(&upp->lock);

	return g;
}

static Npgroup *
np_priv_gid2group(Npuserpool *up, u32 gid)
{
	Npgroup *g;
	Upool *upp;

	upp = up->aux;
	pthread_mutex_lock(&upp->lock);
	for(g = upp->groups; g != NULL; g = g->next)
		if (g->gid == gid) {
			np_group_incref(g);
			break;
		}
	pthread_mutex_unlock(&upp->lock);

	return g;
}

static int
np_priv_ismember(Npuserpool *up, Npuser *u, Npgroup *g)
{
	int i;

	pthread_mutex_lock(&u->lock);
	for(i = 0; i < u->ngroups; i++)
		if (u->groups[i] == g) {
			pthread_mutex_unlock(&u->lock);
			return 1;
		}

	pthread_mutex_unlock(&u->lock);
	return 0;
}

static void
np_priv_udestroy(Npuserpool *up, Npuser *u)
{
}

static void
np_priv_gdestroy(Npuserpool *up, Npgroup *g)
{
}
