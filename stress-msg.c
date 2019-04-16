/*
 * Copyright (C) 2013-2019 Canonical, Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * This code is a complete clean re-write of the stress tool by
 * Colin Ian King <colin.king@canonical.com> and attempts to be
 * backwardly compatible with the stress tool by Amos Waterland
 * <apw@rossby.metr.ou.edu> but has more stress tests and more
 * functionality.
 *
 */
#include "stress-ng.h"

#if defined(HAVE_SYS_IPC_H) &&	\
    defined(HAVE_SYS_MSG_H) &&	\
    defined(HAVE_MQ_SYSV)

#define MAX_SIZE	(8)
#define MSG_STOP	"STOPMSG"

typedef struct {
	long mtype;
	char msg[MAX_SIZE];
} msg_t;

static int stress_msg_getstats(const args_t *args, const int msgq_id)
{
	struct msqid_ds buf;

	if (msgctl(msgq_id, IPC_STAT, &buf) < 0) {
		pr_fail_err("msgctl: IPC_STAT");
		return -errno;
	}
#if defined(IPC_INFO) &&	\
    defined(HAVE_MSG_INFO)
	{
		struct msginfo info;

		if (msgctl(msgq_id, IPC_INFO, (struct msqid_ds *)&info) < 0) {
			pr_fail_err("msgctl: IPC_INFO");
			return -errno;
		}
	}
#endif
#if defined(MSG_INFO) &&	\
    defined(HAVE_MSG_INFO)
	{
		struct msginfo info;

		if (msgctl(msgq_id, MSG_INFO, (struct msqid_ds *)&info) < 0) {
			pr_fail_err("msgctl: MSG_INFO");
			return -errno;
		}
	}
#endif

	return 0;
}

/*
 *  stress_msg
 *	stress by message queues
 */
static int stress_msg(const args_t *args)
{
	pid_t pid;
	int msgq_id;

	msgq_id = msgget(IPC_PRIVATE, S_IRUSR | S_IWUSR | IPC_CREAT | IPC_EXCL);
	if (msgq_id < 0) {
		pr_fail_dbg("msgget");
		return exit_status(errno);
	}
	pr_dbg("%s: System V message queue created, id: %d\n", args->name, msgq_id);

again:
	pid = fork();
	if (pid < 0) {
		if (g_keep_stressing_flag &&
		    ((errno == EAGAIN) || (errno == ENOMEM)))
			goto again;
		pr_fail_dbg("fork");
		return EXIT_FAILURE;
	} else if (pid == 0) {
		(void)setpgid(0, g_pgrp);
		stress_parent_died_alarm();

		while (keep_stressing()) {
			msg_t msg;
			uint64_t i;

			for (i = 0; keep_stressing(); i++) {
				uint64_t v;
				if (msgrcv(msgq_id, &msg, sizeof(msg.msg), 0, 0) < 0) {
					pr_fail_dbg("msgrcv");
					break;
				}
				if (!strcmp(msg.msg, MSG_STOP))
					break;
				if (g_opt_flags & OPT_FLAGS_VERIFY) {
					(void)memcpy(&v, msg.msg, sizeof(v));
					if (v != i)
						pr_fail("%s: msgrcv: expected msg containing 0x%" PRIx64
							" but received 0x%" PRIx64 " instead\n", args->name, i, v);
				}
			}
			_exit(EXIT_SUCCESS);
		}
	} else {
		msg_t msg;
		uint64_t i = 0;
		int status;

		/* Parent */
		(void)setpgid(pid, g_pgrp);

		do {
			(void)memcpy(msg.msg, &i, sizeof(i));
			msg.mtype = 1;
			if (msgsnd(msgq_id, &msg, sizeof(i), 0) < 0) {
				if (errno != EINTR)
					pr_fail_dbg("msgsnd");
				break;
			}
			if ((i & 0x1f) == 0)
				if (stress_msg_getstats(args, msgq_id) < 0)
					break;
			/*
			 *  NetBSD can shove loads of messages onto
			 *  a queue before it blocks, so force
			 *  a scheduling yield every so often so that
			 *  consumer can read them.
			 */
			if ((i & 0xff) == 0)
				(void)shim_sched_yield();
			i++;
			inc_counter(args);
		} while (keep_stressing());

		(void)shim_strlcpy(msg.msg, MSG_STOP, sizeof(msg.msg));
		if (msgsnd(msgq_id, &msg, sizeof(msg.msg), 0) < 0)
			pr_fail_dbg("termination msgsnd");
		(void)kill(pid, SIGKILL);
		(void)shim_waitpid(pid, &status, 0);

		if (msgctl(msgq_id, IPC_RMID, NULL) < 0)
			pr_fail_dbg("msgctl");
		else
			pr_dbg("%s: System V message queue deleted, id: %d\n", args->name, msgq_id);
	}
	return EXIT_SUCCESS;
}

stressor_info_t stress_msg_info = {
	.stressor = stress_msg,
	.class = CLASS_SCHEDULER | CLASS_OS
};
#else
stressor_info_t stress_msg_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_SCHEDULER | CLASS_OS
};
#endif
