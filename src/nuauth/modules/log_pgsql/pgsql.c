/*
 ** Copyright(C) 2003-2010 INL
 ** Written by Eric Leblond <eleblond@inl.fr>
 **	       Vincent Deffontaines <vincent@gryzor.com>
 **	       Pierre Chifflier <chifflier@inl.fr>
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


/* SSL notes :
 * the client cert needs to go in
 *                $HOME/.postgresql/root.crt see the comments at the top of
 *                               src/interfaces/libpq/fe-secure.c */

#include <auth_srv.h>
#include <log_pgsql.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <inttypes.h>
#include "security.h"

#include "nuauthconf.h"

#include "nubase.h"

static nu_error_t pgsql_close_open_user_sessions(struct log_pgsql_params
						 *params);
static PGconn *pgsql_conn_init(struct log_pgsql_params *params);

/*
 * Returns version of nuauth API
 */
G_MODULE_EXPORT uint32_t get_api_version()
{
	return NUAUTH_API_VERSION;
}


/**
 *
 * \ingroup LoggingNuauthModules
 * \defgroup PGSQLModule PgSQL logging module
 *
 * @{ */

/**
 * Convert an IPv6 address to PostgreSQL SQL format.
 *
 * \return Returns 0 on error, 1 otherwise.
 */
static int formatINET(struct log_pgsql_params *params,
	char *buffer, socklen_t buflen,
	const struct in6_addr *addr6,
	int use_ntohl)
{
	struct in_addr addr4;
	int af;
	const char *ret;
	const void *addr;
	if (params->pgsql_use_ipv4) {
		if (!is_ipv4(addr6)) {
			log_message(SERIOUS_WARNING, DEBUG_AREA_MAIN,
				    "PostgreSQL: Packet has IPV6 address but PostgreSQL use IPV4 only schema");
			return 0;
		}
		addr4.s_addr = addr6->s6_addr32[3];
		if (use_ntohl) {
			addr4.s_addr = ntohl(addr4.s_addr);
		}
		af = AF_INET;
		addr = &addr4;
	} else {
		af = AF_INET6;
		addr = addr6;
	}
	ret = inet_ntop (af, addr, buffer, buflen);
	if (ret == NULL) {
		buffer[0] = 0;
		return 0;
	}
	buffer[buflen-1] = 0;
	return 1;
}

G_MODULE_EXPORT gboolean unload_module_with_params(gpointer params_p)
{
	struct log_pgsql_params *params =
	    (struct log_pgsql_params *) params_p;
	if (params) {
		if (!nuauth_is_reloading()) {
			if (pgsql_close_open_user_sessions(params) !=
			    NU_EXIT_OK) {
				log_message(WARNING, DEBUG_AREA_MAIN,
					    "Could not close session when unloading module");
			}
		}

		g_free(params->pgsql_user);
		g_free(params->pgsql_passwd);
		g_free(params->pgsql_server);
		g_free(params->pgsql_ssl);
		g_free(params->pgsql_db_name);
		g_free(params->pgsql_table_name);
		g_free(params->pgsql_users_table_name);
		g_free(params->pgsql_auth_failure_table_name);
	}
	g_free(params);

	return TRUE;
}

/**
 * \brief Close all open user sessions
 *
 * \return A nu_error_t
 */

static nu_error_t pgsql_close_open_user_sessions(struct log_pgsql_params
						 *params)
{
	PGconn *ld = pgsql_conn_init(params);
	char request[INSERT_REQUEST_VALUES_SIZE];
	gboolean ok;
	PGresult *Result;

	if (!ld) {
		return NU_EXIT_ERROR;
	}

	ok = secure_snprintf(request, sizeof(request),
			     "UPDATE %s SET end_time=ABSTIME(%lu) WHERE end_time is NULL",
			     params->pgsql_users_table_name, time(NULL));
	if (!ok) {
		if (ld) {
			PQfinish(ld);
		}
		return NU_EXIT_ERROR;
	}

	/* do the query */
	Result = PQexec(ld, request);

	/* check error */
	if (!Result || PQresultStatus(Result) != PGRES_COMMAND_OK) {
		log_message(SERIOUS_WARNING, DEBUG_AREA_MAIN,
			    "[PostgreSQL] Cannot insert session: %s",
			    PQerrorMessage(ld));
		PQclear(Result);
		PQfinish(ld);
		return NU_EXIT_ERROR;
	}
	PQclear(Result);
	PQfinish(ld);
	return NU_EXIT_OK;
}


