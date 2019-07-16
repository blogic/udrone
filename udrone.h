/*
 *   udrone - Multicast Device Remote Control
 *   Copyright (C) 2010 Steven Barth <steven@midlink.org>
 *   Copyright (C) 2010-2019 John Crispin <blogic@openwrt.org>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#ifndef UDRONE_H_
#define UDRONE_H_

#include <stdint.h>
#include <netinet/in.h>
#include <libubox/uloop.h>
#include <libubox/blobmsg.h>
#include <libubox/blobmsg_json.h>
#include <libubus.h>

#define UDRONE_MAX_DGRAM		(32 * 1024)
#define UDRONE_GROUP_DEFAULT		"!all-default"
#define UDRONE_GROUP_LOST		"!all-lost"
#define UDRONE_GROUP_TIMEOUT		60

#define UDRONE_PORT 21337
#define UDRONE_ADDR "239.6.6.6"

#define UDRONE_DATAREPLY 1
#define UDRONE_HANDLER_ATOMIC 0x01

typedef int(udrone_handler_t)(struct blob_attr **);

struct udrone_registry {
	int flags;
	char *type;
	udrone_handler_t *handler;
};

struct udrone_module {
	struct udrone_module *next;
	struct udrone_registry *registry;
};

struct udrone_ctx {
	struct uloop_fd sock;
	struct uloop_timeout timeout;
	struct uloop_process worker;
	struct ubus_auto_conn ubus;
	struct sockaddr_in worker_addr;
	char *worker_buf;
	char board[64];
	char uniqueid[32];
	char group[32];
	const char *ifname;
	struct blob_buf in, out;
	uint32_t assigned;
};

extern struct udrone_ctx udrone;

enum {
	MSG_TO = 0,
	MSG_FROM,
	MSG_SEQ,
	MSG_TYPE,
	MSG_DATA,
	__MSG_MAX
};

void udrone_prepare(struct blob_attr **tb, char *type);
void udrone_register(struct udrone_module *module);

#define UDRONE_MODULE_REGISTER(module) \
static void __attribute__((constructor)) udrone_plugin_ctor_##module() { \
	udrone_register(&module); \
}

#endif /* UDRONE_H_ */
