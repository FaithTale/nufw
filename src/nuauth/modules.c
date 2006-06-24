/*
 ** Copyright(C) 2005,2006 INL
 ** written by  Eric Leblond <regit@inl.fr>
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
 ** In addition, as a special exception, the copyright holders give
 ** permission to link the code of portions of this program with the
 ** Cyrus SASL library under certain conditions as described in each
 ** individual source file, and distribute linked combinations
 ** including the two.
 ** You must obey the GNU General Public License in all respects
 ** for all of the code used other than Cyrus SASL.  If you modify
 ** file(s) with this exception, you may extend this exception to your
 ** version of the file(s), but you are not obligated to do so.  If you
 ** do not wish to do so, delete this exception statement from your
 ** version.  If you delete this exception statement from all source
 ** files in the program, then also delete it here.
 **
 ** This product includes software developed by Computing Services
 ** at Carnegie Mellon University (http://www.cmu.edu/computing/).
 **
 */

/**
 *
 * \ingroup  Nuauth
 * \defgroup NuauthModules Nuauth Modules
 *
 * \brief Modules are used for every interaction with the outside. They are implemented using Glib modules.
 *
 * A module has to export a set of functions to be able to initialize :
 *  - init_module_from_conf() : Init module with respect to a configuration file
 *  - unload_module_with_params() : Clean a module instance and free related parameter
 * Optionally, the initialisation function of the glib can be used
 * 
 * After this, it has to export the functions that are used by hook :
 *  - define_periods() : define period that can be used in time-based acls
 *  - user_check() : verify user credentials
 *  - get_user_groups() : found groups the user belong to
 *  - get_user_id() : get user id
 *  - acl_check() : verify acl for a packet
 *  - user_session_logs() : log user connection and disconnection
 *  - user_packet_logs() : log packet
 * 
 * @{
 */

/** 
 * \file modules.c
 * \brief Take care of interaction with modules
 *
 * It contains the functions that load and unload modules as well as all 
 * ..._check functions use in the code to interact with the modules
 */

#include <auth_srv.h>
#include "modules_definition.h"

/**
 * Check a user/password against the list of modules used for user authentication
 *  It returns the decision using SASL defined return value.
 */
int modules_user_check (const char *user, const char *pass,unsigned passlen)
{
    /* iter through module list and stop when user is found */
    GSList *walker=user_check_modules;
    int walker_return=0;
    for (;walker!=NULL;walker=walker->next ){
        walker_return=(*(user_check_callback*)(((module_t*)walker->data))->func)(user,
                pass,passlen,((module_t*)walker->data)->params);
        if (walker_return == SASL_OK)
            return SASL_OK;
    }
    return SASL_NOAUTHZ;
}

/**
 * Get group for a given user 
 */
GSList* modules_get_user_groups(const char *user)
{
    /* iter through module list and stop when an acl is found */
    GSList *walker=get_user_groups_modules;
    GSList* walker_return=NULL;

    for (;walker!=NULL;walker=walker->next ){
        walker_return=(*(get_user_groups_callback*)(((module_t*)walker->data))->func)(user,
                ((module_t*)walker->data)->params);
        if (walker_return)
            return walker_return;
    }

    return NULL;

}

uint32_t modules_get_user_id(const char *user)
{
    /* iter through module list and stop when an acl is found */
    GSList *walker=get_user_id_modules;
    uint32_t walker_return=0;

    for (;walker!=NULL;walker=walker->next ){
        walker_return=(*(get_user_id_callback*)(((module_t*)walker->data))->func)(user,
                ((module_t*)walker->data)->params);
        if (walker_return)
            return walker_return;
    }

    return 0;

}
/**
 * Check a connection and return a list of acl that match the information
 * contained in the connection.
 */
GSList* modules_acl_check (connection_t* element)
{
    /* iter through module list and stop when an acl is found */
    GSList *walker=acl_check_modules;
    GSList* walker_return=NULL;

    for (;walker!=NULL;walker=walker->next ){
        walker_return=(*(acl_check_callback*)(((module_t*)walker->data))->func)(element,
                ((module_t*)walker->data)->params);
        if (walker_return)
            return walker_return;
    }

    return NULL;
}

/* ip auth */
gchar* modules_ip_auth(tracking_t * header)
{
    /* iter through module list and stop when decision is made */
    GSList *walker=ip_auth_modules;
    gchar* walker_return=NULL;
    for (;walker!=NULL;walker=walker->next ){
        walker_return=(*(ip_auth_callback*)(((module_t*)walker->data))->func)(header,
                ((module_t*)walker->data)->params);
        if (walker_return)
            return walker_return;
    }
    return NULL;
}


/**
 * log authenticated packets
 */
