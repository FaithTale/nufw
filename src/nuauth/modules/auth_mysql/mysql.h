/*
** Copyright(C) 2003-2006 INL
**          written by Eric Leblond <eric@regit.org>
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; version 3 of the License.
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

#ifndef IPAUTH_MYSQL_H
#define IPAUTH_MYSQL_H

#include <sys/time.h>
#include <mysql/mysql.h>


#define MYSQL_SERVER "127.0.0.1"
#define MYSQL_SERVER_PORT 3306
#define MYSQL_USER "nufw"
#define MYSQL_PASSWD "mypassword"
#define MYSQL_DB_NAME "nufw"
#define MYSQL_TABLE_NAME "ulog"
#define MYSQL_USERS_TABLE_NAME "users"
#define MYSQL_REQUEST_TIMEOUT 10
#define MYSQL_USE_IPV4_SCHEMA 1	/* use IPV4 schema by default for compatibility */

#define MYSQL_IPAUTH_TABLE_NAME "ipauth_sessions"
#define MYSQL_USERINFO_TABLE_NAME "userinfo"
#define MYSQL_GROUPS_TABLE_NAME "groups"
#define MYSQL_GROUPINFO_TABLE_NAME "groupinfo"

#define MYSQL_IPAUTH_CHECK_NETMASK 1

/* SSL options */
#define MYSQL_USE_SSL 1		/* use ssl by default */
#define MYSQL_SSL_KEYFILE NULL
#define MYSQL_SSL_CERTFILE NULL
#define MYSQL_SSL_CA      NULL
#define MYSQL_SSL_CAPATH  NULL
#define MYSQL_SSL_CIPHER "ALL:!ADH:+RC4:@STRENGTH"

#define OSNAME_MAX_SIZE 100
#define APPNAME_MAX_SIZE 256

#define SHORT_REQUEST_SIZE 512
#define LONG_REQUEST_SIZE 1024

#define INSERT_REQUEST_FIELDS_SIZE 200
#define INSERT_REQUEST_VALUES_SIZE 800
#define REQUEST_TMP_BUFFER 500

struct ipauth_mysql_params {
	/* module_hook_t hook; */
	int mysql_request_timeout;
	char *mysql_user;
	char *mysql_passwd;
	char *mysql_server;
	char *mysql_db_name;
	char *mysql_ipauth_table_name;
	char *mysql_userinfo_table_name;
	char *mysql_groups_table_name;
	char *mysql_groupinfo_table_name;
	unsigned char mysql_ipauth_check_netmask;
	int mysql_server_port;
	unsigned char mysql_use_ipv4_schema;
	unsigned char mysql_use_ssl;
	char *mysql_ssl_keyfile;
	char *mysql_ssl_certfile;
	char *mysql_ssl_ca;
	char *mysql_ssl_capath;
	char *mysql_ssl_cipher;
	GPrivate *mysql_priv;	/* private pointer for mysql database access */
};

/* we use mysql.h only in ipauth.c */
GSList *ipauth_mysql_conn_list;

#endif /* IPAUTH_MYSQL_H */

