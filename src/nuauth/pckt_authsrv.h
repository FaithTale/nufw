/*
 ** Copyright(C) 2005 Eric Leblond <regit@inl.fr>
 ** INL http://www.inl.fr/
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

#ifndef PCKT_AUTHSR_H
#define PCKT_AUTHSR_H

void *packet_authsrv();
nu_error_t authpckt_decode(unsigned char **dgram, unsigned int *dgramsize,
			   connection_t **);
void acl_check_and_decide(gpointer userdata, gpointer data);

unsigned char get_proto_version_from_packet(const unsigned char *dgram,
					    size_t dgram_size);

int get_nufw_message_length_from_packet(const unsigned char * dgram,
					size_t dgram_size);
#endif
