/* svc: control client for ssv */

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "arg.h"
#include "util.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#ifndef SOCK_PATH
#define SOCK_PATH "/run/serva.sock"
#endif

static void
usage(void)
{
	eprintf("usage: %s [-udrkts] [-a] [service...]\n", argv0);
}

static int
request(int action, const char *name)
{
	int sock, len;
	struct sockaddr_un addr;
	char buf[4096], resp[4096], prefix[5];
	ssize_t n;
	size_t have = 0, chunk;

	if (strchr(name, '\n') || strchr(name, '\r'))
		eprintf("invalid service name\n");
	len = snprintf(buf, sizeof(buf), "%c %s\n", action, name);
	if (len < 0 || (size_t)len >= sizeof(buf))
		eprintf("command too long\n");

	/* connect to ssv control socket */
	sock = socket(AF_UNIX, SOCK_STREAM, 0);
	if (sock < 0)
		eprintf("socket:");
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	if (strlcpy(addr.sun_path, SOCK_PATH, sizeof(addr.sun_path)) >= sizeof(addr.sun_path))
		eprintf("socket path too long: %s\n", SOCK_PATH);
	if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
		eprintf("connect %s:", SOCK_PATH);

	/* send command, print response */
	if (writeall(sock, buf, len) != len)
		eprintf("write socket:");
	if (shutdown(sock, SHUT_WR) < 0)
		eprintf("shutdown socket:");
	for (;;) {
		n = read(sock, resp, sizeof(resp));
		if (n < 0 && errno == EINTR)
			continue;
		if (n < 0)
			eprintf("read socket:");
		if (n == 0)
			break;
		chunk = MIN((size_t)n, sizeof(prefix) - have);
		memcpy(prefix + have, resp, chunk);
		have += chunk;
		if (writeall(STDOUT_FILENO, resp, n) != n)
			eprintf("write stdout:");
	}
	close(sock);
	return have == sizeof(prefix) && memcmp(prefix, "ERR: ", sizeof(prefix)) == 0;
}

int
main(int argc, char *argv[])
{
	int aflag = 0, action = 0, ret = 0, i;

	ARGBEGIN {
	case 'u': action = 'u'; break;
	case 'd': action = 'd'; break;
	case 'r': action = 'r'; break;
	case 'k': action = 'k'; break;
	case 't': action = 't'; break;
	case 's': action = 's'; break;
	case 'a': aflag = 1;    break;
	default:  usage();
	} ARGEND

	if (!action || (action != 's' && !aflag && argc == 0))
		usage();
	if (aflag && (argc != 0 || (action != 'u' && action != 'd')))
		usage();

	if (aflag || argc == 0)
		ret = request(action, aflag ? "a" : "");
	else
		for (i = 0; i < argc; i++)
			ret |= request(action, argv[i]);

	if (fshut(stdout, "<stdout>"))
		return 2;
	return ret;
}
