/*
 ** Copyright(C) INL 2005
 ** Written by  Eric Leblond <regit@inl.fr>
 **             Vincent Deffontaines <gryzor@inl.fr>
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
 **
 */


#include <auth_srv.h>

#define INVALID_ENC_NAME "INVALID NAME"

/** \addtogroup NuauthCore
 * @{
 */

/*
 * Parse a string containing a list of addresses (separated by spaces).
 * Skip invalid addresses.
 *
 * \return Returns an array of in_addr, or NULL if no valid address has been found.
 * The array always finish with an INADDR_NONE value.
 */
struct in6_addr *generate_inaddr_list(gchar * gwsrv_addr)
{
	gchar **gwsrv_addr_list = NULL;
	gchar **iter = NULL;
	struct in6_addr *authorized_server = NULL;
	struct in6_addr *addrs_array = NULL;
	struct in6_addr addr6;
	struct in_addr addr4;
	unsigned int count = 0;

	if (gwsrv_addr == NULL)
		return NULL;

	/* parse nufw server address */
	gwsrv_addr_list = g_strsplit(gwsrv_addr, " ", 0);

	/* compute array length */
	for (iter = gwsrv_addr_list; *iter != NULL; iter++) {
		if (0 < inet_pton(AF_INET6, *iter, &addr6)
		    || 0 < inet_pton(AF_INET, *iter, &addr4)) {
			count++;
		}
	}

	/* allocate array of struct sock_addr */
	if (0 < count) {
		addrs_array = g_new0(struct in6_addr, count + 1);
		authorized_server = addrs_array;
		for (iter = gwsrv_addr_list; *iter != NULL; iter++) {
			if (0 < inet_pton(AF_INET6, *iter, &addr6)) {
				*authorized_server = addr6;
				authorized_server++;
			} else if (0 < inet_pton(AF_INET, *iter, &addr4)) {
				ipv4_to_ipv6(addr4, authorized_server);
				authorized_server++;
			}

		}
		*authorized_server = in6addr_any;
	}
	g_strfreev(gwsrv_addr_list);
	return addrs_array;
}


gboolean check_inaddr_in_array(struct in6_addr *check_ip,
			       struct in6_addr *iparray)
{
	struct in6_addr *ipitem;
	/* test if server is in the list of authorized servers */
	if (iparray) {
		ipitem = iparray;
		while (!ipv6_equal(ipitem, &in6addr_any)) {
			if (ipv6_equal(ipitem, check_ip))
				return TRUE;
			ipitem++;
		}
	}
	return FALSE;
}

gboolean check_string_in_array(gchar * checkstring, gchar ** stringarray)
{
	gchar **stringitem;
	/* test if server is in the list of authorized servers */
	if (stringarray) {
		stringitem = stringarray;
		while (*stringitem) {
			if (!strcmp(*stringitem, checkstring))
				return TRUE;
			stringitem++;
		}
	}
	return FALSE;

}

gchar *string_escape(const gchar * orig)
{
	gchar *traduc;
	/* convert from utf-8 to locale if needed */
	if (nuauthconf->uses_utf8) {
		size_t bwritten;
		traduc =
		    g_locale_from_utf8(orig, -1, NULL, &bwritten, NULL);
		if (!traduc) {
			log_message(DEBUG, DEBUG_AREA_PACKET | DEBUG_AREA_USER,
				    "UTF-8 conversion failed at %s:%d: %s",
				    __FILE__, __LINE__,
				    orig);
			return g_strdup(INVALID_ENC_NAME);
		}
	} else {
		traduc = g_strdup(orig);
	}

	return traduc;
}

static void print_group(gpointer group, gpointer userdata);
static void print_group_str(gpointer group, gpointer userdata);

gchar *str_print_group(user_session_t * usession)
{
	gchar *str_groups = NULL;
	if (usession->groups) {
		if (nuauthconf->use_groups_name) {
			g_slist_foreach(usession->groups, print_group_str,
					&str_groups);
		} else {
			g_slist_foreach(usession->groups, print_group,
					&str_groups);
		}
	}
	return str_groups;
}

static void print_group(gpointer group, gpointer userdata)
{
	char ** str_groups = (char **) userdata;
	char * userdata_p = *(char **) userdata;
	if (userdata_p) {
		*str_groups = g_strdup_printf("%s,%d",
				userdata_p,
				GPOINTER_TO_INT(group));
	} else {
		*str_groups = g_strdup_printf("%d",
				GPOINTER_TO_INT(group));
	}
	g_free(userdata_p);
}

static void print_group_str(gpointer group, gpointer userdata)
{
	char ** str_groups = (char **) userdata;
	char * userdata_p = *(char **) userdata;
	if (userdata_p) {
		*str_groups = g_strdup_printf("%s,%s",
				userdata_p,
				(char *)group);
	} else {
		*str_groups = g_strdup_printf("%s", (char *)group);
	}
	g_free(userdata_p);
}

/** @} */
