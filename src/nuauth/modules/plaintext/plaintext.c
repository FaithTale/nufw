/*
 **  "plaintext" module
 ** Copyright(C) 2004-2005 Mikael Berthe <mikael+nufw@lists.lilotux.net>
 ** Copyright(C) 2005-2008 INL
 ** Copyright(C) 2010 EdenWall Technologies
 **
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

#include "auth_srv.h"
#include <string.h>
#include "auth_plaintext.h"

#include "nuauthconf.h"

/**
 *
 * \ingroup AuthNuauthModules
 * \defgroup PlaintextModule Plaintext authentication and acl module
 *
 * @{ */


/**
 * Returns version of nuauth API
 */
G_MODULE_EXPORT uint32_t get_api_version()
{
	return NUAUTH_API_VERSION;
}


/**
 * strip_line()
 * Returns a pointer on stripped line or
 * NULL if the line should be skipped and acceptnull is true.
 */
static char *strip_line(char *line, int acceptnull)
{
	char *p_tmp;

	/*  Let's get rid of tabs and spaces */
	while ((*line == ' ') || (*line == '\t'))
		line++;
	/*  Let's get rid of trailing characters */
	for (p_tmp = line; *p_tmp; p_tmp++);
	if (p_tmp != line)
		p_tmp--;
	for (; p_tmp > line && (*p_tmp == '\n' || *p_tmp == '\r' ||
				*p_tmp == ' ' || *p_tmp == '\t');
	     *p_tmp-- = 0);

	if (!acceptnull)
		return line;

	/*  Discard comments and empty lines */
	if (*line == '#' || *line == 0 || *line == '\r' || *line == '\n')
		return NULL;

	return line;
}

/**
 * parse_strings()
 * Extracts integers (like group ids) in intline and fills *p_intlist.
 * prefix is displayed in front of the log messages.
 * Returns 0 if successful.
 */
static int parse_strings(char *stringline, GSList ** p_stringlist, char *prefix)
{
	GSList *stringlist = *p_stringlist;
	gchar **groups_list;
	gchar **groups_list_item;

	/*  parsing strings */
	groups_list = g_strsplit(stringline, ",", 0);
	if (groups_list == NULL) {
		log_message(FATAL, DEBUG_AREA_MAIN,
				"%s parse_stringlist: Malformed line",
				prefix);
		*p_stringlist = stringlist;
		return 1;
	}
	groups_list_item = groups_list;
	for (groups_list_item = groups_list;
			groups_list_item != NULL && *groups_list_item != NULL;
			groups_list_item++) {
		stringlist = g_slist_append(stringlist,
				g_strdup(*groups_list_item));
		debug_log_message(VERBOSE_DEBUG, DEBUG_AREA_MAIN,
				"adding string: \"%s\"",
				*groups_list_item);
	}
	g_strfreev(groups_list);

	*p_stringlist = stringlist;
	return 0;
}

/**
 * parse_ints()
 * Extracts integers (like group ids) in intline and fills *p_intlist.
 * prefix is displayed in front of the log messages.
 * Returns 0 if successful.
 */
static int parse_ints(char *intline, GSList ** p_intlist, char *prefix)
{
	char *p_nextint;
	char *p_ints = intline;
	GSList *intlist = *p_intlist;
	int number;

	/*  parsing ints */
	while (p_ints) {
		p_nextint = strchr(p_ints, ',');
		if (p_nextint) {
			*p_nextint = 0;
		}
		if (sscanf(p_ints, "%u", &number) != 1) {
			/*  We can't read a number.  This will be an error only if we can */
			/*   see a comma next. */
			if (p_nextint) {
				log_message(FATAL, DEBUG_AREA_MAIN,
					    "%s parse_ints: Malformed line",
					    prefix);
				*p_intlist = intlist;
				return 1;
			}
			log_message(WARNING, DEBUG_AREA_MAIN,
				    "%s parse_ints: Garbarge at end of line",
				    prefix);
		} else {
			/*  One number (group, integer...) to add */
			intlist = g_slist_prepend(intlist,
						  GUINT_TO_POINTER((u_int32_t) number));
			debug_log_message(VERBOSE_DEBUG, DEBUG_AREA_MAIN,
					  "%s Added group/int %d", prefix,
					  number);
		}
		if ((p_ints = p_nextint))
			p_ints++;
	}

	*p_intlist = intlist;
	return 0;
}

/**
 * parse_ports()
 * Extracts ports from groupline and fills *p_portslist.
 * prefix is displayed in front of the log messages.
 * Returns 0 if successful.
 */
static int parse_ports(char *portsline, GSList ** p_portslist,
		       char *prefix)
{
	char *p_nextports;
	char *p_ports = portsline;
	GSList *portslist = *p_portslist;
	struct plaintext_ports ports;
	int n, fport, lastport;

	/*  parsing ports */
	while (p_ports) {
		p_nextports = strchr(p_ports, ',');
		if (p_nextports) {
			*p_nextports = 0;
		}
		n = sscanf(p_ports, "%d-%d", &fport, &lastport);
		ports.firstport = (uint16_t) fport;
		if ((n != 1) && (n != 2)) {
			/*  We can't read a port number.  This will be an error only if we can */
			/*   see a comma next. */
			if (p_nextports) {
				log_message(FATAL, DEBUG_AREA_MAIN,
					    "%s parse_ports: Malformed line",
					    prefix);
				*p_portslist = portslist;
				return 1;
			}
			log_message(WARNING, DEBUG_AREA_MAIN,
				    "%s parse_ports: Garbarge at end of line",
				    prefix);
		} else {
			struct plaintext_ports *this_port;
			/*  One port or ports range to add... */
			if (n == 2) {	/*  That's a range */
				if (lastport >= fport) {
					ports.nbports = lastport - fport;
				} else {
					ports.nbports = -1;
					log_message(WARNING, DEBUG_AREA_MAIN,
						    "%s parse_ports: Malformed line",
						    prefix);
				}
			} else
				ports.nbports = 0;

			if (ports.nbports >= 0) {
				this_port = g_new0(struct plaintext_ports, 1);
				this_port->firstport = ports.firstport;
				this_port->nbports = ports.nbports;
				portslist =
				    g_slist_prepend(portslist, this_port);
				debug_log_message(VERBOSE_DEBUG, DEBUG_AREA_MAIN,
						  "%s Adding Port = %d, number = %d",
						  prefix, ports.firstport,
						  ports.nbports);
			}
		}
		if ((p_ports = p_nextports))
			p_ports++;
	}

	*p_portslist = portslist;
	return 0;
}

