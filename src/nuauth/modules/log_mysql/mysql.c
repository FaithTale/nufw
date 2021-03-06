/*
 ** Copyright(C) 2003-2008 INL
 ** Written by Eric Leblond <regit@inl.fr>
 **	       Vincent Deffontaines <vincent@gryzor.com>
 **	       Victor Stinner <haypo@inl.fr>
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

#include <auth_srv.h>
#include <log_mysql.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>

#include "nuauthconf.h"

#include "strings.h"

/** Minimum buffer size to write an IPv6 in SQL syntax */
#define IPV6_SQL_STRLEN (2+16*2+1)

/*
 * Returns version of nuauth API
 */
G_MODULE_EXPORT uint32_t get_api_version()
{
	return NUAUTH_API_VERSION;
}


/**
 * Convert an IPv6 address to SQL binary string.
 * Eg. ::1 => "0x0000000000000001"
 *
 * \return Returns -1 if fails, 0 otherwise.
 */
static int ipv6_to_sql(
	struct log_mysql_params *params,
	struct in6_addr *addr,
	char *buffer,
	size_t buflen,
	int use_ntohl)
{
	unsigned char i;
	unsigned char *addr8;
	size_t written;

	if (!params->mysql_use_ipv4_schema) {
		/* format IPv6 to BINARY(16) as "0x..." */
		if (buflen < IPV6_SQL_STRLEN) {
			buffer[0] = 0;
			return -1;
		}
		buffer[0] = '0';
		buffer[1] = 'x';
		buffer += 2;
		addr8 = &addr->s6_addr[0];
		for (i = 0; i < 4; i++) {
			written = sprintf(buffer, "%02x%02x%02x%02x",
					addr8[0], addr8[1], addr8[2], addr8[3]);
			if (written != 2 * 4) {
				buffer[0] = 0;
				return -1;
			}
			buffer += written;
			addr8 += 4;
		}
		buffer[0] = 0;
	} else {
		int ok;
		uint32_t ip;
		/* format IPv6 to "a.b.c.d" but only for IPv4 in IPv6 */
		if (!is_ipv4(addr)) {
			log_message(SERIOUS_WARNING, DEBUG_AREA_MAIN,
				    "MySQL: Packet has IPV6 address but MySQL use IPV4 only schema");
			return -1;
		}
		ip = addr->s6_addr32[3];
		if (use_ntohl)
			ip = ntohl(ip);
		ok = secure_snprintf(buffer, buflen, "%u", ip);

		if (!ok) return -1;
	}
	return 0;
}

static nu_error_t mysql_close_open_user_sessions(struct log_mysql_params
						 *params);
static MYSQL *mysql_conn_init(struct log_mysql_params *params);
static MYSQL *get_mysql_handler(struct log_mysql_params *params);

/**
 *
 * \ingroup LoggingNuauthModules
 * \defgroup MySQLModule MySQL logging module
 *
 * @{ */

static nu_error_t mysql_close_current(struct log_mysql_params* params)
{
	MYSQL* ld = get_mysql_handler(params);
	if (ld) {
		mysql_close(ld);
	}
	g_private_set(params->mysql_priv, NULL);
	return NU_EXIT_OK;
}

G_MODULE_EXPORT gchar *unload_module_with_params(gpointer params_p)
{
	struct log_mysql_params *params =
	    (struct log_mysql_params *) params_p;

	if (params) {
		if ((!nuauth_is_reloading()) &&
				(params->hook == MOD_LOG_SESSION)) {
			if (mysql_close_open_user_sessions(params) !=
			    NU_EXIT_OK) {
				log_message(WARNING, DEBUG_AREA_MAIN,
					    "Could not close session when unloading module");
			}
		}
		g_free(params->mysql_user);
		g_free(params->mysql_passwd);
		g_free(params->mysql_server);
		g_free(params->mysql_db_name);
		g_free(params->mysql_table_name);
		g_free(params->mysql_users_table_name);
		g_free(params->mysql_ssl_keyfile);
		g_free(params->mysql_ssl_certfile);
		g_free(params->mysql_ssl_ca);
		g_free(params->mysql_ssl_capath);
		g_free(params->mysql_ssl_cipher);
	}
	g_free(params);
	return NULL;
}

