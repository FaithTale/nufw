/*
 ** Copyright(C) 2003-2004 Eric Leblond <regit@inl.fr>
 **                        INL http://www.inl.fr/
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
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <netinet/ip_icmp.h> 

#include <errno.h>

/** 
 *fill ip related part of the connection tracking header.
 * 
 * - Argument 1 : a connection
 * - Argument 2 : pointer to packet datas
 * - Return : offset to next type of headers 
 */

int get_ip_headers(connection_t *connection,char * dgram)
{
	struct iphdr * iphdrs = (struct iphdr *) dgram;
	/* check IP version */
	if (iphdrs->version == 4){
		connection->tracking_hdrs.saddr=ntohl(iphdrs->saddr);
		connection->tracking_hdrs.daddr=ntohl(iphdrs->daddr);
		/* get protocol */
		connection->tracking_hdrs.protocol=iphdrs->protocol;
		return 4*iphdrs->ihl;
	}
#ifdef DEBUG_ENABLE
	if (DEBUG_OR_NOT(DEBUG_LEVEL_DEBUG,DEBUG_AREA_PACKET)){
		g_message("IP version is %d, ihl : %d",iphdrs->version,iphdrs->ihl);
	}
#endif
	return 0;
}

/** 
 * fill udp related part of the connection tracking header.
 * 
 * - Argument 1 : a connection
 * - Argument 2 : pointer to packet datas
 * - Return : 0
 */

int get_udp_headers(connection_t *connection, char * dgram)
{
	struct udphdr * udphdrs=(struct udphdr *)dgram;
	connection->tracking_hdrs.source=ntohs(udphdrs->source);
	connection->tracking_hdrs.dest=ntohs(udphdrs->dest);
	connection->tracking_hdrs.type=0;
	connection->tracking_hdrs.code=0;
	return 0;
}


/**
 * fill tcp related part of the connection tracking header.
 *
 * - Argument 1 : a connection
 * - Argument 2 : pointer to packet datas
 * - Return : STATE of the packet
 */

int get_tcp_headers(connection_t *connection, char * dgram)
{
	struct tcphdr * tcphdrs=(struct tcphdr *) dgram;
	connection->tracking_hdrs.source=ntohs(tcphdrs->source);
	connection->tracking_hdrs.dest=ntohs(tcphdrs->dest);

	connection->tracking_hdrs.type=0;
	connection->tracking_hdrs.code=0;
	/* test if fin ack or syn */
	/* if fin ack return 0 end of connection */
	if (tcphdrs->fin || tcphdrs->rst )
		return STATE_CLOSE;
	/* if syn return 1 */
	if (tcphdrs->syn) {
		if (tcphdrs->ack){
			return STATE_ESTABLISHED;
		} else {
			return STATE_OPEN;
		}
	}
	return -1;
}

/** 
 * fill icmp related part of the connection tracking header.
 * 
 * - Argument 1 : a connection
 * - Argument 2 : pointer to packet datas
 * - Return : 0
 */


int get_icmp_headers(connection_t *connection, char * dgram)
{
	struct icmphdr * icmphdrs= (struct icmphdr *)dgram;
	connection->tracking_hdrs.source=0;
	connection->tracking_hdrs.dest=0;
	connection->tracking_hdrs.type=icmphdrs->type;
	connection->tracking_hdrs.code=icmphdrs->code;
	return 0;
}

/**
 * decode a dgram packet from gateway and create a connection with it.
 * 
 * - Argument 1 : pointer to dgram
 * - Argument 2 : dgram size
 * - Return : pointer to allocated connection
 */