/**
 * Try to match an address from an IP/mask list.
 *
 * \param ip_list Single linked list of plaintext_ip items
 * \param addr Address to match
 * \return 1 if addr match ip_list, 0 otherwise
 */
static int match_ip(GSList * ip_list, struct in6_addr *addr)
{
	if (!ip_list)
		return 1;
	for (; ip_list != NULL; ip_list = g_slist_next(ip_list)) {
		struct plaintext_ip *item = (struct plaintext_ip *) ip_list->data;
		if (compare_ipv6_with_mask
		    (&item->addr, addr, &item->netmask) == 0)
			return 1;
	}
	return 0;
}

/**
 * parse_ips()
 * Extracts IP addresses from ipsline and fills *ipslist.
 * prefix is displayed in front of the log messages.
 * Returns 0 if successful.
 */
static int parse_ips(char *ipsline, GSList ** ip_list, char *prefix)
{
	char addr_ascii[INET6_ADDRSTRLEN];
	char mask_ascii[INET6_ADDRSTRLEN];
	struct in_addr ip_addr4;
	struct in6_addr ip_addr6;
	char *p_tmp;
	gchar **ip_items = g_strsplit(ipsline, ",", 0);
	gchar **iter = ip_items;
	gchar *line;
	struct plaintext_ip this_ip, *this_ip_copy;
	int result = 0;

	/*  parsing IPs */
	for (iter = ip_items; iter != NULL && *iter != NULL; iter++) {
		int32_t mask = 0;
		int n;

		line = strip_line(*iter, FALSE);

		/*  Is there a netmask? */
		p_tmp = strchr(line, '/');
		if (p_tmp != NULL) {
			*p_tmp++ = 0;
			n = sscanf(p_tmp, "%u", &mask);
			if (n != 1) {
				log_message(FATAL, DEBUG_AREA_MAIN,
					    "plaintext warning: wrong network mask (%s)",
					    p_tmp);
				result = 1;
				break;
			}
		} else {	/*  no -> default netmask is 32 bits */
			mask = 128;
		}

		if (0 < inet_pton(AF_INET, line, &ip_addr4)) {
			ipv4_to_ipv6(ip_addr4, &this_ip.addr);
			mask += (128 - 32);
		} else if (0 < inet_pton(AF_INET6, line, &ip_addr6)) {
			this_ip.addr = ip_addr6;
		} else {
			/*  We can't read an IP address.  This will be an error only if we can */
			/*   see a comma next. */
			log_message(FATAL, DEBUG_AREA_MAIN,
				    "%s parse_ips: Unable to parse IP address: %s",
				    prefix, line);
			result = 1;
			break;
		}

		/*  Create netmask IPv6 address from netmask in bits */
		create_ipv6_netmask(&this_ip.netmask, mask);

		if (compare_ipv6_with_mask
		    (&this_ip.addr, &this_ip.addr,
		     &this_ip.netmask) != 0) {
			format_ipv6(&this_ip.addr, addr_ascii, INET6_ADDRSTRLEN, NULL);
			format_ipv6(&this_ip.netmask, mask_ascii, INET6_ADDRSTRLEN, NULL);
			log_message(FATAL, DEBUG_AREA_MAIN,
				    "%s parse_ips: Invalid network specification: IP=%s, netmask=%s!",
				    prefix, addr_ascii, mask_ascii);
			result = 1;
			break;
		}

		this_ip_copy = g_memdup(&this_ip, sizeof(this_ip));
		*ip_list = g_slist_prepend(*ip_list, this_ip_copy);

#ifdef DEBUG_ENABLE
		format_ipv6(&this_ip_copy->addr, addr_ascii, INET6_ADDRSTRLEN, NULL);
		format_ipv6(&this_ip_copy->netmask, mask_ascii, INET6_ADDRSTRLEN, NULL);
		log_message(VERBOSE_DEBUG, DEBUG_AREA_MAIN,
				"%s Adding IP = %s, netmask = %s",
				prefix, addr_ascii,
				mask_ascii);
#endif
	}
	g_strfreev(ip_items);
	return result;
}

/**
 * read_user_list()
 * Reads users conf file and fills the *plaintext_userlist structure.
 * Returns 0 if successful.
 * Line format: "username:passwd:gid1,gid2,gid3" (gid are numbers)
 */
