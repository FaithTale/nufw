/*
 ** Copyright(C) 2005,2006,2007,2008 INL
 ** Written by  Eric Leblond <regit@inl.fr>
 **             Pierre Chifflier <chifflier@inl.fr>
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
 *  - define_periods(): define period that can be used in time-based acls
 *  - user_check(): verify user credentials
 *  - get_user_groups(): found groups the user belong to
 *  - get_user_id(): get user id
 *  - acl_check(): verify acl for a packet
 *  - ip_authentication(): authenticate user packet by using external method
 *  - certificate_check(): check validity of user's certicate
 *  - certificate_to_uid(): build user ID from user's certicate
 *  - user_session_logs(): log user connection and disconnection
 *  - auth_error_log(): log failure of user authentication
 *  - user_session_modify(): modify user session just after authentication
 *  - user_packet_logs(): log packet
 *  - finalize_packet(): modify packet before sending answer to nufw
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

#include "nuauthconf.h"

/** This is a static variable to initiate all pointers to zero */
static hook_t hooks[MOD_END] = {
	{ "nuauth_user_check_module", NULL, NULL, "user_check", "user checking" },
	{ "nuauth_get_user_id_module", NULL, NULL, "get_user_id", "user id fetching" },
	{ "nuauth_get_user_groups_module", NULL, NULL, "get_user_groups", "user groups fetching" },
	{ "nuauth_auth_error_log_module", NULL, NULL, "auth_error_log", "auth error log" },
	{ "nuauth_acl_check_module", NULL, NULL, "acl_check", "acls checking" },
	{ "nuauth_user_session_modify_module", NULL, NULL,  "user_session_modify", "user session modify" },
	{ "nuauth_user_logs_module", NULL, NULL, "user_packet_logs", "user packet logging" },
	{ "nuauth_user_session_logs_module", NULL, NULL, "user_session_logs", "user session logging" },
	{ "nuauth_finalize_packet_module", NULL, NULL, "finalize_packet", "finalize packet" },
	{ "nuauth_periods_module", NULL, NULL, "define_periods", "define periods checking" },
	{ "nuauth_certificate_check_module", NULL, NULL,  "certificate_check", "certificate check" },
	{ "nuauth_certificate_to_uid_module", NULL, NULL, "certificate_to_uid", "certificate to uid" },
	{ "nuauth_postauth_proto_module", NULL, NULL, "postauth_proto", "post auth proto" },
	{ "nuauth_ip_authentication_module", NULL, NULL, "ip_authentication", "ip authentication" },
};

/**
 * Check a user/password against the list of modules used for user authentication
 *  It returns the decision using SASL defined return value.
 */
int modules_user_check(const char *user, const char *pass,
		       unsigned passlen, user_session_t *session)
{
	/* iter through module list and stop when user is found */
	GSList *walker = hooks[MOD_USER_CHECK].modules;
	int walker_return = 0;
	block_on_conf_reload();
	for (; walker != NULL; walker = walker->next) {
		walker_return =
		    (*(user_check_callback *)
		     (((module_t *) walker->data))->func) (user, pass,
							   passlen,
							   session,
							   ((module_t *)
							    walker->data)->
							   params);
		if (walker_return == SASL_OK)
			return SASL_OK;
	}
	return SASL_NOAUTHZ;
}

/**
 * Get group for a given user
 */
GSList *modules_get_user_groups(const char *user)
{
	/* iter through module list and stop when an acl is found */
	GSList *walker = hooks[MOD_USER_GROUPS].modules;
	GSList *walker_return = NULL;

	block_on_conf_reload();
	for (; walker != NULL; walker = walker->next) {
		walker_return =
		    (*(get_user_groups_callback *)
		     (((module_t *) walker->data))->func) (user,
							   ((module_t *)
							    walker->data)->
							   params);
		if (walker_return)
			return walker_return;
	}

	return NULL;

}

