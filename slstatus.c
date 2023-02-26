/* See LICENSE file for copyright and license details. */
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "arg.h"
#include "slstatus.h"
#include "util.h"

char buf[1024];
static volatile sig_atomic_t done;

static void
terminate(const int signo)
{
	if (signo != SIGUSR1)
		done = 1;
}

static void
difftimespec(struct timespec *res, struct timespec *a, struct timespec *b)
{
	res->tv_sec = a->tv_sec - b->tv_sec - (a->tv_nsec < b->tv_nsec);
	res->tv_nsec = a->tv_nsec - b->tv_nsec +
	               (a->tv_nsec < b->tv_nsec) * 1E9;
}

static void
usage(void)
{
	die("usage: %s [-1]", argv0);
}

int
main(int argc, char *argv[])
{
	struct sigaction act;
	struct timespec start, current, diff, intspec, wait;
	char status[MAXLEN];

	ARGBEGIN {
	case '1':
		done = 1;
		break;
	default:
		usage();
	} ARGEND

	if (argc)
		usage();

	memset(&act, 0, sizeof(act));
	act.sa_handler = terminate;
	sigaction(SIGINT,  &act, NULL);
	sigaction(SIGTERM, &act, NULL);
	act.sa_flags |= SA_RESTART;
	sigaction(SIGUSR1, &act, NULL);

	do {
		if (clock_gettime(CLOCK_MONOTONIC, &start) < 0)
			die("clock_gettime:");

		status[0] = '\0';

		get_status(status);

		puts(status);
		fflush(stdout);
		if (ferror(stdout))
			die("puts:");

		if (!done) {
			if (clock_gettime(CLOCK_MONOTONIC, &current) < 0)
				die("clock_gettime:");
			difftimespec(&diff, &current, &start);

			intspec.tv_sec = interval / 1000;
			intspec.tv_nsec = (interval % 1000) * 1E6;
			difftimespec(&wait, &intspec, &diff);

			if (wait.tv_sec >= 0 &&
			    nanosleep(&wait, NULL) < 0 &&
			    errno != EINTR)
					die("nanosleep:");
		}
	} while (!done);

	return 0;
}
