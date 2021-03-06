/*
 ** Copyright(C) 2003-2006 Victor Stinner <victor.stinner AT haypocalc.com>
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

#ifndef NUAUTH_MODULE_LOG_PRELUDE
#define NUAUTH_MODULE_LOG_PRELUDE

#include "auth_srv.h"

/** Prelude version required for the module */
#define PRELUDE_VERSION_REQUIRE "0.9.0"

struct log_prelude_params {
	GPrivate *packet_tpl;
	GPrivate *session_tpl;
	GPrivate *autherr_tpl;
};

#endif
