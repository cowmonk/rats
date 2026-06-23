/* serva: a simple service supervisor */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE /* needed for PATH_MAX */
#endif

#include "util.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define SVC_DIR     "/etc/ssv"
#define SOCK_PATH   "/run/serva.sock"
#define MAX_SVCS    256
#define MAX_BACKOFF 60
#define MAX_DEPS    16
#define MAX_DEPNAME 64

struct Service {
	char stage[NAME_MAX];
	char name[NAME_MAX];
	char dir[PATH_MAX];
	char arg[NAME_MAX];
	pid_t pid;
	pid_t logpid;
	int pipefd[2];
	time_t last_start;
	time_t next_restart;
	int backoff;
	int want_up;
	int noreset;
	int once;
	int down;
	int has_logger;
	char need[MAX_DEPS][MAX_DEPNAME];
	int nneed;
	char after[MAX_DEPS][MAX_DEPNAME];
	int nafter;
};

static struct Service svcs[MAX_SVCS];
static size_t nsvcs;
static int sigpipe[2];
static volatile sig_atomic_t got_term;

/* signal handler: wake self-pipe on SIGCHLD, flag SIGTERM */
static void
signal_handler(int sig)
{
	char c;

	if (sig == SIGTERM)
		got_term = 1;
	c = 0;
	write(sigpipe[1], &c, 1);
}

/* parse @-templates: "tty@tty1" -> arg "tty1" */
static void
parse_arg(const char *name, char *arg)
{
	char *at;

	at = strchr(name, '@');
	if (at)
		strlcpy(arg, at + 1, NAME_MAX);
	else
		arg[0] = '\0';
}

/* has_file: check if <dir>/<name> exists */
static int
has_file(const char *dir, const char *name)
{
	char path[PATH_MAX];

	snprintf(path, sizeof(path), "%s/%s", dir, name);
	return access(path, F_OK) == 0;
}

/* svc_cmp: sort by (stage, name) */
static int
svc_cmp(const void *a, const void *b)
{
	const struct Service *sa = a, *sb = b;
	int c;

	c = strcmp(sa->stage, sb->stage);
	if (c != 0)
		return c;
	return strcmp(sa->name, sb->name);
}

/* endswith_at: true if name ends with '@' (template dir) */
static int
endswith_at(const char *name)
{
	size_t len;

	len = strlen(name);
	return len > 0 && name[len - 1] == '@';
}

/* find_service: lookup by "name" or "stage/name" */
static struct Service *
find_service(const char *id)
{
	size_t i;
	const char *slash;
	char stage[NAME_MAX];

	slash = strchr(id, '/');
	if (slash) {
		size_t len;

		len = slash - id;
		if (len >= NAME_MAX)
			return NULL;
		memcpy(stage, id, len);
		stage[len] = '\0';
		for (i = 0; i < nsvcs; i++)
			if (strcmp(svcs[i].stage, stage) == 0
			    && strcmp(svcs[i].name, slash + 1) == 0)
				return &svcs[i];
	} else {
		for (i = 0; i < nsvcs; i++)
			if (strcmp(svcs[i].name, id) == 0)
				return &svcs[i];
	}
	return NULL;
}

/* deps_met: true if all need-deps are running */
static int
deps_met(struct Service *s)
{
	int i;
	struct Service *dep;

	for (i = 0; i < s->nneed; i++) {
		dep = find_service(s->need[i]);
		if (!dep || dep->pid <= 0)
			return 0;
	}
	return 1;
}

/* has_after: true if s has after-hint for name */
static int
has_after(struct Service *s, const char *name)
{
	int i;

	for (i = 0; i < s->nafter; i++)
		if (strcmp(s->after[i], name) == 0)
			return 1;
	return 0;
}

/* adjust_after_order: swap services so after-hints are respected
   within each stage. */
static void
adjust_after_order(void)
{
	int swapped;
	size_t i, j;
	struct Service tmp;

	do {
		swapped = 0;
		for (i = 0; i < nsvcs; i++) {
			for (j = i + 1; j < nsvcs; j++) {
				if (strcmp(svcs[i].stage, svcs[j].stage) != 0)
					continue;
				if (has_after(&svcs[i], svcs[j].name)) {
					tmp = svcs[i];
					svcs[i] = svcs[j];
					svcs[j] = tmp;
					swapped = 1;
				}
			}
		}
	} while (swapped);
}