int modules_user_logs (connection_t* element, tcp_state_t state)
{
    /* iter through all modules list */
    GSList *walker=user_logs_modules;
    for (; walker!=NULL; walker=walker->next) {
        user_logs_callback *handler = (user_logs_callback*)((module_t*)walker->data)->func;
        handler (element, state, ((module_t*)walker->data)->params);
    }
    return 0;
}

/**
 * log user connection and disconnection
 */
int modules_user_session_logs(user_session_t* user, session_state_t state)
{
    /* iter through all modules list */
    GSList *walker=user_session_logs_modules;
    for (; walker!=NULL; walker=walker->next) {
        user_session_logs_callback *handler = (user_session_logs_callback*)((module_t*)walker->data)->func;
        handler (user, state, ((module_t*)walker->data)->params);
    }
    return 0;
}

/** 
 * parse time period configuration for each module
 * and fille the given hash (first argument)
 */
void modules_parse_periods(GHashTable* periods)
{
    /* iter through all modules list */
    GSList *walker=period_modules;
    for (; walker!=NULL; walker=walker->next ){
        define_period_callback *handler = (define_period_callback*)(((module_t*)walker->data)->func);
        handler (periods,((module_t*)walker->data)->params);
    }
}

/**
 * Check certificate
 *
 * \param session TLS connection
 * \param cert x509 certificate
 * \return SASL_OK if certificate is correct
 */
int modules_check_certificate (gnutls_session session, gnutls_x509_crt cert)
{
    /* iter through all modules list */
    GSList *walker=certificate_check_modules;
    int ret;

    log_message(VERBOSE_DEBUG,AREA_MAIN,"module check certificate");
    for (; walker!=NULL; walker=walker->next) {
        certificate_check_callback *handler = (certificate_check_callback*)((module_t*)walker->data)->func;
        ret = handler (session, cert, ((module_t*)walker->data)->params);
        if (ret != SASL_OK){
            return ret;
        }
    }
    return SASL_OK;
}

/**
 * certificate to uid
 *
 * \param session TLS connection
 * \param cert x509 certificate
 * \return uid 
 */
gchar* modules_certificate_to_uid (gnutls_session session, gnutls_x509_crt cert)
{
    /* iter through all modules list */
    GSList *walker=certificate_to_uid_modules;
    gchar* uid;
    for (; walker!=NULL; walker=walker->next) {
        certificate_to_uid_callback *handler = (certificate_to_uid_callback*)((module_t*)walker->data)->func;
        uid = handler (session, cert, ((module_t*)walker->data)->params);
        if (uid){
            return uid;
        }
    }
    return NULL;
}



void free_module_t(module_t* module)
{
    if (module){
        if (module->free_params){
            module->free_params(module->params);
            module->params = NULL;
        }
        g_free(module->module_name);
        g_free(module->name);
        g_free(module->configfile);
        g_module_close(module->module);
    }
}

/**
 * Initialise module system
 *
 * Please note it has only to be called once
 *
 */

int init_modules_system()
{
    /* init modules list mutex */
    modules_mutex = g_mutex_new ();
    user_check_modules=NULL;
    get_user_groups_modules=NULL;
    get_user_id_modules=NULL;
    acl_check_modules=NULL;
    period_modules=NULL;
    ip_auth_modules=NULL;
    user_logs_modules=NULL;
    user_session_logs_modules=NULL;
    certificate_to_uid_modules=NULL;
    certificate_check_modules=NULL;

    return 1;
}

/**
 * Load module for a task
 *
 * Please note that last args is a pointer of pointer
 */