connection_t*  authpckt_decode(char * dgram, int  dgramsiz)
{
	int offset; 
	int8_t *pointer;
	uint8_t msg_type;
	uint16_t data_len;
	connection_t*  connection = NULL;

	switch (*dgram) {
		case PROTO_VERSION:
			msg_type=*(dgram+1);
                        switch (msg_type){
                          case AUTH_REQUEST:
                          case AUTH_CONTROL:
                                  {
				/* allocate connection */
				connection = g_new0( connection_t,1);
				if (connection == NULL){
					if (DEBUG_OR_NOT(DEBUG_LEVEL_WARNING,DEBUG_AREA_PACKET)){
						g_message("Can not allocate connection\n");
					}
					return NULL;
				}
#ifdef PERF_DISPLAY_ENABLE
			      gettimeofday(&(connection->arrival_time),NULL);
#endif
				/* parse packet */
				pointer=(int8_t*)dgram + 2;


				data_len=ntohs(*(uint16_t *)pointer);
				if (data_len != dgramsiz){
					if (DEBUG_OR_NOT(DEBUG_LEVEL_WARNING,DEBUG_AREA_PACKET)){
						g_warning("packet seems to contain other datas, left %d byte(s) (announced : %d, get : %d)",
								dgramsiz-data_len,
								data_len,
								dgramsiz);
					}
				}
				pointer+=2;
				connection->acl_groups=NULL;
				connection->user_groups=NULL;
				connection->packet_id=NULL;
				connection->packet_id=g_slist_append(connection->packet_id, GUINT_TO_POINTER(ntohl(*(uint32_t * )pointer)));
#ifdef DEBUG_ENABLE
				if (DEBUG_OR_NOT(DEBUG_LEVEL_DEBUG,DEBUG_AREA_PACKET)) {
					g_message("Working on  %u\n",(uint32_t)GPOINTER_TO_UINT(connection->packet_id->data));
				}
#endif
				pointer+=sizeof (uint32_t);

				connection->timestamp=ntohl(*(uint32_t * )pointer);
				pointer+=sizeof ( int32_t);
				/* get ip headers till tracking is filled */
				offset = get_ip_headers(connection, (char*)pointer);
				if ( offset) {
					pointer+=offset;
					/* get saddr and daddr */
					/* check if proto is in Hello mode list (when hello authentication is used) */
					if ( nuauthconf->hello_authentication &&  localid_authenticated_protocol(connection->tracking_hdrs.protocol) ) {
						connection->state=STATE_HELLOMODE;
					} 
					switch (connection->tracking_hdrs.protocol) {
						case IPPROTO_TCP:
							switch (get_tcp_headers(connection, (char*)pointer)){
								case STATE_OPEN:
									break; 
								case STATE_CLOSE:
									if (msg_type == AUTH_CONTROL ){
										log_user_packet(*connection,STATE_CLOSE);
										return NULL;
									}
									break;
								case STATE_ESTABLISHED:
									if (msg_type == AUTH_CONTROL ){
										log_user_packet(*connection,STATE_ESTABLISHED);
										return NULL;
									}
									break;
								default:
									if (DEBUG_OR_NOT(DEBUG_LEVEL_WARNING,DEBUG_AREA_PACKET))
										g_warning ("Can't parse TCP headers\n");
									free_connection(connection);
									return NULL;
							}
							break;
						case IPPROTO_UDP:
							if ( get_udp_headers(connection, (char*)pointer) ){
								if (DEBUG_OR_NOT(DEBUG_LEVEL_WARNING,DEBUG_AREA_PACKET))
									g_warning ("Can't parse UDP headers\n");
								free_connection(connection);
								return NULL;
							}
							break;
						case IPPROTO_ICMP:
							if ( get_icmp_headers(connection, (char*)pointer)){
								if (DEBUG_OR_NOT(DEBUG_LEVEL_WARNING,DEBUG_AREA_PACKET))
									g_message ("Can't parse ICMP headers\n");
								free_connection(connection);
								return NULL;
							}
							break;
						default:
							if ( connection->state != STATE_HELLOMODE){
								if (DEBUG_OR_NOT(DEBUG_LEVEL_WARNING,DEBUG_AREA_PACKET))
									g_message ("Can't parse this protocol\n");
								free_connection(connection);
								return NULL;
							}
					}
				}
				else {
					if (DEBUG_OR_NOT(DEBUG_LEVEL_WARNING,DEBUG_AREA_PACKET))
						g_message ("Can't parse IP headers\n");
					free_connection(connection);
					return NULL;
				}
				connection->user_groups = ALLGROUP;
				/* have look at timestamp */
				if ( connection->timestamp == 0 ){
					connection->timestamp=time(NULL);
				}
#ifdef DEBUG_ENABLE
				if (DEBUG_OR_NOT(DEBUG_LEVEL_VERBOSE_DEBUG,DEBUG_AREA_PACKET)){
					g_message("Packet : ");
					print_connection(connection,NULL);
				}
#endif
				return connection;
			} 
                                  break;
                          case AUTH_CONN_DESTROY:
                                  {
                                      struct nuv2_conntrack_message* msg=(struct nuv2_conntrack_message*) dgram;
                                      tracking* datas=g_new0(tracking,1);
                                      struct internal_message  *message = g_new0(struct internal_message,1);
                                      datas->protocol = msg->ipproto;
                                      datas->saddr = ntohl(msg->src);
                                      datas->daddr = ntohl(msg->dst);
                                      if (msg->ipproto == IPPROTO_ICMP) {
                                          datas->type = ntohs(msg->sport);
                                          datas->code = ntohs(msg->dport);
                                      } else {
                                          datas->source = ntohs(msg->sport);
                                          datas->dest = ntohs(msg->dport);
                                      }               
                                      message->datas = datas;
                                      message->type = FREE_MESSAGE;
                                      g_async_queue_push (nuauthdatas->limited_connections_queue, message);
                                  }
                                  break;
                         case AUTH_CONN_UPDATE:
                                  {
                                      struct nuv2_conntrack_message* msg=(struct nuv2_conntrack_message*) dgram;
                                      tracking* datas=g_new0(tracking,1);
                                      struct internal_message  *message = g_new0(struct internal_message,1);
                                      datas->protocol = msg->ipproto;
                                      datas->saddr = ntohl(msg->src);
                                      datas->daddr = ntohl(msg->dst);
                                      if (msg->ipproto == IPPROTO_ICMP) {
                                          datas->type = ntohs(msg->sport);
                                          datas->code = ntohs(msg->dport);
                                      } else {
                                          datas->source = ntohs(msg->sport);
                                          datas->dest = ntohs(msg->dport);
                                      }               
                                      message->datas = datas;
                                      message->type = UPDATE_MESSAGE;
                                      g_async_queue_push (nuauthdatas->limited_connections_queue, message);
                                  }
                                  break;

                          default:
				if (DEBUG_OR_NOT(DEBUG_LEVEL_VERBOSE_DEBUG,DEBUG_AREA_PACKET)) {
					g_message("Not for us\n");
				}
                        }
                                  

	}
	return NULL;
}