/* Init pgsql system */
G_MODULE_EXPORT gboolean init_module_from_conf(module_t * module)
{
	struct log_pgsql_params *params =
	    g_new0(struct log_pgsql_params, 1);
	module->params = params;

	log_message(VERBOSE_DEBUG, DEBUG_AREA_MAIN,
		    "Log_pgsql module ($Revision$)");

	/* set variables */
	params->pgsql_server = nuauth_config_table_get_or_default("pgsql_server_addr", PGSQL_SERVER);
	params->pgsql_server_port = nuauth_config_table_get_or_default_int("pgsql_server_port", PGSQL_SERVER_PORT);
	params->pgsql_user = nuauth_config_table_get_or_default("pgsql_user", PGSQL_USER);
	params->pgsql_passwd = nuauth_config_table_get_or_default("pgsql_passwd", PGSQL_PASSWD);
	params->pgsql_ssl = nuauth_config_table_get_or_default("pgsql_ssl", PGSQL_SSL);
	params->pgsql_db_name = nuauth_config_table_get_or_default("pgsql_db_name", PGSQL_DB_NAME);
	params->pgsql_table_name = nuauth_config_table_get_or_default("pgsql_table_name", PGSQL_TABLE_NAME);
	params->pgsql_users_table_name = nuauth_config_table_get_or_default("pgsql_users_table_name", PGSQL_USERS_TABLE_NAME);
	params->pgsql_auth_failure_table_name = nuauth_config_table_get_or_default("pgsql_auth_failure_table_name", PGSQL_AUTH_FAILURE_TABLE_NAME);
	params->pgsql_request_timeout = nuauth_config_table_get_or_default_int("pgsql_request_timeout", PGSQL_REQUEST_TIMEOUT);
	params->pgsql_use_ipv4 = nuauth_config_table_get_or_default_int("pgsql_use_ipv4", PGSQL_USE_IPV4);

	/* init thread private stuff */
	params->pgsql_priv = g_private_new((GDestroyNotify) PQfinish);

	/* do initial update of user session if needed */
	if (!nuauth_is_reloading()) {
		pgsql_close_open_user_sessions(params);
	}

	module->params = (gpointer) params;
	return TRUE;
}


/*
 * Initialize connection to pgsql server
 */
static PGconn *pgsql_conn_init(struct log_pgsql_params *params)
{
	char *pgsql_conninfo;
	PGconn *ld = NULL;
	int pgsql_status;

	log_message(DEBUG, DEBUG_AREA_MAIN,
		    "Going to init PostgreSQL connection.");

	pgsql_conninfo =
	    g_strdup_printf
	    ("host=%s port=%d dbname=%s user=%s password=%s connect_timeout=%d",
	     /* " sslmode=%s" */
	     params->pgsql_server,
	     params->pgsql_server_port,
	     params->pgsql_db_name,
	     params->pgsql_user,
	     params->pgsql_passwd, params->pgsql_request_timeout
	     /* params->pgsql_ssl */
	    );

	ld = PQconnectdb(pgsql_conninfo);
	pgsql_status = PQstatus(ld);
	if (pgsql_status != CONNECTION_OK) {
		log_message(WARNING, DEBUG_AREA_MAIN,
			    "PostgreSQL init error: %s", strerror(errno));
		g_free(pgsql_conninfo);
		PQfinish(ld);
		return NULL;
	}
	log_message(DEBUG, DEBUG_AREA_MAIN, "PostgreSQL init done");
	g_free(pgsql_conninfo);
	return ld;
}

static char *quote_pgsql_string(PGconn *ld, char *text)
{
	unsigned int length = 0;
	char *quoted = NULL;

	if (text == NULL) {
		return NULL;
	}
	length = strlen(text);
	quoted = g_new0(char, length * 2 + 1);
	if (PQescapeStringConn(ld, quoted, text, length, NULL) == 0) {
		g_free(quoted);
		return NULL;
	}
	return quoted;
}

static gchar *generate_osname(PGconn *ld, gchar * Name, gchar * Version,
			      gchar * Release)
{
	char *all, *quoted;
	if (Name == NULL || Release == NULL || Version == NULL
	    || OSNAME_MAX_SIZE <
	    (strlen(Name) + strlen(Release) + strlen(Version) + 3)) {
		return g_strdup("");
	}
	all = g_strjoin("-", Name, Version, Release, NULL);
	quoted = quote_pgsql_string(ld,  all);
	g_free(all);
	return quoted;
}