/* scan_deps: read need/ and after/ directories into service struct */
static void
scan_deps(struct Service *s)
{
	DIR *d;
	struct dirent *e;
	char path[PATH_MAX];

	snprintf(path, sizeof(path), "%s/need", s->dir);
	d = opendir(path);
	if (d) {
		while ((e = readdir(d)) != NULL && s->nneed < MAX_DEPS) {
			if (e->d_name[0] == '.')
				continue;
			strlcpy(s->need[s->nneed], e->d_name, MAX_DEPNAME);
			s->nneed++;
		}
		closedir(d);
	}

	snprintf(path, sizeof(path), "%s/after", s->dir);
	d = opendir(path);
	if (d) {
		while ((e = readdir(d)) != NULL && s->nafter < MAX_DEPS) {
			if (e->d_name[0] == '.')
				continue;
			strlcpy(s->after[s->nafter], e->d_name, MAX_DEPNAME);
			s->nafter++;
		}
		closedir(d);
	}
}

static void
scan_services(void)
{
	DIR *d, *sd;
	struct dirent *e, *se;
	char stagepath[PATH_MAX], svcpath[PATH_MAX], tmp[PATH_MAX];
	struct stat st, sst;

	d = opendir(SVC_DIR);
	if (!d)
		eprintf("opendir %s:", SVC_DIR);

	nsvcs = 0;
	while ((e = readdir(d)) != NULL && nsvcs < MAX_SVCS) {
		if (e->d_name[0] == '.')
			continue;

		snprintf(stagepath, sizeof(stagepath), "%s/%s",
		    SVC_DIR, e->d_name);
		if (stat(stagepath, &st) < 0)
			continue;
		if (!S_ISDIR(st.st_mode))
			continue;

		/* scan services within this stage */
		sd = opendir(stagepath);
		if (!sd)
			continue;
		while ((se = readdir(sd)) != NULL && nsvcs < MAX_SVCS) {
			struct Service *s;

			if (se->d_name[0] == '.')
				continue;
			if (endswith_at(se->d_name))
				continue;

			snprintf(svcpath, sizeof(svcpath), "%s/%s",
			    stagepath, se->d_name);
			if (stat(svcpath, &sst) < 0)
				continue;
			if (!S_ISDIR(sst.st_mode))
				continue;

			/* run is required */
			snprintf(tmp, sizeof(tmp), "%s/run", svcpath);
			if (access(tmp, X_OK) < 0)
				continue;

			s = &svcs[nsvcs];
			memset(s, 0, sizeof(*s));
			strlcpy(s->stage, e->d_name, NAME_MAX);
			strlcpy(s->name, se->d_name, NAME_MAX);

			if (!realpath(svcpath, s->dir))
				strlcpy(s->dir, svcpath, PATH_MAX);

			parse_arg(s->name, s->arg);

			snprintf(tmp, sizeof(tmp), "%s/log", s->dir);
			s->has_logger = (access(tmp, X_OK) == 0);

			s->noreset = has_file(s->dir, "noreset");
			s->once    = has_file(s->dir, "once");
			s->down    = has_file(s->dir, "down");

			scan_deps(s);

			s->pipefd[0] = -1;
			s->pipefd[1] = -1;
			nsvcs++;
		}
		closedir(sd);
	}
	closedir(d);

	qsort(svcs, nsvcs, sizeof(*svcs), svc_cmp);
	adjust_after_order();
}