uint32_t modules_get_user_id(const char *user)
{
	/* iter through module list and stop when an acl is found */
	GSList *walker = hooks[MOD_USER_ID].modules;
	uint32_t walker_return = 0;

	block_on_conf_reload();
	for (; walker != NULL; walker = walker->next) {
		walker_return =
		    (*(get_user_id_callback *)
		     (((module_t *) walker->data))->func) (user,
							   ((module_t *)
							    walker->data)->
							   params);
		if (walker_return)
			return walker_return;
	}

	return 0;

}

/**
 * Check a connection and return a list of acl that match the information
 * contained in the connection.
 */
GSList *modules_acl_check(connection_t * element)
{
	/* iter through module list and stop when an acl is found */
	GSList *walker = hooks[MOD_ACL_CHECK].modules;
	GSList *walker_return = NULL;

	block_on_conf_reload();
	for (; walker != NULL; walker = walker->next) {
		walker_return =
		    (*(acl_check_callback *)
		     (((module_t *) walker->data))->func) (element,
							   ((module_t *)
							    walker->data)->
							   params);
		if (walker_return)
			return walker_return;
	}

	return NULL;
}

/* ip auth */
gchar *modules_ip_auth(auth_pckt_t * header)
{
	/* iter through module list and stop when decision is made */
	GSList *walker = hooks[MOD_IP_AUTH].modules;
	gchar *walker_return = NULL;

	block_on_conf_reload();
	for (; walker != NULL; walker = walker->next) {
		walker_return =
		    (*(ip_auth_callback *) (((module_t *) walker->data))->
		     func) (header, ((module_t *) walker->data)->params);
		if (walker_return)
			return walker_return;
	}
	return NULL;
}


/**
 * log authenticated packets
 */
nu_error_t modules_user_logs(void *element, tcp_state_t state)
{
	/* iter through all modules list */
	GSList *walker = hooks[MOD_LOG_PACKETS].modules;
	nu_error_t ret = NU_EXIT_OK;

	block_on_conf_reload();
	for (; walker != NULL; walker = walker->next) {
		user_logs_callback *handler =
		    (user_logs_callback *) ((module_t *) walker->data)->
		    func;
		if (handler(element, state,
			((module_t *) walker->data)->params) == -1) {
			ret = NU_EXIT_ERROR;
			/* A module has failed, this packet will be dropped if
			 * drop_if_no_logging is set */
			if (nuauthconf->drop_if_no_logging) {
				((connection_t *)element)->decision = DECISION_DROP;
				/* stop iterating over modules (nuauth is in DOS mode there) */
				return ret;
			}
		}
	}
	return ret;
}

/**
 * log user connection and disconnection
 */
int modules_user_session_logs(user_session_t * user, session_state_t state)
{
	/* iter through all modules list */
	GSList *walker = hooks[MOD_LOG_SESSION].modules;

	block_on_conf_reload();
	for (; walker != NULL; walker = walker->next) {
		user_session_logs_callback *handler =
		    (user_session_logs_callback *) ((module_t *) walker->
						    data)->func;
		handler(user, state, ((module_t *) walker->data)->params);
	}
	return 0;
}

/**
 * parse time period configuration for each module
 * and fille the given hash (first argument)
 */
void modules_parse_periods(GHashTable * periods)
{
	/* iter through all modules list */
	GSList *walker = hooks[MOD_PERIOD].modules;

	for (; walker != NULL; walker = walker->next) {
		define_period_callback *handler =
		    (define_period_callback
		     *) (((module_t *) walker->data)->func);
		handler(periods, ((module_t *) walker->data)->params);
	}
}

/**
 * Check certificate
 *
 * \param nussl NuSSL connection
 * \return SASL_OK if certificate is correct
 */
