/*
 ** Copyright(C) 2003-2006 Eric Leblond <eric@regit.org>
 **		     Vincent Deffontaines <vincent@gryzor.com>
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
 */

#include <auth_srv.h>
#include <bofh_mysql.h>
#include <string.h>
#include <errno.h>

/** Minimum buffer size to write an IPv6 in SQL syntax */
#define IPV6_SQL_STRLEN (2+16*2+1)

/**
 * Convert an IPv6 address to SQL binary string.
 * Eg. ::1 => "0x0000000000000001"
 *
 * \return Returns -1 if fails, 0 otherwise.
 */
static int ipv6_to_sql(struct in6_addr *addr, char *buffer, size_t buflen)
{
    unsigned char i;
    unsigned char *addr8;
    size_t written;
    if (buflen < IPV6_SQL_STRLEN)
    {
        buffer[0] = 0;
        return -1;
    }
    buffer[0] = '0';
    buffer[1] = 'x';
    buffer += 2;
    addr8 = &addr->s6_addr[0];
    for (i=0; i<4; i++)
    {
        written = sprintf(buffer, "%02x%02x%02x%02x", 
                addr8[0],
                addr8[1],
                addr8[2],
                addr8[3]);
        if (written != 2*4)
        {
            buffer[0] = 0;
            return -1;
        }
        buffer += written;
        addr8 += 4;
    }
    buffer[0] = 0;
    return 0;
}

static nu_error_t mysql_close_open_user_sessions(struct log_mysql_params* params);
static MYSQL* mysql_conn_init(struct log_mysql_params* params);

/**
 *
 * \ingroup LoggingNuauthModules
 * \defgroup SQLModule MySQL logging module
 *
 * @{ */