/**
 * \brief Close all open user sessions
 *
 * \return A nu_error_t
 */

static nu_error_t mysql_close_open_user_sessions(struct log_mysql_params
						 *params)
{
	MYSQL *ld = NULL;
	char request[LONG_REQUEST_SIZE];
	int mysql_ret;
	int ok;


	ld = mysql_conn_init(params);

	if (!ld) {
		return NU_EXIT_ERROR;
	}

	ok = secure_snprintf(request, sizeof(request),
			     "UPDATE %s SET end_time=FROM_UNIXTIME(%lu) where end_time is NULL",
			     params->mysql_users_table_name, time(NULL));
	if (!ok) {
		mysql_close(ld);
		return NU_EXIT_ERROR;
	}

	/* execute query */
	mysql_ret = mysql_real_query(ld, request, strlen(request));
	if (mysql_ret != 0) {
		log_message(SERIOUS_WARNING, DEBUG_AREA_MAIN,
			    "[MySQL] Cannot execute request: %s",
			    mysql_error(ld));
		mysql_close(ld);
		return NU_EXIT_ERROR;
	}
	mysql_close(ld);
	return NU_EXIT_OK;

}

/* Init mysql system */
G_MODULE_EXPORT gboolean init_module_from_conf(module_t * module)
{
	/* char *ldap_base_dn=LDAP_BASE; */
	struct log_mysql_params *params =
	    g_new0(struct log_mysql_params, 1);

	log_message(VERBOSE_DEBUG, DEBUG_AREA_MAIN,
		    "Log_mysql module ($Revision$)");
	/* init global variables */
	params->mysql_ssl_cipher = MYSQL_SSL_CIPHER;
	params->hook = module->hook;

	/* set variables */
	params->mysql_server = nuauth_config_table_get_or_default("mysql_server_addr", MYSQL_SERVER);
	params->mysql_user = nuauth_config_table_get_or_default("mysql_user", MYSQL_USER);
	params->mysql_passwd = nuauth_config_table_get_or_default("mysql_passwd", MYSQL_PASSWD);
	params->mysql_db_name = nuauth_config_table_get_or_default("mysql_db_name", MYSQL_DB_NAME);
	params->mysql_table_name = nuauth_config_table_get_or_default("mysql_table_name", MYSQL_TABLE_NAME);
	params->mysql_users_table_name =
	    nuauth_config_table_get_or_default("mysql_users_table_name", MYSQL_USERS_TABLE_NAME);
	params->mysql_ssl_keyfile =
	    nuauth_config_table_get_or_default("mysql_ssl_keyfile", MYSQL_SSL_KEYFILE);
	params->mysql_ssl_certfile =
	    nuauth_config_table_get_or_default("mysql_ssl_certfile", MYSQL_SSL_CERTFILE);
	params->mysql_ssl_ca = nuauth_config_table_get_or_default("mysql_ssl_ca", MYSQL_SSL_CA);
	params->mysql_ssl_capath = nuauth_config_table_get_or_default("mysql_ssl_capath", MYSQL_SSL_CAPATH);
	params->mysql_ssl_cipher = nuauth_config_table_get_or_default("mysql_ssl_cipher", MYSQL_SSL_CIPHER);

	params->mysql_server_port = nuauth_config_table_get_or_default_int("mysql_server_port", MYSQL_SERVER_PORT);
	params->mysql_request_timeout = nuauth_config_table_get_or_default_int("mysql_request_timeout", MYSQL_REQUEST_TIMEOUT);
	params->mysql_use_ssl = nuauth_config_table_get_or_default_int("mysql_use_ssl", MYSQL_USE_SSL);
	params->mysql_use_ipv4_schema = nuauth_config_table_get_or_default_int("mysql_use_ipv4_schema", MYSQL_USE_IPV4_SCHEMA);
	params->mysql_prefix_version = nuauth_config_table_get_or_default_int("mysql_prefix_version", PREFIX_VERSION_NULOG2); /* XXX: Was previously initialized as PREFIX_VERSION_ORIG*/

	/* init thread private stuff */
	params->mysql_priv = g_private_new(NULL);
	log_message(DEBUG, DEBUG_AREA_MAIN,
		    "mysql part of the config file is parsed");

	module->params = (gpointer) params;

	/* do initial update of user session if needed */
	if ((!nuauth_is_reloading()) && (params->hook == MOD_LOG_SESSION)) {
		mysql_close_open_user_sessions(params);
	}

	return TRUE;
}