int modules_check_certificate(nussl_session* nussl)
{
	/* iter through all modules list */
	GSList *walker = hooks[MOD_CERT_CHECK].modules;
	int ret;

	log_message(VERBOSE_DEBUG, DEBUG_AREA_MAIN, "module check certificate");

	block_on_conf_reload();
	for (; walker != NULL; walker = walker->next) {
		certificate_check_callback *handler =
		    (certificate_check_callback *) ((module_t *) walker->
						    data)->func;
		ret = handler(nussl, ((module_t *) walker->data)->params);
		if (ret != SASL_OK) {
			return ret;
		}
	}
	return SASL_OK;
}

/**
 * certificate to uid
 *
 * \param nussl NuSSL connection
 * \return uid
 */
gchar *modules_certificate_to_uid(nussl_session* nussl)
{
	/* iter through all modules list */
	GSList *walker = hooks[MOD_CERT_TO_UID].modules;
	gchar *uid;

	block_on_conf_reload();
	for (; walker != NULL; walker = walker->next) {
		certificate_to_uid_callback *handler =
		    (certificate_to_uid_callback *) ((module_t *) walker->
						     data)->func;
		uid = handler(nussl, ((module_t *) walker->data)->params);
		if (uid) {
			return uid;
		}
	}
	return NULL;
}

/**
 * Modify user session
 *
 */
int modules_user_session_modify(user_session_t * c_session)
{
	/* iter through all modules list */
	GSList *walker = hooks[MOD_SESSION_MODIFY].modules;
	int ret;

	block_on_conf_reload();
	for (; walker != NULL; walker = walker->next) {
		user_session_modify_callback *handler =
		    (user_session_modify_callback
		     *) (((module_t *) walker->data)->func);
		ret = handler(c_session, ((module_t *) walker->data)->params);
		if (ret != SASL_OK) {
			return ret;
		}
	}

	return SASL_OK;
}

/**
 * Compute packet mark
 *
 */
nu_error_t modules_finalize_packet(connection_t * connection)
{
	/* iter through all modules list */
	GSList *walker = hooks[MOD_FINALIZE_PACKET].modules;

	block_on_conf_reload();
	for (; walker != NULL; walker = walker->next) {
		finalize_packet_callback *handler =
		    (finalize_packet_callback
		     *) (((module_t *) walker->data)->func);
		handler(connection, ((module_t *) walker->data)->params);
	}

	return NU_EXIT_OK;
}

/**
 * Log authentication error
 */
void modules_auth_error_log(user_session_t * session,
			    nuauth_auth_error_t error, const char *message)
{
	GSList *walker = hooks[MOD_USER_FAIL].modules;

	block_on_conf_reload();
	for (; walker != NULL; walker = walker->next) {
		auth_error_log_callback *handler =
		    (auth_error_log_callback
		     *) (((module_t *) walker->data)->func);
		handler(session, error, message,
			((module_t *) walker->data)->params);
	}
}


/**
 * custom modification of post authentication exchange
 */
int modules_postauth_proto(user_session_t * user)
{
	/* iter through all modules list */
	GSList *walker = hooks[MOD_POSTAUTH_PROTO].modules;
	int ret;

	block_on_conf_reload();
	for (; walker != NULL; walker = walker->next) {
		postauth_proto_callback *handler =
		    (postauth_proto_callback *) ((module_t *) walker->
						    data)->func;
		ret = handler(user, ((module_t *) walker->data)->params);
		if (ret != SASL_OK) {
			return ret;
		}
	}
	return SASL_OK;
}

void clean_module_t(module_t *module)
{
	if (module) {
		log_message(VERBOSE_DEBUG, DEBUG_AREA_MAIN,
				"Module %s cleaning",
				module->name);
		if (module->free_params) {
			module->free_params(module->params);
			module->params = NULL;
		}
	}
}