static int read_user_list(struct plaintext_params *params)
{
	struct plaintext_user *plaintext_user;
	FILE *fd;
	char line[1024];
	char *p_username, *p_passwd, *p_uid, *p_groups;
	u_int32_t uid;
	char log_prefix[16];
	int ln = 0;		/*  Line number */

	log_message(VERBOSE_DEBUG, DEBUG_AREA_AUTH,
		    "[plaintext] read_user_list: reading [%s]",
		    params->plaintext_userfile);

	fd = fopen(params->plaintext_userfile, "r");

	if (!fd) {
		log_message(WARNING, DEBUG_AREA_AUTH,
			    "read_user_list: fopen error");
		return 1;
	}

	while (fgets(line, sizeof(line), fd) != NULL) {
		ln++;
		p_username = strip_line(line, TRUE);

		if (!p_username)
			continue;

		/*  User Name */
		if (!p_username) {
			log_message(WARNING, DEBUG_AREA_AUTH,
				    "L.%d: read_user_list: Malformed line (no username)",
				    ln);
			fclose(fd);
			return 2;
		}

		/*  Password */
		p_passwd = strchr(p_username, ':');
		if (!p_passwd) {
			log_message(WARNING, DEBUG_AREA_AUTH,
				    "L.%d: read_user_list: Malformed line (no passwd)",
				    ln);
			fclose(fd);
			return 2;
		}
		*p_passwd++ = 0;

		/*  UID */
		p_uid = strchr(p_passwd, ':');
		if (!p_uid) {
			log_message(WARNING, DEBUG_AREA_AUTH,
				    "L.%d: read_user_list: Malformed line (no uid)",
				    ln);
			fclose(fd);
			return 2;
		}
		*p_uid++ = 0;
		if (sscanf(p_uid, "%d", &uid) != 1) {
			log_message(WARNING, DEBUG_AREA_AUTH,
				    "L.%d: read_user_list: Malformed line "
				    "(uid should be a number)", ln);
			fclose(fd);
			return 2;
		}

		/*  List of groups */
		p_groups = strchr(p_uid, ':');
		if (!p_groups) {
			log_message(WARNING, DEBUG_AREA_AUTH,
				    "L.%d: read_user_list: Malformed line (no groups)",
				    ln);
			fclose(fd);
			return 2;
		}
		*p_groups++ = 0;

		debug_log_message(VERBOSE_DEBUG, DEBUG_AREA_AUTH,
				  "L.%d: Read username=[%s], uid=%d",
				  ln, p_username, uid);

		/*  Let's create an user node */
		plaintext_user = g_new0(struct plaintext_user, 1);
		if (!plaintext_user) {
			log_message(WARNING, DEBUG_AREA_AUTH,
				    "read_user_list: Cannot allocate plaintext_user!");
			fclose(fd);
			return 5;
		}
		plaintext_user->groups = NULL;
		plaintext_user->passwd = g_strdup(p_passwd);
		plaintext_user->username = g_strdup(p_username);
		plaintext_user->uid = uid;

		snprintf(log_prefix, sizeof(log_prefix) - 1, "L.%d: ", ln);
		/*  parsing groups */
		if (parse_ints
		    (p_groups, &plaintext_user->groups, log_prefix)) {
			g_free(plaintext_user);
			fclose(fd);
			return 2;
		}

		/*  User node is ready */
		params->plaintext_userlist =
		    g_slist_prepend(params->plaintext_userlist,
				    plaintext_user);
	}

	fclose(fd);

	return 0;
}

/**
 * read_acl_list()
 * Reads acls conf file and fills the *plaintext_acllist structure.
 * Returns 0 if successful.
 *
 * ACL begins with "[ACL name]", then each line should have the structure
 * "key = value".  For example "proto = 6".
 */
