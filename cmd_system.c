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
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>

static int
execl_redir_stdout(char *buf, int len, const char *path, ...)
{
	int pipe_fd[2];
	int pid;
	int ret;
	int status;
	int loop = 30;
	if (len == 1)
		return 0;
	len--;
	if (pipe(pipe_fd)) {
		printf("pipe failed\n");
		return 0;
	}
	pid = fork();
	if (!pid) {
		int i;
		va_list arg;
		char **argv;

		close(pipe_fd[0]);
		dup2(pipe_fd[1], STDOUT_FILENO);
		va_start(arg, path);
		for(i = 0; va_arg(arg, char*) != 0; i++);
		va_end(arg);
		argv = malloc((i+2)*sizeof(char*));
		if(argv == 0)
			exit(0);
		argv[0] = (char*)path;
		va_start(arg, path);
		for (i = 1; (argv[i] = va_arg(arg, char*)) != 0; i++);
		va_end(arg);
		execv(path, argv);
		free(argv);
		exit(0);
	}
	close(pipe_fd[1]);
	buf[len] = '\0';

	while (loop) {
		if(waitpid(pid, &status, WNOHANG) == pid)
			break;
		printf("waiting on child ...\n");
		sleep(1);
		loop--;
	}
	if (WEXITSTATUS(status) == 0 && loop > 0) {
		ret = read(pipe_fd[0], buf, len);
		if(ret > 0)
			buf[ret] = '\0';
	} else {
		kill(pid, SIGKILL);
		ret = 0;
	}
	close(pipe_fd[0]);
	return ret;
}

enum {
	SYSTEM_CMD = 0,
	SYSTEM_STDIN,
	__SYSTEM_MAX
};

static const struct blobmsg_policy system_policy[__SYSTEM_MAX] = {
	[SYSTEM_CMD] = { .name = "cmd", .type = BLOBMSG_TYPE_ARRAY },
	[SYSTEM_STDIN] = { .name = "stdin", .type = BLOBMSG_TYPE_ARRAY },
};

static int
handler_system(struct blob_attr **msg)
{
	char buf[8193] = { 0 };
	struct blob_attr *tb[__SYSTEM_MAX];

	if (!msg[MSG_DATA] || (blobmsg_type(msg[MSG_DATA]) != BLOBMSG_TYPE_TABLE))
		return -EINVAL;

	blobmsg_parse(system_policy, __SYSTEM_MAX, tb, blobmsg_data(msg[MSG_DATA]), blobmsg_len(msg[MSG_DATA]));
	if (!execl_redir_stdout(buf, 8192, blobmsg_get_string(msg[MSG_DATA])))
		return -EIO;

	blobmsg_add_string(&udrone.out, "stdout", buf);

	return UDRONE_DATAREPLY;
}

static struct udrone_registry system_handler[] =
{
	{ .type = "system", .handler = handler_system },
	{ 0 }
};

static struct udrone_module sys = {
	.registry = system_handler,
};
UDRONE_MODULE_REGISTER(sys)
