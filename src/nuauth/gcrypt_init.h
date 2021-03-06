/*
 ** Copyright(C) 2005 INL
 ** Written by Eric Leblond <regit@inl.fr>
 **
 ** $Id$
 **
 ** This program is free software; you can redistribute it and/or modify
 ** it under the terms of the GNU General Public License as published by
 ** the Free Software Foundation, version 3 of the License.
 **
 ** This program is distributed in the hope that it will be useful,
 ** but WITHOUT ANY WARRANTY; without even the implied warranty of
 ** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 ** GNU General Public License for more details.
 **
 ** You should have received a copy of the GNU General Public License
 ** along with this program; if not, write to the Free Software
 ** Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef GCRYPT_NUAUTH_H
#define GCRYPT_NUAUTH_H

/*! \file nuauth/gcrypt_init.h
    \brief Contains gcrypt init functions

*/

/*#define GCRYPT_PTHEAD_IMPLEMENTATION */

#ifdef GCRYPT_PTHEAD_IMPLEMENTATION

#include <pthread.h>

/****************** gcrypt use 'pthread' thread implementation *************/
GCRY_THREAD_OPTION_PTHREAD_IMPL;

#else

/****************** gcrypt use 'glib' thread implementation ****************/

/* gcrypt init function */
static int gcry_gthread_mutex_init(void **priv)
{				/* to check */
	GMutex *lock = g_mutex_new();
	if (!lock)
		return ENOMEM;
	*priv = lock;
	return 0;
}

static int gcry_gthread_mutex_destroy(void **lock)
{
	g_mutex_free(*lock);
	return 0;
}

static int gcry_gthread_mutex_lock(void **lock)
{
	g_mutex_lock(*lock);
	return 0;
}

static int gcry_gthread_mutex_unlock(void **lock)
{
	g_mutex_unlock(*lock);
	return 0;
}

static struct gcry_thread_cbs gcry_threads_gthread = {
	GCRY_THREAD_OPTION_USER, NULL,
	gcry_gthread_mutex_init, gcry_gthread_mutex_destroy,
	gcry_gthread_mutex_lock, gcry_gthread_mutex_unlock,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};

#endif				/* #ifdef GCRYPT_PTHEAD_IMPLEMENTATION */
#endif				/* #ifndef GCRYPT_NUAUTH_H */
