/*
 * Copyright(C) 2004-2006 INL http://www.inl.fr/
 ** written by  Eric Leblond <regit@inl.fr>
 **             Vincent Deffontaines <vincent@inl.fr>
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
#include <crypt.h>
#include <sys/time.h>
#include <sasl/saslutil.h>

static GSList * userpckt_decode(struct tls_buffer_read * datas);

/**
 * Get user datas (containing datagram) and goes till inclusion
 * (or decision) on packet.
 * 
 * Call userpckt_decode()
 *
 * \param userdata Pointer to a struct tls_buffer_read: containing the datagram
 * \param data NULL (unused)
 */
void user_check_and_decide (gpointer userdata, gpointer data)
{
  GSList * conn_elts=NULL;
  GSList* conn_elt_l;
  connection_t* conn_elt;

  debug_log_message (VERBOSE_DEBUG, AREA_USER, "entering user_check");
  
  /* reload condition */
  block_on_conf_reload();
  conn_elts = userpckt_decode(userdata);

  if (conn_elts == NULL)
  {
      free_buffer_read(userdata);
      log_message (INFO, AREA_USER, "User packet decoding failed");
      return;
  }
  
  /* if OK search and fill */
  for (conn_elt_l=conn_elts; conn_elt_l!=NULL; conn_elt_l=conn_elt_l->next)
  {
      conn_elt = conn_elt_l->data;

      /* in this case we have an HELLO MODE packet */
      if (conn_elt->packet_id){
              struct internal_message *message = g_new0(struct internal_message,1);
              /* We assume the source address we try to authenticate is source address
               * of client connection */
              conn_elt->tracking.saddr = ((struct tls_buffer_read *)userdata)->ip_addr;
              message->type=INSERT_MESSAGE;
              message->datas=conn_elt;
              g_async_queue_push (nuauthdatas->localid_auth_queue,message);
              continue;
      } 
      /* Sanity check : verify source IP equality */
      if  ( memcmp(& ((struct tls_buffer_read *)userdata)->ip_addr,
              &conn_elt->tracking.saddr, sizeof(conn_elt->tracking.saddr)) == 0 ){
#ifdef DEBUG_ENABLE
          if (DEBUG_OR_NOT(DEBUG_LEVEL_DEBUG,DEBUG_AREA_PACKET)){
              g_message("User : %s",conn_elt->username);
              print_connection(conn_elt,NULL);
          }
#endif
          g_async_queue_push (nuauthdatas->connections_queue,conn_elt);
      } else {
          if (DEBUG_OR_NOT(DEBUG_LEVEL_INFO,DEBUG_AREA_USER)){
              char ip_ascii[INET6_ADDRSTRLEN];

              if (inet_ntop(AF_INET6, &((struct tls_buffer_read *)userdata)->ip_addr, ip_ascii, sizeof(ip_ascii)) != NULL)
              {
                  g_message("User %s on %s tried to authenticate packet from other ip",
                          conn_elt->username,
                          ip_ascii);
              }
              print_connection(conn_elt,NULL);
          }
          /* free connection */
          free_connection(conn_elt);
      }
  }
  free_buffer_read(userdata);
}

void user_process_field_hello(connection_t* connection, struct nu_authfield_hello* hellofield)
{
    debug_log_message (VERBOSE_DEBUG, AREA_USER, "\tgot hello field");
    connection->packet_id=g_slist_prepend(NULL,GINT_TO_POINTER(hellofield->helloid));
}    

int user_process_field_ipv6(connection_t* connection, struct nu_authfield_ipv6 *ipfield)
{
    connection->tracking.saddr = ipfield->src;
    connection->tracking.daddr = ipfield->dst;
    connection->tracking.protocol = ipfield->proto;

    debug_log_message (VERBOSE_DEBUG, AREA_USER, "\tgot IPv4 field");
    switch (connection->tracking.protocol) 
    {
        case IPPROTO_TCP:
        case IPPROTO_UDP:
            connection->tracking.source=ntohs(ipfield->sport);
            connection->tracking.dest=ntohs(ipfield->dport);
            connection->tracking.type=0;
            connection->tracking.code=0;
            break;
            
        case IPPROTO_ICMP:
        case IPPROTO_ICMPV6:
            connection->tracking.source=0;
            connection->tracking.dest=0;
            connection->tracking.type=ntohs(ipfield->sport);
            connection->tracking.code=ntohs(ipfield->dport);
            break;

        default: 
            return -1;
    }
    return 0;
}    

