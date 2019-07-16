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

#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <net/if.h>
#include <syslog.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#include "udrone.h"

static const struct blobmsg_policy msg_policy[__MSG_MAX] = {
	[MSG_TO] = { .name = "to", .type = BLOBMSG_TYPE_STRING },
	[MSG_FROM] = { .name = "from", .type = BLOBMSG_TYPE_STRING },
	[MSG_SEQ] = { .name = "seq", .type = BLOBMSG_TYPE_INT32 },
	[MSG_TYPE] = { .name = "type", .type = BLOBMSG_TYPE_STRING },
	[MSG_DATA] = { .name = "data", .type = BLOBMSG_TYPE_UNSPEC },
};

enum {
        ASSIGN_GROUP = 0,
	ASSIGN_SEQ,
	__ASSIGN_MAX
};

static const struct blobmsg_policy assign_policy[__ASSIGN_MAX] = {
	[ASSIGN_GROUP] = { .name = "group", .type = BLOBMSG_TYPE_STRING },
	[ASSIGN_SEQ] = { .name = "seq", .type = BLOBMSG_TYPE_INT32 },
};

static struct sockaddr_in addr = {
	.sin_addr = {INADDR_ANY},
	.sin_family = AF_INET
};

struct udrone_ctx udrone = { 0 };
static struct udrone_module *modules = NULL;

static void
udrone_reset(char *grp)
{
	uloop_timeout_cancel(&udrone.timeout);
	udrone.assigned = 0;
	memset(udrone.group, 0, sizeof(udrone.group));
	strcpy(udrone.group, grp);
	if (udrone.worker.pending) {
		uloop_process_delete(&udrone.worker);
		kill(udrone.worker.pid, SIGTERM);
	}
}

static void
udrone_timeout_default(struct uloop_timeout *t)
{
        udrone_reset(UDRONE_GROUP_DEFAULT);
}

static void
udrone_timeout(struct uloop_timeout *t)
{
	udrone.timeout.cb = udrone_timeout_default;
	udrone_reset(UDRONE_GROUP_LOST);
	uloop_timeout_set(&udrone.timeout, UDRONE_GROUP_TIMEOUT * 1000);
}

static void
udrone_reset_timer(void)
{
	udrone.timeout.cb = udrone_timeout;
	uloop_timeout_set(&udrone.timeout, UDRONE_GROUP_TIMEOUT * 1000);
}

void
udrone_register(struct udrone_module *module)
{
	module->next = modules;
	modules = module;
}

void
udrone_prepare(struct blob_attr **tb, char *type)
{
	blob_buf_init(&udrone.out, 0);
	blobmsg_add_string(&udrone.out, "to", blobmsg_get_string(tb[MSG_FROM]));
	blobmsg_add_string(&udrone.out, "from", udrone.uniqueid);
	blobmsg_add_u32(&udrone.out, "seq", blobmsg_get_u32(tb[MSG_SEQ]));
	blobmsg_add_string(&udrone.out, "type", type);
}

static void
udrone_prepare_status(struct blob_attr **tb, int code)
{
	void *c;

	udrone_prepare(tb, "status");

	if (code >= 0) {
		c = blobmsg_open_table(&udrone.out, "data");
		blobmsg_add_u32(&udrone.out, "code", code);
		if (code)
			blobmsg_add_string(&udrone.out, "errstr", strerror(code));
		blobmsg_close_table(&udrone.out, c);
	}
}

static void
udrone_prepare_ctrl(struct blob_attr **tb, int code)
{
	void *c;

	udrone_prepare(tb, "status");

	if (code >= 0) {
		c = blobmsg_open_table(&udrone.out, "data");
		blobmsg_add_string(&udrone.out, "board", udrone.board);
		blobmsg_add_u32(&udrone.out, "code", code);
		if (code)
			blobmsg_add_string(&udrone.out, "errstr", strerror(code));
		blobmsg_close_table(&udrone.out, c);
	}
}

static void
udrone_prepare_accept(struct blob_attr **tb)
{
	udrone_prepare(tb, "accept");
}

static void
udrone_send(struct sockaddr_in addr)
{
	char *buf = blobmsg_format_json(udrone.out.head, 1);

	fprintf(stderr, "send\t%s\n", buf);
	sendto(udrone.sock.fd, buf, strlen(buf), 0,
		(struct sockaddr*)&addr, sizeof(addr));
	free(buf);
}

static void
udrone_worker_cb(struct uloop_process *c, int ret)
{
	fprintf(stderr, "send\t%s\n", udrone.worker_buf);
	sendto(udrone.sock.fd, udrone.worker_buf, strlen(udrone.worker_buf), 0,
		(struct sockaddr*)&udrone.worker_addr, sizeof(udrone.worker_addr));
}