static void
start_service(struct Service *s)
{
	char runpath[PATH_MAX], logpath[PATH_MAX];
	char *argv[3];
	pid_t pid;

	if (s->pid > 0)
		return;

	snprintf(runpath, sizeof(runpath), "%s/run", s->dir);
	snprintf(logpath, sizeof(logpath), "%s/log", s->dir);

	/* create logging pipe if needed */
	if (s->has_logger && s->pipefd[0] < 0) {
		if (pipe(s->pipefd) < 0)
			weprintf("pipe:");
	}

	s->last_start = time(NULL);

	/* fork service process */
	pid = fork();
	if (pid < 0) {
		weprintf("fork:");
		return;
	}
	if (pid == 0) {
		sigset_t empty;

		sigemptyset(&empty);
		sigprocmask(SIG_SETMASK, &empty, NULL);
		setsid();

		if (chdir(s->dir) < 0)
			eprintf("chdir %s:", s->dir);

		if (s->pipefd[1] >= 0) {
			dup2(s->pipefd[1], STDOUT_FILENO);
			dup2(s->pipefd[1], STDERR_FILENO);
			if (s->pipefd[1] > STDERR_FILENO)
				close(s->pipefd[1]);
			close(s->pipefd[0]);
		}

		setenv("SVC_NAME", s->name, 1);
		setenv("SVC_STAGE", s->stage, 1);
		if (s->arg[0])
			setenv("SVC_ARG", s->arg, 1);

		argv[0] = "run";
		argv[1] = s->arg[0] ? s->arg : NULL;
		argv[2] = NULL;

		execv(runpath, argv);
		eprintf("exec %s:", runpath);
	}
	s->pid = pid;

	/* fork logger process if pipe was created */
	if (s->pipefd[0] >= 0) {
		pid = fork();
		if (pid < 0) {
			weprintf("fork logger:");
		} else if (pid == 0) {
			sigset_t empty;

			sigemptyset(&empty);
			sigprocmask(SIG_SETMASK, &empty, NULL);

			if (chdir(s->dir) < 0)
				eprintf("chdir %s:", s->dir);

			dup2(s->pipefd[0], STDIN_FILENO);
			if (s->pipefd[0] > STDIN_FILENO)
				close(s->pipefd[0]);
			close(s->pipefd[1]);

			setenv("SVC_NAME", s->name, 1);
			setenv("SVC_STAGE", s->stage, 1);
			if (s->arg[0])
				setenv("SVC_ARG", s->arg, 1);

			execl(logpath, "log", NULL);
			eprintf("exec %s:", logpath);
		}
		s->logpid = pid;

		/* serva closes both pipe ends */
		close(s->pipefd[0]);
		close(s->pipefd[1]);
		s->pipefd[0] = -1;
		s->pipefd[1] = -1;
	}
}

static void
stop_service(struct Service *s)
{
	int i;

	if (s->pid > 0) {
		kill(s->pid, SIGTERM);
		for (i = 0; i < 50; i++) {
			if (waitpid(s->pid, NULL, WNOHANG) > 0)
				break;
			if (i == 40)
				kill(s->pid, SIGKILL);
			usleep(100000);
		}
		s->pid = 0;
	}
	if (s->logpid > 0) {
		kill(s->logpid, SIGTERM);
		waitpid(s->logpid, NULL, 0);
		s->logpid = 0;
	}
	s->want_up = 0;
	s->next_restart = 0;
	s->backoff = 0;
}

/* stop_for_dep: stop but keep want_up - waiting for dependency */
static void
stop_for_dep(struct Service *s)
{
	int i;

	if (s->pid > 0) {
		kill(s->pid, SIGTERM);
		for (i = 0; i < 50; i++) {
			if (waitpid(s->pid, NULL, WNOHANG) > 0)
				break;
			if (i == 40)
				kill(s->pid, SIGKILL);
			usleep(100000);
		}
		s->pid = 0;
	}
	if (s->logpid > 0) {
		kill(s->logpid, SIGTERM);
		waitpid(s->logpid, NULL, 0);
		s->logpid = 0;
	}
	s->next_restart = 0;
	s->backoff = 0;
	/* want_up stays 1 - will restart when deps are met */
}

static void
restart_service(struct Service *s)
{
	stop_service(s);
	s->want_up = 1;
	start_service(s);
}

/* stop_cascade: stop all services whose need-deps are not running. */
static void
stop_cascade(void)
{
	size_t i, j;
	int found;

	do {
		found = 0;
		for (i = 0; i < nsvcs; i++) {
			struct Service *s = &svcs[i];

			if (s->pid <= 0 || !s->want_up)
				continue;
			for (j = 0; j < s->nneed; j++) {
				struct Service *dep;

				dep = find_service(s->need[j]);
				if (!dep || dep->pid <= 0) {
					stop_for_dep(s);
					found = 1;
					break;
				}
			}
		}
	} while (found);
}

static void
handle_child(pid_t pid)
{
	size_t i;
	time_t now;

	now = time(NULL);
	for (i = 0; i < nsvcs; i++) {
		struct Service *s = &svcs[i];

		if (s->pid == pid) {
			s->pid = 0;
			/* kill logger too - pipe is broken */
			if (s->logpid > 0) {
				kill(s->logpid, SIGTERM);
				waitpid(s->logpid, NULL, 0);
				s->logpid = 0;
			}

			/* cascade-stop services that need this one */
			stop_cascade();

			if (!s->want_up)
				return;
			if (s->once || s->noreset) {
				/* don't restart - clear want_up so check_restart skips */
				s->want_up = 0;
				return;
                        }
			if (now - s->last_start < 60)
				s->backoff = MIN(s->backoff + 1, MAX_BACKOFF);
			else
				s->backoff = 1;
			s->next_restart = now + s->backoff;
			return;
		}
		if (s->logpid == pid) {
			s->logpid = 0;
			/* logger died: restart whole service for clean pipe */
			if (s->want_up && s->pid > 0)
				restart_service(s);
			return;
		}
	}
}