void free_module_t(module_t * module)
{
	if (module) {
#ifndef DEBUG_WITH_VALGRIND
		log_message(VERBOSE_DEBUG, DEBUG_AREA_MAIN,
				"Module %s closing", module->name);
		if (! g_module_close(module->module)) {
			log_message(VERBOSE_DEBUG, DEBUG_AREA_MAIN,
					"Module %s can't be closed", module->name);
		} else {
			log_message(VERBOSE_DEBUG, DEBUG_AREA_MAIN,
					"Module %s closed", module->name);
		}

#endif
		g_free(module->module_name);
		g_free(module->name);
		g_free(module->configfile);
	}
	g_free(module);
	module = NULL;
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
	modules_mutex = g_mutex_new();

	return 1;
}

/**
 * Check API version of a module: should be NUAUTH_API_VERSION.
 * Use the function 'get_api_version' of the module.
 *
 * \return Returns 0 if the function missing or the function is different,
 * and 1 otherwise.
 */
int check_module_version(GModule * module)
{
	get_module_version_func_t get_version;
	uint32_t api_version;

	/* get module function handler */
	if (!g_module_symbol
	    (module, "get_api_version", (gpointer *) & get_version)) {
		g_warning
		    ("Unable to load function 'get_api_version' from module %s",
		     g_module_name(module));
		exit(EXIT_FAILURE);
	}

	api_version = get_version();
	if (NUAUTH_API_VERSION != api_version) {
		g_warning
		    ("Not loading module %s: wrong API version (%u instead of %u)",
		     g_module_name(module), api_version,
		     NUAUTH_API_VERSION);
		exit(EXIT_FAILURE);
	}
	return 1;
}

/**
 * Load module for a task
 *
 * Please note that last args is a pointer of pointer
 */
static int load_modules_from(gchar * confvar, gchar * func,
			     GSList ** target, module_hook_t hook)
{
	gchar **modules_list;
	gchar *module_path;
	init_module_from_conf_t *initmod;
	gchar **params_list;
	module_t *current_module;
	int i;

	if (confvar == NULL)
		return 1;

	modules_list = g_strsplit(confvar, " ", 0);
	if (modules_list == NULL)
		return 1;

	for (i = 0; modules_list[i] != NULL; i++) {
		current_module = g_new0(module_t, 1);

		/* var format is NAME:MODULE:CONFFILE */
		params_list = g_strsplit(modules_list[i], ":", 3);
		current_module->name = g_strdup(params_list[0]);
		if (params_list[1]) {
			current_module->module_name =
			    g_strdup(params_list[1]);
			if (params_list[2]) {
				current_module->configfile =
				    g_strdup(params_list[2]);
			} else {
				/* we build config file name */
				current_module->configfile =
				    g_strjoin(NULL, CONFIG_DIR, "/",
					      MODULES_CONF_DIR, "/",
					      current_module->name,
					      MODULES_CONF_EXTENSION,
					      NULL);
			}
		} else {
			current_module->module_name =
			    g_strdup(current_module->name);
			current_module->configfile = NULL;
		}

		/* Open dynamic library */
		module_path =
		    g_module_build_path(MODULE_PATH,
					current_module->module_name);
		current_module->module = g_module_open(module_path, 0);
		g_free(module_path);

		log_message(VERBOSE_DEBUG, DEBUG_AREA_MAIN,
			    "\tmodule %s: using %s with configfile %s",
			    current_module->name,
			    current_module->module_name,
			    current_module->configfile);
		if (current_module->module == NULL) {
			log_message(FATAL, DEBUG_AREA_MAIN,
				    "Unable to load module %s in %s\nError: %s",
				    modules_list[i], MODULE_PATH,
				    g_module_error ());
			free_module_t(current_module);
			exit(EXIT_FAILURE);
		}

		/* check module version */
		if (!check_module_version(current_module->module)) {
			free_module_t(current_module);
			return 0;
		}

		/* get module function handler */
		if (!g_module_symbol
		    (current_module->module, func,
		     (gpointer *) & current_module->func)) {
			log_message(FATAL, DEBUG_AREA_MAIN,
				    "Unable to load function %s in %s", func,
				    g_module_name(current_module->module));
			free_module_t(current_module);
			g_strfreev(params_list);
			exit(EXIT_FAILURE);
		}

		current_module->hook = hook;

		/* get params for module by calling module exported function */
		if (g_module_symbol
		    (current_module->module, INIT_MODULE_FROM_CONF,
		     (gpointer *) & initmod)) {
			/* Initialize module */
			if (!initmod(current_module)) {
				log_message(WARNING, DEBUG_AREA_MAIN,
					    "Unable to init module \"%s\"",
					    g_module_name(current_module->module)
					   );
				current_module->params = NULL;
				return 0;
			}
		} else {
			log_message(WARNING, DEBUG_AREA_MAIN,
				    "No init function for module %s: PLEASE UPGRADE!",
				    current_module->module_name);
			current_module->params = NULL;
			return 0;
		}

		/* get params for module by calling module exported function */
		if (!g_module_symbol
		    (current_module->module, "unload_module_with_params",
		     (gpointer *) & (current_module->free_params))) {
			log_message(WARNING, DEBUG_AREA_MAIN,
				    "No unload function for module %s: PLEASE UPGRADE!",
				    current_module->module_name);
			current_module->free_params = NULL;
			return 0;
		}

		/* store module in module list */
		*target =
		    g_slist_append(*target, (gpointer) current_module);
		nuauthdatas->modules =
		    g_slist_prepend(nuauthdatas->modules, current_module);

		/* free memory */
		g_strfreev(params_list);
	}
	g_strfreev(modules_list);
	return 1;
}

