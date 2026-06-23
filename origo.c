/* origo: PID 1 - minimal init */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE /* needed for usleep, kill, and sig*() */
#endif

#include "util.h"

#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static sigset_t set;
static pid_t serva_pid;
static char *const serva_argv[] = { "serva", NULL };

static pid_t
spawn(const char *path, char *const argv[])
{
	pid_t pid;

	pid = fork();
	if (pid < 0)
		eprintf("fork:");
	if (pid == 0) {
		sigprocmask(SIG_UNBLOCK, &set, NULL);
		setsid();
		setpgid(0, 0);
		execvp(path, argv);
		eprintf("execvp %s:", path);
	}
	return pid;
}

/* run_script: execute a shell script and wait for it */
static void
run_script(const char *path)
{
	char *const argv[] = { "sh", (char *)path, NULL };
	pid_t pid;

	if (access(path, F_OK) < 0)
		return;
	pid = spawn("/bin/sh", argv);
	for (;;) {
		if (waitpid(pid, NULL, 0) >= 0)
			break;
		if (errno != EINTR)
			break;
	}
}

/* mount_fs: bare-minimum kernel vfs */
static void
mount_fs(void)
{
	if (mount("proc", "/proc", "proc", 0, NULL) < 0)
		weprintf("mount /proc:");
	if (mount("sysfs", "/sys", "sysfs", 0, NULL) < 0)
		weprintf("mount /sys:");
	if (mount("devtmpfs", "/dev", "devtmpfs", 0, NULL) < 0)
		weprintf("mount /dev:");
	mkdir("/dev/pts", 0755);
	if (mount("devpts", "/dev/pts", "devpts", 0, NULL) < 0)
		weprintf("mount /dev/pts:");
}

/* reap: handle SIGCHLD, respawn serva if it died */
static void
reap(void)
{
	pid_t pid;

	while ((pid = waitpid(-1, NULL, WNOHANG)) > 0) {
		if (pid == serva_pid)
			serva_pid = spawn("serva", serva_argv);
	}
}

/* do_shutdown: stop serva, run rc.shutdown, sync, reboot */
static void
do_shutdown(int how)
{
	int i;

	if (serva_pid > 0) {
		kill(serva_pid, SIGTERM);
		for (i = 0; i < 50; i++) {
			if (waitpid(serva_pid, NULL, WNOHANG) > 0)
				break;
			if (i == 40)
				kill(serva_pid, SIGKILL);
			usleep(100000);
		}
		serva_pid = 0;
	}
	run_script("/etc/rc.shutdown");

	/* kill any remaining processes - exclude PID 1 */
	kill(-1, SIGTERM);
	usleep(500000);
	kill(-1, SIGKILL);

	/* remount / ro */
	if (mount(NULL, "/", NULL, MS_REMOUNT | MS_RDONLY, NULL) < 0)
		weprintf("remount / read-only:");

	sync();
	reboot(how);
	eprintf("reboot failed:");
}

/* single_user: stop serva, run rc.single or /bin/sh, respawn serva */
static void
single_user(void)
{
	if (serva_pid > 0) {
		kill(serva_pid, SIGTERM);
		waitpid(serva_pid, NULL, 0);
		serva_pid = 0;
	}
	kill(-1, SIGTERM);
	usleep(500000);
	kill(-1, SIGKILL);
	reap();
	if (access("/etc/rc.single", F_OK) == 0)
		run_script("/etc/rc.single");
	else
		run_script("/bin/sh");
	serva_pid = spawn("serva", serva_argv);
}

int
main(void)
{
	int sig;
	int fd;

	if (getpid() != 1)
		eprintf("must be run as PID 1");

	/* reopen console */
	fd = open("/dev/console", O_RDWR);
	if (fd >= 0) {
		dup2(fd, STDIN_FILENO);
		dup2(fd, STDOUT_FILENO);
		dup2(fd, STDERR_FILENO);
		if (fd > STDERR_FILENO)
			close(fd);
	}

	/* block all signals — we use sigwait */
	sigfillset(&set);
	sigprocmask(SIG_BLOCK, &set, NULL);

	setenv("PATH", "/sbin:/bin:/usr/sbin:/usr/bin", 1);

	mount_fs();

	run_script("/etc/rc.boot");

	/* start supervisor */
	serva_pid = spawn("serva", serva_argv);

	/* signal loop */
	for (;;) {
		if (sigwait(&set, &sig) < 0)
			continue;
		switch (sig) {
		case SIGCHLD:
			reap();
			break;
		case SIGINT:
		case SIGUSR2:
			do_shutdown(RB_AUTOBOOT);
			break;
		case SIGUSR1:
			do_shutdown(RB_POWER_OFF);
			break;
		case SIGTERM:
			single_user();
			break;
		}
	}
	return 0;
}
