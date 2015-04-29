/*
 * Copyright (c) 2015 YASUOKA Masahiko <yasuoka@yasuoka.net>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <err.h>
#include <errno.h>
#include <iconv.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include "mailestd.h"
#include "parser.h"

#ifndef	nitems
#define nitems(_n)	(sizeof((_n)) / sizeof((_n)[0]))
#endif

static int		 mailestc_sock = -1;
static const char	*mailestc_path = NULL;

static void		 mailestc_connect(void);
static bool		 mailestc_check_connection(void);
size_t			 ic_strlcpy(char *, const char *, size_t, const char *);
static void		 run_daemon(const char *, char *[]);
static void		 stop_daemon(void);

static void
usage(void)
{
	extern char *__progname;

	fprintf(stderr,
	    "usage: %s [-h] [-s suffix] [-b basedir] command [args...]\n",
	    __progname);
}

int
mailestctl_main(int argc, char *argv[])
{
	int			 i, ch, sz, cmdc = 0;
	struct mailestctl	 ctl;
	struct parse_result	*result;
	const char		*home, *cmd;
	char			*cmdv[64], msgbuf[MAILESTD_SOCK_MSGSIZ],
				 path0[PATH_MAX], *maildir = NULL;

	cmd = argv[0];
	cmdv[cmdc++] = "mailestd";

	if ((home = getenv("HOME")) == NULL)
		errx(EX_OSERR,
		    "HOME environment variable is not set");

	while ((ch = getopt(argc, argv, "dhb:s:")) != -1)
		switch (ch) {
		case 'b':
			maildir = optarg;
			break;

		case 's':
			cmdv[cmdc++] = "-s";
			cmdv[cmdc++] = optarg;
			break;

		case 'h':
			usage();
			exit(EX_USAGE);
			break;

		default:
			exit(EX_USAGE);
			break;
		}

	if (maildir)
		cmdv[cmdc++] = maildir;
	cmdv[cmdc++] = NULL;

	argc -= optind;
	argv += optind;

	signal(SIGPIPE, SIG_IGN);

	if (mailestc_path == NULL) {
		if (maildir == NULL) {
			strlcpy(path0, home, sizeof(path0));
			strlcat(path0, "/" MAILESTD_MAIL_DIR, sizeof(path0));
		} else
			strlcpy(path0, maildir, sizeof(path0));
		strlcat(path0, "/" MAILESTD_SOCK_PATH, sizeof(path0));
		mailestc_path = path0;
	}

	result = parse(argc, argv);
	if (result == NULL)
		exit(EX_USAGE);

	switch (result->action) {
	case RESTART:
		stop_daemon();
		run_daemon(cmd, cmdv);
		break;

	case START:
		if (mailestc_check_connection())
			errx(1, "mailestd is already running?");
		run_daemon(cmd, cmdv);
		break;

	case STOP:
		ctl.command = MAILESTCTL_CMD_STOP;
do_common:
		if (!mailestc_check_connection()) {
			warnx("could not connect to mailestd.  not running?");
			break;
		}
		if (write(mailestc_sock, &ctl, sizeof(ctl)) < 0)
			err(1, "write");
		break;

	case DEBUGI:
		ctl.command = MAILESTCTL_CMD_DEBUGI;
		goto do_common;

	case DEBUGD:
		ctl.command = MAILESTCTL_CMD_DEBUGD;
		goto do_common;

	case SUSPEND:
		ctl.command = MAILESTCTL_CMD_SUSPEND;
		goto do_common;

	case RESUME:
		ctl.command = MAILESTCTL_CMD_RESUME;
		goto do_common;

	case UPDATE:
	    {
		struct mailestctl_update	 update;

		run_daemon(cmd, cmdv);
		memset(&update, 0, sizeof(update));
		update.command = MAILESTCTL_CMD_UPDATE;
		if (result->folder != NULL)
			strlcpy(update.folder, result->folder,
			    sizeof(update.folder));

		if (!mailestc_check_connection()) {
			warnx("could not connect to mailestd.  not running?");
			break;
		}
		if (write(mailestc_sock, &update, sizeof(update)) < 0)
			err(1, "write");
		while ((sz = read(mailestc_sock, msgbuf, sizeof(msgbuf))) > 0)
			fwrite(msgbuf, 1, sz, stdout);
		close(mailestc_sock);
	    }
		break;

	case CSEARCH:
	    {
		struct mailestctl_search	 search;

		run_daemon(cmd, cmdv);
		memset(&search, 0, sizeof(search));
		search.command = MAILESTCTL_CMD_SEARCH;
		if (result->search.max)
			search.max = result->search.max;
		else
			search.max = 10;
		if ((result->search.flags & SEARCH_FLAG_VU) == 0)
			errx(EX_USAGE, "-vu must be always specified.  "
			    "Since any other is not implemented yet.");
		if (result->search.attrs != NULL) {
			for (i = 0; result->search.attrs[i] != NULL; i++) {
				if (i >= (int)nitems(search.attrs))
					errx(EX_USAGE, "Too many -attr.  "
					    "It's limited %d",
					    (int)nitems(search.attrs));
				if (ic_strlcpy(
				    search.attrs[i], result->search.attrs[i],
				    sizeof(search.attrs[i]),
				    result->search.ic) >=
				    sizeof(search.attrs[i]))
					err(EX_USAGE, "-attr %.9s...",
					    result->search.attrs[i]);
			}
		}
		if (ic_strlcpy(search.phrase, result->search.phrase,
		    sizeof(search.phrase), result->search.ic)
		    >= sizeof(search.phrase))
			err(EX_USAGE, "<phrase>");
		if (result->search.ord != NULL) {
			if (strlcpy(search.order, result->search.ord,
			    sizeof(search.order)) >= sizeof(search.order))
				err(EX_USAGE, "-ord");
		}

		if (!mailestc_check_connection()) {
			warnx("could not connect to mailestd.  not running?");
			break;
		}
		if (write(mailestc_sock, &search, sizeof(search)) < 0)
			err(1, "write");
		while ((sz = read(mailestc_sock, msgbuf, sizeof(msgbuf))) > 0)
			fwrite(msgbuf, 1, sz, stdout);
		close(mailestc_sock);
	    }

	case NONE:
		break;
	}

	if (mailestc_sock >= 0)
		close(mailestc_sock);

	exit(EXIT_SUCCESS);
}

static bool
mailestc_check_connection(void)
{
	if (mailestc_sock < 0)
		mailestc_connect();

	return ((mailestc_sock >= 0)? true : false);
}

static void
mailestc_connect(void)
{
	int			 sock;
	struct sockaddr_un	 sun;

	memset(&sun, 0, sizeof(sun));
	sun.sun_family = AF_UNIX;
	strlcpy(sun.sun_path, mailestc_path, sizeof(sun.sun_path));

	if ((sock = socket(PF_UNIX, SOCK_SEQPACKET, 0)) < 0)
		err(EX_OSERR, "socket");
	if (connect(sock, (struct sockaddr *)&sun, sizeof(sun)) == -1) {
		if (errno == EEXIST || errno == ECONNREFUSED || errno == ENOENT)
			return;
		err(EX_OSERR, "connect(%s)", mailestc_path);
	}

	mailestc_sock = sock;
}

static void
run_daemon(const char *cmd, char *argv[])
{
	int	 ntry;

	if (!mailestc_check_connection()) {
		if (fork() == 0) {
			setsid();
			execvp(cmd, argv);
			/* NOTREACHED */
			abort();
		}
		for (ntry = 8; !mailestc_check_connection() && ntry-- > 0;)
			usleep(500000);
		if (ntry <= 0)
			errx(1, "cannot start mailestd");
	}
}