static int read_acl_list(struct plaintext_params *params)
{
	FILE *fd;
	char line[1024];
	char *p_key, *p_value, *p_tmp;
	struct plaintext_acl *newacl = NULL;
	int ln = 0;		/*  Line number */

	log_message(VERBOSE_DEBUG, DEBUG_AREA_MAIN,
		    "[plaintext] read_acl_list: reading [%s]",
		    params->plaintext_aclfile);

	fd = fopen(params->plaintext_aclfile, "r");

	if (!fd) {
		log_message(FATAL, DEBUG_AREA_MAIN,
			    "read_acl_list: fopen error");
		return 1;
	}

	while (fgets(line, 1000, fd)) {
		ln++;
		p_key = strip_line(line, TRUE);

		if (!p_key)
			continue;

		/*  New ACL? */
		if (p_key[0] == '[') {
			if (newacl) {
				debug_log_message(VERBOSE_DEBUG, DEBUG_AREA_MAIN,
						  "Done with ACL [%s]",
						  newacl->aclname);
				/*  check if ACL node has minimal information */
				/*  Warning: this code is duplicated after the loop */
				if (!(newacl->groups || newacl->users)) {
					log_message(WARNING, DEBUG_AREA_MAIN,
						    "L.%d: No user or group(s) declared in ACL %s",
						    ln, newacl->aclname);
					fclose(fd);
					return 2;
				} else if (newacl->proto == IPPROTO_TCP ||
					   newacl->proto == IPPROTO_UDP ||
					   newacl->proto == IPPROTO_ICMP) {
					/*  ACL node is ready */
					params->plaintext_acllist =
					    g_slist_append(params->
							    plaintext_acllist,
							    newacl);
				} else {
					log_message(WARNING, DEBUG_AREA_MAIN,
						    "No valid protocol declared in ACL %s",
						    newacl->aclname);
				}
			}

			debug_log_message(VERBOSE_DEBUG, DEBUG_AREA_MAIN,
					  "L.%d: New ACL", ln);

			p_tmp = strchr(++p_key, ']');
			if (!p_tmp) {
				log_message(FATAL, DEBUG_AREA_MAIN,
					    "L.%d: Malformed line (ACLname)",
					    ln);
				fclose(fd);
				return 2;
			}
			*p_tmp = 0;
			/*  Ok, new ACL declaration here.  Let's allocate a structure! */
			newacl = g_new0(struct plaintext_acl, 1);
			if (!newacl) {
				log_message(FATAL, DEBUG_AREA_MAIN,
					    "read_acl_list: Cannot allocate plaintext_acl!");
				fclose(fd);
				return 5;
			}

			newacl->aclname = g_strdup(p_key);
			newacl->proto = IPPROTO_TCP;
			newacl->period = NULL;
			newacl->log_prefix = NULL;
			newacl->flags = ACL_FLAGS_NONE;
			newacl->auth_quality = 0;
			newacl->decision = DECISION_ACCEPT;
			debug_log_message(VERBOSE_DEBUG, DEBUG_AREA_MAIN,
					  "L.%d: ACL name found: [%s]", ln,
					  newacl->aclname);
			/*  We're done with this line */
			continue;
		}

		/*  We shouldn't be here if we aren't in an ACL declaration */
		if (!newacl) {
			log_message(FATAL, DEBUG_AREA_MAIN,
				    "L.%d: Malformed line (Not in an ACL declaration)",
				    ln);
			fclose(fd);
			return 2;
		}

		p_value = strchr(p_key, '=');
		if (!p_value) {
			log_message(FATAL, DEBUG_AREA_MAIN,
				    "L.%d: Malformed line (No '=' inside)",
				    ln);
			fclose(fd);
			return 2;
		}
		*p_value++ = 0;

		p_key = strip_line(p_key, FALSE);
		p_value = strip_line(p_value, FALSE);

		/*  Ok.  Let's study the key/value we've found, now. */
		if (!strcasecmp("decision", p_key)) {	/*  Decision */
			unsigned int decis = atoi(p_value);

			switch (decis) {
			case DECISION_ACCEPT:
				newacl->decision = DECISION_ACCEPT;
				break;
			case DECISION_DROP:
				newacl->decision = DECISION_DROP;
				break;
			case DECISION_REJECT:
				newacl->decision = DECISION_REJECT;
				break;
			default:
				{
					log_message(FATAL, DEBUG_AREA_MAIN,
						    "L.%d: Malformed line, decision should be 0 (DROP) or 1 (ACCEPT) or 3 (REJECT)",
						    ln);
					fclose(fd);
					return 2;
				}
			}
			debug_log_message(VERBOSE_DEBUG, DEBUG_AREA_MAIN,
					  "L.%d: Read decision = %d", ln,
					  newacl->decision);
		} else if (!strcasecmp("gid", p_key)) {	/*  Groups */
			char log_prefix[16];
			snprintf(log_prefix, sizeof(log_prefix) - 1,
				 "L.%d: ", ln);
			if (nuauthconf->use_groups_name) {
				/*  parsing groups */
				if (parse_strings
						(p_value, &newacl->groups, log_prefix)) {
					fclose(fd);
					return 2;
				}
			} else {
				/*  parsing groups */
				if (parse_ints
						(p_value, &newacl->groups, log_prefix)) {
					fclose(fd);
					return 2;
				}
			}
		} else if (!strcasecmp("uid", p_key)) {	/*  Users */
			char log_prefix[16];
			snprintf(log_prefix, sizeof(log_prefix) - 1,
				 "L.%d: ", ln);
			/*  parsing groups */
			if (parse_ints
			    (p_value, &newacl->users, log_prefix)) {
				fclose(fd);
				return 2;
			}
		} else if (!strcasecmp("proto", p_key)) {	/*  Protocol */
			if (sscanf(p_value, "%d", &newacl->proto) != 1) {
				log_message(FATAL, DEBUG_AREA_MAIN,
					    "L.%d: Malformed line (proto should be a number)",
					    ln);
				fclose(fd);
				return 2;
			}
			if (newacl->proto != IPPROTO_TCP && newacl->proto != IPPROTO_UDP
					&& newacl->proto != IPPROTO_ICMP) {
				log_message(FATAL, DEBUG_AREA_MAIN,
					    "L.%d: Unsupported protocol: %d",
					    ln, newacl->proto);
				fclose(fd);
				return 2;
			}

			debug_log_message(VERBOSE_DEBUG, DEBUG_AREA_MAIN,
					  "L.%d: Read proto = %d", ln,
					  newacl->proto);
		} else if (!strcasecmp("type", p_key)) {	/*  Type (icmp) */
			char log_prefix[16];
			snprintf(log_prefix, sizeof(log_prefix) - 1,
				 "L.%d: type ", ln);
			/*  parse type values */
			if (parse_ints
			    (p_value, &newacl->types, log_prefix)) {
				fclose(fd);
				return 2;
			}
		} else if (!strcasecmp("srcip", p_key)) {	/*  SrcIP */
			char log_prefix[16];
			snprintf(log_prefix, sizeof(log_prefix) - 1,
				 "L.%d: ", ln);
			/*  parsing IPs */
			if (parse_ips
			    (p_value, &newacl->src_ip, log_prefix)) {
				fclose(fd);
				return 2;
			}
		} else if (!strcasecmp("srcport", p_key)) {	/*  SrcPort */
			char log_prefix[16];
			snprintf(log_prefix, sizeof(log_prefix) - 1,
				 "L.%d: ", ln);
			/*  parsing ports */
			if (parse_ports
			    (p_value, &newacl->src_ports, log_prefix)) {
				fclose(fd);
				return 2;
			}
		} else if (!strcasecmp("dstip", p_key)) {	/*  DstIP */
			char log_prefix[16];
			snprintf(log_prefix, sizeof(log_prefix) - 1,
				 "L.%d: ", ln);
			/*  parsing IPs */
			if (parse_ips
			    (p_value, &newacl->dst_ip, log_prefix)) {
				fclose(fd);
				return 2;
			}
		} else if (!strcasecmp("dstport", p_key)) {	/*  DstPort */
			char log_prefix[16];
			snprintf(log_prefix, sizeof(log_prefix) - 1,
				 "L.%d: ", ln);
			/*  parsing ports */
			if (parse_ports
			    (p_value, &newacl->dst_ports, log_prefix)) {
				fclose(fd);
				return 2;
			}
		} else if (!strcasecmp("app", p_key)) {	/*  App */
			char *sep;
			struct plaintext_app *newapp = g_new0(struct plaintext_app, 1);

			sep = strchr(p_value, ';');
			if (sep)
				*sep++ = 0;
			newapp->appname = g_strdup(strip_line(p_value, 0));
			debug_log_message(VERBOSE_DEBUG, DEBUG_AREA_MAIN,
					  "L.%d: Read App name [%s]", ln,
					  newapp->appname);

			newacl->apps = g_slist_prepend(newacl->apps, newapp);
		} else if (!strcasecmp("os", p_key)) {	/*  OS */
			char *sep;
			struct plaintext_os *newos = g_new0(struct plaintext_os, 1);

			sep = strchr(p_value, ';');
			if (sep)
				*sep++ = 0;
			newos->sysname = g_strdup(strip_line(p_value, 0));
			debug_log_message(VERBOSE_DEBUG, DEBUG_AREA_MAIN,
					  "L.%d: Read OS sysname [%s]", ln,
					  newos->sysname);

			/*  Release: */
			if (sep) {
				p_value = sep;
				sep = strchr(p_value, ';');
				if (sep)
					*sep++ = 0;
				newos->release =
				    g_strdup(strip_line(p_value, 0));
				debug_log_message(VERBOSE_DEBUG, DEBUG_AREA_MAIN,
						  "L.%d: Read OS release [%s]",
						  ln, newos->release);
			}
			/*  Version: */
			if (sep) {
				p_value = sep;
				newos->version =
				    g_strdup(strip_line(p_value, 0));
				debug_log_message(VERBOSE_DEBUG, DEBUG_AREA_MAIN,
						  "L.%d: Read OS version [%s]",
						  ln, newos->version);
			}

			newacl->os = g_slist_prepend(newacl->os, newos);
		} else if (!strcasecmp("period", p_key)) {	/*  Period */
			newacl->period = g_strdup(p_value);
			debug_log_message(VERBOSE_DEBUG, DEBUG_AREA_MAIN,
					  "L.%d: Read  period [%s]", ln,
					  newacl->period);
		} else if (!strcasecmp("indev", p_key)) {	/*  input dev */
			memcpy(newacl->iface_nfo.indev, p_value, IFNAMSIZ);
			debug_log_message(VERBOSE_DEBUG, DEBUG_AREA_MAIN,
					"L.%d: Read  indev [%s]", ln,
					newacl->iface_nfo.indev);
		} else if (!strcasecmp("physindev", p_key)) {	/*  phys input dev */
			memcpy(newacl->iface_nfo.physindev, p_value, IFNAMSIZ);
			debug_log_message(VERBOSE_DEBUG, DEBUG_AREA_MAIN,
					"L.%d: Read  physindev [%s]", ln,
					newacl->iface_nfo.physindev);
		} else if (!strcasecmp("outdev", p_key)) {	/*  output dev */
			memcpy(newacl->iface_nfo.outdev, p_value, IFNAMSIZ);
			debug_log_message(VERBOSE_DEBUG, DEBUG_AREA_MAIN,
					"L.%d: Read  indev [%s]", ln,
					newacl->iface_nfo.outdev);
		} else if (!strcasecmp("physoutdev", p_key)) {	/*  phys output dev */
			memcpy(newacl->iface_nfo.physoutdev, p_value, IFNAMSIZ);
			debug_log_message(VERBOSE_DEBUG, DEBUG_AREA_MAIN,
					"L.%d: Read  physoutdev [%s]", ln,
					newacl->iface_nfo.physoutdev);
		} else if (!strcasecmp("log_prefix", p_key)) {
			newacl->log_prefix = g_strdup(p_value);
			debug_log_message(VERBOSE_DEBUG, DEBUG_AREA_MAIN,
					  "L.%d: Read log_prefix [%s]", ln,
					  newacl->log_prefix);
		} else if (!strcasecmp("flags", p_key)) {
			if (sscanf(p_value, "%d", &newacl->flags) != 1) {
				log_message(FATAL, DEBUG_AREA_MAIN,
						"L.%d: Malformed line (flags should be a number)",
						ln);
				fclose(fd);
				return 2;
			}
			debug_log_message(VERBOSE_DEBUG, DEBUG_AREA_MAIN,
					"L.%d: Read acl flags [%d]", ln, newacl->flags);
		} else if (!strcasecmp("authquality", p_key)) {
			if (sscanf(p_value, "%d", &newacl->auth_quality) != 1) {
				log_message(FATAL, DEBUG_AREA_MAIN,
						"L.%d: Malformed line (flags should be a number)",
						ln);
				fclose(fd);
				return 2;
			}
			debug_log_message(VERBOSE_DEBUG, DEBUG_AREA_MAIN,
					"L.%d: Read acl authquality [%d]", ln, newacl->auth_quality);
		} else {
			log_message(SERIOUS_WARNING, DEBUG_AREA_MAIN,
				    "L.%d: Unknown key [%s] in ACL %s",
				    ln, p_key, newacl->aclname);
		}		/*  End of key/value parsing */
	}
	if (newacl) {

		debug_log_message(VERBOSE_DEBUG, DEBUG_AREA_MAIN,
				  "Done with ACL [%s]", newacl->aclname);
		/*  check if ACL node has minimal information */
		/*  Warning: this code is duplicated after the loop */
		if (!(newacl->groups || newacl->users)) {
			log_message(WARNING, DEBUG_AREA_MAIN,
				    "No user or group(s) declared in ACL %s",
				    newacl->aclname);
		} else if (newacl->proto == IPPROTO_TCP
			   || newacl->proto == IPPROTO_UDP
			   || newacl->proto == IPPROTO_ICMP) {
			/*  ACL node is ready */
			params->plaintext_acllist =
			    g_slist_append(params->plaintext_acllist,
					    newacl);
		} else {
			log_message(WARNING, DEBUG_AREA_MAIN,
				    "No valid protocol declared in ACL %s",
				    newacl->aclname);
		}
	}

	fclose(fd);
	return 0;
}