/*
 * Initialize connection to mysql server
 */
static MYSQL *mysql_conn_init(struct log_mysql_params *params)
{
	MYSQL *ld = NULL;
#ifdef MYSQL_OPT_RECONNECT
	my_bool trueval = 1;
#endif

	/* init connection */
	ld = mysql_init(ld);
	if (ld == NULL) {
		log_message(WARNING, DEBUG_AREA_MAIN, "mysql init error: %s",
			    strerror(errno));
		return NULL;
	}
#if HAVE_MYSQL_SSL
	/* Set SSL options, if configured to do so */
	if (params->mysql_use_ssl)
		mysql_ssl_set(ld, params->mysql_ssl_keyfile,
			      params->mysql_ssl_certfile,
			      params->mysql_ssl_ca,
			      params->mysql_ssl_capath,
			      params->mysql_ssl_cipher);
#endif
#if 0
	/* Set MYSQL object properties */
	if (mysql_options(ld, MYSQL_OPT_CONNECT_TIMEOUT, mysql_conninfo) !=
	    0) {
		log_message(WARNING, DEBUG_AREA_MAIN,
			    "mysql options setting failed : %s",
			    mysql_error(ld));
	}
#endif
#ifdef MYSQL_OPT_RECONNECT
#  if defined(MYSQL_VERSION_ID) && (MYSQL_VERSION_ID >= 50019)
	mysql_options(ld, MYSQL_OPT_RECONNECT, &trueval);
#  endif
#endif
	if (!mysql_real_connect
	    (ld, params->mysql_server, params->mysql_user,
	     params->mysql_passwd, params->mysql_db_name,
	     params->mysql_server_port, NULL, 0)) {
		log_message(WARNING, DEBUG_AREA_MAIN,
			    "mysql connection failed: %s",
			    mysql_error(ld));
		mysql_close(ld);
		return NULL;
	}
#ifdef MYSQL_OPT_RECONNECT
#  if defined(MYSQL_VERSION_ID) && (MYSQL_VERSION_ID < 50019)
	mysql_options(ld, MYSQL_OPT_RECONNECT, &trueval);
#  endif
#endif
	mysql_conn_list = g_slist_prepend(mysql_conn_list, ld);

	return ld;
}

static gchar *generate_osname(gchar * Name, gchar * Version,
			      gchar * Release)
{
	if (Name != NULL && Release != NULL && Version != NULL
	    && ((strlen(Name) + strlen(Release) + strlen(Version) + 3) <=
		OSNAME_MAX_SIZE)) {
		return g_strjoin("-", Name, Version, Release, NULL);
	} else {
		return g_strdup("");
	}
}

static gchar *generate_appname(gchar * appname)
{
	if (appname != NULL && strlen(appname) < APPNAME_MAX_SIZE) {
		return g_strdup(appname);
	} else {
		return g_strdup("");
	}
}

