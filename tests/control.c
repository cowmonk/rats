#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

int
main(int argc, char *argv[])
{
	int fd;
	struct sockaddr_un addr;
	struct stat st;
	struct sigaction sa;

	if (argc == 1) {
		for (fd = 3; fd < 256; fd++) {
			errno = 0;
			if (fcntl(fd, F_GETFD) >= 0 || errno != EBADF) {
				printf("inherited descriptor: %d\n", fd);
				return 1;
			}
		}
		if (sigaction(SIGPIPE, NULL, &sa) < 0 || sa.sa_handler != SIG_DFL) {
			puts("SIGPIPE disposition was not restored");
			return 1;
		}
		puts("OK: no supervisor descriptors inherited");
		return 0;
	}
	if (argc != 2)
		return 1;
	if (strcmp(argv[1], "-mode") == 0)
		return stat(SOCK_PATH, &st) < 0 || (st.st_mode & 0777) != 0600;
	if (strcmp(argv[1], "-disconnect") != 0)
		return 1;

	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0)
		return 1;
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	if (strlen(SOCK_PATH) >= sizeof(addr.sun_path))
		return 1;
	memcpy(addr.sun_path, SOCK_PATH, sizeof(SOCK_PATH));
	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
		return 1;
	if (write(fd, "s\n", 2) != 2)
		return 1;
	shutdown(fd, SHUT_RDWR);
	close(fd);
	return 0;
}