G_MODULE_EXPORT gboolean unload_module_with_params(struct plaintext_params
						   * params)
{
	if (!params) {
		return TRUE;
	}

	if (params->plaintext_userlist) {
		GSList *p_userlist;
		struct plaintext_user *p_user;

		debug_log_message(VERBOSE_DEBUG, DEBUG_AREA_MAIN,
				  "Freeing users list");

		/*  Let's free each node separately */
		for (p_userlist = params->plaintext_userlist; p_userlist;
		     p_userlist = g_slist_next(p_userlist)) {
			p_user =
			    (struct plaintext_user *) p_userlist->data;
			g_free(p_user->passwd);
			g_free(p_user->username);
			if (p_user->groups)
				g_slist_free(p_user->groups);
		}
		/*  Now we can free the list */
		g_slist_free(params->plaintext_userlist);
	}

	/*  Free acl list */
	if (params->plaintext_acllist) {
		GSList *p_acllist;
		GSList *p_app;
		GSList *p_os;
		GSList *p_ip;
		struct plaintext_acl *p_acl;

		debug_log_message(VERBOSE_DEBUG, DEBUG_AREA_MAIN,
				  "Freeing ACLs");

		/*  Let's free each node separately */
		for (p_acllist = params->plaintext_acllist; p_acllist;
		     p_acllist = g_slist_next(p_acllist)) {
			p_acl = (struct plaintext_acl *) p_acllist->data;

			/*  Let's free app attributes */
			for (p_app = p_acl->apps; p_app != NULL;
			     p_app = g_slist_next(p_app)) {
				struct plaintext_app *app = p_app->data;
				g_free(app->appname);
				g_free(app);
			}
			/*  Free OS attributes */
			for (p_os = p_acl->os; p_os != NULL;
			     p_os = g_slist_next(p_os)) {
				struct plaintext_os *os = p_os->data;
				g_free(os->version);
				g_free(os->release);
				g_free(os->sysname);
				g_free(os);
			}
			/*  Free IPs */
			p_ip = p_acl->src_ip;
			for (; p_ip != NULL; p_ip = g_slist_next(p_ip)) {
				g_free(p_ip->data);
			}
			p_ip = p_acl->dst_ip;
			for (; p_ip != NULL; p_ip = g_slist_next(p_ip)) {
				g_free(p_ip->data);
			}
			g_slist_free(p_acl->apps);
			g_slist_free(p_acl->os);
			g_slist_free(p_acl->types);
			g_slist_free(p_acl->src_ip);
			g_slist_free(p_acl->dst_ip);
			g_slist_foreach(p_acl->src_ports, (GFunc) g_free,
					NULL);
			g_slist_free(p_acl->src_ports);
			g_slist_foreach(p_acl->dst_ports, (GFunc) g_free,
					NULL);
			g_slist_free(p_acl->dst_ports);
			g_slist_free(p_acl->users);
			if (nuauthconf->use_groups_name) {
				g_slist_foreach(p_acl->groups, (GFunc) g_free, NULL);
			}
			g_slist_free(p_acl->groups);
			g_free(p_acl->aclname);
			g_free(p_acl->period);
			g_free(p_acl);
		}
		/*  Now we can free the list */
		g_slist_free(params->plaintext_acllist);
	}
	g_free(params->plaintext_userfile);
	g_free(params->plaintext_aclfile);
	g_free(params);
	return TRUE;
}