static void
reap_all(void)
{
	pid_t pid;

	while ((pid = waitpid(-1, NULL, WNOHANG)) > 0)
		handle_child(pid);
}

/* check_restarts: start services that are ready (deps met, backoff
   expired). */
static void
check_restarts(void)
{
	size_t i;
	time_t now;
	int progress;

	now = time(NULL);
	do {
		progress = 0;
		for (i = 0; i < nsvcs; i++) {
			struct Service *s = &svcs[i];

			if (!s->want_up || s->pid > 0 || s->down)
				continue;
			if (!deps_met(s))
				continue;
			if (s->next_restart > 0 && now < s->next_restart)
				continue;
			s->next_restart = 0;
			s->backoff = 0;
			start_service(s);
			if (s->pid > 0)
				progress = 1;
		}
	} while (progress);
}

static int
compute_timeout(void)
{
	size_t i;
	time_t now, min, wait;

	now = time(NULL);
	min = 0;
	for (i = 0; i < nsvcs; i++) {
		struct Service *s = &svcs[i];

		if (!s->want_up || s->pid > 0 || s->next_restart == 0)
			continue;
		if (!deps_met(s))
			continue;
		wait = s->next_restart - now;
		if (wait <= 0)
			return 0;
		if (min == 0 || wait < min)
			min = wait;
	}
	if (min == 0)
		return -1;
	return (int)(min * 1000);
}

/* status_str: human-readable service state */
static const char *
status_str(struct Service *s)
{
	if (s->down)
		return "DOWN*";
	if (s->pid > 0)
		return "RUN";
	if (s->want_up && !deps_met(s))
		return "WAIT*";
	if (s->want_up)
		return "WAIT";
        if (s->once)
                return "DONE";
	return "DOWN";
}

/* handle_conn: process one control connection */
static void
handle_conn(int conn)
{
	char buf[4096], resp[8192], cmd, *name, *p;
	char full[2 * NAME_MAX + 2];
	ssize_t n;
	size_t i, j, off;
	struct Service *s;

	n = read(conn, buf, sizeof(buf) - 1);
	if (n <= 0) {
		close(conn);
		return;
	}
	buf[n] = '\0';

	p = strchr(buf, '\n');
	if (p)
		*p = '\0';

	cmd = buf[0];
	name = (buf[1] == ' ') ? buf + 2 : buf + 1;

	switch (cmd) {
	case 'u':
		if (strcmp(name, "a") == 0) {
			for (i = 0; i < nsvcs; i++) {
				if (svcs[i].down)
					continue;
				svcs[i].want_up = 1;
			}
			check_restarts();
			off = snprintf(resp, sizeof(resp),
			    "OK: started all\n");
		} else {
			s = find_service(name);
			if (!s) {
				off = snprintf(resp, sizeof(resp),
				    "ERR: no such service: %s\n", name);
				break;
			}
			/* explicit svc -u overrides down and deps */
			s->down = 0;
			s->want_up = 1;
			s->next_restart = 0;
			s->backoff = 0;
			start_service(s);
			off = snprintf(resp, sizeof(resp),
			    "OK: %s up\n", name);
		}
		break;
	case 'd':
		if (strcmp(name, "a") == 0) {
			for (i = 0; i < nsvcs; i++)
				stop_service(&svcs[i]);
			off = snprintf(resp, sizeof(resp),
			    "OK: stopped all\n");
		} else {
			s = find_service(name);
			if (!s) {
				off = snprintf(resp, sizeof(resp),
				    "ERR: no such service: %s\n", name);
				break;
			}
			stop_service(s);
			off = snprintf(resp, sizeof(resp),
			    "OK: %s down\n", name);
		}
		break;
	case 'r':
		s = find_service(name);
		if (!s) {
			off = snprintf(resp, sizeof(resp),
			    "ERR: no such service: %s\n", name);
			break;
		}
		restart_service(s);
		off = snprintf(resp, sizeof(resp),
		    "OK: %s restarted\n", name);
		break;
	case 'k':
		s = find_service(name);
		if (!s) {
			off = snprintf(resp, sizeof(resp),
			    "ERR: no such service: %s\n", name);
			break;
		}
		if (s->pid > 0)
			kill(s->pid, SIGKILL);
		off = snprintf(resp, sizeof(resp),
		    "OK: %s killed\n", name);
		break;
	case 't':
		s = find_service(name);
		if (!s) {
			off = snprintf(resp, sizeof(resp),
			    "ERR: no such service: %s\n", name);
			break;
		}
		if (s->pid > 0)
			kill(s->pid, SIGTERM);
		off = snprintf(resp, sizeof(resp),
		    "OK: %s terminated\n", name);
		break;
	case 's':
		off = 0;
		for (i = 0; i < nsvcs; i++) {
			s = &svcs[i];
			if (off + 128 >= sizeof(resp))
				break;
			snprintf(full, sizeof(full), "%s/%s",
			    s->stage, s->name);
			off += snprintf(resp + off, sizeof(resp) - off,
			    "%-32s %s%s pid=%d",
			    full,
			    status_str(s),
			    s->noreset ? " (noreset)" : "",
			    s->pid);
			if (s->nneed > 0) {
				off += snprintf(resp + off,
				    sizeof(resp) - off, " need=");
				for (j = 0; j < s->nneed; j++)
					off += snprintf(resp + off,
					    sizeof(resp) - off, "%s%s",
					    j ? "," : "", s->need[j]);
			}
			if (s->nafter > 0) {
				off += snprintf(resp + off,
				    sizeof(resp) - off, " after=");
				for (j = 0; j < s->nafter; j++)
					off += snprintf(resp + off,
					    sizeof(resp) - off, "%s%s",
					    j ? "," : "", s->after[j]);
			}
			off += snprintf(resp + off, sizeof(resp) - off,
			    "\n");
		}
		break;
	default:
		off = snprintf(resp, sizeof(resp),
		    "ERR: unknown command\n");
		break;
	}

	writeall(conn, resp, off);
	close(conn);
}