static char *quote_string(MYSQL * mysql, char *text)
{
	unsigned int length = strlen(text);
	char *quoted;
	if (length == 0)
		return strdup(text);
	quoted = (char *) malloc(length * 2 + 1);
	if (mysql_real_escape_string(mysql, quoted, text, length) == 0) {
		g_free(quoted);
		return NULL;
	}
	return quoted;
}

char* create_log_prefix(int prefix_version, const char* oob_sufix, connection_t *element)
{
	const gchar *place;

	if (prefix_version == PREFIX_VERSION_ORIG) {
		/* old log format: "42:ETH-IF ACCEPT" */
		const gchar *prefix;
		if (element->log_prefix) {
			prefix = element->log_prefix;
		} else {
			prefix = "Default";
		}
		return g_strdup_printf("%s %s", prefix, oob_sufix);
	}

	/* new log format: "F42A:message" */
	if (!element->log_prefix) {
		return g_strdup_printf("F0%c:Default DROP", oob_sufix[0]);
	}

	/* prefix is "[FIO]${ID_ACL}?:${description}",
	 * replace ? by decision (in fact first letter of prefix) */
	place = strchr(element->log_prefix, '?');
	if (place) {
		char *log_prefix = g_strdup(element->log_prefix);
		log_prefix[place - element->log_prefix] = oob_sufix[0];
		return log_prefix;
	} else {
		/* old log format? (eg. log prefix from plaintext module) */
		return g_strdup_printf("%s %s", element->log_prefix, oob_sufix);
	}
}

static char *build_insert_request(MYSQL * ld, connection_t * element,
				  tcp_state_t state,
				  char *auth_oob_prefix,
				  char *unauth_oob_prefix,
				  struct log_mysql_params *params)
{
	char request_fields[INSERT_REQUEST_FIELDS_SIZE];
	char request_values[INSERT_REQUEST_VALUES_SIZE];
	char src_ascii[IPV6_SQL_STRLEN];
	char dst_ascii[IPV6_SQL_STRLEN];
	char tmp_buffer[REQUEST_TMP_BUFFER];
	char *log_prefix;
	short unsigned int proto;
	gboolean ok;
	const char* oob_prefix;

	/* Write common informations */
	ok = secure_snprintf(request_fields, sizeof(request_fields),
			     "INSERT INTO %s (state, oob_time_sec, ip_protocol, ip_saddr, ip_daddr, ",
			     params->mysql_table_name);
	if (!ok) {
		return NULL;
	}

	if (ipv6_to_sql
			(params, &element->tracking.saddr, src_ascii,
			 sizeof(src_ascii), 1) != 0)
		return NULL;
	if (ipv6_to_sql
			(params, &element->tracking.daddr, dst_ascii,
			 sizeof(dst_ascii), 1) != 0)
		return NULL;

	proto = (short unsigned int) element->tracking.protocol;

	ok = secure_snprintf(request_values,
			sizeof(request_values),
			"VALUES ('%hu', '%lu', '%hu', %s, %s, ",
			(short unsigned int) state,
			(long unsigned int) element->
			timestamp,
			proto, src_ascii,
			dst_ascii);
	if (!ok) {
		return NULL;
	}

	if (element->iface_nfo.indev) {
		g_strlcat(request_fields, "oob_in, ",
			  INSERT_REQUEST_FIELDS_SIZE);
		g_strlcat(request_values, "'", INSERT_REQUEST_VALUES_SIZE);
		g_strlcat(request_values, element->iface_nfo.indev,
			  INSERT_REQUEST_VALUES_SIZE);
		g_strlcat(request_values, "', ",
			  INSERT_REQUEST_VALUES_SIZE);
	}

	if (element->iface_nfo.outdev) {
		g_strlcat(request_fields, "oob_out,",
			  INSERT_REQUEST_FIELDS_SIZE);
		g_strlcat(request_values, "'", INSERT_REQUEST_VALUES_SIZE);
		g_strlcat(request_values, element->iface_nfo.outdev,
			  INSERT_REQUEST_VALUES_SIZE);
		g_strlcat(request_values, "', ",
			  INSERT_REQUEST_VALUES_SIZE);
	}