static int pgsql_insert(PGconn * ld, connection_t * element,
			char *oob_prefix, tcp_state_t state,
			struct log_pgsql_params *params)
{
	char request_fields[INSERT_REQUEST_FIELDS_SIZE];
	char request_values[INSERT_REQUEST_VALUES_SIZE];
	char tmp_buffer[INSERT_REQUEST_VALUES_SIZE];
	char ip_src[INET6_ADDRSTRLEN];
	char ip_dst[INET6_ADDRSTRLEN];
	gboolean ok;
	PGresult *Result;
	char *sql_query;
	char *log_prefix = "Default";

	if (!formatINET(params, ip_src, sizeof(ip_src),
				&element->tracking.saddr, 0)) {
		return -1;
	}
	if (!formatINET(params, ip_dst, sizeof(ip_dst),
				&element->tracking.daddr, 0)) {
		return -1;
	}

	if (element->log_prefix) {
		log_prefix = element->log_prefix;
	}

	/* Write common informations */
	ok = secure_snprintf(request_fields, sizeof(request_fields),
			     "INSERT INTO %s (oob_prefix, state, "
			     "oob_time_sec, oob_time_usec, start_timestamp, "
			     "ip_protocol, ip_saddr, ip_daddr",
			     params->pgsql_table_name);
	if (!ok) {
		return -1;
	}
	ok = secure_snprintf(request_values, sizeof(request_values),
			     "VALUES ('%s: %s', '%hu', "
			     "'%lu', '0', '%lu', "
			     "'%u', '%s', '%s'",
			     log_prefix, oob_prefix, state,
			     element->timestamp, element->timestamp,
			     element->tracking.protocol, ip_src, ip_dst);
	if (!ok) {
		return -1;
	}

	/* Add user informations */
	if (element->username) {
		/* Get OS and application names */
		char *quoted_username =
		    quote_pgsql_string(ld, element->username);
		char *quoted_osname = generate_osname(ld, element->os_sysname,
						      element->os_version,
						      element->os_release);
		char *quoted_appname;

		if (element->app_name != NULL
		    && strlen(element->app_name) < APPNAME_MAX_SIZE)
			quoted_appname =
			    quote_pgsql_string(ld, element->app_name);
		else
			quoted_appname = g_strdup("");

		/* Quote strings send to PostgreSQL */
		g_strlcat(request_fields,
			  ", user_id, username, client_os, client_app",
			  sizeof(request_fields));
		ok = secure_snprintf(tmp_buffer, sizeof(tmp_buffer),
				     ", '%u', E'%s', E'%s', E'%s'",
				     element->user_id,
				     quoted_username,
				     quoted_osname, quoted_appname);
		g_free(quoted_username);
		g_free(quoted_osname);
		g_free(quoted_appname);
		if (!ok) {
			return -1;
		}
		g_strlcat(request_values, tmp_buffer,
			  sizeof(request_values));
	}

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
				     ", '%hu', '%hu');",
				     element->tracking.source,
				     element->tracking.dest);
		if (!ok) {
			return -1;
		}
		g_strlcat(request_values, tmp_buffer,
			  sizeof(request_values));
	} else {
		g_strlcat(request_fields, ")", sizeof(request_fields));
		g_strlcat(request_values, ");", sizeof(request_values));
	}

	/* Check overflow */
	if (((sizeof(request_fields) - 1) <= strlen(request_fields))
	    || ((sizeof(request_values) - 1) <= strlen(request_values))) {
		return -1;
	}

	/* create the sql query */
	sql_query =
	    g_strconcat(request_fields, "\n", request_values, NULL);
	if (sql_query == NULL) {
		log_message(SERIOUS_WARNING, DEBUG_AREA_MAIN,
			    "Fail to build PostgreSQL query (maybe too long)!");
		return -1;
	}

	/* do the query */
	Result = PQexec(ld, sql_query);

	g_free(sql_query);

	/* check error */
	if (!Result || PQresultStatus(Result) != PGRES_COMMAND_OK) {
		log_message(SERIOUS_WARNING, DEBUG_AREA_MAIN,
			    "[PostgreSQL] Cannot insert data: %s",
			    PQerrorMessage(ld));
		PQclear(Result);
		return -1;
	}
	PQclear(Result);
	return 0;
}

static int pgsql_update_close(PGconn * ld, connection_t * element,
			      struct log_pgsql_params *params)
{
	char ip_src[INET6_ADDRSTRLEN];
	char request[SHORT_REQUEST_SIZE];
	PGresult *Result;
	gboolean ok;

	if (!formatINET(params, ip_src, sizeof(ip_src),
				&element->tracking.saddr, 1)) {
		return -1;
	}