static int load_modules_from(gchar* confvar, gchar* func,GSList** target)
{
    gchar** modules_list=g_strsplit(confvar," ",0);
    gchar* module_path;
    init_module_from_conf_t* initmod;
    gchar **params_list;
    module_t *current_module;
    int i;

    for (i=0;modules_list[i]!=NULL;i++) {
        current_module = g_new0(module_t,1);

        /* var format is NAME:MODULE:CONFFILE */
        params_list = g_strsplit(modules_list[i],":",3);
        current_module->name=g_strdup(params_list[0]);
        if (params_list[1]) {
            current_module->module_name=g_strdup(params_list[1]);
            if (params_list[2]) {
                current_module->configfile=g_strdup(params_list[2]);
            } else {
                /* we build config file name */
                current_module->configfile=g_strjoin(
                        NULL, CONFIG_DIR, "/", MODULES_CONF_DIR, "/",
                        current_module->name, MODULES_CONF_EXTENSION, NULL);
            }
        } else {
            current_module->module_name=g_strdup(current_module->name);
            current_module->configfile=NULL;
        }

        /* Open dynamic library */
        module_path = g_module_build_path(MODULE_PATH, current_module->module_name);
        current_module->module = g_module_open (module_path, 0);
        g_free(module_path);
        
        log_message(VERBOSE_DEBUG,AREA_MAIN,
                "\tmodule %s: using %s with configfile %s",
                current_module->name,
                current_module->module_name,
                current_module->configfile);
        if (current_module->module == NULL) {
            g_error("Unable to load module %s in %s",modules_list[i],MODULE_PATH);
            free_module_t(current_module);
            continue;
        }

        /* get module function handler */
        if (!g_module_symbol (current_module->module, func, (gpointer*)&current_module->func)) {
            g_error ("Unable to load function %s in %s",
                    func,
                    g_module_name(current_module->module));
            free_module_t(current_module);
            g_strfreev(params_list);
            continue;
        }

        /* get params for module by calling module exported function */
        if (g_module_symbol (current_module->module, INIT_MODULE_FROM_CONF, (gpointer*)&initmod)) {
            /* Initialize module */
            if (! initmod(current_module) ) {
                g_warning("Unable to init module, continuing anyway");
                current_module->params=NULL;
            }
        } else {
            log_message(WARNING,AREA_MAIN,
                    "No init function for module %s: PLEASE UPGRADE!",
                    current_module->module_name);
            current_module->params=NULL;
        }
        
        /* get params for module by calling module exported function */
        if (!g_module_symbol (current_module->module, "unload_module_with_params", (gpointer*)&(current_module->free_params))) {
            log_message(WARNING, AREA_MAIN,
                    "No init function for module %s: PLEASE UPGRADE!",
                    current_module->module_name);
            current_module->free_params=NULL;
        } 

        /* store module in module list */
        *target=g_slist_append(*target,(gpointer)current_module);
        nuauthdatas->modules=g_slist_prepend(nuauthdatas->modules,current_module);

        /* free memory */
        g_strfreev(params_list);
    }
    g_strfreev(modules_list);
    return 1;

}
/**
 * Load modules for user and acl checking as well as for user logging and ip authentication
 */
int load_modules()
{
    confparams nuauth_vars[] = {
        { "nuauth_user_check_module" , G_TOKEN_STRING , 1, g_strdup(DEFAULT_USERAUTH_MODULE) },
        { "nuauth_get_user_groups_module" , G_TOKEN_STRING , 1, g_strdup(DEFAULT_USERAUTH_MODULE) },
        { "nuauth_get_user_id_module" , G_TOKEN_STRING , 1, g_strdup(DEFAULT_USERAUTH_MODULE) },
        { "nuauth_acl_check_module" , G_TOKEN_STRING , 1, g_strdup(DEFAULT_ACLS_MODULE) },
        { "nuauth_periods_module" , G_TOKEN_STRING , 1, g_strdup(DEFAULT_PERIODS_MODULE) },
        { "nuauth_user_logs_module" , G_TOKEN_STRING , 1, g_strdup(DEFAULT_LOGS_MODULE) },
        { "nuauth_user_session_logs_module" , G_TOKEN_STRING , 1, g_strdup(DEFAULT_LOGS_MODULE) },
        { "nuauth_ip_authentication_module" , G_TOKEN_STRING , 1, g_strdup(DEFAULT_IPAUTH_MODULE) },
        { "nuauth_certificate_check_module" , G_TOKEN_STRING , 1, g_strdup(DEFAULT_CERTIFICATE_CHECK_MODULE) },
        { "nuauth_certificate_to_uid_module" , G_TOKEN_STRING , 1, g_strdup(DEFAULT_CERTIFICATE_TO_UID_MODULE) }
    };
    char *nuauth_acl_check_module;
    char *nuauth_user_check_module;
    char *nuauth_get_user_groups_module;
    char *nuauth_get_user_id_module;
    char *nuauth_user_logs_module;
    char *nuauth_user_session_logs_module;
    char *nuauth_ip_authentication_module;
    char *nuauth_periods_module;
    char *nuauth_certificate_check_module;
    char *nuauth_certificate_to_uid_module;
    char *configfile=DEFAULT_CONF_FILE;

    /* parse conf file */
    parse_conffile(configfile,sizeof(nuauth_vars)/sizeof(confparams),nuauth_vars);

#define READ_CONF(KEY) \
    get_confvar_value(nuauth_vars, sizeof(nuauth_vars)/sizeof(confparams), KEY);

    nuauth_user_check_module = (char*)READ_CONF("nuauth_user_check_module");
    nuauth_get_user_groups_module = (char*)READ_CONF("nuauth_get_user_groups_module");
    nuauth_get_user_id_module = (char*)READ_CONF("nuauth_get_user_id_module");
    nuauth_user_logs_module = (char*)READ_CONF("nuauth_user_logs_module");
    nuauth_user_session_logs_module = (char*)READ_CONF("nuauth_user_session_logs_module");
    nuauth_acl_check_module = (char*)READ_CONF("nuauth_acl_check_module");
    nuauth_periods_module = (char*)READ_CONF("nuauth_periods_module");
    nuauth_ip_authentication_module = (char*)READ_CONF("nuauth_ip_authentication_module");
    nuauth_certificate_check_module = (char*)READ_CONF("nuauth_certificate_check_module");
    nuauth_certificate_to_uid_module = (char*)READ_CONF("nuauth_certificate_to_uid_module");

    /* free config struct */
    free_confparams(nuauth_vars,sizeof(nuauth_vars)/sizeof(confparams));

    /* external auth module loading */
    g_mutex_lock(modules_mutex);

#define LOAD_MODULE(VAR, LIST, KEY, TEXT) \
    log_message(VERBOSE_DEBUG, AREA_MAIN, "Loading " TEXT " modules:"); \
    load_modules_from(VAR, KEY, &(LIST)); \
    g_free(VAR);
    
    /* loading modules */
    LOAD_MODULE (nuauth_user_check_module, user_check_modules, 
            "user_check", "user checking");
    LOAD_MODULE (nuauth_get_user_groups_module, get_user_groups_modules, 
            "get_user_groups", "user groups fetching");
    LOAD_MODULE (nuauth_get_user_id_module, get_user_id_modules, 
            "get_user_id", "user id fetching");
    LOAD_MODULE (nuauth_acl_check_module, acl_check_modules, 
            "acl_check", "acls checking");
    LOAD_MODULE (nuauth_periods_module, period_modules, 
            "define_periods", "define periods checking");
    LOAD_MODULE (nuauth_user_logs_module, user_logs_modules, 
            "user_packet_logs", "user packet logging");
    LOAD_MODULE (nuauth_user_session_logs_module, user_session_logs_modules, 
            "user_session_logs", "user session logging");
    LOAD_MODULE(nuauth_certificate_check_module, certificate_check_modules, 
            "certificate_check", "certificate check");
    LOAD_MODULE(nuauth_certificate_to_uid_module, certificate_to_uid_modules,
            "certificate_to_uid", "certificate to uid");
    if (nuauthconf->do_ip_authentication){
        LOAD_MODULE(nuauth_ip_authentication_module, ip_auth_modules, 
                "ip_authentication", "ip authentication");
    }

    g_mutex_unlock(modules_mutex);
    return 1;
}

