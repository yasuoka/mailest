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
#ifndef MAILESTD_H
#define MAILESTD_H 1

#include <sys/cdefs.h>

#define MAILESTD_MAIL_DIR	"Mail"
#define MAILESTD_SOCK_PATH	".mailest.sock"
#define MAILESTD_SOCK_MSGSIZ	256	/* response message size */
#define MAILESTD_MAX_MESSAGE_ID	256

enum MAILESTCTL_CMD {
	MAILESTCTL_CMD_NONE = 0,
	MAILESTCTL_CMD_DEBUGI,
	MAILESTCTL_CMD_DEBUGD,
	MAILESTCTL_CMD_STOP,
	MAILESTCTL_CMD_UPDATE,
	MAILESTCTL_CMD_SUSPEND,
	MAILESTCTL_CMD_RESUME,
	MAILESTCTL_CMD_SEARCH,
	MAILESTCTL_CMD_SMEW,
	MAILESTCTL_CMD_GUESS_AGAIN
};

enum MAILESTCTL_OUTFORM {
	MAILESTCTL_OUTFORM_COMPAT_VU,
	MAILESTCTL_OUTFORM_SMEW
};

#define	ATTR_MSGID	"message-id"
#define	ATTR_PARID	"x-mew-parid"
#define	ATTR_TITLE	"@title"
#define	ATTR_CDATE	"@cdate"

struct mailestctl {
	enum MAILESTCTL_CMD	 command;
};

struct mailestctl_update {
	enum MAILESTCTL_CMD	 command;
	char			 folder[PATH_MAX];
};
struct mailestctl_search {
	enum MAILESTCTL_CMD	 command;
	enum MAILESTCTL_OUTFORM	 outform;
	int			 max;
	char			 attrs[8][MAILESTD_MAX_MESSAGE_ID + 80];
	char			 order[80];
	char			 phrase[BUFSIZ];
};
struct mailestctl_smew {
	enum MAILESTCTL_CMD	 command;
	char			 msgid[MAILESTD_MAX_MESSAGE_ID];
	char			 folder[PATH_MAX];
};

__BEGIN_DECLS
int			 mailestctl_main(int, char *[]);
void			 mailestd_log(int, const char *, ...)
			    __attribute__((__format__(__syslog__,2,3)));
struct mailestd_conf	*parse_config(const char *, const char *);
void			 free_config(struct mailestd_conf *);
int			 cmdline_symset(char *);
__END_DECLS

#endif
