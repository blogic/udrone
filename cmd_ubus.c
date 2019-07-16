/*
 *   udrone - Multicast Device Remote Control
 *   Copyright (C) 2019 John Crispin <blogic@openwrt.org>
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

#include "udrone.h"

enum {
	UBUS_PATH = 0,
	UBUS_METHOD,
	UBUS_PARAM,
	UBUS_TIMEOUT,
	__UBUS_MAX
};

static const struct blobmsg_policy ubus_policy[__UBUS_MAX] = {
	[UBUS_PATH] = { .name = "path", .type = BLOBMSG_TYPE_STRING },
	[UBUS_METHOD] = { .name = "method", .type = BLOBMSG_TYPE_STRING },
	[UBUS_PARAM] = { .name = "param", .type = BLOBMSG_TYPE_TABLE },
	[UBUS_TIMEOUT] = { .name = "timeout", .type = BLOBMSG_TYPE_INT32 },
};

static void
ubus_data_handler(struct ubus_request *req, int type, struct blob_attr *msg)
{
	struct blob_attr *cur;
	int rem;

	//blobmsg_add_blob(&udrone.out, blobmsg_data(msg));
	blobmsg_for_each_attr(cur, msg, rem)
		blobmsg_add_blob(&udrone.out, cur);
}

static int
handler_ubus(struct blob_attr **msg)
{
	struct blob_attr *tb[__UBUS_MAX];
	char *path, *method;
	int timeout = 2000;
	unsigned int id;
	int ret;

	if (!msg[MSG_DATA] || (blobmsg_type(msg[MSG_DATA]) != BLOBMSG_TYPE_TABLE))
		return -EINVAL;

	blobmsg_parse(ubus_policy, __UBUS_MAX, tb, blobmsg_data(msg[MSG_DATA]), blobmsg_len(msg[MSG_DATA]));
	if (!tb[UBUS_PATH] || !tb[UBUS_METHOD])
		return -EINVAL;

	path = blobmsg_get_string(tb[UBUS_PATH]);
	method = blobmsg_get_string(tb[UBUS_METHOD]);
	if (tb[UBUS_TIMEOUT])
		timeout = blobmsg_get_u32(tb[UBUS_TIMEOUT]);;

	if (ubus_lookup_id(&udrone.ubus.ctx, path, &id))
		return -ENOENT;

	ret = ubus_invoke(&udrone.ubus.ctx, id, method, tb[UBUS_PARAM], ubus_data_handler, NULL, timeout);

	return ret ? -EINVAL : UDRONE_DATAREPLY;
}

static struct udrone_registry ubus_handler[] =
{
	{ .flags = UDRONE_HANDLER_ATOMIC, .type = "ubus", .handler = handler_ubus},
	{ 0 }
};

static struct udrone_module ubus = {
	.registry = ubus_handler,
};
UDRONE_MODULE_REGISTER(ubus)