G_MODULE_EXPORT gchar* unload_module_with_params(gpointer params_p)
{
  struct log_mysql_params* params = (struct log_mysql_params*)params_p;
  
  if (params){
    if (! nuauth_is_reloading()){
      if ( mysql_close_open_user_sessions(params) != NU_EXIT_OK){
            log_message(WARNING, AREA_MAIN,
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

static nu_error_t mysql_close_open_user_sessions(struct log_mysql_params* params)
{
    MYSQL* ld = mysql_conn_init(params);
    char request[LONG_REQUEST_SIZE];
    int mysql_ret;
    int ok;

    if (! ld){
        return NU_EXIT_ERROR;
    }

    ok = secure_snprintf(request, sizeof(request),
                    "UPDATE %s SET last_time=FROM_UNIXTIME(%lu) where last_time is NULL",
                    params->mysql_users_table_name,
                    time(NULL));
    if (!ok) {
        return NU_EXIT_ERROR;
    }

    /* execute query */
    mysql_ret = mysql_real_query(ld, request, strlen(request));
    if (mysql_ret != 0){
        log_message (SERIOUS_WARNING, AREA_MAIN,
            "[MySQL] Cannot execute request: %s", mysql_error(ld));
        mysql_close(ld);
        return NU_EXIT_ERROR;
    }
    mysql_close(ld);
    return NU_EXIT_OK;

}

/* Init mysql system */
G_MODULE_EXPORT gboolean 
init_module_from_conf(module_t *module)
{
  confparams mysql_nuauth_vars[] = {
      { "mysql_server_addr" , G_TOKEN_STRING, 0 , g_strdup(MYSQL_SERVER) },
      { "mysql_server_port" ,G_TOKEN_INT , MYSQL_SERVER_PORT,NULL },
      { "mysql_user" , G_TOKEN_STRING , 0 ,g_strdup(MYSQL_USER)},
      { "mysql_passwd" , G_TOKEN_STRING , 0 ,g_strdup(MYSQL_PASSWD)},
      { "mysql_db_name" , G_TOKEN_STRING , 0 ,g_strdup(MYSQL_DB_NAME)},
      { "mysql_table_name" , G_TOKEN_STRING , 0 ,g_strdup(MYSQL_TABLE_NAME)},
      { "mysql_users_table_name" , G_TOKEN_STRING , 0 ,g_strdup(MYSQL_USERS_TABLE_NAME)},
      { "mysql_request_timeout" , G_TOKEN_INT , MYSQL_REQUEST_TIMEOUT , NULL },
      { "mysql_use_ssl" , G_TOKEN_INT , MYSQL_USE_SSL, NULL},
      { "mysql_ssl_keyfile" , G_TOKEN_STRING , 0, g_strdup(MYSQL_SSL_KEYFILE)},
      { "mysql_ssl_certfile" , G_TOKEN_STRING , 0, g_strdup(MYSQL_SSL_CERTFILE)},
      { "mysql_ssl_ca" , G_TOKEN_STRING , 0, g_strdup(MYSQL_SSL_CA)},
      { "mysql_ssl_capath" , G_TOKEN_STRING , 0, g_strdup(MYSQL_SSL_CAPATH)},
      { "mysql_ssl_cipher" , G_TOKEN_STRING , 0, g_strdup(MYSQL_SSL_CIPHER)}
  };
    char *configfile=DEFAULT_CONF_FILE;
    /* char *ldap_base_dn=LDAP_BASE; */
    struct log_mysql_params* params=g_new0(struct log_mysql_params,1);

    /* init global variables */
    params->mysql_ssl_cipher=MYSQL_SSL_CIPHER;

    /* parse conf file */
    if (module->configfile){
        parse_conffile(module->configfile,sizeof(mysql_nuauth_vars)/sizeof(confparams),mysql_nuauth_vars);
    } else {
        parse_conffile(configfile,sizeof(mysql_nuauth_vars)/sizeof(confparams),mysql_nuauth_vars);
    }
    /* set variables */

#define READ_CONF(KEY) \
    get_confvar_value(mysql_nuauth_vars, sizeof(mysql_nuauth_vars)/sizeof(confparams), KEY)
#define READ_CONF_INT(VAR, KEY, DEFAULT) \
    do { gpointer vpointer = READ_CONF(KEY); if (vpointer) VAR = *(int *)vpointer; else VAR = DEFAULT; } while (0)

    params->mysql_server = (char *)READ_CONF("mysql_server_addr");
    params->mysql_user = (char *)READ_CONF("mysql_user");
    params->mysql_passwd = (char *)READ_CONF("mysql_passwd");
    params->mysql_db_name = (char *)READ_CONF("mysql_db_name");
    params->mysql_table_name = (char *)READ_CONF("mysql_table_name");
    params->mysql_users_table_name = (char *)READ_CONF("mysql_users_table_name");
    params->mysql_ssl_keyfile = (char *)READ_CONF("mysql_ssl_keyfile");
    params->mysql_ssl_certfile = (char *)READ_CONF("mysql_ssl_certfile");
    params->mysql_ssl_ca = (char *)READ_CONF("mysql_ssl_ca");
    params->mysql_ssl_capath = (char *)READ_CONF("mysql_ssl_capath");
    params->mysql_ssl_cipher = (char *)READ_CONF("mysql_ssl_cipher");

    READ_CONF_INT(params->mysql_server_port, "mysql_server_port", MYSQL_SERVER_PORT);
    READ_CONF_INT(params->mysql_request_timeout, "mysql_request_timeout", MYSQL_REQUEST_TIMEOUT);
    READ_CONF_INT(params->mysql_use_ssl, "mysql_use_ssl", MYSQL_USE_SSL);


    /* free config struct */
    free_confparams(mysql_nuauth_vars,sizeof(mysql_nuauth_vars)/sizeof(confparams));

    /* init thread private stuff */
    params->mysql_priv = g_private_new ((GDestroyNotify)mysql_close); 
    log_message(DEBUG, AREA_MAIN, "mysql part of the config file is parsed\n");

    /* do initial update of user session if needed */
    if (! nuauth_is_reloading()){
        mysql_close_open_user_sessions(params);
    }
    
    module->params=(gpointer)params;
    return TRUE;
}

/* 
 * Initialize connection to mysql server
 */
static MYSQL* mysql_conn_init(struct log_mysql_params* params)
{
    MYSQL *ld = NULL;

    /* init connection */
    ld = mysql_init(ld);     
    if (ld == NULL) {
        log_message(WARNING, AREA_MAIN, "mysql init error : %s\n",strerror(errno));
        return NULL;
    }
#if HAVE_MYSQL_SSL
    /* Set SSL options, if configured to do so */
    if (params->mysql_use_ssl)
        mysql_ssl_set(ld,params->mysql_ssl_keyfile,params->mysql_ssl_certfile,params->mysql_ssl_ca,params->mysql_ssl_capath,params->mysql_ssl_cipher);
#endif
#if 0
    /* Set MYSQL object properties */
    if (mysql_options(ld,MYSQL_OPT_CONNECT_TIMEOUT,mysql_conninfo) != 0){
        log_message(WARNING, AREA_MAIN, "mysql options setting failed : %s\n",mysql_error(ld));
    }
#endif
    if (!mysql_real_connect(ld,params->mysql_server,params->mysql_user,
                params->mysql_passwd,params->mysql_db_name,
                params->mysql_server_port,NULL,0)) {
        log_message(WARNING, AREA_MAIN, "mysql connection failed : %s\n",mysql_error(ld));
        return NULL;
    }
    return ld;
}

static char* quote_string(MYSQL *mysql, char *text)
{
    unsigned int length = strlen(text);
    char *quoted;
    if (length == 0)
        return strdup(text);
    quoted = (char *)malloc(length*2 + 1);
    if (mysql_real_escape_string(mysql, quoted, text, length) == 0)
    {
        g_free(quoted);
        return NULL;
    }
    return quoted;
}    

    

static MYSQL* get_mysql_handler(struct log_mysql_params* params)
{
    MYSQL *ld = g_private_get (params->mysql_priv);
    if (ld != NULL) {
        return ld;
    }
    
    ld = mysql_conn_init(params);
    if (ld == NULL){
        log_message (SERIOUS_WARNING, AREA_MAIN,
            "Can not initiate MYSQL connection");
        return NULL;
    }
    g_private_set(params->mysql_priv,ld);
    return ld;

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
G_MODULE_EXPORT int user_session_logs(user_session_t *c_session, session_state_t state,gpointer params_p)
{
    struct log_mysql_params* params = (struct log_mysql_params*)params_p;
    char request[LONG_REQUEST_SIZE];
    char ip_ascii[IPV6_SQL_STRLEN];
    int mysql_ret;
    MYSQL *ld;
    gboolean ok;
    
    ld = get_mysql_handler(params);
    if (ld == NULL) {
        return -1;
    }

    if (ipv6_to_sql(&c_session->addr, ip_ascii, sizeof(ip_ascii)) != 0)
        return -1;

    switch (state) {
        case SESSION_OPEN:
		return 0;
            
        case SESSION_CLOSE:
            /* update existing user session */
            ok = secure_snprintf(request, sizeof(request),
                    "SELECT * FROM  %s"
                    "WHERE socket=%u AND ip_saddr=%s"
		    "AND (state = 1 OR state =2)",
                    params->mysql_users_table_name,
                    time(NULL),
                    c_session->socket,
                    ip_ascii);
            break;

        default:
            return -1;
    }
    if (!ok) {
        return -1;
    }

    /* execute query */
    mysql_ret = mysql_real_query(ld, request, strlen(request));
    if (mysql_ret != 0){
        log_message (SERIOUS_WARNING, AREA_MAIN,
            "[MySQL] Cannot execute request: %s", mysql_error(ld));
        return -1;
    }
    /** \todo Loop on answer:
     * 
     * For each answer:
     *  - generate conntrack message
     *  - send destroy message to nufw
     */

    return 1;
}

/** @} */