	ok = secure_snprintf(request, sizeof(request),
			     "UPDATE %s SET state='%hu', end_timestamp='%lu' "
			     "WHERE (ip_saddr='%s' AND tcp_sport='%u' "
			     "AND (state=1 OR state=2));",
			     params->pgsql_table_name,
			     TCP_STATE_CLOSE,
			     element->timestamp,
			     ip_src, element->tracking.source);
	if (!ok) {
		log_message(SERIOUS_WARNING, DEBUG_AREA_MAIN,
			    "Fail to build PostgreSQL query (maybe too long)!");
		return -1;
	}

	/* do the query */
	Result = PQexec(ld, request);
	if (!Result || PQresultStatus(Result) != PGRES_COMMAND_OK) {
		log_message(SERIOUS_WARNING, DEBUG_AREA_MAIN,
			    "[PostgreSQL] Cannot update data: %s",
			    PQerrorMessage(ld));
		PQclear(Result);
		return -1;
	}
	PQclear(Result);
	return 0;
}

static PGconn *get_pgsql_handler(struct log_pgsql_params *params)
{
	/* get/open postgresql connection */
	PGconn *ld = g_private_get(params->pgsql_priv);
	if (ld == NULL) {
		ld = pgsql_conn_init(params);
		if (ld == NULL) {
			log_message(SERIOUS_WARNING, DEBUG_AREA_MAIN,
				    "Can not initiate PgSQL connection!");
			return NULL;
		}
		g_private_set(params->pgsql_priv, ld);
	}
	return ld;
}

G_MODULE_EXPORT gint user_packet_logs(void *element,
				      tcp_state_t state, gpointer params_p)
{
	struct log_pgsql_params *params =
	    (struct log_pgsql_params *) params_p;
	PGconn *ld = get_pgsql_handler(params);
	if (ld == NULL)
		return -1;

	switch (state) {
	case TCP_STATE_OPEN:
		if (((connection_t *)element)->tracking.protocol == IPPROTO_TCP &&
			(nuauthconf->log_users_strict ||
			 (((connection_t *)element)->flags & ACL_FLAGS_STRICT))) {
			int ret = pgsql_update_close(ld,
						     (connection_t *)element,
						     params);
			if (ret != 0) {
				return ret;
			}
		}

		return pgsql_insert(ld, (connection_t *)element,
				    "ACCEPT", state, params);
	case TCP_STATE_DROP:
		return pgsql_insert(ld, (connection_t *)element,
				    "DROP", state, params);

		/* Skip other messages */
	default:
		return 0;
	}
}

G_MODULE_EXPORT int user_session_logs(user_session_t * c_session,
				      session_state_t state,
				      gpointer params_p)
{
	char request[INSERT_REQUEST_VALUES_SIZE];
	char addr_ascii[INET6_ADDRSTRLEN];
	gchar *str_groups;
	struct log_pgsql_params *params =
	    (struct log_pgsql_params *) params_p;
	gboolean ok;
	PGresult *Result;
	PGconn *ld = get_pgsql_handler(params);
	gchar *q_user_name;
	gchar *q_sysname;
	gchar *q_release;
	gchar *q_version;

	if (ld == NULL)
		return -1;

	if (!formatINET(params, addr_ascii, sizeof(addr_ascii),
				&c_session->addr, 0)) {
		return -1;
	}

	switch (state) {
	case SESSION_OPEN:
		/* build list of user groups */
		str_groups = str_print_group(c_session);
		/* quote pgsql strings */
		q_user_name = quote_pgsql_string(ld, c_session->user_name);
		q_sysname = quote_pgsql_string(ld, c_session->sysname);
		q_release = quote_pgsql_string(ld, c_session->release);
		q_version = quote_pgsql_string(ld, c_session->version);
		/* create new user session */
		ok = secure_snprintf(request, sizeof(request),
				     "INSERT INTO %s (user_id, username, user_groups, ip_saddr, "
				     "os_sysname, os_release, os_version, socket, start_time) "
				     "VALUES ('%lu', E'%s', '%s', '%s', E'%s', E'%s', E'%s', '%u', ABSTIME(%lu))",
				     params->pgsql_users_table_name,
				     (unsigned long)c_session->user_id,
				     q_user_name,
				     str_groups,
				     addr_ascii,
				     q_sysname,
				     q_release,
				     q_version,
				     c_session->socket, time(NULL));
		g_free(q_user_name);
		g_free(q_sysname);
		g_free(q_release);
		g_free(q_version);
		g_free(str_groups);
		break;

	case SESSION_CLOSE:
		/* update existing user session */
		ok = secure_snprintf(request, sizeof(request),
				     "UPDATE %s SET end_time=ABSTIME(%lu) "
				     "WHERE socket='%u' and ip_saddr='%s' AND end_time IS NULL",
				     params->pgsql_users_table_name,
				     time(NULL),
				     c_session->socket, addr_ascii);
		break;

	default:
		return -1;
	}
	if (!ok) {
		return -1;
	}

	/* do the query */
	Result = PQexec(ld, request);

	/* check error */
	if (!Result || PQresultStatus(Result) != PGRES_COMMAND_OK) {
		log_message(SERIOUS_WARNING, DEBUG_AREA_MAIN,
			    "[PostgreSQL] Cannot insert session: %s",
			    PQerrorMessage(ld));
		PQclear(Result);
		return -1;
	}
	PQclear(Result);
	return 0;
}