static void
shutdown_serva(void)
{
	size_t i;

	for (i = 0; i < nsvcs; i++)
		stop_service(&svcs[i]);
	unlink(SOCK_PATH);
}

int
main(void)
{
	int sock, nfds, timeout, conn;
	struct sockaddr_un addr;
	struct pollfd pfds[2];
	struct sigaction sa;
	char c;
	size_t i;

	/* self-pipe for signal integration with poll */
	if (pipe(sigpipe) < 0)
		eprintf("pipe:");
	fcntl(sigpipe[0], F_SETFL, O_NONBLOCK);
	fcntl(sigpipe[1], F_SETFL, O_NONBLOCK);

	/* install signal handlers */
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = signal_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	sigaction(SIGCHLD, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);

	scan_services();

	/* create control socket */
	sock = socket(AF_UNIX, SOCK_STREAM, 0);
	if (sock < 0)
		eprintf("socket:");
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strlcpy(addr.sun_path, SOCK_PATH, sizeof(addr.sun_path));
	unlink(SOCK_PATH);
	if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
		eprintf("bind %s:", SOCK_PATH);
	if (listen(sock, 16) < 0)
		eprintf("listen:");
	chmod(SOCK_PATH, 0600);

	/* mark all non-down services as wanting to be up */
	for (i = 0; i < nsvcs; i++) {
		if (svcs[i].down)
			continue;
		svcs[i].want_up = 1;
	}

	/* start services whose deps are met */
	check_restarts();

	/* warn about services stuck on unmet deps */
	for (i = 0; i < nsvcs; i++) {
		if (svcs[i].want_up && svcs[i].pid == 0
		    && !svcs[i].down && !deps_met(&svcs[i]))
			weprintf("unmet dependency for %s/%s",
			    svcs[i].stage, svcs[i].name);
	}

	/* main loop */
	for (;;) {
		timeout = compute_timeout();

		pfds[0].fd = sock;
		pfds[0].events = POLLIN;
		pfds[1].fd = sigpipe[0];
		pfds[1].events = POLLIN;

		nfds = poll(pfds, 2, timeout);
		if (nfds < 0) {
			if (errno == EINTR)
				continue;
			eprintf("poll:");
		}

		if (pfds[0].revents & POLLIN) {
			conn = accept(sock, NULL, NULL);
			if (conn >= 0)
				handle_conn(conn);
		}

		if (pfds[1].revents & POLLIN) {
			while (read(sigpipe[0], &c, 1) > 0)
				;
			reap_all();
		}

		if (got_term)
			break;

		check_restarts();
	}

	shutdown_serva();
	return 0;
}