static void
udrone_msg_cmd(struct blob_attr **msg)
{
	struct udrone_registry *reg = NULL;
	struct udrone_module *m;
	char *type = blobmsg_get_string(msg[MSG_TYPE]);
	int stat;
	void *c;

	for (m = modules; !reg && m; m = m->next) {
		struct udrone_registry *r;

		for (r = m->registry; r->handler; r++) {
			if (!strcmp(r->type, type)) {
				reg = r;
				break;
			}
		}
	}

	udrone_prepare(msg, type);
	c = blobmsg_open_table(&udrone.out, "data");
	if (!reg || !reg->handler) {
		/* No handler */
		stat = -ENOTSUP;
	} else if (reg->flags & UDRONE_HANDLER_ATOMIC) {
		/* Atomic handler */
		stat = reg->handler(msg);
	} else {
		if (!(udrone.worker.pid = fork())) {
			close(udrone.sock.fd);
			stat = reg->handler(msg);
			if (stat <= 0)
				udrone_prepare_status(msg, -stat);
			else
				blobmsg_close_table(&udrone.out, c);
			strncpy(udrone.worker_buf, blobmsg_format_json(udrone.out.head, 1), UDRONE_MAX_DGRAM);
			exit(stat);
		}
		uloop_process_add(&udrone.worker);
		udrone_prepare_accept(msg);
		return;
	}
	if (stat <= 0)
		udrone_prepare_status(msg, -stat);
	else
		blobmsg_close_table(&udrone.out, c);

}

static int
udrone_msg_ctrl(struct blob_attr **msg)
{
	char *type = blobmsg_get_string(msg[MSG_TYPE]);
	struct blob_attr *tb[__ASSIGN_MAX];

	if (!strcmp(type, "!whois")) {
		if (msg[MSG_DATA] &&
		    (blobmsg_type(msg[MSG_DATA]) == BLOBMSG_TYPE_STRING) &&
		    strcmp(udrone.board, blobmsg_get_string(msg[MSG_DATA])))
			return -ENOTSUP;
		return 0;
	}

	if (!strcmp(type, "!assign")) {
		if (!msg[MSG_DATA] || (blobmsg_type(msg[MSG_DATA]) != BLOBMSG_TYPE_TABLE))
			return -EINVAL;

		blobmsg_parse(assign_policy, __ASSIGN_MAX, tb, blobmsg_data(msg[MSG_DATA]), blobmsg_len(msg[MSG_DATA]));

		if (!tb[ASSIGN_GROUP] || !strcmp(blobmsg_get_string(tb[ASSIGN_GROUP]), UDRONE_GROUP_DEFAULT))
			return -EINVAL;

		strcpy(udrone.group, blobmsg_get_string(tb[ASSIGN_GROUP]));

		if (tb[ASSIGN_SEQ])
			udrone.assigned = blobmsg_get_u32(tb[ASSIGN_SEQ]);

		udrone_reset_timer();
		return 0;
	}

	if (!strcmp(type, "!reset")) {
		udrone_reset(UDRONE_GROUP_DEFAULT);
		return 0;
	}

	return -ENOTSUP;
}

static int
udrone_read(struct blob_attr **tb, struct sockaddr_in *sender)
{
	socklen_t addrlen = sizeof(*sender);
	char data[UDRONE_MAX_DGRAM];
	char addr[16] = {0};
	ssize_t len;

	len = recvfrom(udrone.sock.fd, data, sizeof(data) - 1, MSG_TRUNC | MSG_DONTWAIT,
		       (struct sockaddr*)sender, &addrlen);

	if (len == -1 && errno == EAGAIN)
		return 0;
	if (len < 16 || len >= sizeof(data))
		return -1;
	data[len] = 0;

	if (data[0] != '{')
		return -1;

	fprintf(stderr, "recv \t%s\n", data);

	blob_buf_init(&udrone.in, 0);
	if (!blobmsg_add_json_from_string(&udrone.in, data))
		return -1;

	blobmsg_parse(msg_policy, __MSG_MAX, tb, blob_data(udrone.in.head), blob_len(udrone.in.head));

	if (!tb[MSG_TO] || !tb[MSG_FROM] || !tb[MSG_TYPE])
		return -1;

	strncpy(addr, blobmsg_get_string(tb[MSG_TO]), sizeof(addr));

	if (strcmp(addr, udrone.uniqueid) && strcmp(addr, udrone.group))
		return -1;

	return 1;
}

static void
udrone_read_cb(struct uloop_fd *u, unsigned int events)
{
	struct blob_attr *tb[__MSG_MAX] = { 0 };
	struct sockaddr_in addr;
	int status;

	while ((status = udrone_read(tb, &addr))) {
		char *type;
		int seq;

		if (status <= 0)
			continue;

		type = blobmsg_get_string(tb[MSG_TYPE]);
		seq = blobmsg_get_u32(tb[MSG_SEQ]);
		if (type[0] == '!') {
			/* Control messages */
			int ret = udrone_msg_ctrl(tb);

			if (ret < 0)
				continue;
			udrone_prepare_ctrl(tb, -ret);
			if (udrone.assigned)
				udrone_reset_timer();
		} else if (seq == udrone.assigned) {
			/* Resend lost message */
			if (udrone.worker.pending) {
				udrone_prepare_accept(tb);
			}
			udrone_reset_timer();
		} else if (seq != udrone.assigned + 1) {
			/* Out of sync */
			udrone_prepare_status(tb, ESRCH);
			udrone_timeout(&udrone.timeout);
		} else if (udrone.worker.pending) {
			/* Busy */
			udrone_prepare_status(tb, EBUSY);
		} else {
			/* New command */
			udrone_msg_cmd(tb);
			udrone.worker_addr = addr;
			udrone.assigned++;
			udrone_reset_timer();
		}

		udrone_send(addr);
	}
}