G_MODULE_EXPORT gboolean init_module_from_conf(module_t * module)
{
	struct plaintext_params *params =
	    g_new0(struct plaintext_params, 1);

	log_message(VERBOSE_DEBUG, DEBUG_AREA_MAIN,
		    "Plaintext module ($Revision$)");

	/*  set variables */
	params->plaintext_userfile = nuauth_config_table_get_or_default("plaintext_userfile", TEXplaintext_USERFILE);
	params->plaintext_aclfile = nuauth_config_table_get_or_default("plaintext_aclfile", TEXplaintext_ACLFILE);
	params->plaintext_userlist = NULL;
	params->plaintext_acllist = NULL;

	module->params = (gpointer) params;

	/*  Depending on the use of the module load user list or acl list */
	if (module->hook == MOD_USER_CHECK || module->hook == MOD_USER_ID || module->hook == MOD_USER_GROUPS) {
		/*  Initialization of the user list */
		if (read_user_list(params)) {
			log_message(FATAL, DEBUG_AREA_AUTH,
				    "Can't parse users file [%s]",
				    ((struct plaintext_params *) params)->
				    plaintext_userfile);
			return FALSE;
		}
	} else if (module->hook == MOD_ACL_CHECK) {
		/*  Initialization of the ACL list */
		if (read_acl_list(params)) {
			log_message(SERIOUS_WARNING, DEBUG_AREA_MAIN,
				    "Can't parse ACLs file [%s]",
				    ((struct plaintext_params *) params)->
				    plaintext_aclfile);
			return FALSE;
		}
	} else  {
		log_message(CRITICAL, DEBUG_AREA_MAIN, "Wrong plugin use: %i", module->hook);
		return FALSE;
	}

	return TRUE;
}

/*  This function is used by g_slist_find_custom() in user_check(). */
static gint find_by_username(struct plaintext_user *a,
			     struct plaintext_user *b)
{
	return strcmp(a->username, b->username);
}