G_MODULE_EXPORT void auth_error_log(user_session_t * c_session,
				    nuauth_auth_error_t error,
				    const char *text, gpointer params_p)
{
	struct log_pgsql_params *params =
	    (struct log_pgsql_params *) params_p;
	char addr_ascii[INET6_ADDRSTRLEN];
	char request_values[INSERT_REQUEST_VALUES_SIZE];
	char request_fields[INSERT_REQUEST_FIELDS_SIZE];
	char tmp_buffer[INSERT_REQUEST_VALUES_SIZE];
	char * quoted_username = NULL;
	gchar *str_groups;
	gchar * sql_query;
	gboolean ok;
	PGresult *Result;
	PGconn *ld = get_pgsql_handler(params);
	if (ld == NULL)
		return;


	if (!formatINET(params, addr_ascii, sizeof(addr_ascii),
				&c_session->addr, 0)) {
		return;
	}

	quoted_username = quote_pgsql_string(ld, c_session->user_name);
	/* create new user session */
	ok = secure_snprintf(request_fields, sizeof(request_fields),
			"INSERT INTO %s (username, ip_saddr, reason, time, "
			"sport",
			params->pgsql_auth_failure_table_name);

	if (!ok) {
		g_free(quoted_username);
		return;
	}

	ok = secure_snprintf(request_values, sizeof(request_values),
			"VALUES (E'%s', '%s', '%s', ABSTIME(%lu), '%d'",
			quoted_username,
			addr_ascii,
			text,
			time(NULL),
			c_session->sport);

	g_free(quoted_username);

	if (!ok) {
		return;
	}

	if (c_session->groups) {
		g_strlcat(request_fields, ",user_id, user_groups",
				sizeof(request_fields));
		/* build list of user groups */
		str_groups = str_print_group(c_session);

		ok = secure_snprintf(tmp_buffer, sizeof(tmp_buffer),
				     ", '%u', '%s'",
				     c_session->user_id,
				     str_groups);
		g_free(str_groups);
		if (!ok) {
			return;
		}
		g_strlcat(request_values, tmp_buffer,
			  sizeof(request_values));
	}

	if (c_session->sysname) {
		char * q_sysname = quote_pgsql_string(ld, c_session->sysname);
		char * q_release = quote_pgsql_string(ld, c_session->release);
		char * q_version = quote_pgsql_string(ld, c_session->version);


		g_strlcat(request_fields, "os_sysname, os_release, os_version)",
				sizeof(request_fields));
		ok = secure_snprintf(tmp_buffer, sizeof(tmp_buffer),
				     ", E'%s', E'%s', E'%s')",
				     q_sysname,
				     q_release,
				     q_version);
		g_free(q_sysname);
		g_free(q_release);
		g_free(q_version);

		if (!ok) {
			return;
		}
		g_strlcat(request_values, tmp_buffer,
			  sizeof(request_values));
	} else {
		g_strlcat(request_fields, ")", sizeof(request_fields));
		g_strlcat(request_values, ");", sizeof(request_values));
	}

	/* create the sql query */
	sql_query =
	    g_strconcat(request_fields, "\n", request_values, NULL);
	if (sql_query == NULL) {
		log_message(SERIOUS_WARNING, DEBUG_AREA_MAIN,
			    "Fail to build PostgreSQL query (maybe too long)!");
		return;
	}


	/* do the query */
	Result = PQexec(ld, sql_query);

	g_free(sql_query);
	/* check error */
	if (!Result || PQresultStatus(Result) != PGRES_COMMAND_OK) {
		log_message(SERIOUS_WARNING, DEBUG_AREA_MAIN,
			    "[PostgreSQL] Cannot insert session: %s",
			    PQerrorMessage(ld));
	}
	PQclear(Result);
	return;


}

/** @} */
