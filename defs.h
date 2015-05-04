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

#define MAILESTD_CONF_PATH		"mailestd.conf"
#define MAILESTD_LOG_PATH		"mailestd.log"
#define MAILESTD_LOGSIZ			(30 * 1024)
#define MAILESTD_LOGROTMAX		8
#define MAILESTD_LOGROTWHEN		(60 * 60)	/* hourly */
#define MAILESTD_NTASKS			32
#define MAILESTD_DBNAME			"casket"
#define MAILESTD_TRIMSIZE		(128 * 1024)
#define MAILESTD_DBFLUSHSIZ		1024
#define MAILESTD_DEFAULT_SUFFIX		".mew"
#define MAILESTD_DEFAULT_FOLDERS	"!trash", "!casket", "!casket_replica"
#define MAILESTD_DBSYNC_NITER		4000

struct mailestd_conf {
	int	  debug;
	char	 *sock_path;
	char	 *log_path;
	int	  log_size;
	int	  log_count;
	int	  trim_size;
	char	 *db_path;
	int	  tasks;
	char	 *maildir;
	char	**suffixes;
	char	**folders;
};
