/*
 * Copyright (C) 2013-2020 Canonical, Ltd.
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

static const stress_help_t help[] = {
	{ "R",	"rename N",	"start N workers exercising file renames" },
	{ NULL,	"rename-ops N",	"stop after N rename bogo operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(HAVE_RENAMEAT) &&	\
    defined(O_DIRECTORY)
/*
 *  exercise_renameat2()
 *	exercise renameat2 with various illegal argument combinations
 */
static int exercise_renameat(
	const stress_args_t *args,
	char *old_name,
	char *new_name,
	const int bad_fd)
{
	int ret, file_fd;
	char *oldname = old_name, *newname = new_name;

	/* Exercise on bad_fd */
	ret = renameat(bad_fd, oldname, AT_FDCWD, newname);
	if (ret >= 0) {
		pr_fail("%s: renameat unexpectedly succeeded on a bad file "
			"descriptor, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return -1;
	}

	/* Exercise on file fd */
	file_fd = open(oldname, O_RDONLY);
	if (file_fd < 0)
		return -1;
	ret = renameat(file_fd, oldname, AT_FDCWD, newname);
	if (ret >= 0) {
		pr_fail("%s: renameat unexpectedly succeeded on a file "
			"descriptor rather than a directory descriptor, "
			"errno=%d (%s)\n",
			 args->name, errno, strerror(errno));
		(void)close(file_fd);
		return -1;
	}
	(void)close(file_fd);

	return 0;
}
#endif

#if defined(HAVE_RENAMEAT2) &&	\
    defined(O_DIRECTORY) &&	\
    defined(RENAME_NOREPLACE)
/*
 *  exercise_renameat2()
 *	exercise renameat2 with various illegal argument combination
 */
static void exercise_renameat2(
	const stress_args_t *args,
	int oldfd,
	char *old_name,
	char *new_name,
	const int bad_fd)
{
	int ret, newfd = AT_FDCWD, tmpfd, file_fd;
	char *tempname, *oldname = old_name, *newname = new_name;

	/* Exercise with invalid flags */
	ret = renameat2(oldfd, oldname, newfd, newname, ~0);
	if (ret >= 0) {
		tmpfd = newfd;
		newfd = oldfd;
		oldfd = tmpfd;
		tempname = newname;
		newname = oldname;
		oldname = tempname;
	}

#if defined(RENAME_EXCHANGE)
	/* Exercise with invalid combination of flags */
	ret = renameat2(oldfd, oldname, newfd, newname,
		RENAME_EXCHANGE | RENAME_NOREPLACE);
	if (ret >= 0) {
		tmpfd = newfd;
		newfd = oldfd;
		oldfd = tmpfd;
		tempname = newname;
		newname = oldname;
		oldname = tempname;
	}

#if defined(RENAME_WHITEOUT)
	ret = renameat2(oldfd, oldname, newfd, newname,
		RENAME_EXCHANGE | RENAME_WHITEOUT);
	if (ret >= 0) {
		tmpfd = newfd;
		newfd = oldfd;
		oldfd = tmpfd;
		tempname = newname;
		newname = oldname;
		oldname = tempname;
	}
#endif

	/* Exercise RENAME_EXCHANGE on non-existed directory */
	ret = renameat2(oldfd, oldname, newfd, newname, RENAME_EXCHANGE);
	if (ret >= 0) {
		pr_fail("%s: renameat2 unexpectedly succeeded on "
			"non-existent directory with RENAME_EXCHANGE "
			"flag, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return;
	}

	/*
	 * Exercise RENAME_EXCHANGE on same file creating absolutely
	 * no difference other than increase in kernel test coverage
	 */
	ret = renameat2(oldfd, oldname, oldfd, oldname,
		RENAME_EXCHANGE);
	(void)ret;

#endif

	/* Exercise RENAME_NOREPLACE on same file */
	ret = renameat2(oldfd, oldname, oldfd, oldname, RENAME_NOREPLACE);
	if (ret >= 0) {
		pr_fail("%s: renameat2 unexpectedly succeeded on existent "
			"directory/file with RENAME_NOREPLACE flag, "
			"errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return;
	}

	/* Exercise on bad_fd */
	ret = renameat2(bad_fd, oldname, newfd, newname, RENAME_NOREPLACE);
	if (ret >= 0) {
		pr_fail("%s: renameat2 unexpectedly succeeded on bad file "
			"descriptor, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return;
	}

	/* Exercise on file fd */
	file_fd = open(oldname, O_RDONLY);
	if (file_fd < 0)
		return;
	ret = renameat2(file_fd, oldname, newfd, newname, RENAME_NOREPLACE);
	if (ret >= 0) {
		pr_fail("%s: renameat2 unexpectedly succeeded on file "
			"descriptor rather than" "directory descriptor, "
			"errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		(void)close(file_fd);
		return;
	}
	(void)close(file_fd);
}
#endif

/*
 *  stress_rename()
 *	stress system by renames
 */
static int stress_rename(const stress_args_t *args)
{
	char name1[PATH_MAX], name2[PATH_MAX];
	char *oldname = name1, *newname = name2, *tmpname;
	FILE *fp;
	uint64_t i = 0;
	const uint32_t inst1 = args->instance * 2;
	const uint32_t inst2 = inst1 + 1;
	const int bad_fd = stress_get_bad_fd();

	if (stress_temp_dir_mk(args->name, args->pid, inst1) < 0)
		return EXIT_FAILURE;
	if (stress_temp_dir_mk(args->name, args->pid, inst2) < 0) {
		(void)stress_temp_dir_rm(args->name, args->pid, inst1);
		return EXIT_FAILURE;
	}
restart:
	(void)stress_temp_filename(oldname, PATH_MAX,
		args->name, args->pid, inst1, i++);

	if ((fp = fopen(oldname, "w+")) == NULL) {
		int rc = exit_status(errno);
		pr_err("%s: fopen failed: errno=%d: (%s)\n",
			args->name, errno, strerror(errno));
		(void)stress_temp_dir_rm(args->name, args->pid, inst1);
		(void)stress_temp_dir_rm(args->name, args->pid, inst2);
		return rc;
	}
	(void)fclose(fp);

	for (;;) {
		(void)stress_temp_filename(newname, PATH_MAX,
			args->name, args->pid, inst2, i++);
		if (rename(oldname, newname) < 0) {
			(void)unlink(oldname);
			(void)unlink(newname);
			goto restart;
		}

		tmpname = oldname;
		oldname = newname;
		newname = tmpname;
		inc_counter(args);
		if (!keep_stressing())
			break;

		(void)stress_temp_filename(newname, PATH_MAX,
			args->name, args->pid, inst1, i++);
		if (rename(oldname, newname) < 0) {
			(void)unlink(oldname);
			(void)unlink(newname);
			goto restart;
		}

		tmpname = oldname;
		oldname = newname;
		newname = tmpname;
		inc_counter(args);
		if (!keep_stressing())
			break;

#if defined(HAVE_RENAMEAT) &&	\
    defined(O_DIRECTORY)
		{
			int oldfd;
			int newfd = AT_FDCWD;

			(void)stress_temp_filename(newname, PATH_MAX,
				args->name, args->pid, inst1, i++);

			oldfd = open(".", O_RDONLY | O_DIRECTORY);
			if (oldfd < 0) {
				(void)unlink(oldname);
				(void)unlink(newname);
				goto restart;
			}

			if (exercise_renameat(args, oldname, newname, bad_fd) < 0) {
				(void)close(oldfd);
				(void)unlink(oldname);
				(void)unlink(newname);
				goto restart;
			}

			if (renameat(oldfd, oldname, newfd, newname) < 0) {
				(void)close(oldfd);
				(void)unlink(oldname);
				(void)unlink(newname);
				goto restart;
			}
			(void)shim_fsync(oldfd);
			(void)close(oldfd);
			tmpname = oldname;
			oldname = newname;
			newname = tmpname;

			inc_counter(args);
			if (!keep_stressing())
				break;
		}
#endif

#if defined(HAVE_RENAMEAT2) &&	\
    defined(O_DIRECTORY) &&	\
    defined(RENAME_NOREPLACE)
		{
			int oldfd;

			(void)stress_temp_filename(newname, PATH_MAX,
				args->name, args->pid, inst1, i++);

			oldfd = open(".", O_RDONLY | O_DIRECTORY);
			if (oldfd < 0) {
				(void)unlink(oldname);
				(void)unlink(newname);
				goto restart;
			}

			exercise_renameat2(args, oldfd, oldname, newname, bad_fd);

			if (renameat2(oldfd, oldname, AT_FDCWD, newname, RENAME_NOREPLACE) < 0) {
				(void)close(oldfd);
				(void)unlink(oldname);
				(void)unlink(newname);
				goto restart;
			}
			(void)close(oldfd);
			tmpname = oldname;
			oldname = newname;
			newname = tmpname;

			inc_counter(args);
			if (!keep_stressing())
				break;
		}
#endif
	}

	(void)unlink(oldname);
	(void)unlink(newname);
	(void)stress_temp_dir_rm(args->name, args->pid, inst1);
	(void)stress_temp_dir_rm(args->name, args->pid, inst2);

	return EXIT_SUCCESS;
}

stressor_info_t stress_rename_info = {
	.stressor = stress_rename,
	.class = CLASS_FILESYSTEM | CLASS_OS,
	.help = help
};
