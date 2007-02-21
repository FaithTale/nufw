/*
 ** Copyright(C) 2007 INL
 ** Written by Victor Stinner <victor.stinner@inl.fr>
 **
 ** $Id$
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

#include "auth_srv.h"
#include "command.h"
#include <sys/un.h>		/* unix socket */

#define SOCKET_FILENAME "/tmp/nuauth-command.socket"

typedef struct {
	time_t start_timestamp;
	int socket;
	int client;
	struct sockaddr_un client_addr;
	int select_max;
	fd_set select_set;
} command_t;

int command_new(command_t * this)
{
	struct sockaddr_un addr;
	int len;
	int res;
	int on = 1;

	this->start_timestamp = time(NULL);
	this->socket = -1;
	this->client = -1;
	this->select_max = 0;

	/* Remove socket file */
	(void) unlink(SOCKET_FILENAME);

	/* set address */
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, SOCKET_FILENAME, sizeof(addr.sun_path));
	addr.sun_path[sizeof(addr.sun_path) - 1] = 0;
	len = strlen(addr.sun_path) + sizeof(addr.sun_family);

	/* create socket */
	this->socket = socket(AF_UNIX, SOCK_STREAM, 0);
	if (this->socket == -1) {
		log_message(CRITICAL, AREA_MAIN,
			    "Command server: enable to create UNIX socket %s: %s",
			    addr.sun_path, g_strerror(errno));
		return 0;
	}
	this->select_max = this->socket + 1;

	/* set reuse option */
	res =
	    setsockopt(this->socket, SOL_SOCKET, SO_REUSEADDR,
		       (char *) &on, sizeof(on));

	/* bind socket */
	res = bind(this->socket, (struct sockaddr *) &addr, len);
	if (res == -1) {
		log_message(CRITICAL, AREA_MAIN,
			    "Command server: UNIX socket bind() error: %s",
			    g_strerror(errno));
		return 0;
	}

	/* listen */
	if (listen(this->socket, 1) == -1) {
		log_message(CRITICAL, AREA_MAIN,
			    "Command server: UNIX socket listen() error: %s",
			    g_strerror(errno));
		return 0;
	}
	return 1;
}

int command_client_accept(command_t * this)
{
	socklen_t len = sizeof(this->client_addr);

	this->client =
	    accept(this->socket, (struct sockaddr *) &this->client_addr,
		   &len);
	if (this->client < 0) {
		log_message(CRITICAL, AREA_MAIN,
			    "Command server: accept() error: %s",
			    g_strerror(errno));
		return 0;
	}
	if (this->socket < this->client)
		this->select_max = this->client + 1;
	else
		this->select_max = this->socket + 1;
	log_message(WARNING, AREA_MAIN,
		    "Command server: client connected");
	return 1;
}

void command_client_close(command_t * this)
{
	log_message(WARNING, AREA_MAIN,
		    "Command server: close client connection");
	close(this->client);
	this->client = -1;
	this->select_max = this->socket + 1;
}

void command_execute(command_t * this, char *command)
{
	char *buffer = "ok";
	static char static_buffer[1024];
	char *help =
"version: display nuauth version\n"
"uptime: display nuauth starting time and uptime\n"
"reload: reload nuauth configuration\n"
"help: display this help\n"
"quit: disconnect";
	int ret;

	/* process command */
	if (strcmp(command, "quit") == 0) {
		command_client_close(this);
		return;
	} else if (strcmp(command, "help") == 0) {
		buffer = help;
	} else if (strcmp(command, "uptime") == 0) {
		char time_text[sizeof(static_buffer)];
		time_t diff;
		struct tm timestamp;

		/* compute uptime and format starting time */
		diff = time(NULL) - this->start_timestamp;
		localtime_r(&this->start_timestamp, &timestamp);
		ret = strftime(time_text, sizeof(time_text)-1,
			       "%F %H:%M:%S", &timestamp);
		time_text[ret] = 0;

		/* create answer message */
		(void)secure_snprintf(static_buffer, sizeof(static_buffer),
				"%u sec since %s", diff, time_text);
		buffer = static_buffer;
	} else if (strcmp(command, "version") == 0) {
		secure_snprintf(static_buffer, sizeof(static_buffer),
				"Nuauth %s", NUAUTH_FULL_VERSION);
		buffer = static_buffer;
	} else if (strcmp(command, "reload") == 0) {
		nuauth_reload(0);
		buffer = "Reload configuration";
	} else {
		(void)secure_snprintf(static_buffer, sizeof(static_buffer)-1,
				      "Error: Unknown command \"%s\"",
				      command);
		buffer = static_buffer;
	}

	/* send answer */
	ret = send(this->client, buffer, strlen(buffer), 0);
	if (ret < 0) {
		log_message(WARNING, AREA_MAIN,
			    "Command server: client send() error: %s",
			    g_strerror(errno));
		command_client_close(this);
	}
}

void command_client_run(command_t * this)
{
	char buffer[40];
	int ret;
	ret = recv(this->client, buffer, sizeof(buffer) - 1, 0);
	printf("CLIENT recv=%i\n", ret);
	if (ret <= 0) {
		if (ret == 0) {
			log_message(WARNING, AREA_MAIN, "Command server: "
				    "lost connection with client");
			printf("lost\n");
		} else {
			log_message(WARNING, AREA_MAIN, "Command server: "
				    "error on recv() from client: %s",
				    g_strerror(errno));
		}
		command_client_close(this);
		return;
	}
	if (ret == (sizeof(buffer)-1))
	{
		log_message(WARNING, AREA_MAIN,
			    "Command server: client command is too long, "
			    "disconnect him.");
		command_client_close(this);
	}
	buffer[ret] = 0;
	printf("CLIENT command: >>%s<<\n", buffer);
	command_execute(this, buffer);
}

int command_main(command_t * this)
{
	struct timeval tv;
	int ret;

	/* Wait activity on the socket */
	FD_ZERO(&this->select_set);
	FD_SET(this->socket, &this->select_set);
	if (0 <= this->client)
		FD_SET(this->client, &this->select_set);
	tv.tv_sec = 1;
	tv.tv_usec = 0;
	ret = select(this->select_max, &this->select_set, NULL, NULL, &tv);

	/* catch select() error */
	if (ret == -1) {
		/* Signal was catched: just ignore it */
		if (errno == EINTR) {
			return 1;
		}

		log_message(CRITICAL, AREA_MAIN,
			    "Command server: select() fatal error: %s",
			    g_strerror(errno));
		return 0;
	}

	/* timeout: continue */
	if (ret == 0) {
		return 1;
	}

	if (0 <= this->client && FD_ISSET(this->client, &this->select_set)) {
		command_client_run(this);
	}
	if (FD_ISSET(this->socket, &this->select_set)) {
		if (!command_client_accept(this))
			return 0;
	}
	return 1;
}

void *command_server(GMutex * mutex)
{
	command_t command;

	if (!command_new(&command))
		return NULL;

	while (g_mutex_trylock(mutex)) {
		g_mutex_unlock(mutex);

		command_main(&command);
	}
	return NULL;
}