static char *module_default_value(int type)
{
	switch (type) {
		case MOD_USER_CHECK:
		case MOD_USER_ID:
		case MOD_USER_GROUPS:
			return DEFAULT_USERAUTH_MODULE;
			break;
		case MOD_USER_FAIL:
		case MOD_POSTAUTH_PROTO:
			return "";
			break;
		case MOD_ACL_CHECK:
			return DEFAULT_ACLS_MODULE;
			break;
		case MOD_SESSION_MODIFY:
			return DEFAULT_USER_SESSION_MODIFY_MODULE;
			break;
		case MOD_LOG_PACKETS:
		case MOD_LOG_SESSION:
			return DEFAULT_LOGS_MODULE;
			break;
		case MOD_FINALIZE_PACKET:
			return DEFAULT_FINALIZE_PACKET_MODULE;
			break;
		case MOD_PERIOD:
			return DEFAULT_PERIODS_MODULE;
			break;
		case MOD_CERT_CHECK:
			return DEFAULT_CERTIFICATE_CHECK_MODULE;
			break;
		case MOD_CERT_TO_UID:
			return DEFAULT_CERTIFICATE_TO_UID_MODULE;
			break;
		case MOD_IP_AUTH:
			return DEFAULT_IPAUTH_MODULE;
			break;
		default:
			return NULL;
	}
}

/**
 * Load modules for user and acl checking as well as for user logging and ip authentication
 */