static void
udrone_generate_id(void)
{
	const char hexdigits[] = "0123456789abcdef";
	struct ifreq ifr = {.ifr_name = ""};
	char *c = udrone.uniqueid;
	uint8_t *a;
	size_t i;

	strncpy(ifr.ifr_name, udrone.ifname, sizeof(ifr.ifr_name));
	ioctl(udrone.sock.fd, SIOCGIFHWADDR, &ifr);
	a = (uint8_t*)ifr.ifr_hwaddr.sa_data;
	for (i = 0; i < 6; i++) {
		*c++ = hexdigits[a[i] >> 4];
		*c++ = hexdigits[a[i] & 0x0f];
	}
	syslog(LOG_INFO, "Unique ID set to: %.16s", udrone.uniqueid);
}

static void
udrone_socket(void)
{
	struct ifreq ifr = {.ifr_name = ""};
	struct ip_mreqn imr = {
		.imr_address = {INADDR_ANY},
	};
	int one = 1;

	udrone.sock.fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (udrone.sock.fd < 0) {
		syslog(LOG_ERR, "Failed to open socket\n");
		exit(EXIT_FAILURE);
	}
	udrone.sock.cb = udrone_read_cb;
	fcntl(udrone.sock.fd, F_SETOWN, getpid());
	fcntl(udrone.sock.fd, F_SETFL, fcntl(udrone.sock.fd, F_GETFL) | O_NONBLOCK);
	setsockopt(udrone.sock.fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	addr.sin_port = htons(UDRONE_PORT);
	if (bind(udrone.sock.fd, (struct sockaddr*)&addr, sizeof(addr))) {
		syslog(LOG_ERR, "Failed to bind socket\n");
		exit(EXIT_FAILURE);
	}
	inet_pton(AF_INET, UDRONE_ADDR, &addr.sin_addr);
	strncpy(ifr.ifr_name, udrone.ifname, sizeof(ifr.ifr_name) - 1);
	if (ioctl(udrone.sock.fd, SIOCGIFINDEX, &ifr)) {
		syslog(LOG_ERR, "Unable to identify interface %s: %s",
			udrone.ifname, strerror(errno));
		exit(EXIT_FAILURE);
	}
	imr.imr_ifindex = ifr.ifr_ifindex;
	imr.imr_multiaddr = addr.sin_addr;
	if (setsockopt(udrone.sock.fd, SOL_IP, IP_ADD_MEMBERSHIP, &imr, sizeof(imr)) ||
	    setsockopt(udrone.sock.fd, SOL_SOCKET, SO_BINDTODEVICE, udrone.ifname, strlen(udrone.ifname))) {
		syslog(LOG_ERR, "Failed to setup multicast %s: %s",
			UDRONE_ADDR, strerror(errno));
		exit(EXIT_FAILURE);
	}
	uloop_fd_add(&udrone.sock, ULOOP_READ);
}

static void
ubus_connect_handler(struct ubus_context *ctx)
{

}

int
main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "udrone - Multicast drone client\n\nUsage: %s <interface>\n", *argv);
		return EXIT_FAILURE;
	}

	if (argc > 2)
		strncpy(udrone.board, argv[2], sizeof(udrone.board));
	else
		strncpy(udrone.board, "generic", sizeof(argv[2]));

	udrone.worker_buf = mmap(NULL, UDRONE_MAX_DGRAM, PROT_WRITE | PROT_READ,
				 MAP_SHARED | MAP_ANONYMOUS, -1, 0);

        if (udrone.worker_buf == MAP_FAILED) {
		syslog(LOG_ERR, "Unable to create memory map: %s", strerror(errno));
		return EXIT_FAILURE;
	}

	udrone.ifname = argv[1];
	udrone.worker.cb = udrone_worker_cb;

	uloop_init();
	udrone.ubus.cb = ubus_connect_handler;
        ubus_auto_connect(&udrone.ubus);

	udrone_reset(UDRONE_GROUP_DEFAULT);
	udrone_socket();
	udrone_generate_id();
	udrone_reset_timer();
	uloop_run();
	uloop_done();
	ubus_auto_shutdown(&udrone.ubus);

	munmap(udrone.worker_buf, UDRONE_MAX_DGRAM);
	close(udrone.sock.fd);

	return 0;
}
