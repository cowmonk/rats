/* slog: simple logging daemon */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE /* needed for PATH_MAX, to, and from */
#endif

#include "arg.h"
#include "util.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void
usage(void)
{
	eprintf("usage: %s [-d dir] [-s bytes] [-n count]\n", argv0);
}

static void
mkdir_p(const char *path)
{
	char tmp[PATH_MAX];
	char *p;
	size_t len;

	strlcpy(tmp, path, sizeof(tmp));
	len = strlen(tmp);
	if (len > 0 && tmp[len - 1] == '/')
		tmp[len - 1] = '\0';

	for (p = tmp + 1; *p; p++) {
		if (*p == '/') {
			*p = '\0';
			if (mkdir(tmp, 0755) < 0 && errno != EEXIST)
				eprintf("mkdir %s:", tmp);
			*p = '/';
		}
	}
	if (mkdir(tmp, 0755) < 0 && errno != EEXIST)
		eprintf("mkdir %s:", tmp);
}

int
main(int argc, char *argv[])
{
	char *dir;
	char cur[PATH_MAX], from[PATH_MAX], to[PATH_MAX];
	long maxsize, maxfiles;
	int fd, i, n;
	size_t total;
	char buf[4096];

	dir = ".";
	maxsize = 1048576;
	maxfiles = 10;

	ARGBEGIN {
	case 'd': dir = EARGF(usage()); break;
	case 's': maxsize = estrtonum(EARGF(usage()), 1, LONG_MAX); break;
	case 'n': maxfiles = estrtonum(EARGF(usage()), 1, 1000); break;
	default:  usage();
	} ARGEND

	mkdir_p(dir);

	snprintf(cur, sizeof(cur), "%s/current", dir);
	fd = open(cur, O_WRONLY | O_CREAT | O_APPEND, 0644);
	if (fd < 0)
		eprintf("open %s:", cur);

	total = 0;
	while ((n = read(STDIN_FILENO, buf, sizeof(buf))) > 0) {
		if (writeall(fd, buf, n) < 0)
			eprintf("write %s:", cur);
		total += n;
		if (total < (size_t)maxsize)
			continue;

		/* rotate: delete oldest, shift others, current -> 0 */
		close(fd);
		snprintf(to, sizeof(to), "%s/%ld", dir, maxfiles - 1);
		unlink(to);
		for (i = maxfiles - 2; i >= 0; i--) {
			snprintf(from, sizeof(from), "%s/%d", dir, i);
			snprintf(to, sizeof(to), "%s/%d", dir, i + 1);
			if (rename(from, to) < 0 && errno != ENOENT)
				weprintf("rename %s -> %s:", from, to);
		}
		snprintf(to, sizeof(to), "%s/0", dir);
		if (rename(cur, to) < 0)
			weprintf("rename %s -> %s:", cur, to);

		fd = open(cur, O_WRONLY | O_CREAT | O_APPEND, 0644);
		if (fd < 0)
			eprintf("open %s:", cur);
		total = 0;
	}

	close(fd);
	return 0;
}