static void
stop_daemon(void)
{
	int			 ntry;
	struct mailestctl	 ctl;

	for (ntry = 8; mailestc_check_connection() && ntry-- > 0;) {
		ctl.command = MAILESTCTL_CMD_STOP;
		if (write(mailestc_sock, &ctl, sizeof(ctl)) < 0) {
			if (errno != EPIPE)
				err(1, "write");
			close(mailestc_sock);
			mailestc_sock = -1;
			break;
		}
		usleep(500000);
	}
	if (ntry <= 0)
		warnx(1, "cannot stop mailestd");
}

size_t
ic_strlcpy(char *output, const char *input, size_t output_siz,
    const char *input_encoding)
{
	iconv_t		 cd;
	size_t		 isiz, osiz;
	char		*in = (char *)input;

	if (input_encoding == NULL)
		return (strlcpy(output, input, output_siz));
	if ((cd = iconv_open("UTF-8", input_encoding)) == (iconv_t)-1)
		err(1, "iconv_open(\"UTF-8\", \"%s\")", input_encoding);
	isiz = strlen(input) + 1;
	osiz = output_siz;
	iconv(cd, &in, &isiz, &output, &osiz);
	iconv_close(cd);
	if (isiz != 0)
		return ((size_t)-1);

	return (osiz);
}
