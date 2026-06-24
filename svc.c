/* svc: control client for ssv */

#include "arg.h"
#include "util.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void
usage(void)
{
	eprintf("usage: %s [-udrkts] [-a] [service...]\n", argv0);
}

int
main(int argc, char *argv[])
{
	int sock, aflag, action, i, n;
	struct sockaddr_un addr;
	char buf[4096], resp[4096];
	size_t off;

	aflag = 0;
	action = 0;

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

	if (!action)
		usage();
	if (action != 's' && !aflag && argc == 0)
		usage();

	/* connect to ssv control socket */
	sock = socket(AF_UNIX, SOCK_STREAM, 0);
	if (sock < 0)
		eprintf("socket:");
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strlcpy(addr.sun_path, "/run/serva.sock", sizeof(addr.sun_path));
	if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
		eprintf("connect /run/serva.sock:");

	/* build command line */
	if (action == 's') {
		strlcpy(buf, "s\n", sizeof(buf));
	} else if (aflag) {
		snprintf(buf, sizeof(buf), "%c a\n", action);
	} else {
		off = snprintf(buf, sizeof(buf), "%c", action);
		for (i = 0; i < argc; i++)
			off += snprintf(buf + off, sizeof(buf) - off,
			    " %s", argv[i]);
		off += snprintf(buf + off, sizeof(buf) - off, "\n");
	}

	/* send command, print response */
	writeall(sock, buf, strlen(buf));
	shutdown(sock, SHUT_WR);
	while ((n = read(sock, resp, sizeof(resp))) > 0)
		writeall(STDOUT_FILENO, resp, n);
	close(sock);

	if (fshut(stdout, "<stdout>"))
		return 2;
	return 0;
}