int user_process_field_ipv4(connection_t* connection, struct nu_authfield_ipv4 *ipfield)
{

    connection->tracking.saddr.s6_addr32[0] = 0;
    connection->tracking.saddr.s6_addr32[1] = 0;
    connection->tracking.saddr.s6_addr32[2] = 0xffff0000;
    connection->tracking.saddr.s6_addr32[3] = ipfield->src;

    connection->tracking.daddr.s6_addr32[0] = 0;
    connection->tracking.daddr.s6_addr32[1] = 0;
    connection->tracking.daddr.s6_addr32[2] = 0xffff0000;
    connection->tracking.daddr.s6_addr32[3] = ipfield->dst;

    connection->tracking.protocol = ipfield->proto;

    debug_log_message (VERBOSE_DEBUG, AREA_USER, "\tgot IPv4 field");
    switch (connection->tracking.protocol) 
    {
        case IPPROTO_TCP:
        case IPPROTO_UDP:
            connection->tracking.source=ntohs(ipfield->sport);
            connection->tracking.dest=ntohs(ipfield->dport);
            connection->tracking.type=0;
            connection->tracking.code=0;
            break;
            
        case IPPROTO_ICMP:
            connection->tracking.source=0;
            connection->tracking.dest=0;
            connection->tracking.type=ntohs(ipfield->sport);
            connection->tracking.code=ntohs(ipfield->dport);
            break;
	/* Non supported protocol HAVE to be rejected */
        default:
	    return -1;
    }
    return 0;
}    

int user_process_field_app(
        struct nu_authreq* authreq,
        connection_t* connection, 
        int field_buffer_len,
        struct nu_authfield_app *appfield)
{
    unsigned int reallen=0;
    gchar* dec_appname=NULL;
    unsigned int len=appfield->length-4;

    debug_log_message (VERBOSE_DEBUG, AREA_USER, "\tgot APP field");

    /* this has to be smaller than field size */
    if (field_buffer_len < (int)appfield->length) {
        log_message(WARNING, AREA_USER, "Improper application field length signaled in authreq header %d < %d",field_buffer_len,appfield->length);
        return -1;
    }

    if (len > 512 || (len <= 0)){
        /* it is reaaally long (or too short), we ignore packet (too lasy to kill client) */
        log_message (INFO, AREA_USER,
            "user packet announced a bad length app name : %d\n",len);
        return -1;
    }
    dec_appname = g_new0(gchar,len);
    if (sasl_decode64((char*)appfield+4,len, dec_appname,len,&reallen) 
            ==
            SASL_BUFOVER) {
        dec_appname=g_try_realloc(dec_appname,reallen+1);
        if (dec_appname)
            sasl_decode64((char*)appfield+4,len, dec_appname,reallen,&reallen) ;
    } else {
        dec_appname=g_try_realloc(dec_appname,reallen+1);
    }
    dec_appname[reallen]=0;

    if (dec_appname != NULL)
    {
        connection->app_name= string_escape(dec_appname);
        if (connection->app_name == NULL)
            log_message(WARNING, AREA_USER, "user packet received an invalid app name\n");
    }else{
        connection->app_name=NULL;
    }
    g_free(dec_appname);
    connection->app_md5=NULL;
    return 1;
}    


int user_process_field(
        struct nu_authreq* authreq, 
        uint8_t header_option,
        connection_t* connection, 
        gboolean *multiclient_ok,
        int auth_buffer_len,
        struct nu_authfield* field)
{
    /* check field length */
    field->length = ntohs(field->length);
    if (auth_buffer_len < (int)field->length)
    {
        return -1;
    }