int load_modules()
{
	int i;

	hooks[MOD_USER_CHECK].config =
		nuauth_config_table_get_or_default(hooks[MOD_USER_CHECK].configstring, module_default_value(MOD_USER_CHECK));
	for (i = MOD_SIMPLE; i < MOD_OPTIONAL; i++) {
		hooks[i].config = nuauth_config_table_get_or_default(hooks[i].configstring, module_default_value(i));
	}

	if (nuauthconf->do_ip_authentication) {
		hooks[MOD_IP_AUTH].config =
			nuauth_config_table_get_or_default(hooks[MOD_IP_AUTH].configstring, module_default_value(MOD_IP_AUTH));
	}

	/* MOD_USER_CHECK is *always* set to something */
	hooks[MOD_USER_ID].config = nuauth_config_table_get_or_default(hooks[MOD_USER_ID].configstring, hooks[MOD_USER_CHECK].config);
	hooks[MOD_USER_GROUPS].config = nuauth_config_table_get_or_default(hooks[MOD_USER_GROUPS].configstring, hooks[MOD_USER_CHECK].config);

	/* external auth module loading */
	g_mutex_lock(modules_mutex);

#define LOAD_MODULE(HOOK) \
	log_message(VERBOSE_DEBUG, DEBUG_AREA_MAIN, "Loading %s modules:", hooks[HOOK].message); \
	if (!load_modules_from(hooks[HOOK].config, hooks[HOOK].funcstring, &(hooks[HOOK].modules), HOOK)) \
	{ \
		log_message(FATAL, DEBUG_AREA_MAIN, "Failed to load modules %s", hooks[HOOK].message); \
		return 0; \
	}


	/* loading modules */
	for (i = MOD_FIRST; i < MOD_OPTIONAL; i++) {
		LOAD_MODULE(i);
	}

	if (nuauthconf->do_ip_authentication) {
		LOAD_MODULE(MOD_IP_AUTH);
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
	unsigned int i;

	g_mutex_lock(modules_mutex);

	/* call cleaning function before free */
	for (c_module = nuauthdatas->modules; c_module;
	     c_module = c_module->next) {
		clean_module_t((module_t *) c_module->data);
	}
	for (c_module = nuauthdatas->modules; c_module;
	     c_module = c_module->next) {
		free_module_t((module_t *) c_module->data);
	}

	/* free nuauthdatas modules list */
	g_slist_free(nuauthdatas->modules);
	nuauthdatas->modules = NULL;

	/* free all lists */
	for(i = 0; i < (sizeof(hooks) / sizeof(*hooks)); ++i) {
		g_slist_free(hooks[i].modules);
		hooks[i].modules = NULL;
		g_free(hooks[i].config);
	}


	g_mutex_unlock(modules_mutex);
}

/**
 * \brief Test if this is initial start of nuauth
 *
 * \return TRUE if this is the initial start, FALSE if this is not the case
 */
gboolean nuauth_is_reloading()
{
	gboolean reloading = FALSE;
	if (nuauthdatas->is_starting == TRUE) {
		return FALSE;
	}
	g_mutex_lock(nuauthdatas->reload_cond_mutex);
	if (nuauthdatas->need_reload) {
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
	if (nuauthdatas->need_reload) {
		g_mutex_unlock(nuauthdatas->reload_cond_mutex);
		while (nuauthdatas->need_reload) {
			g_cond_wait(nuauthdatas->reload_cond,
				    nuauthdatas->reload_cond_mutex);
		}
	}
	g_mutex_unlock(nuauthdatas->reload_cond_mutex);
}

/**
 * \brief Register client capabilities (for plugin)
 */

nu_error_t register_client_capa(const char * name, unsigned int * index)
{
	int i;

	for (i = 0; i < 32; i++) {
		if (! capa_array[i]) {
			capa_array[i] = g_strdup(name);
			*index = i;
			return NU_EXIT_OK;
		}

	}

	return NU_EXIT_ERROR;
}

/**
 * \brief Unregister client capabilities (for plugin)
 */

nu_error_t unregister_client_capa(int index)
{
	g_free(capa_array[index]);
	capa_array[index] = NULL;

	return NU_EXIT_OK;
}

/*
 * protocol extension handling
 */

nu_error_t init_protocol_extension(struct nuauth_datas * ndatas)
{
	INIT_LLIST_HEAD(&(ndatas->ext_proto_l));
	return NU_EXIT_OK;
}

nu_error_t register_protocol_extension(struct nuauth_datas * ndatas, struct proto_ext_t *extproto)
{
	INIT_LLIST_HEAD(&(extproto->list));
	llist_add(&(ndatas->ext_proto_l), &(extproto->list));
	return NU_EXIT_OK;
}

nu_error_t unregister_protocol_extension(struct proto_ext_t *extproto)
{
	llist_del(&(extproto->list));
	return NU_EXIT_OK;
}

/* @} */