/**
 * Unload all modules of NuAuth (variable ::nuauthdatas->modules).
 */
void unload_modules()
{
    GSList *c_module;

    g_mutex_lock(modules_mutex);
    g_slist_free(user_check_modules);
    user_check_modules=NULL;
    g_slist_free(acl_check_modules);
    acl_check_modules=NULL;
    g_slist_free(period_modules);
    period_modules=NULL;
    g_slist_free(ip_auth_modules);
    ip_auth_modules=NULL;
    g_slist_free(user_logs_modules);
    user_logs_modules=NULL;
    g_slist_free(user_session_logs_modules);
    user_session_logs_modules=NULL;

    g_slist_free(certificate_check_modules);
    certificate_check_modules=NULL;
    g_slist_free(certificate_to_uid_modules);
    certificate_to_uid_modules=NULL;

    for(c_module=nuauthdatas->modules;c_module;c_module=c_module->next) {
        free_module_t((module_t *)c_module->data);
    }
    g_slist_free(nuauthdatas->modules);
    nuauthdatas->modules=NULL;
    g_mutex_unlock(modules_mutex);
}

/**
 * \brief Test if this is initial start of nuauth
 *
 * \return TRUE if this is the initial start, FALSE if this is not the case
 */
gboolean nuauth_is_reloading()
{
  gboolean reloading=FALSE;
  g_mutex_lock(nuauthdatas->reload_cond_mutex);
  if (nuauthdatas->need_reload){
      reloading = TRUE;
  }
  g_mutex_unlock(nuauthdatas->reload_cond_mutex);
  return reloading;
}

/**
 * \brief Block till reload is over
 *
 */
void block_on_conf_reload()
{
    g_mutex_lock(nuauthdatas->reload_cond_mutex);
    if (nuauthdatas->need_reload){
        g_mutex_unlock(nuauthdatas->reload_cond_mutex);
        g_atomic_int_inc(&(nuauthdatas->locked_threads_number));
        while(nuauthdatas->need_reload){
            g_cond_wait (nuauthdatas->reload_cond, nuauthdatas->reload_cond_mutex);
        }
    }
    g_mutex_unlock(nuauthdatas->reload_cond_mutex);
}

/* @} */
