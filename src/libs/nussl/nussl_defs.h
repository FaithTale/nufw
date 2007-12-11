/*
 ** Copyright (C) 2002-2007 INL
 ** Written by S.Tricaud <stricaud@inl.fr>
 **            L.Defert <ldefert@inl.com>
 ** INL http://www.inl.fr/
 **
 ** $Id: main.c 3668 2007-08-20 09:55:12Z haypo $
 **
 ** This program is free software; you can redistribute it and/or modify
 ** it under the terms of the GNU General Public License as published by
 ** the Free Software Foundation, version 2 of the License.
 **
 ** This program is distributed in the hope that it will be useful,
 ** but WITHOUT ANY WARRANTY; without even the implied warranty of
 ** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 ** GNU General Public License for more details.
 **
 ** You should have received a copy of the GNU General Public License
 ** along with this program; if not, write to the Free Software
 ** Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 **
 ** NuSSL: OpenSSL / GnuTLS layer based on libneon
 */


/* 
   Standard definitions for neon headers
   Copyright (C) 2003-2006, Joe Orton <joe@manyfish.co.uk>

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.
   
   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with this library; if not, write to the Free
   Software Foundation, Inc., 59 Temple Place - Suite 330, Boston,
   MA 02111-1307, USA

*/

#undef NE_BEGIN_DECLS
#undef NE_END_DECLS
#ifdef __cplusplus
# define NE_BEGIN_DECLS extern "C" {
# define NE_END_DECLS }
#else
# define NE_BEGIN_DECLS /* empty */
# define NE_END_DECLS /* empty */
#endif

#ifndef NE_DEFS_H
#define NE_DEFS_H

#include <sys/types.h>

#ifdef NE_LFS
typedef off64_t ne_off_t;
#else
typedef off_t ne_off_t;
#endif

/* define ssize_t for Win32 */
#if defined(WIN32) && !defined(ssize_t)
#define ssize_t int
#endif

#ifdef __GNUC__
#if __GNUC__ >= 3
#define ne_attribute_malloc __attribute__((malloc))
#else
#define ne_attribute_malloc
#endif
#define ne_attribute(x) __attribute__(x)
#else
#define ne_attribute(x)
#define ne_attribute_malloc
#endif

#ifndef NE_BUFSIZ
#define NE_BUFSIZ 8192
#endif

#endif /* NE_DEFS_H */