    switch (field->type) {
        case IPV6_FIELD:
            if (auth_buffer_len < (int)sizeof(struct nu_authfield_ipv6)) {
                return -1;
            }
            if (user_process_field_ipv6(connection, (struct nu_authfield_ipv6 *)field))
                return -1;
            break;

	case IPV4_FIELD:
            if (auth_buffer_len < (int)sizeof(struct nu_authfield_ipv6)) {
                return -1;
            }
	    switch (connection->client_version){
	    	case PROTO_VERSION_V22:
			log_message(WARNING,AREA_USER,"Proto V4 user sends an IPV4_FIELD");
			return -1;
		case PROTO_VERSION_V20:
			if (user_process_field_ipv4(connection, (struct nu_authfield_ipv4 *)field))
				return -1;
			break;
		default:
			log_message(WARNING,AREA_USER,"Unknown protocol %d client has sent an IPV4_FIELD",connection->client_version);
	    }
            break;

        case APP_FIELD:
            if (auth_buffer_len < (int)sizeof(struct nu_authfield_app)) {
                return -1;
            }
            if (user_process_field_app(authreq, connection, field->length, (struct nu_authfield_app *)field) < 0)
                return -1;
            break;

        case USERNAME_FIELD:
	    log_message(WARNING,AREA_USER,"Received USERNAME_FIELD, this is BAD! multiuser client are born-dead");
	    return -1;

        case HELLO_FIELD:
            if (auth_buffer_len < (int)sizeof(struct nu_authfield_hello)) {
                return -1;
            }
            user_process_field_hello(connection, (struct nu_authfield_hello *)field);
            break;

        default:
            log_message (INFO, AREA_USER,
                "unknown field type: %d", field->type);
            return -1;
    }
    return field->length;
}    

/**
 * \param datas Buffer read on a TLS socket
 * \return Single linked list of connections (of type connection_t:).
 */
