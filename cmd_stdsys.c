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

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>

#include "udrone.h"

static int
handler_sysinfo(struct blob_attr **msg)
{
	struct sysinfo si;

	sysinfo(&si);

	blobmsg_add_u32(&udrone.out, "uptime", si.uptime);
	blobmsg_add_double(&udrone.out, "load1", si.loads[0] / 256.);
	blobmsg_add_double(&udrone.out, "load5", si.loads[1] / 256.);
	blobmsg_add_double(&udrone.out, "load15", si.loads[2] / 256.);
	blobmsg_add_double(&udrone.out, "totalram", si.totalram);
	blobmsg_add_double(&udrone.out, "freeram", si.freeram);
	blobmsg_add_double(&udrone.out, "sharedram", si.sharedram);
	blobmsg_add_double(&udrone.out, "bufferram", si.bufferram);
	blobmsg_add_double(&udrone.out, "totalswap", si.totalswap);
	blobmsg_add_double(&udrone.out, "freeswap", si.freeswap);
	blobmsg_add_u32(&udrone.out, "procs", si.procs);

	return UDRONE_DATAREPLY;
}

static int
handler_readfile(struct blob_attr **msg)
{
#if 0
	const char *path = json_object_get_string(msg->req); // Request Data
	if (!path) return -EINVAL; // Status reply

	int fd = open(path, O_RDONLY | O_NONBLOCK | O_NOCTTY);
	if (fd < 0) return -errno;

	char buffer[4096];
	ssize_t len = read(fd, buffer, sizeof(buffer));
	close(fd);
	if (len < 0) return -errno;

	msg->resp = json_object_new_string_len(buffer, len);
#endif
	return UDRONE_DATAREPLY;
}

static int
handler_comment(struct blob_attr **msg)
{
	if (!msg[MSG_DATA] || (blobmsg_type(msg[MSG_DATA]) != BLOBMSG_TYPE_STRING))
		return -EINVAL;

	printf("%s\n", blobmsg_get_string(msg[MSG_DATA]));

	return 0;
}

static struct udrone_registry stdsys_handler[] =
{
	{ .flags = UDRONE_HANDLER_ATOMIC, .type = "sysinfo", .handler = handler_sysinfo},
	{ .flags = UDRONE_HANDLER_ATOMIC, .type = "readfile", .handler = handler_readfile},
	{ .flags = UDRONE_HANDLER_ATOMIC, .type = "comment", .handler = handler_comment},
	{ 0 }
};

static struct udrone_module stdsys = {
	.registry = stdsys_handler,
};
UDRONE_MODULE_REGISTER(stdsys)