	if (element->username) {
		oob_prefix = auth_oob_prefix;
	} else {
		oob_prefix = unauth_oob_prefix;
	}
	log_prefix = create_log_prefix(params->mysql_prefix_version, oob_prefix, element);

	/* Add user informations */
	if (element->username) {
		/* Get OS and application names */
		char *osname = generate_osname(element->os_sysname,
					       element->os_version,
					       element->os_release);
		char *appname = generate_appname(element->app_name);	/*Just a size check actually */

		/* Quote strings send to MySQL */
		char *quoted_username =
		    quote_string(ld, element->username);
		char *quoted_osname = quote_string(ld, osname);
		char *quoted_appname = quote_string(ld, appname);
		g_free(osname);
		g_free(appname);

		ok = (quoted_username != NULL) && (quoted_osname != NULL)
		    && (quoted_appname != NULL);
		if (ok) {
			/* Add oob prefix, informations about user, OS an application */
			g_strlcat(request_fields,
				  "oob_prefix, user_id, username, client_os, client_app",
				  sizeof(request_fields));
			ok = secure_snprintf(tmp_buffer,
					     sizeof(tmp_buffer),
					     "'%s', '%lu', '%s', '%s', '%s'",
					     log_prefix,
					     (long unsigned int) element->
					     user_id, quoted_username,
					     quoted_osname,
					     quoted_appname);
		}
		g_free(quoted_username);
		g_free(quoted_osname);
		g_free(quoted_appname);
		if (!ok) {
			return NULL;
		}
		g_strlcat(request_values, tmp_buffer,
			  sizeof(request_values));
	} else {
		/* Add oob prefix */
		g_strlcat(request_fields,
			  "oob_prefix", sizeof(request_fields));
		ok = secure_snprintf(tmp_buffer, sizeof(tmp_buffer),
				     "'%s'", log_prefix);
		if (!ok) {
			return NULL;
		}
		g_strlcat(request_values, tmp_buffer,
			  sizeof(request_values));
	}
	g_free(log_prefix);

	/* Add TCP/UDP parameters */
	if ((element->tracking.protocol == IPPROTO_TCP)
	    || (element->tracking.protocol == IPPROTO_UDP)) {
		if (element->tracking.protocol == IPPROTO_TCP) {
			g_strlcat(request_fields,
				  ", tcp_sport, tcp_dport)",
				  sizeof(request_fields));
		} else {
			g_strlcat(request_fields,
				  ", udp_sport, udp_dport)",
				  sizeof(request_fields));
		}
		ok = secure_snprintf(tmp_buffer, sizeof(tmp_buffer),
				     ", '%hu', '%hu')",
				     element->tracking.source,
				     element->tracking.dest);
		if (!ok) {
			return NULL;
		}
		g_strlcat(request_values, tmp_buffer,
			  sizeof(request_values));
	} else {
		g_strlcat(request_fields, ")", sizeof(request_fields));
		g_strlcat(request_values, ")", sizeof(request_values));
	}

	/* Check overflow */
	if (((sizeof(request_fields) - 1) <= strlen(request_fields))
	    || ((sizeof(request_values) - 1) <= strlen(request_values))) {
		return NULL;
	}

	/* do the mysql request */
	return g_strconcat(request_fields, "\n", request_values, NULL);
}

static inline int log_state_open(MYSQL * ld, connection_t * element,
				 struct log_mysql_params *params)
{
	char *request;
	int mysql_ret;

