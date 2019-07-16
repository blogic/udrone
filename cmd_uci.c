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

#include <uci.h>

#include "udrone.h"

static struct blob_buf b;

enum {
	UCI_CONFIG = 0,
	UCI_SECTION,
	UCI_TYPE,
	__UCI_MAX
};

static const struct blobmsg_policy uci_policy[__UCI_MAX] = {
	[UCI_CONFIG] = { .name = "config", .type = BLOBMSG_TYPE_STRING },
	[UCI_SECTION] = { .name = "section", .type = BLOBMSG_TYPE_STRING },
	[UCI_TYPE] = { .name = "type", .type = BLOBMSG_TYPE_STRING },
};

static int
handler_uci_set(struct blob_attr **msg)
{
	char *tmpfile = tmpnam(NULL);
	FILE *fp = fopen(tmpfile, "w+b");
	struct blob_attr *cur;
	int lines = 0;
	int rem;

	if (!msg[MSG_DATA] || (blobmsg_type(msg[MSG_DATA]) != BLOBMSG_TYPE_TABLE))
		return -EINVAL;

	blobmsg_for_each_attr(cur, msg[MSG_DATA], rem) {
		const char *config = blobmsg_name(cur);
		struct blob_attr *cur2;
		int rem2;

		if (blobmsg_type(cur) != BLOBMSG_TYPE_TABLE)
			continue;

		blobmsg_for_each_attr(cur2, cur, rem2) {
			const char *section = blobmsg_name(cur2);
			struct blob_attr *cur3;
			int rem3;

			if (blobmsg_type(cur2) != BLOBMSG_TYPE_TABLE)
				continue;

			blobmsg_for_each_attr(cur3, cur2, rem3) {
				const char *key = blobmsg_name(cur3);
				const char *val;

				if (blobmsg_type(cur3) != BLOBMSG_TYPE_STRING)
					continue;
				val  = blobmsg_get_string(cur3);
				fprintf(stderr, "%s:%s[%d]%s %s\n", __FILE__, __func__, __LINE__, key, val);
				fprintf(fp, "set %s.%s.%s=%s\n", config, section, key, val);
				lines++;
			}
		}
	}

	fclose(fp);
//	unlink(tmpfile);

	return 0;
}

static int
handler_uci_get(struct blob_attr **msg)
{
	char *type = NULL, *section = NULL;
	struct blob_attr *tb[__UCI_MAX];
        struct uci_package *pkg = NULL;
	struct uci_context *uci;
	struct uci_element *_s;
	int ret = UDRONE_DATAREPLY;

	if (!msg[MSG_DATA] || (blobmsg_type(msg[MSG_DATA]) != BLOBMSG_TYPE_TABLE))
		return -EINVAL;

	blobmsg_parse(uci_policy, __UCI_MAX, tb, blobmsg_data(msg[MSG_DATA]), blobmsg_len(msg[MSG_DATA]));
	if (!tb[UCI_CONFIG])
		return -EINVAL;

	if (tb[UCI_SECTION])
		section = blobmsg_get_string(tb[UCI_SECTION]);

	if (tb[UCI_TYPE])
		type = blobmsg_get_string(tb[UCI_TYPE]);

	uci = uci_alloc_context();
	if (uci_load(uci, blobmsg_get_string(tb[UCI_CONFIG]), &pkg)) {
		ret = -ENOENT;
		goto err_out;
	}

	blob_buf_init(&b, 0);
	uci_foreach_element(&pkg->sections, _s) {
		struct uci_section *s = uci_to_section(_s);
		struct uci_element *_o;
		void *c;

		if (type && strcmp(s->type, type))
			continue;

		if (section && strcmp(s->e.name, section))
			continue;

		c = blobmsg_open_table(&udrone.out, s->e.name);
		blobmsg_add_string(&udrone.out, ".type", s->type);

		uci_foreach_element(&s->options, _o) {
		        struct uci_option *o = uci_to_option(_o);

			if (o->type == UCI_TYPE_STRING)
				blobmsg_add_string(&udrone.out, _o->name, o->v.string);
			else if (o->type == UCI_TYPE_LIST) {
				void *l = blobmsg_open_array(&udrone.out, _o->name);
				struct uci_element *_l;

				uci_foreach_element(&o->v.list, _l)
					blobmsg_add_string(&udrone.out, NULL, _l->name);

				blobmsg_close_array(&udrone.out, l);
			}
		}
		blobmsg_close_table(&udrone.out, c);
	}

err_out:
	if (pkg)
		uci_unload(uci, pkg);
	uci_free_context(uci);
	return ret;
}

static struct udrone_registry uci_handler[] =
{
	{ .flags = UDRONE_HANDLER_ATOMIC, .type = "uci_get", .handler = handler_uci_get},
	{ .flags = UDRONE_HANDLER_ATOMIC, .type = "uci_set", .handler = handler_uci_set},
	{ 0 }
};

static struct udrone_module uci = {
	.registry = uci_handler,
};
UDRONE_MODULE_REGISTER(uci)