static GSList *fill_user_by_username(const char *username, gpointer params)
{
	struct plaintext_user ref;
	GSList *res;
	/* strip username from domain */
	ref.username = get_rid_of_domain((char *) username);
	/*  Let's look for the first node with matching username */
	res =
	    g_slist_find_custom(((struct plaintext_params *) params)->
				plaintext_userlist, &ref,
				(GCompareFunc) find_by_username);
	g_free(ref.username);
	if (!res) {
		log_message(WARNING, DEBUG_AREA_AUTH, "Unknown user [%s]!",
			    username);
		return NULL;
	}
	return res;
}


/**
 *  user_check()
 *
 *  \param username user name string
 *  \param clientpass user provided password
 *  \param passlen password length
 *  \param session pointer to the user_session_t:: that we working on
 *  \param params module related parameter
 *  \return SASL_OK if password is correct, other values are authentication
 *           failures
 */
G_MODULE_EXPORT int user_check(const char *username,
			       const char *clientpass, unsigned passlen,
			       user_session_t *session,
			       gpointer params)
{
	GSList *res;
	char *realpass;

	res = fill_user_by_username(username, params);
	if (res == NULL) {
		return SASL_BADAUTH;
	}

	realpass = ((struct plaintext_user *) res->data)->passwd;

	if (!strcmp(realpass, "*") || !strcmp(realpass, "!")) {
		log_message(INFO, DEBUG_AREA_AUTH,
			    "user_check: Account is disabled (%s)",
			    username);
		return SASL_BADAUTH;
	}

	/*  If both clientpass and passlen are null, we just need to */
	/*  return the groups list (no checks needed) */
	if (clientpass) {
		if (verify_user_password(clientpass, realpass) != SASL_OK) {
			log_message(INFO, DEBUG_AREA_AUTH,
				    "user_check: Wrong password for %s",
				    username);
			return SASL_BADAUTH;
		}
	}


	debug_log_message(VERBOSE_DEBUG, DEBUG_AREA_MAIN,
			  "We are leaving (plaintext) user_check()");

	return SASL_OK;
}

G_MODULE_EXPORT uint32_t get_user_id(const char *username, gpointer params)
{
	GSList *res;

	res = fill_user_by_username(username, params);
	if (res == NULL)
		return 0;

	return ((struct plaintext_user *) res->data)->uid;
}


G_MODULE_EXPORT GSList *get_user_groups(const char *username,
					gpointer params)
{
	GSList *res;

	res = fill_user_by_username(username, params);
	if (res == NULL) {
		return NULL;
	}
	return g_slist_copy(((struct plaintext_user *) res->data)->
			    groups);
}