	if (element->tracking.protocol == IPPROTO_TCP &&
		(nuauthconf->log_users_strict ||
		 (element->flags & ACL_FLAGS_STRICT))) {
		gboolean ok;
		char request[SHORT_REQUEST_SIZE];
		char src_ascii[IPV6_SQL_STRLEN];

		if (ipv6_to_sql
		    (params, &element->tracking.saddr, src_ascii,
		     sizeof(src_ascii), 1) != 0)
			return -1;

		ok = secure_snprintf(request, sizeof(request),
				     "UPDATE %s SET state='%hu', end_timestamp=FROM_UNIXTIME('%lu') "
				     "WHERE (ip_saddr=%s AND tcp_sport='%u' AND (state=1 OR state=2))",
				     params->mysql_table_name,
				     TCP_STATE_CLOSE,
				     element->timestamp,
				     src_ascii,
				     (element->tracking).source);

		/* need to update table to suppress double field */
		if (!ok) {
			log_message(SERIOUS_WARNING, DEBUG_AREA_MAIN,
				    "Building mysql update query, the SHORT_REQUEST_SIZE limit was reached!");
			return -1;
		}

		mysql_ret = mysql_real_query(ld, request, strlen(request));
		if (mysql_ret != 0) {
			log_message(SERIOUS_WARNING, DEBUG_AREA_MAIN,
				    "[MySQL] Cannot update data: %s",
				    mysql_error(ld));
			mysql_close_current(params);
			return -1;
		}
	}

	/* build sql request */

	request = build_insert_request(ld, element,
			TCP_STATE_OPEN, "ACCEPT", "ACCEPT",
			params);

	if (request == NULL) {
		log_message(SERIOUS_WARNING, DEBUG_AREA_MAIN,
			    "Error while building MySQL insert query (state OPEN)!");
		return -1;
	}

	/* do query */
	mysql_ret = mysql_real_query(ld, request, strlen(request));
	g_free(request);


	/* check request error code */
	if (mysql_ret != 0) {
		log_message(SERIOUS_WARNING, DEBUG_AREA_MAIN,
			    "[MySQL] Cannot insert data: %s",
			    mysql_error(ld));
		mysql_close_current(params);
		return -1;
	}
	return 0;
}

static int log_state_drop(MYSQL * ld, connection_t * element,
			  struct log_mysql_params *params)
{
	int mysql_ret;

	/* build sql request */
	char *request = build_insert_request(ld, element,
					     TCP_STATE_DROP, "DROP",
					     "UNAUTHENTICATED DROP",
					     params);
	if (request == NULL) {
		log_message(SERIOUS_WARNING, DEBUG_AREA_MAIN,
			    "Error while building MySQL insert query (state DROP)!");
		return -1;
	}

	/* do query */
	mysql_ret = mysql_real_query(ld, request, strlen(request));
	g_free(request);

	/* check request error code */
	if (mysql_ret != 0) {
		log_message(SERIOUS_WARNING, DEBUG_AREA_MAIN,
			    "[MySQL] Cannot insert data: %s",
			    mysql_error(ld));
		mysql_close_current(params);
		return -1;
	}
	return 0;
}

static MYSQL *get_mysql_handler(struct log_mysql_params *params)
{
	MYSQL *ld = g_private_get(params->mysql_priv);

	if (ld) {
		return ld;
	}

	ld = mysql_conn_init(params);
	if (ld == NULL) {
		log_message(SERIOUS_WARNING, DEBUG_AREA_MAIN,
			    "Can not initiate MYSQL connection");
		return NULL;
	}
	g_private_set(params->mysql_priv, ld);
	return ld;

}

/**
 * \brief User packet logging
 *
 * This function is exported by the module and called by nuauth core when a packet needs to be logged
 *
 * \param element A pointer to a ::connection_t containing all information about the packet to be logged
 * \param state A ::tcp_state_t that indicate the state of the packet
 * \param params_p A pointer to the parameters of the module instance we're working for
 * \return -1 in case of error, 0 if there is no problem
 */