GSList* user_request(struct tls_buffer_read *datas)
{
    char * dgram = datas->buffer;
    struct nu_header* header = (struct nu_header *)dgram;
    GSList* conn_elts=NULL;
    connection_t* connection=NULL;
    char* start;
    gboolean multiclient_ok=FALSE;
    int buffer_len = datas->buffer_len;
    int auth_buffer_len;
    int field_length;
    struct nu_authreq* authreq;
    char *req_start;

    for (start = dgram + sizeof(struct nu_header), buffer_len -= sizeof(struct nu_header); 
         0 < buffer_len; 
         start += authreq->packet_length, buffer_len -= authreq->packet_length)
    {
        /* check buffer underflow */
        if (buffer_len < (int)sizeof(struct nu_authreq))
        {
            free_connection_list(conn_elts);
            return NULL;
        }
        authreq=(struct nu_authreq* )start;

        authreq->packet_length=ntohs(authreq->packet_length);
        if (authreq->packet_length == 0
                || buffer_len < (int)authreq->packet_length)
        {
            log_message (WARNING, AREA_USER,
                "Improper length signaled in authreq header: %d",
                authreq->packet_length);
            free_connection_list(conn_elts);
            return NULL;
        }

        connection = g_new0( connection_t,1);
        connection->acl_groups=NULL;
        connection->user_groups=NULL;
        connection->app_name=NULL;
        connection->app_md5=NULL;
        connection->username=NULL;
        connection->cacheduserdatas=NULL;
        connection->packet_id=NULL;
        connection->expire=-1;
	connection->client_version = datas->client_version;
#ifdef PERF_DISPLAY_ENABLE
        gettimeofday(&(connection->arrival_time),NULL);
#endif
       
        /*** process all fields ***/
        debug_log_message (VERBOSE_DEBUG, AREA_USER, "Authreq start");
        req_start = start + sizeof(struct nu_authreq);
        auth_buffer_len = authreq->packet_length - sizeof(struct nu_authreq);
        for (; 
                0 < auth_buffer_len; 
                req_start += field_length, auth_buffer_len -= field_length)
        {
            struct nu_authfield* field = (struct nu_authfield* )req_start;

            /* check buffer underflow */
            if (auth_buffer_len < (int)sizeof(struct nu_authfield))
            {
                free_connection_list(conn_elts);
                free_connection(connection);
                return NULL;
            }

            /* process field */
            field_length = user_process_field (authreq, header->option, 
                    connection, &multiclient_ok, auth_buffer_len, field);
            if (field_length < 0) {
                free_connection_list(conn_elts);
                free_connection(connection);
                return NULL;
            }
        }

	/* Sanity check on received packet :
	 * Source address can be 0 only if it's a hello mode packet 
	 * we also want to have APPNAME defined 
	 * We destroy all the received message and stop parsing */
	if (
			(
			 memcmp(&connection->tracking.saddr, &in6addr_any, sizeof(connection->tracking.saddr)) == 0
			 ||
			 (connection->app_name == NULL)
			)
			&& 
			(connection->packet_id == NULL)
	   ) {
		free_connection_list(conn_elts);
		free_connection(connection);
		return NULL;
	}

        /* here all packet related information are filled-in */
        if (connection->username == NULL){	
            connection->username=g_strdup(datas->user_name);
        }
        connection->mark=datas->user_id;
        connection->user_groups = g_slist_copy(datas->groups);
        connection->os_sysname=g_strdup(datas->os_sysname);
        connection->os_release=g_strdup(datas->os_release);
        connection->os_version=g_strdup(datas->os_version);
	/* copy client version information */
        connection->client_version=datas->client_version;

        if (connection->user_groups == NULL) {
            if ((header->option == 0x1) && multiclient_ok) {
                /* group is not fill in multi users mode
                 * need to be done now */
                log_message (INFO, AREA_USER, "Get users info");
                if ( nuauthconf->user_cache ){
                    get_users_from_cache(connection);
                } else {
                    connection->user_groups = modules_get_user_groups(connection->username);
                    connection->mark = modules_get_user_id(connection->username);
                    if (connection->user_groups == NULL){
                        log_message (INFO, AREA_PACKET, "User not found");
                    }
                }
            } else {
                log_message (INFO, AREA_USER, "User_check return is bad");
                free_connection_list(conn_elts);
                free_connection(connection);
                return NULL;
            }
        }
        
        connection->state=AUTH_STATE_USERPCKT;

        connection->acl_groups=NULL;            /* acl part is NULL */
        connection->timestamp=time(NULL);       /* first reset timestamp to now */
        conn_elts=g_slist_prepend(conn_elts,connection);

        debug_log_message (VERBOSE_DEBUG, AREA_USER, "Authreq end");
    }
    return conn_elts;
}    

/**
 * Decode user datagram packet and fill a connection with datas
 * (called by user_check_and_decide()).
 *
 * \param datas Pointer to a struct tls_buffer_read:
 * \return Single linked list of connections (of type connection_t).
 */
static GSList* userpckt_decode(struct tls_buffer_read *datas)
{
    char * dgram = datas->buffer;
    struct nu_header* header=(struct nu_header*)dgram;

    /* check buffer underflow */
    if (datas->buffer_len < (int)sizeof(struct nu_header))
    {
        return NULL;
    }

    /* check protocol version */
    if (check_protocol_version(header->proto) != NU_EXIT_OK)
    {
        log_message (INFO, AREA_USER,
            "unsupported protocol, got protocol %d (msg %d) with option %d (length %d)",
            header->proto, header->msg_type, header->option, header->length);
        return NULL;
    }

    header->length=ntohs(header->length);

    if(header->length > MAX_NUFW_PACKET_SIZE){
        log_message (WARNING, AREA_USER,
                "Improper length signaled in packet header");
        return NULL;
    }

    if (header->msg_type != USER_REQUEST)
    {
        log_message (INFO, AREA_USER, "unsupported message type");
        return NULL;
    }
    return user_request(datas);
}