/*  acl_check() */
G_MODULE_EXPORT GSList *acl_check(connection_t * element, gpointer params)
{
	GSList *g_list = NULL;
	GSList *p_acllist;
	struct acl_group *this_acl;
	tracking_t *netdata = &element->tracking;
	struct plaintext_acl *p_acl;

	/*  netdata.protocol     IPPROTO_TCP || IPPROTO_UDP || IPPROTO_ICMP */
	/*  netdata.type         for ICMP */
	/*  netdata.code         for ICMP */
	/*  netdata.saddr        IP source */
	/*  netdata.daddr        IP destination */
	/*  netdata.source       Port source */
	/*  netdata.dest         Port destination */

	for (p_acllist =
	     ((struct plaintext_params *) params)->plaintext_acllist;
	     p_acllist; p_acllist = g_slist_next(p_acllist)) {
		p_acl = (struct plaintext_acl *) p_acllist->data;

		debug_log_message(VERBOSE_DEBUG, DEBUG_AREA_MAIN,
				"(DBG) [plaintext] test acl %s", p_acl->aclname);

		if (netdata->protocol != p_acl->proto) {
			debug_log_message(VERBOSE_DEBUG, DEBUG_AREA_MAIN,
					"(DBG) skip ACL %s: protocol doesn't match", p_acl->aclname);
			continue;
		}
		/*  ICMP? */
		if (netdata->protocol == IPPROTO_ICMP) {
			if (p_acl->proto == IPPROTO_ICMP) {
				int found = 0;
				GSList *sl_type = p_acl->types;
				for (; sl_type;
				     sl_type = g_slist_next(sl_type)) {
					if (GPOINTER_TO_INT(sl_type->data)
					    == netdata->type) {
						found = 1;
						break;
					}
				}
				if (!found) {
					debug_log_message(VERBOSE_DEBUG, DEBUG_AREA_MAIN,
							"(DBG) skip ACL %s: ICMP type doesn't match", p_acl->aclname);
					continue;
				}
			}
		} else {
			/*  Check destination port */
			if (p_acl->dst_ports) {
				int found = 0;
				struct plaintext_ports *p_ports;
				GSList *pl_ports = p_acl->dst_ports;
				for (; pl_ports;
				     pl_ports = g_slist_next(pl_ports)) {
					p_ports =
					    (struct plaintext_ports *) pl_ports->
					    data;
					if (!p_ports->firstport
					    ||
					    ((netdata->dest >=
					      p_ports->firstport)
					     && (netdata->dest <=
						 p_ports->firstport +
						 p_ports->nbports))) {
						found = 1;
						break;
					}
				}
				if (!found) {
					debug_log_message(VERBOSE_DEBUG, DEBUG_AREA_MAIN,
							"(DBG) skip ACL %s: TCP/UDP destination port doesn't match", p_acl->aclname);
					continue;
				}
			}
			/*  Check source port */
			if (p_acl->src_ports) {
				int found = 0;
				struct plaintext_ports *p_ports;
				GSList *pl_ports = p_acl->src_ports;
				for (; pl_ports;
				     pl_ports = g_slist_next(pl_ports)) {
					p_ports =
					    (struct plaintext_ports *) pl_ports->
					    data;
					if (!p_ports->firstport
					    ||
					    ((netdata->source >=
					      p_ports->firstport)
					     && (netdata->source <=
						 p_ports->firstport +
						 p_ports->nbports))) {
						found = 1;
						break;
					}
				}
				if (!found) {
					debug_log_message(VERBOSE_DEBUG, DEBUG_AREA_MAIN,
							"(DBG) skip ACL %s: TCP/UDP source port doesn't match", p_acl->aclname);
					continue;
				}
			}
		}

		/*  Check source address */
		if (!match_ip(p_acl->src_ip, &netdata->saddr)) {
			debug_log_message(VERBOSE_DEBUG, DEBUG_AREA_MAIN,
					"(DBG) skip ACL %s: source IP doesn't match", p_acl->aclname);
			continue;
		}

		/*  Check destination address */
		if (!match_ip(p_acl->dst_ip, &netdata->daddr)) {
			debug_log_message(VERBOSE_DEBUG, DEBUG_AREA_MAIN,
					"(DBG) skip ACL %s: destination IP doesn't match", p_acl->aclname);
			continue;
		}

		if (compare_iface_nfo_t(&p_acl->iface_nfo, &element->iface_nfo) == NU_EXIT_ERROR) {
			debug_log_message(VERBOSE_DEBUG, DEBUG_AREA_MAIN,
					"(DBG) skip ACL %s: interfaces doesn't match", p_acl->aclname);
			continue;
		}

		/*  O.S. filtering? */
		debug_log_message(VERBOSE_DEBUG, DEBUG_AREA_MAIN,
				  "(DBG) current ACL os=%p", p_acl->os);

		if (element->os_sysname && p_acl->os) {
			GSList *p_os = p_acl->os;
			gchar *p_sysname, *p_release, *p_version;
			int found = 0;

			/*  sysname */
			log_message(VERBOSE_DEBUG, DEBUG_AREA_MAIN,
				    "[plaintext] Checking for OS sysname=[%s]",
				    element->os_sysname);

			for (; p_os; p_os = g_slist_next(p_os)) {
				p_sysname =
				    ((struct plaintext_os *) p_os->data)->sysname;
				p_release =
				    ((struct plaintext_os *) p_os->data)->release;
				p_version =
				    ((struct plaintext_os *) p_os->data)->version;
				if (!strcasecmp
				    (p_sysname, element->os_sysname)) {
					if (element->os_release
					    && p_release) {
						if (!strcasecmp
						    (p_release,
						     element->
						     os_release)) {
							if (element->
							    os_version
							    && p_version) {
								if (!strcasecmp(p_version, element->os_version)) {
									found
									    =
									    1;
									break;
								}
							} else {
								found = 1;
								break;
							}
						}
					} else {
						found = 1;
						break;
					}
				}
			}
			debug_log_message(VERBOSE_DEBUG, DEBUG_AREA_MAIN,
					  "(DBG) Checking OS sysname ACL found=%d",
					  found);
			if (!found) {
				debug_log_message(VERBOSE_DEBUG, DEBUG_AREA_MAIN,
						"(DBG) skip ACL %s: OS doesn't match", p_acl->aclname);
				continue;
			}
			log_message(VERBOSE_DEBUG, DEBUG_AREA_MAIN,
				    "[plaintext] OS match (%s)",
				    element->os_sysname);
		}

		/*  Application filtering? */
		debug_log_message(VERBOSE_DEBUG, DEBUG_AREA_MAIN,
				  "(DBG) current ACL apps=%p",
				  p_acl->apps);

		if (element->app_name && p_acl->apps) {
			GSList *p_app = p_acl->apps;
			int found = 0;

			log_message(VERBOSE_DEBUG, DEBUG_AREA_MAIN,
				    "[plaintext] Checking for App=[%s]",
				    element->app_name);

			for (; p_app; p_app = g_slist_next(p_app)) {
				if (g_pattern_match_simple
				    (((struct plaintext_app *) p_app->data)->
				     appname, element->app_name)) {
					found = 1;
					break;
				}
			}
			log_message(VERBOSE_DEBUG, DEBUG_AREA_MAIN,
				    "(DBG) Checking App ACL found=%d",
				    found);
			if (!found) {
				debug_log_message(VERBOSE_DEBUG, DEBUG_AREA_MAIN,
						"(DBG) skip ACL %s: Application doesn't match", p_acl->aclname);
				continue;
			}
			log_message(VERBOSE_DEBUG, DEBUG_AREA_MAIN,
				    "[plaintext] App match (%s)",
				    element->app_name);
		}
		/* period checking
		 * */

		/*  We have a match 8-) */
		log_message(VERBOSE_DEBUG, DEBUG_AREA_MAIN,
			    "[plaintext] matching with ACL %s and decision %d",
			    p_acl->aclname, p_acl->decision);
		this_acl = g_new0(struct acl_group, 1);
		g_assert(this_acl);
		this_acl->answer = p_acl->decision;
		this_acl->users = g_slist_copy(p_acl->users);
		if (nuauthconf->use_groups_name) {
			this_acl->groups = duplicate_str_list(p_acl->groups);
		} else {
			this_acl->groups = g_slist_copy(p_acl->groups);
		}
		if (p_acl->period) {
			this_acl->period = g_strdup(p_acl->period);
		} else {
			this_acl->period = NULL;
		}
		if (p_acl->log_prefix) {
			this_acl->log_prefix = g_strdup(p_acl->log_prefix);
		} else {
			this_acl->log_prefix = NULL;
		}
		this_acl->flags = p_acl->flags;
		this_acl->auth_quality = p_acl->auth_quality;

		g_list = g_slist_append(g_list, this_acl);
	}

	debug_log_message(VERBOSE_DEBUG, DEBUG_AREA_MAIN,
			  "[plaintext] We are leaving acl_check()");
	debug_log_message(VERBOSE_DEBUG, DEBUG_AREA_MAIN,
			  "(DBG) [plaintext] check_acls leaves with %p",
			  g_list);
	return g_list;
}

/** @} */