G_MODULE_EXPORT gint user_packet_logs(void *element, tcp_state_t state,
				      gpointer params_p)
{
	struct log_mysql_params *params =
	    (struct log_mysql_params *) params_p;
	MYSQL *ld = get_mysql_handler(params);
	if (ld == NULL) {
		return -1;
	}

	/* contruct request */
	switch (state) {
	case TCP_STATE_OPEN:
		return log_state_open(ld, (connection_t *) element,
				      params);
	case TCP_STATE_DROP:
		return log_state_drop(ld, (connection_t *) element,
				      params);

	default:
		/* Ignore other states */
		return 0;
	}
}

/**
 * \brief User session logging
 *
 * This function is exported by the module and called by nuauth core when a user connect or disconnect
 *
 * \param c_session A pointer to a ::user_session_t containing all information about the user
 * \param state A ::session_state_t that indicate the state of the user session (basically starting or ending)
 * \param params_p A pointer to the parameters of the module instance we're working for
 * \return -1 in case of error, 1 if there is no problem
 */
G_MODULE_EXPORT int user_session_logs(user_session_t * c_session,
				      session_state_t state,
				      gpointer params_p)
{
	struct log_mysql_params *params =
	    (struct log_mysql_params *) params_p;
	char request[LONG_REQUEST_SIZE];
	char ip_ascii[IPV6_SQL_STRLEN];
	int mysql_ret;
	MYSQL *ld;
	gboolean ok;

	ld = get_mysql_handler(params);
	if (ld == NULL) {
		return -1;
	}

	if (ipv6_to_sql(params, &c_session->addr, ip_ascii, sizeof(ip_ascii), 0) != 0)
		return -1;

	switch (state) {
	case SESSION_OPEN:
		{
			/* create new user session */
			char *quoted_username = quote_string(ld, c_session->user_name);
			char *quoted_osname = quote_string(ld, c_session->sysname);

			ok = (quoted_username != NULL) && (quoted_osname != NULL);
			if (ok) {
				ok = secure_snprintf(request, sizeof(request),
						"INSERT INTO %s (user_id, username, ip_saddr, "
						"os_sysname, os_release, os_version, socket, start_time) "
						"VALUES ('%lu', '%s', '%s', '%s', '%s', '%s', '%u', FROM_UNIXTIME(%lu))",
						params->mysql_users_table_name,
						(unsigned long)c_session->user_id,
						quoted_username,
						ip_ascii,
						quoted_osname,
						c_session->release,
						c_session->version,
						c_session->socket, time(NULL));
			}
			g_free(quoted_username);
			g_free(quoted_osname);
		}
		break;

	case SESSION_CLOSE:
		/* update existing user session */
		ok = secure_snprintf(request, sizeof(request),
				     "UPDATE %s SET end_time=FROM_UNIXTIME(%lu) "
				     "WHERE socket=%u AND ip_saddr=%s AND end_time IS NULL",
				     params->mysql_users_table_name,
				     time(NULL),
				     c_session->socket, ip_ascii);
		break;

	default:
		return -1;
	}
	if (!ok) {
		return -1;
	}

	/* execute query */
	mysql_ret = mysql_real_query(ld, request, strlen(request));
	if (mysql_ret != 0) {
		log_message(SERIOUS_WARNING, DEBUG_AREA_MAIN,
			    "[MySQL] Cannot execute request: %s",
			    mysql_error(ld));
		mysql_close_current(params);
		return -1;
	}

	return 1;
}

const gchar *g_module_check_init(GModule *module)
{
	mysql_conn_list = NULL;

	mysql_server_init(0, NULL, NULL);
	return NULL;
}

void g_module_unload(GModule *module)
{
#if 0
	GSList* pointer = mysql_conn_list;
	/* comment code as it seems to cause a crash when nuauth terminate */
	if (mysql_conn_list) {
		while (pointer) {
			mysql_close((MYSQL *)pointer->data);
			pointer = pointer->next;
		}
		g_slist_free(mysql_conn_list);
	}
#endif
	mysql_server_end();
}

/** @} */
