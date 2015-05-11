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
#include "compat.h"

#include <sys/types.h>
#ifdef MONITOR_KQUEUE
#include <sys/event.h>
#endif
#ifdef MONITOR_INOTIFY
#include <sys/inotify.h>
#endif
#include <sys/queue.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/tree.h>
#include <sys/mman.h>
#include <sys/un.h>

#include <ctype.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <event.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <fts.h>
#include <glob.h>
#include <libgen.h>
#include <limits.h>
#include <paths.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>
#ifdef MAILESTD_MT
#include <pthread.h>
#endif

#include "bytebuf.h"
#include <cabin.h>
#include <estraier.h>
#ifdef HAVE_LIBESTDRAFT
#include <estdraft.h>
#else
/*
 * Without estdraft, forking "estdoc" to create a draft message, this
 * decreases performance so much.
 */
#endif

#include "defs.h"
#include "mailestd.h"
#include "mailestd_local.h"

int	mailestctl_main(int argc, char *argv[]);

static void
usage(void)
{
	extern char	*__progname;

	fprintf(stderr, "usage: %s [-dnh] [-S suffix] [-f file] [maildir]\n",
	    __progname);
}

int
main(int argc, char *argv[])
{
	int			 ch, suffixcount = 0;
	struct mailestd		 mailestd_s;
	const char		*maildir = NULL, *home, *conf_file = NULL,
				*suffix[11];
	char			 pathtmp[PATH_MAX], maildir0[PATH_MAX];
	struct mailestd_conf	*conf;
	bool			 noaction = false;
	struct stat		 st;
	extern char		*__progname;
	rlim_t			 olim;
	struct rlimit		 rl;

	if (strcmp(__progname, "mailestctl") == 0)
		return (mailestctl_main(argc, argv));

	memset(suffix, 0, sizeof(suffix));
	while ((ch = getopt(argc, argv, "+dhS:nf:")) != -1)
		switch (ch) {
		case 'S':
			if (suffixcount + 2 >= (int)nitems(suffix)) {
				errx(EX_USAGE, "too many suffixes.  "
				    "limited %d", (int)nitems(suffix) - 1);
			}
			suffix[suffixcount++] = optarg;
			suffix[suffixcount] = NULL;
			break;

		case 'd':
			if (!foreground)
				foreground = 1;
			else
				debug++;
			break;

		case 'f':
			conf_file = optarg;
			break;

		case 'n':
			noaction = true;
			break;

		case 'h':
			usage();
			exit(EX_USAGE);
			break;

		default:
			exit(EX_USAGE);
			break;
		}
	argc -= optind;
	argv += optind;

	if (argc > 0) {
		if (argc != 1) {
			usage();
			exit(EX_USAGE);
		}
		maildir = argv[0];
	}

	if ((home = getenv("HOME")) == NULL)
		err(EX_USAGE, "Missing HOME environment variable");
	strlcpy(pathtmp, home, sizeof(pathtmp));
	strlcat(pathtmp, "/" MAILESTD_MAIL_DIR, sizeof(pathtmp));
	if (maildir == NULL) {
		strlcpy(maildir0, pathtmp, sizeof(maildir0));
		maildir = maildir0;
	}
	if (lstat(maildir, &st) == -1)
		err(EX_USAGE, "%s", maildir);

	if (conf_file == NULL) {
		strlcpy(pathtmp, maildir, sizeof(pathtmp));
		strlcat(pathtmp, "/", sizeof(pathtmp));
		strlcat(pathtmp, MAILESTD_CONF_PATH, sizeof(pathtmp));
		conf_file = pathtmp;
	}
	if ((conf = parse_config(conf_file, maildir)) == NULL)
		exit(EXIT_FAILURE);
	if (noaction) {
		free_config(conf);
		fputs("configration OK\n", stderr);
		exit(EXIT_SUCCESS);
	}

	mailestd_init(&mailestd_s, conf, (isnull(suffix[0]))? NULL : suffix);
	free_config(conf);
	if (!foreground) {
		if (daemon(1, 0) == -1)
			err(EX_OSERR, "daemon");
	}

	mailestd_log_init();
	if (unlimit_data() == -1)
		mailestd_log(LOG_ERR, "unlimit_data: %m");
	if (unlimit_nofile() == -1)
		mailestd_log(LOG_ERR, "unlimit_nofile: %m");
	EVENT_INIT();
	mailestd_start(&mailestd_s, foreground);

	EVENT_LOOP(0);
	EVENT_BASE_FREE();

	mailestd_fini(&mailestd_s);
	mailestd_log_fini();

	exit(EXIT_SUCCESS);
}

static void
mailestd_init(struct mailestd *_this, struct mailestd_conf *conf,
    char const **suffix)
{
	int			 sock, i, ns = 0;
	mode_t			 oumask;
	struct sockaddr_un	 sun;
	extern char		*__progname;
	const char		*deffolder[] = { MAILESTD_DEFAULT_FOLDERS };

	memset(_this, 0, sizeof(struct mailestd));

	strlcpy(_this->maildir, conf->maildir, sizeof(_this->maildir));

	if (debug == 0)
		debug = conf->debug;
	RB_INIT(&_this->root);
	TAILQ_INIT(&_this->rfc822_pendings);
	TAILQ_INIT(&_this->gather_pendings);
	TAILQ_INIT(&_this->rfc822_tasks);
	_this->rfc822_task_max = conf->tasks;
	TAILQ_INIT(&_this->ctls);
	TAILQ_INIT(&_this->gathers);
	strlcpy(_this->logfn, conf->log_path, sizeof(_this->logfn));
	strlcpy(_this->dbpath, conf->db_path, sizeof(_this->dbpath));
	_this->logsiz = conf->log_size;
	_this->logmax = conf->log_count;
	_this->doc_trimsize = conf->trim_size;
	if (conf->folders == NULL) {
		_this->folder = xcalloc(nitems(deffolder) + 1, sizeof(char *));
		for (i = 0; i < (int)nitems(deffolder); i++)
			_this->folder[i] = xstrdup(deffolder[i]);
		_this->folder[i] = NULL;
	} else {
		_this->folder = conf->folders;
		conf->folders = NULL;
	}
	_this->monitor = conf->monitor;
	_this->monitor_delay.tv_sec = conf->monitor_delay / 1000;
	_this->monitor_delay.tv_nsec = (conf->monitor_delay % 1000) * 1000000UL;

	for (i = 0; suffix != NULL && !isnull(suffix[i]); i++)
		/* nothing */;
	ns += i;
	for (i = 0; conf->suffixes != NULL && !isnull(conf->suffixes[i]); i++)
		/* nothing */;
	ns += i;
	if (ns == 0) {
		_this->suffix = xcalloc(2, sizeof(char *));
		_this->suffix[ns++] = xstrdup(MAILESTD_DEFAULT_SUFFIX);
	} else {
		_this->suffix = xcalloc(ns + 1, sizeof(char *));
		ns = 0;
		for (i = 0; suffix != NULL && !isnull(suffix[i]); i++)
			_this->suffix[ns++] = xstrdup(suffix[i]);
		for (i = 0; conf->suffixes != NULL &&
		    !isnull(conf->suffixes[i]); i++)
			_this->suffix[ns++] = xstrdup(conf->suffixes[i]);
	}
	_this->suffix[ns++] = NULL;

	_thread_spin_init(&_this->id_seq_lock, 0);

	memset(&sun, 0, sizeof(sun));
	sun.sun_family = AF_UNIX;
	strlcpy(sun.sun_path, conf->sock_path, sizeof(sun.sun_path));

	_this->sock_ctl = socket(PF_UNIX, SOCK_SEQPACKET, 0);
	if (_this->sock_ctl < 0)
		err(EX_OSERR, "socket");
	sock = socket(PF_UNIX, SOCK_SEQPACKET, 0);
	oumask = umask(077);
	if (connect(sock, (struct sockaddr *)&sun, sizeof(sun)) == 0)
		errx(1, "already running");
	else if (errno != EEXIST)
		unlink(conf->sock_path);
	if (bind(_this->sock_ctl, (struct sockaddr *)&sun, sizeof(sun)) == -1)
		err(EX_OSERR, "bind");
	umask(oumask);
}

static void
mailestd_start(struct mailestd *_this, bool fg)
{
	int		 i;
	int		 ntask = 0;
	struct task	*task;

	if (!fg)
		mailestd_log_rotation(_this->logfn, 0, 0);
	EVENT_SET(&_this->evsigterm, SIGTERM, EV_SIGNAL | EV_PERSIST,
	    mailestd_on_sigterm, _this);
	EVENT_SET(&_this->evsigint, SIGINT,  EV_SIGNAL | EV_PERSIST,
	    mailestd_on_sigint, _this);
	EVENT_SET(&_this->evtimer, -1, 0, mailestd_on_timer, _this);
	signal_add(&_this->evsigterm, NULL);
	signal_add(&_this->evsigint,  NULL);
	mailestd_on_timer(-1, 0, _this);    /* dummy to make a schedule */
	time(&_this->curr_time);

	/*
	 * prepare limited number of tasks to control the resource usage.
	 * works like kanban method.
	 */
	for (i = 0; i < _this->rfc822_task_max; i++) {
		task = xcalloc(1, sizeof(struct task_rfc822));
		TAILQ_INSERT_TAIL(&_this->rfc822_tasks, task, queue);
	}
	mailestd_monitor_init(_this);

	_this->workers[ntask++] = &_this->mainworker;
	_this->workers[ntask++] = &_this->dbworker;
	if (_this->monitor)
		_this->workers[ntask++] = &_this->monitorworker;
	_this->workers[ntask++] = NULL;
	for (i = 0; _this->workers[i] != NULL; i++) {
		task_worker_init(_this->workers[i], _this);
#ifndef MAILESTD_MT
		task_worker_start(_this->workers[i]);
#endif
	}
#ifdef MAILESTD_MT
	task_worker_start(&_this->mainworker);	/* this thread */
	task_worker_run(&_this->dbworker);	/* another thread */
	if (_this->monitor)
		mailestd_monitor_run(_this);	/* another thread */
#endif

	if (listen(_this->sock_ctl, 5) == -1)
		mailestd_log(LOG_ERR, "listen(): %m");
	mailestc_reset_ctl_event(_this);

	mailestd_log(LOG_INFO, "Started mailestd.  Process-Id=%d",
	    (int)getpid());
	mailestd_db_add_msgid_index(_this);
	mailestd_schedule_db_sync(_this);
}

static uint64_t
mailestd_new_id(struct mailestd *_this)
{
	uint64_t	 task_id;

	_thread_spin_lock(&_this->id_seq_lock);
	task_id = ++(_this->id_seq);
	_thread_spin_unlock(&_this->id_seq_lock);

	return (task_id);
}

static void
mailestd_stop(struct mailestd *_this)
{
	struct mailestc	*ctle, *ctlt;
	int		 i;

	/* stop the task threads */
	mailestd_schedule_message_all(_this, MAILESTD_TASK_STOP);

	evtimer_del(&_this->evtimer);
	/* stop signals and receiving controls */
	signal_del(&_this->evsigterm);
	signal_del(&_this->evsigint);
	event_del(&_this->evsock_ctl);
	close(_this->sock_ctl);
	TAILQ_FOREACH_SAFE(ctle, &_this->ctls, queue, ctlt) {
		mailestc_stop(ctle);
	}

	for (i = 0; _this->workers[i] != NULL; i++) {
		if (_thread_self() != _this->workers[i]->thread)
			    _thread_join(_this->workers[i]->thread, NULL);
	}
	/* entered single thread */

	mailestd_log(LOG_INFO, "Stopped mailestd");
}

static void
mailestd_fini(struct mailestd *_this)
{
	int		 i;
	struct rfc822	 *msge, *msgt;
	struct task	 *tske, *tskt;
	struct gather	 *gate, *gatt;

	TAILQ_FOREACH_SAFE(gate, &_this->gathers, queue, gatt) {
		TAILQ_REMOVE(&_this->gathers, gate, queue);
		free(gate);
	}
	TAILQ_FOREACH_SAFE(tske, &_this->gather_pendings, queue, tskt) {
		TAILQ_REMOVE(&_this->gather_pendings, tske, queue);
		free(tske);
	}
	TAILQ_FOREACH_SAFE(tske, &_this->rfc822_tasks, queue, tskt) {
		TAILQ_REMOVE(&_this->rfc822_tasks, tske, queue);
		free(tske);
	}
	TAILQ_FOREACH_SAFE(msge, &_this->rfc822_pendings, queue, msgt) {
		TAILQ_REMOVE(&_this->rfc822_pendings, msge, queue);
	}
	RB_FOREACH_SAFE(msge, rfc822_tree, &_this->root, msgt) {
		RB_REMOVE(rfc822_tree, &_this->root, msge);
		rfc822_free(msge);
	}
	mailestd_monitor_fini(_this);

	if (_this->suffix != NULL) {
		for (i = 0; !isnull(_this->suffix[i]); i++)
			free(_this->suffix[i]);
	}
	free(_this->suffix);
	if (_this->folder != NULL) {
		for (i = 0; !isnull(_this->folder[i]); i++)
			free(_this->folder[i]);
	}
	free(_this->folder);
	free(_this->sync_prev);

	_thread_spin_destroy(&_this->id_seq_lock);
}

static struct gather *
mailestd_get_gather(struct mailestd *_this, uint64_t id)
{
	static struct gather	*gathere;

	TAILQ_FOREACH(gathere, &_this->gathers, queue) {
		if (gathere->id == id)
			return (gathere);
	}

	return (NULL);
}

/***********************************************************************
 * Event handlers
 ***********************************************************************/
static void
mailestd_on_timer(int fd, short evmask, void *ctx)
{
	struct mailestd	*_this = ctx;
	struct timeval	 next;

	time(&_this->curr_time);
	if (evmask & EV_TIMEOUT) {
		mailestd_log_rotation(_this->logfn, _this->logsiz,
		    _this->logmax);
	}

	next.tv_sec = MAILESTD_LOGROTWHEN -
	    (_this->curr_time % MAILESTD_LOGROTWHEN);
	next.tv_usec = 0;
	event_add(&_this->evtimer, &next);
}

static void
mailestd_on_sigterm(int fd, short evmask, void *ctx)
{
	struct mailestd	*_this = ctx;

	mailestd_log(LOG_INFO, "Recevied SIGTERM");
	mailestd_stop(_this);
}

static void
mailestd_on_sigint(int fd, short evmask, void *ctx)
{
	struct mailestd	*_this = ctx;

	mailestd_log(LOG_INFO, "Recevied SIGINT");
	mailestd_stop(_this);
}

static void
mailestc_on_ctl_event(int fd, short evmask, void *ctx)
{
	int			 sock;
	struct sockaddr_un	 sun;
	socklen_t		 slen;
	struct mailestd		*_this = ctx;
	struct mailestc		*ctl;

	MAILESTD_DBG((LOG_DEBUG, "%s()", __func__));
	if (evmask & EV_READ) {
		slen = sizeof(sun);
		if ((sock = accept(_this->sock_ctl, (struct sockaddr *)&sun,
		    &slen)) < 0) {
			mailestd_log(LOG_ERR, "accept(): %m");
			goto out_read;
		}
		ctl = xcalloc(1, sizeof(struct mailestc));
		mailestc_start(ctl, _this, sock);
	}
out_read:
	mailestc_reset_ctl_event(_this);
	return;
}

static void
mailestc_reset_ctl_event(struct mailestd *_this)
{
	MAILESTD_ASSERT(_this->sock_ctl >= 0);

	EVENT_SET(&_this->evsock_ctl, _this->sock_ctl, EV_READ,
	    mailestc_on_ctl_event, _this);
	event_add(&_this->evsock_ctl, NULL);
}

static void
mailestd_get_all_folders(struct mailestd *_this, struct folder_tree *tree)
{
	int		 len, lmaildir;
	char		*ps;
	struct rfc822	*msge, msg0;
	char		 path[PATH_MAX];
	struct folder	*fld;

	lmaildir = strlen(_this->maildir);
	strlcpy(path, _this->maildir, sizeof(path));
	path[lmaildir++] = '/';
	path[lmaildir] = '\0';
	msg0.path = path;
	msge = RB_NFIND(rfc822_tree, &_this->root, &msg0);
	while (msge != NULL) {
		if (!is_parent_dir(_this->maildir, msge->path))
			break;
		ps = strrchr(msge->path, '/');
		MAILESTD_ASSERT(ps != NULL);
		len = (ps - msge->path);
		if (len <= 0)
			break;
		memcpy(path, msge->path, len);
		path[len] = '\0';
		fld = xcalloc(1, sizeof(struct folder));
		fld->path = xstrdup(path + lmaildir);
		RB_INSERT(folder_tree, tree, fld);
		path[len++] = '/' + 1;
		path[len] = '\0';
		msg0.path = path;
		msge = RB_NFIND(rfc822_tree, &_this->root, &msg0);
	}
}

static const char *
mailestd_folder_name(struct mailestd *_this, const char *dir, char *buf,
    int lbuf)
{
	int	ldirp;

	ldirp = strlen(_this->maildir);
	if (strncmp(_this->maildir, dir, ldirp) == 0 && dir[ldirp] == '/') {
		buf[0] = '+';
		strlcpy(buf + 1, dir + ldirp + 1, lbuf - 1);
	} else
		strlcpy(buf, dir, lbuf);

	return buf;
}

/***********************************************************************
 * Database operations, gathering messages from file system
 ***********************************************************************/
static ESTDB *
mailestd_db_open_rd(struct mailestd *_this)
{
	ESTDB	*db = NULL;
	int	 ecode;

	if (_this->db != NULL)
		return (_this->db);

	if ((db = est_db_open(_this->dbpath, ESTDBREADER, &ecode)) == NULL) {
		mailestd_log(LOG_ERR, "Opening DB: %s", est_err_msg(ecode));
		mailestd_db_error(_this);
	} else  {
		_this->db = db;
		_this->db_wr = false;
		mailestd_log(LOG_DEBUG, "Opened DB");
	}

	return (db);
}

static ESTDB *
mailestd_db_open_wr(struct mailestd *_this)
{
	ESTDB	*db = NULL;
	int	 ecode;

	if (_this->db != NULL) {
		if (_this->db_wr)
			return (_this->db);
		if (!est_db_close(_this->db, &ecode))
			mailestd_log(LOG_ERR, "est_db_close: %s",
			    est_err_msg(ecode));
		_this->db = NULL;
	}

	if ((db = est_db_open(_this->dbpath,
	    ESTDBWRITER | ESTDBCREAT | ESTDBHUGE, &ecode)) == NULL) {
		mailestd_log(LOG_ERR, "Opening DB: %s", est_err_msg(ecode));
		mailestd_db_error(_this);
	} else  {
		_this->db = db;
		_this->db_wr = true;
		mailestd_log(LOG_INFO, "Opened DB for writing");
	}

	return (db);
}

static void
mailestd_db_close(struct mailestd *_this)
{
	int	 ecode;

	if (_this->db != NULL) {
		if (debug > 1)
			est_db_set_informer(_this->db, mailestd_db_informer,
			    NULL);
		if (!est_db_close(_this->db, &ecode))
			mailestd_log(LOG_ERR, "Closing DB: %s",
			    est_err_msg(ecode));
		_this->db = NULL;
	}
}

static void
mailestd_db_add_msgid_index(struct mailestd *_this)
{
	int	 i;
	CBLIST	*exprs;
	ESTDB	*db;
	bool	 msgid_found = false, parid_found = false;
	struct stat	 st;

	if (lstat(_this->dbpath, &st) == -1 && errno == ENOENT)
		goto db_noent;
	if ((db = mailestd_db_open_rd(_this)) == NULL)
		return;
	if ((exprs = est_db_attr_index_exprs(db)) != NULL) {
		for (i = 0; i < cblistnum(exprs); i++) {
			if (!strncasecmp(ATTR_MSGID "=",
			    cblistval(exprs, i, NULL), sizeof(ATTR_MSGID)))
				msgid_found = true;
			if (!strncasecmp(ATTR_PARID "=",
			    cblistval(exprs, i, NULL), sizeof(ATTR_PARID)))
				parid_found = true;
			if (msgid_found && parid_found)
				break;
		}
		cblistclose(exprs);
	}
db_noent:
	if (!msgid_found || !parid_found) {
		if ((db = mailestd_db_open_wr(_this)) == NULL)
			return;
		if (!msgid_found) {
			mailestd_log(LOG_INFO, "Adding \""ATTR_MSGID"\" index");
			est_db_add_attr_index(db, ATTR_MSGID, ESTIDXATTRSTR);
		}
		if (!parid_found) {
			mailestd_log(LOG_INFO, "Adding \""ATTR_PARID"\" index");
			est_db_add_attr_index(db, ATTR_PARID, ESTIDXATTRSTR);
		}
		mailestd_db_close(_this);
	}
}

static int
mailestd_db_sync(struct mailestd *_this)
{
	ESTDB		*db;
	int		 i, id, ldir, delete;
	char		 dir[PATH_MAX + 128];
	const char	*prev, *fn, *uri, *errstr, *folder;
	ESTDOC		*doc;
	struct rfc822	*msg, msg0;
	struct tm	 tm;
	struct task	*tske, *tskt;
	struct folder	*flde, *fldt;
	struct folder_tree
			 folders;

	MAILESTD_ASSERT(_thread_self() == _this->dbworker.thread);

	if ((db = mailestd_db_open_rd(_this)) == NULL)
		return (-1);

	prev = _this->sync_prev;
	est_db_iter_init(db, prev);
	for (i = 0; (id = est_db_iter_next(db)) > 0; i++) {
		doc = est_db_get_doc(db, id, ESTGDNOTEXT);
		if (doc == NULL)
			continue;

		uri = est_doc_attr(doc, ESTDATTRURI);
		if (uri == NULL || strncmp(uri, URIFILE "/", 8) != 0)
			continue;
		fn = URI2PATH(uri);

		msg0.path = (char *)fn;
		msg = RB_FIND(rfc822_tree, &_this->root, &msg0);
		if (msg == NULL) {
			msg = xcalloc(1, sizeof(struct rfc822));
			msg->path = xstrdup(fn);
			RB_INSERT(rfc822_tree, &_this->root, msg);
		}
		if (!msg->ontask && msg->db_id == 0) {
			strptime(est_doc_attr(doc, ESTDATTRMDATE),
			    MAILESTD_TIMEFMT, &tm);
			msg->db_id = id;
			msg->mtime = timegm(&tm);
			msg->size = strtonum(est_doc_attr(doc, ESTDATTRSIZE), 0,
			    INT64_MAX, &errstr);
		}

		if (i >= MAILESTD_DBSYNC_NITER) {
			free(_this->sync_prev);
			_this->sync_prev = xstrdup(uri);
			est_doc_delete(doc);
			mailestd_schedule_db_sync(_this); /* schedule again */
			return (0);
		}
		est_doc_delete(doc);
	}
	free(_this->sync_prev);
	_this->sync_prev = NULL;

	mailestd_log(LOG_INFO, "Database cache updated");

	TAILQ_FOREACH_SAFE(tske, &_this->gather_pendings, queue, tskt) {
		TAILQ_REMOVE(&_this->gather_pendings, tske, queue);
		delete = 0;
		folder = ((struct task_gather *)tske)->folder;
		strlcpy(dir, _this->maildir, sizeof(dir));
		strlcat(dir, "/", sizeof(dir));
		strlcat(dir, folder, sizeof(dir));
		strlcat(dir, "/", sizeof(dir));
		ldir = strlen(dir);
		msg0.path = dir;
		for (msg = RB_NFIND(rfc822_tree, &_this->root, &msg0);
		    msg != NULL; msg = RB_NEXT(rfc822_tree, &_this->root, msg)){
			if (strncmp(msg->path, dir, ldir) != 0)
				break;
			if (msg->fstime == 0 && !msg->ontask) {
				mailestd_schedule_deldb(_this, msg);
				delete++;
			}
		}
		if (delete > 0)
			mailestd_log(LOG_INFO, "Cleanup %s%s (Remove: %d)",
			    (folder[0] != '/')? "+" : "", folder, delete);
		free(tske);
	}
	_this->db_sync_time = _this->curr_time;
	if (_this->monitor) {
		RB_INIT(&folders);
		mailestd_get_all_folders(_this, &folders);
		RB_FOREACH_SAFE(flde, folder_tree, &folders, fldt) {
			RB_REMOVE(folder_tree, &folders, flde);
			mailestd_schedule_monitor(_this, flde->path);
			folder_free(flde);
		}
	}

	return (0);
}

static int
mailestd_gather(struct mailestd *_this, struct task_gather *task)
{
	int		 lrdir, update = 0, delete = 0, total = 0;
	char		 rdir[PATH_MAX], buf[PATH_MAX], *paths[2];
	const char	*folder = task->folder;
	struct gather	*ctx;
	FTS		*fts;
	struct rfc822	*msge, msg0;
	time_t		 curr_time;
	struct folder	*flde, *fldt;
	struct folder_tree
			 folders;

	RB_INIT(&folders);
	ctx = mailestd_get_gather(_this, task->gather_id);
	mailestd_log(LOG_DEBUG, "Gathering %s ...", mailestd_folder_name(
	    _this, folder, buf, sizeof(buf)));
	if (folder[0] == '/')
		strlcpy(rdir, folder, sizeof(rdir));
	else {
		strlcpy(rdir, _this->maildir, sizeof(rdir));
		strlcat(rdir, "/", sizeof(rdir));
		strlcat(rdir, folder, sizeof(rdir));
	}
	paths[0] = rdir;
	paths[1] = NULL;
	lrdir = strlen(rdir);

	if ((fts = fts_open(paths, FTS_LOGICAL | FTS_NOCHDIR, NULL)) == NULL) {
		mailestd_log(LOG_ERR, "fts_open(%s): %m", folder);
		goto out;
	}
	curr_time = _this->curr_time;
	update = mailestd_fts(_this, ctx, curr_time, fts, fts_read(fts),
	    &folders);
	fts_close(fts);

	MAILESTD_ASSERT(lrdir + 1 < (int)sizeof(rdir));
	rdir[lrdir++] = '/';
	rdir[lrdir] = '\0';

	msg0.path = rdir;
	for (msge = RB_NFIND(rfc822_tree, &_this->root, &msg0);
	    msge != NULL; msge = RB_NEXT(rfc822_tree, &_this->root, msge)) {
		if (strncmp(msge->path, rdir, lrdir) != 0)
			break;
		total++;
		if (msge->fstime != curr_time && !msge->ontask) {
			delete++;
			MAILESTD_ASSERT(msge->db_id != 0);
			if (ctx != NULL) {
				ctx->dels++;
				msge->gather_id = ctx->id;
			}
			mailestd_schedule_deldb(_this, msge);
		}
	}

	mailestd_log(LOG_DEBUG, "Gathered %s (Total: %d Remove: %d Update: %d)",
	    mailestd_folder_name(_this, folder, buf, sizeof(buf)),
	    total, delete, update);
out:
	if (ctx != NULL) {
		if (ctx->puts == ctx->puts_done &&
		    ctx->dels == ctx->dels_done &&
		    (update > 0 || delete > 0)) {
			strlcpy(ctx->errmsg, "other task exists",
			    sizeof(ctx->errmsg));
			mailestd_gather_inform(_this, NULL, ctx);
		} else if (_this->dbworker.suspend) {
			strlcpy(ctx->errmsg,
			    "database tasks are suspended",
			    sizeof(ctx->errmsg));
			mailestd_gather_inform(_this, NULL, ctx);
		} else
			mailestd_gather_inform(_this, (struct task *)task, ctx);
	}
	RB_FOREACH_SAFE(flde, folder_tree, &folders, fldt) {
		RB_REMOVE(folder_tree, &folders, flde);
		if (_this->monitor)
			mailestd_schedule_monitor(_this, flde->path);
		folder_free(flde);
	}

	return (0);
}

static void
mailestd_gather_inform(struct mailestd *_this, struct task *task,
    struct gather *gat)
{
	int		 notice = 0;
	struct gather	*gather = gat;

	if (task != NULL) {
		if (gather == NULL && (gather = mailestd_get_gather(_this,
		    ((struct task_rfc822 *)task)->msg->gather_id)) == NULL)
			return;
		switch (task->type) {
		default:
			break;

		case MAILESTD_TASK_GATHER:
			if (++gather->folders_done == gather->folders && (
			    gather->dels_done == gather->dels ||
			    gather->puts_done == gather->puts))
				notice++;
			break;

		case MAILESTD_TASK_RFC822_DELDB:
			if (++gather->dels_done == gather->dels &&
			    gather->folders_done == gather->folders)
				notice++;
			break;

		case MAILESTD_TASK_RFC822_PUTDB:
			if (++gather->puts_done == gather->puts &&
			    gather->folders_done == gather->folders)
				notice++;
			break;
		}
		if (notice > 0)
			mailestd_schedule_inform(_this, gather->id,
			    (u_char *)gather, sizeof(struct gather));
		if (gather->folders_done == gather->folders &&
		    gather->dels_done == gather->dels &&
		    gather->puts_done == gather->puts) {
			mailestd_log(LOG_INFO,
			    "Updated %s (Folders: %d Remove: %d "
			    "Update: %d)", gather->target,
			    gather->folders_done, gather->dels_done,
			    gather->puts_done);
			TAILQ_REMOVE(&_this->gathers, gather, queue);
			free(gather);
		}
	} else {
		mailestd_log(LOG_INFO,
		    "Updating %s failed (Folders: %d Remove: %d Update: %d): "
		    "%s", gather->target, gather->folders_done,
		    gather->dels_done, gather->puts_done, gather->errmsg);
		mailestd_schedule_inform(_this, gather->id,
		    (u_char *)gather, sizeof(struct gather));
		TAILQ_REMOVE(&_this->gathers, gather, queue);
		free(gather);
	}
}

static int
mailestd_fts(struct mailestd *_this, struct gather *ctx, time_t curr_time,
    FTS *fts, FTSENT *ftse, struct folder_tree *folders)
{
	int		 i, j, update = 0;
	const char	*bn, *errstr;
	struct rfc822	*msg, msg0;
	bool		 needupdate = false;
	char		 uri[PATH_MAX + 128];
	struct tm	 tm;
	ESTDOC		*doc;
	int		 db_id;
	struct folder	*fld;

	strlcpy(uri, URIFILE, sizeof(uri));
	do {
		needupdate = false;
		if (ftse == NULL)
			break;
		if (_this->monitor && ftse->fts_info == FTS_D) {
			fld = xcalloc(1, sizeof(struct folder));
			fld->path = xstrdup(ftse->fts_path);
			RB_INSERT(folder_tree, folders, fld);
		}
		if (!(ftse->fts_info == FTS_F || ftse->fts_info == FTS_SL))
			continue;

		bn = ftse->fts_name;
		for (i = 0; bn[i] != '\0'; i++) {
			if (!isdigit(bn[i]))
				break;
		}
		if (bn[i] != '\0') {
			if (_this->suffix == NULL)
				continue;
			for (j = 0; !isnull(_this->suffix[j]); j++) {
				if (strcmp(bn + i, _this->suffix[j]) == 0)
					break;
			}
			if (isnull(_this->suffix[j]))
				continue;
		}
		msg0.path = ftse->fts_path;
		msg = RB_FIND(rfc822_tree, &_this->root, &msg0);
		if (msg == NULL) {
			msg = xcalloc(1, sizeof(struct rfc822));
			msg->path = xstrdup(ftse->fts_path);
			RB_INSERT(rfc822_tree, &_this->root, msg);
			strlcpy(uri + 7, ftse->fts_path, sizeof(uri) - 7);
			if (_this->db_sync_time == 0 &&
			    mailestd_db_open_rd(_this) != NULL &&
			    (db_id = est_db_uri_to_id(_this->db, uri)) != -1 &&
			    (doc = est_db_get_doc(_this->db, db_id,
				    ESTGDNOKWD)) != NULL) {
				strptime(est_doc_attr(doc, ESTDATTRMDATE),
				    MAILESTD_TIMEFMT, &tm);
				msg->db_id = db_id;
				msg->mtime = timegm(&tm);
				msg->size = strtonum(est_doc_attr(doc,
				    ESTDATTRSIZE), 0, INT64_MAX, &errstr);
				est_doc_delete(doc);
			}
		}
		if (msg->db_id == 0 ||
		    msg->mtime != ftse->fts_statp->st_mtime ||
		    msg->size != ftse->fts_statp->st_size)
			needupdate = true;

		msg->fstime = curr_time;
		msg->mtime = ftse->fts_statp->st_mtime;
		msg->size = ftse->fts_statp->st_size;
		if (needupdate) {
			update++;
			if (!msg->ontask) {
				mailestd_schedule_draft(_this, ctx, msg);
				if (ctx != NULL)
					ctx->puts++;
			}
		}
	} while ((ftse = fts_read(fts)) != NULL);

	return (update);
}

static void
mailestd_draft(struct mailestd *_this, struct rfc822 *msg)
{
#ifdef HAVE_LIBESTDRAFT
	int		 fd = -1;
	struct stat	 st;
	char		*msgs = NULL, buf[PATH_MAX + 128];
	struct tm	 tm;

	if ((fd = open(msg->path, O_RDONLY)) < 0) {
		mailestd_log(LOG_WARNING, "open(%s): %m", msg->path);
		goto on_error;
	}
	if (fstat(fd, &st) == -1) {
		mailestd_log(LOG_WARNING, "fstat(%s): %m", msg->path);
		goto on_error;
	}
	if ((msgs = mmap(0, st.st_size, PROT_READ, MAP_PRIVATE | MAP_FILE, fd,
	    0)) == MAP_FAILED) {
		mailestd_log(LOG_WARNING, "mmap(%s): %m", msg->path);
		goto on_error;
	}
	msg->draft = est_doc_new_from_mime(
	    msgs, st.st_size, NULL, ESTLANGEN, 0);
	if (msg->draft == NULL) {
		mailestd_log(LOG_WARNING, "est_doc_new_from_mime(%s) failed",
		    msg->path);
		goto on_error;
	}
	est_doc_slim(msg->draft, MAILESTD_TRIMSIZE);
	strlcpy(buf, URIFILE, sizeof(buf));
	strlcat(buf, msg->path, sizeof(buf));
	est_doc_add_attr(msg->draft, ESTDATTRURI, buf);
	gmtime_r(&msg->mtime, &tm);
	strftime(buf, sizeof(buf), MAILESTD_TIMEFMT "\n", &tm);
	est_doc_add_attr(msg->draft, ESTDATTRMDATE, buf);

on_error:
	if (fd >= 0)
		close(fd);
	if (msgs != NULL)
		munmap(msgs, st.st_size);
	return;
#else
	FILE		*fpin, *fpout;
	char		*draft = NULL, buf[BUFSIZ];
	size_t		 siz, draftsiz = 0;
	int		 status;
	struct tm	 tm;

	fpout = open_memstream(&draft, &draftsiz);
	snprintf(buf, sizeof(buf), "estcmd draft -fm %s", msg->path);
	fpin = popen(buf, "r");
	while ((siz = fread(buf, 1, sizeof(buf), fpin)) > 0)
		fwrite(buf, 1, siz, fpout);
	status = pclose(fpin);
	if (status != 0)
		mailestd_log(LOG_ERR, "%s returns %d: %m", msg->path, status);
	fflush(fpout);
	fclose(fpout);
	if (draftsiz == 0) {
		mailestd_log(LOG_ERR, "couldn not parse %s??", msg->path);
		free(draft);
		return;
	} else {
		MAILESTD_ASSERT(msg->draft == NULL);
		MAILESTD_ASSERT(draftsiz > 0);
		msg->draft = est_doc_new_from_draft(draft);
		gmtime_r(&msg->mtime, &tm);
		strftime(buf, sizeof(buf), MAILESTD_TIMEFMT "\n", &tm);
		est_doc_add_attr(msg->draft, ESTDATTRMDATE, buf);
	}
#endif
}

static void
mailestd_putdb(struct mailestd *_this, struct rfc822 *msg)
{
	int		 ecode;

	MAILESTD_ASSERT(_thread_self() == _this->dbworker.thread);
	MAILESTD_ASSERT(msg->draft != NULL);
	MAILESTD_ASSERT(_this->db != NULL);

	if (est_db_put_doc(_this->db, msg->draft, 0)) {
		msg->db_id = est_doc_id(msg->draft);
		if (debug > 2)
			mailestd_log(LOG_DEBUG, "put %s successfully.  id=%d",
			    msg->path, msg->db_id);
	} else {
		ecode = est_db_error(_this->db);
		mailestd_log(LOG_WARNING,
		    "putting %s failed: %s", msg->path, est_err_msg(ecode));
		mailestd_db_error(_this);
	}

	est_doc_delete(msg->draft);
	msg->draft = NULL;
}

static void
mailestd_deldb(struct mailestd *_this, struct rfc822 *msg)
{
	int		 ecode;

	MAILESTD_ASSERT(_thread_self() == _this->dbworker.thread);
	MAILESTD_ASSERT(msg->db_id != 0);
	MAILESTD_ASSERT(_this->db != NULL);

	if (est_db_out_doc(_this->db, msg->db_id, 0)) {
		if (debug > 2)
			mailestd_log(LOG_DEBUG, "delete %s(%d) successfully",
			    msg->path, msg->db_id);
	} else {
		ecode = est_db_error(_this->db);
		mailestd_log(LOG_WARNING, "deleting %s(%d) failed: %s",
		    msg->path, msg->db_id, est_err_msg(ecode));
		mailestd_db_error(_this);
	}
}

static void
mailestd_db_smew(struct mailestd *_this, struct task_smew *smew)
{
	int		 i, cnt = 0, *res, rnum, lmaildir, lfolder;
	const char	*uri, *msgid;
	char		 buf[BUFSIZ], *bufp = NULL;
	bool		 keepthis;
	size_t		 bufsiz = 0;
	FILE		*out;
	ESTDB		*db;
	ESTCOND		*cond;
	ESTDOC		*doc;
	struct doclist {
		ESTDOC			*doc;
		const char		*msgid;
		const char		*uri;
		TAILQ_ENTRY(doclist)	 queue;
	}		*doce, *docc, *doct;
	TAILQ_HEAD(, doclist)	 children, ancestors;

	lfolder = (isnull(smew->folder))? -1 : (int)strlen(smew->folder);

	if ((out = open_memstream(&bufp, &bufsiz)) == NULL)
		abort();

	if ((db = mailestd_db_open_rd(_this)) == NULL)
		goto out;

	lmaildir = strlen(_this->maildir);
	TAILQ_INIT(&children);
	TAILQ_INIT(&ancestors);
	/* search ancestors */
	for (i = 0, msgid = smew->msgid; msgid != NULL; i++) {
		strlcpy(buf, ATTR_MSGID	" " ESTOPSTREQ " ", sizeof(buf));
		strlcat(buf, msgid, sizeof(buf));
		if ((cond = est_cond_new()) == NULL)
			break;
		est_cond_add_attr(cond, buf);
		res = est_db_search(db, cond, &rnum, NULL);
		est_cond_delete(cond);
		msgid = NULL;
		if (rnum > 0) {
			if ((doc = est_db_get_doc(db, res[0], ESTGDNOTEXT))
			    == NULL)
				continue;
			doce = xcalloc(1, sizeof(struct doclist));
			doce->doc = doc;
			doce->msgid = est_doc_attr(doc, ATTR_MSGID);
			if (i == 0)
				TAILQ_INSERT_TAIL(&children, doce, queue);
			else
				TAILQ_INSERT_HEAD(&ancestors, doce, queue);
			msgid = est_doc_attr(doc, ATTR_PARID);
		}
	}

	/* search descendants */
	while ((doce = TAILQ_FIRST(&children)) != NULL) {
		TAILQ_REMOVE(&children, doce, queue);
		TAILQ_INSERT_TAIL(&ancestors, doce, queue);
		if (doce->msgid == NULL)
			continue;	/* can't become parent */
		strlcpy(buf, ATTR_PARID	" " ESTOPSTREQ " ", sizeof(buf));
		strlcat(buf, doce->msgid, sizeof(buf));
		if ((cond = est_cond_new()) == NULL)
			break;
		est_cond_add_attr(cond, buf);
		res = est_db_search(db, cond, &rnum, NULL);
		est_cond_delete(cond);
		for (i = 0; i < rnum; i++) {
			if ((doc = est_db_get_doc(db, res[i], ESTGDNOTEXT))
			    == NULL)
				continue;
			docc = xcalloc(1, sizeof(struct doclist));
			docc->doc = doc;
			docc->msgid = est_doc_attr(doc, ATTR_MSGID);
			TAILQ_INSERT_HEAD(&children, docc, queue);
		}
	}

	/* make the list unique */
	TAILQ_FOREACH_SAFE(doce, &ancestors, queue, doct) {
		keepthis = false;
		uri = est_doc_attr(doce->doc, ESTDATTRURI);
		if (uri != NULL) {
			if (is_parent_dir(_this->maildir, URI2PATH(uri))) {
				doce->uri = uri + lmaildir + 8;
				if (lfolder > 0 &&
				    !strncmp(doce->uri, smew->folder, lfolder)
				    && doce->uri[lfolder] == '/')
					/*
					 * When removing the duplcated
					 * messages, keep the messages in the
					 * folder specified.
					 */
					keepthis = true;
			} else
				doce->uri = URI2PATH(uri);
		}

		/* removing duplicated messges */
		TAILQ_FOREACH(docc, &ancestors, queue) {
			if (doce == docc)
				break;
			if (doce->msgid != NULL && docc->msgid != NULL &&
			    strcmp(doce->msgid, docc->msgid) == 0) {
				if (!keepthis) {
					TAILQ_REMOVE(&ancestors, doce, queue);
					est_doc_delete(doce->doc);
					free(doce);
				} else {
					TAILQ_REMOVE(&ancestors, docc, queue);
					est_doc_delete(docc->doc);
					free(docc);
				}
				break;
			}
		}
	}

	TAILQ_FOREACH_SAFE(doce, &ancestors, queue, doct) {
		TAILQ_REMOVE(&ancestors, doce, queue);
		fprintf(out, "%s\n", doce->uri);
		est_doc_delete(doce->doc);
		free(doce);
		cnt++;
	}
out:
	mailestd_log(LOG_INFO, "Done smew (%s%s%s)  Hit %d",
	    smew->msgid, (lfolder > 0)? ", +" : "",
	    (lfolder > 0)? smew->folder : "", cnt);
	fclose(out);
	mailestd_schedule_inform(_this, smew->id, (u_char *)bufp, bufsiz);
	free(bufp);
}

static void
mailestd_search(struct mailestd *_this, uint64_t task_id, ESTCOND *cond,
    enum SEARCH_OUTFORM outform)
{
	int	 i, rnum, *res, ecode;
	char	*bufp = NULL;
	size_t	 bufsiz = 0;
	ESTDOC	*doc;
	FILE	*out;

	MAILESTD_ASSERT(_thread_self() == _this->dbworker.thread);
	MAILESTD_ASSERT(_this->db != NULL);
	if ((out = open_memstream(&bufp, &bufsiz)) == NULL)
		abort();
	res = est_db_search(_this->db, cond, &rnum, NULL);
	if (res == NULL) {
		ecode = est_db_error(_this->db);
		mailestd_log(LOG_INFO,
		    "Search(%s) failed: %s", cond->phrase, est_err_msg(ecode));
		mailestd_schedule_inform(_this, task_id, NULL, 0);
	} else {
		mailestd_log(LOG_INFO,
		    "Searched(%s).  Hit %d", cond->phrase, rnum);
		for (i = 0; i < rnum; i++) {
			doc = est_db_get_doc(_this->db, res[i], ESTGDNOKWD);
			if (doc == NULL) {
				ecode = est_db_error(_this->db);
				mailestd_log(LOG_WARNING,
				    "est_db_get_doc(id=%d) failed: %s",
				    res[i], est_err_msg(ecode));
			} else {
				switch (outform) {
				case SEARCH_OUTFORM_COMPAT_VU:
					fprintf(out, "%d\t%s\n", res[i],
					    est_doc_attr(doc, ESTDATTRURI));
					break;
				}
			}
		}
		fclose(out);
		mailestd_schedule_inform(_this, task_id, (u_char *)bufp,
		    bufsiz);
		free(bufp);
	}
}

static void
mailestd_db_informer(const char *msg, void *opaque)
{
	mailestd_log(LOG_DEBUG, "DB: %s", msg);
}

static void
mailestd_db_error(struct mailestd *_this)
{
	struct gather *gate, *gatt;

	TAILQ_FOREACH_SAFE(gate, &_this->gathers, queue, gatt) {
		strlcpy(gate->errmsg, "Database broken", sizeof(gate->errmsg));
		mailestd_gather_inform(_this, NULL, gate);
	}
	mailestd_log(LOG_WARNING, "Database may be broken.  Operations for "
	    "the datatabase will be suspended.  Try \"estcmd repair -rst %s\", "
	    "then \"mailestctl resume\".  If the error continues, you may "
	    "need to recreate the database", _this->dbpath);
	mailestd_schedule_message_all(_this, MAILESTD_TASK_SUSPEND);
}

static uint64_t
mailestd_schedule_db_sync(struct mailestd *_this)
{
	struct task	*task;

	task = xcalloc(1, sizeof(struct task));
	task->type = MAILESTD_TASK_SYNCDB;

	return (task_worker_add_task(&_this->dbworker, task));
}

static bool
mailestd_folder_match(struct mailestd *_this, const char *folder)
{
	int		 i;
	bool		 neg;
	const char	*pat;

	for (i = 0; _this->folder != NULL && !isnull(_this->folder[i]); i++) {
		neg = false;
		pat = _this->folder[i];
		if (*pat == '!') {
			pat++;
			neg = true;
		}
		if (fnmatch(pat, folder, 0) == 0) {
			if (neg)
				return (false);
			break;
		}
	}

	return (true);
}

static uint64_t
mailestd_schedule_gather0(struct mailestd *_this, struct gather *ctx,
    const char *folder)
{
	struct task_gather	*task;

	task = xcalloc(1, sizeof(struct task_gather));
	task->type = MAILESTD_TASK_GATHER;
	task->highprio = true;
	task->gather_id = ctx->id;
	ctx->folders++;
	strlcpy(task->folder, folder, sizeof(task->folder));

	return (task_worker_add_task(&_this->dbworker, (struct task *)task));
}

static uint64_t
mailestd_schedule_gather(struct mailestd *_this, const char *folder)
{
	int			 i, len;
	char			 path[PATH_MAX];
	glob_t			 gl;
	struct stat		 st;
	DIR			*dp;
	struct dirent		*de;
	ssize_t			 lmaildir;
	struct gather		*ctx;
	uint64_t		 ctx_id;
	struct folder_tree	 folders;
	struct folder		*flde, *fldt, fld0;
	bool			 found = false;
	struct rfc822		 msg0;
	const char		*fn;

	ctx = xcalloc(1, sizeof(struct gather));
	ctx->id = mailestd_new_id(_this);
	if (isnull(folder))
		strlcpy(ctx->target, "all", sizeof(ctx->target));
	else
		mailestd_folder_name(_this, folder, ctx->target,
		    sizeof(ctx->target));

	ctx_id = ctx->id;	/* need this backup */
	TAILQ_INSERT_TAIL(&_this->gathers, ctx, queue);

	if (folder[0] != '\0') {
		if (folder[0] != '/') {
			memset(&gl, 0, sizeof(gl));
			strlcpy(path, _this->maildir, sizeof(path));
			lmaildir = strlcat(path, "/", sizeof(path));
			strlcat(path, folder, sizeof(path));
			if (glob(path, GLOB_BRACE, NULL, &gl) == 0) {
				for (i = 0; i < gl.gl_pathc; i++) {
					if (lstat(gl.gl_pathv[i], &st) != 0 ||
					    !S_ISDIR(st.st_mode))
						continue;
					if (!mailestd_folder_match(_this,
					    gl.gl_pathv[i] + lmaildir))
						continue;
					mailestd_schedule_gather0(_this, ctx,
					    gl.gl_pathv[i] + lmaildir);
				}
				globfree(&gl);
				found = true;
			}
		} else {
			if (lstat(folder, &st) == 0 && S_ISDIR(st.st_mode)) {
				mailestd_schedule_gather0(_this, ctx, folder);
				found = true;
			} else
				strlcpy(path, folder, sizeof(path));
		}
		if (!found) {
			len = strlen(path);
			strlcat(path, "/", sizeof(path));
			msg0.path = path;
			if (RB_NFIND(rfc822_tree, &_this->root, &msg0) != NULL){
				path[len] = '\0';
				mailestd_schedule_gather0(_this, ctx, path);
			}
		}
	} else {
		if ((dp = opendir(_this->maildir)) == NULL)
			mailestd_log(LOG_ERR, "%s: %m", _this->maildir);
		else {
			RB_INIT(&folders);
			mailestd_get_all_folders(_this, &folders);
			while ((de = readdir(dp)) != NULL) {
				if (de->d_type != DT_DIR ||
				    strcmp(de->d_name, ".") == 0 ||
				    strcmp(de->d_name, "..") == 0)
					continue;
				if (!mailestd_folder_match(_this, de->d_name))
					continue;
				mailestd_schedule_gather0(_this, ctx,
				    de->d_name);
				fld0.path = de->d_name;
				flde = RB_FIND(folder_tree, &folders, &fld0);
				if (flde != NULL) {
					RB_REMOVE(folder_tree, &folders, flde);
					folder_free(flde);
				}
			}
			closedir(dp);
			RB_FOREACH_SAFE(flde, folder_tree, &folders, fldt) {
				RB_REMOVE(folder_tree, &folders, flde);
				mailestd_schedule_gather0(_this, ctx,
				    flde->path);
				folder_free(flde);
			}
		}
		if (_this->monitor)
			mailestd_schedule_monitor(_this, _this->maildir);
	}
	if (ctx->folders == 0) {
		strlcpy(ctx->errmsg, "grabing folders", sizeof(ctx->errmsg));
		mailestd_gather_inform(_this, NULL, ctx); /* ctx is freed */
	}

	return (ctx_id);
}

static uint64_t
mailestd_schedule_draft(struct mailestd *_this, struct gather *gather,
    struct rfc822 *msg)
{
	struct task	*task;

	MAILESTD_ASSERT(_thread_self() == _this->dbworker.thread);
	MAILESTD_ASSERT(!msg->ontask);

	msg->ontask = true;
	msg->gather_id = (gather != NULL)? gather->id : 0;
	task = TAILQ_FIRST_ITEM(&_this->rfc822_tasks);
	if (task) {
		TAILQ_REMOVE(&_this->rfc822_tasks, task, queue);
		((struct task_rfc822 *)task)->msg = msg;
		task->type = MAILESTD_TASK_RFC822_DRAFT;
		_this->rfc822_ntask++;

		return (task_worker_add_task(&_this->mainworker, task));
	} else
		TAILQ_INSERT_TAIL(&_this->rfc822_pendings, msg, queue);

	return (0);
}

static uint64_t
mailestd_reschedule_draft(struct mailestd *_this)
{
	struct rfc822	*msg;
	struct task	*task;

	MAILESTD_ASSERT(_thread_self() == _this->dbworker.thread);
	for (;;) {
		msg = TAILQ_FIRST_ITEM(&_this->rfc822_pendings);
		task = TAILQ_FIRST_ITEM(&_this->rfc822_tasks);
		if (msg == NULL || task == NULL)
			break;
		TAILQ_REMOVE(&_this->rfc822_tasks, task, queue);
		TAILQ_REMOVE(&_this->rfc822_pendings, msg, queue);
		((struct task_rfc822 *)task)->msg = msg;
		task->type = MAILESTD_TASK_RFC822_DRAFT;
		_this->rfc822_ntask++;

		return (task_worker_add_task(&_this->dbworker, task));
	}

	return (0);
}

static uint64_t
mailestd_schedule_putdb(struct mailestd *_this, struct task *task,
    struct rfc822 *msg)
{
	/* given task is a member of mailestd.rfc822_tasks */
	task->type = MAILESTD_TASK_RFC822_PUTDB;
	((struct task_rfc822 *)task)->msg = msg;

	return (task_worker_add_task(&_this->dbworker, task));
}

static uint64_t
mailestd_schedule_deldb(struct mailestd *_this, struct rfc822 *msg)
{
	struct task	*task;

	MAILESTD_ASSERT(!msg->ontask);

	msg->ontask = true;
	task = xcalloc(1, sizeof(struct task_rfc822));
	task->type = MAILESTD_TASK_RFC822_DELDB;
	((struct task_rfc822 *)task)->msg = msg;

	return (task_worker_add_task(&_this->dbworker, task));
}

static uint64_t
mailestd_schedule_search(struct mailestd *_this, ESTCOND *cond)
{
	struct task_search	*task;

	mailestd_log(LOG_INFO, "Searching(%s)", cond->phrase);

	task = xcalloc(1, sizeof(struct task_search));
	task->type = MAILESTD_TASK_SEARCH;
	task->cond = cond;
	task->highprio = true;

	return (task_worker_add_task(&_this->dbworker, (struct task *)task));
}

static uint64_t
mailestd_schedule_smew(struct mailestd *_this, const char *msgid,
    const char *folder)
{
	struct task_smew	*task;

	if (!isnull(folder))
		mailestd_log(LOG_INFO, "Starting smew (%s, +%s)",
		    msgid, folder);
	else
		mailestd_log(LOG_INFO, "Starting smew (%s)", msgid);

	task = xcalloc(1, sizeof(struct task_smew));
	task->type = MAILESTD_TASK_SMEW;
	strlcpy(task->msgid, msgid, sizeof(task->msgid));
	strlcpy(task->folder, folder, sizeof(task->folder));
	task->highprio = true;

	return (task_worker_add_task(&_this->dbworker, (struct task *)task));
}

static uint64_t
mailestd_schedule_inform(struct mailestd *_this, uint64_t task_id,
    u_char *inform, size_t informsiz)
{
	struct task_inform	*task;

	task = xcalloc(1, sizeof(struct task_inform) + informsiz);
	task->type = MAILESTD_TASK_INFORM;
	task->highprio = true;
	task->informsiz = informsiz;
	task->src_id = task_id;
	if (informsiz > 0)
		memcpy(&task->inform[0], inform, informsiz);

	return (task_worker_add_task(&_this->mainworker, (struct task *)task));
}

static void
mailestd_schedule_message_all(struct mailestd *_this,
    enum MAILESTD_TASK tsk_type)
{
	int		 i;
	struct task	*task;

	for (i = 0; _this->workers[i] != NULL; i++) {
		task = xcalloc(1, sizeof(struct task));
		task->type = tsk_type;
		task->highprio = true;
		task_worker_add_task(_this->workers[i], task);
	}
}

static uint64_t
mailestd_schedule_monitor(struct mailestd *_this, const char *path)
{
	struct task_monitor	*task;

	if (!_this->monitor)
		return (0);
	task = xcalloc(1, sizeof(struct task_monitor));
	task->type = MAILESTD_TASK_MONITOR_FOLDER;
	task->highprio = true;
	if (path[0] == '/')
		strlcpy(task->path, path, sizeof(task->path));
	else {
		strlcpy(task->path, _this->maildir, sizeof(task->path));
		strlcat(task->path, "/", sizeof(task->path));
		strlcat(task->path, path, sizeof(task->path));
	}

	return (task_worker_add_task(&_this->monitorworker,
	    (struct task *)task));
}

/***********************************************************************
 * Tasks
 ***********************************************************************/
static void
task_worker_init(struct task_worker *_this, struct mailestd *mailestd)
{
	int	 pairsock[2];

	memset(_this, 0, sizeof(struct task_worker));
	TAILQ_INIT(&_this->head);
	_thread_mutex_init(&_this->lock, NULL);
	_this->mailestd_this = mailestd;
	if (socketpair(PF_UNIX, SOCK_SEQPACKET, 0, pairsock) == -1)
		err(EX_OSERR, "socketpair()");
	if (setnonblock(pairsock[0]) == -1)
		err(EX_OSERR, "setnonblock()");
	if (setnonblock(pairsock[1]) == -1)
		err(EX_OSERR, "setnonblock()");
	_this->sock = pairsock[1];
	_this->sock_itc = pairsock[0];
}

static void
task_worker_start(struct task_worker *_this)
{
	_this->thread = _thread_self();

	EVENT_SET(&_this->evsock, _this->sock, EV_READ | EV_PERSIST,
		task_worker_on_itc_event, _this);
	event_add(&_this->evsock, NULL);
}

#ifdef MAILESTD_MT
static void *
task_worker_start0(void *ctx)
{
	struct task_worker *_this = ctx;

	EVENT_INIT();
	task_worker_start(_this);
	EVENT_LOOP(0);
	EVENT_BASE_FREE();
	return (NULL);
}
#endif

static void
task_worker_run(struct task_worker *_this)
{
#ifdef MAILESTD_MT
	_thread_create(&_this->thread, NULL, task_worker_start0, _this);
#endif
}

static void
task_worker_stop(struct task_worker *_this)
{
	struct task	*tske, *tskt;

	MAILESTD_ASSERT(_thread_self() == _this->thread);
	if (_this->sock >= 0) {
		if (event_initialized(&_this->evsock))
			event_del(&_this->evsock);
		close(_this->sock);
		close(_this->sock_itc);
		_this->sock = _this->sock_itc = -1;
	}
	_thread_mutex_destroy(&_this->lock);
	TAILQ_FOREACH_SAFE(tske, &_this->head, queue, tskt) {
		free(tske);
	}
}

static uint64_t
task_worker_add_task(struct task_worker *_this, struct task *task)
{
	uint64_t	 id;

	task->id = mailestd_new_id(_this->mailestd_this);
	id = task->id;

	_thread_mutex_lock(&_this->lock);
	if (task->highprio)
		TAILQ_INSERT_HEAD(&_this->head, task, queue);
	else
		TAILQ_INSERT_TAIL(&_this->head, task, queue);
	_thread_mutex_unlock(&_this->lock);

	if (write(_this->sock_itc, "A", 1) < 0) {
		if (errno != EAGAIN)
			mailestd_log(LOG_WARNING, "%s: write(): %m", __func__);
	}

	return (id);
}

static void
task_worker_on_itc_event(int fd, short evmask, void *ctx)
{
	ssize_t			 siz;
	u_char			 buf[BUFSIZ];
	struct task_worker	*_this = ctx;

	time(&_this->mailestd_this->curr_time);
	if (evmask & EV_READ) {
		for (;;) {
			siz = read(_this->sock, buf, sizeof(buf));
			if (siz <= 0) {
				if (siz < 0) {
					if (errno == EINTR || errno == EAGAIN)
						break;
				}
				task_worker_stop(_this);
				return;
			}
		}
		task_worker_on_proc(_this);
	}
}

static void
task_worker_on_proc(struct task_worker *_this)
{
	struct task			*task;
	struct task_inform		*inf;
	struct rfc822			*msg;
	bool				 stop = false;
	struct mailestd			*mailestd = _this->mailestd_this;
	_thread_t			 thread_this = _thread_self();
	struct task_dbworker_context	 dbctx;
	struct mailestc			*ce, *ct;
	enum MAILESTD_TASK		 task_type;

	memset(&dbctx, 0, sizeof(dbctx));
	while (!stop) {
		_thread_mutex_lock(&_this->lock);
		task = TAILQ_FIRST_ITEM(&_this->head);
		if (task != NULL) {
			if (_this->suspend && mailestd->db_sync_time == 0 &&
			    task->type == MAILESTD_TASK_GATHER)
				/*
				 * gathering before the first db_sync
				 * requires the database is working.
				 */
				task = NULL;
			else if (!_this->suspend || task->highprio)
				TAILQ_REMOVE(&_this->head, task, queue);
			else
				task = NULL;
		}
		_thread_mutex_unlock(&_this->lock);
		if (task == NULL) {
			if (!stop && thread_this == mailestd->dbworker.thread &&
			    !task_worker_on_proc_db(_this, &dbctx, NULL))
				continue;
			break;
		}

		task_type = task->type;
		switch (task_type) {
		case MAILESTD_TASK_RFC822_DRAFT:
			msg = ((struct task_rfc822 *)task)->msg;
			MAILESTD_ASSERT(msg->draft == NULL);
			mailestd_draft(mailestd, msg);
			if (msg->draft != NULL)
				estdoc_add_parid(msg->draft);
			/*
			 * This thread can't return the task even if creating
			 * a draft failed.  (since it's not dbworker)
			 */
			mailestd_schedule_putdb(mailestd, task, msg);
			task = NULL;	/* recycled */
			break;

		case MAILESTD_TASK_RFC822_PUTDB:
		case MAILESTD_TASK_RFC822_DELDB:
		case MAILESTD_TASK_SEARCH:
		case MAILESTD_TASK_SMEW:
			MAILESTD_ASSERT(thread_this ==
			    mailestd->dbworker.thread);
			task_worker_on_proc_db(_this, &dbctx, task);
			if (task_type == MAILESTD_TASK_RFC822_PUTDB)
				task = NULL;	/* recycled */
			break;

		case MAILESTD_TASK_SYNCDB:
			mailestd_db_sync(mailestd);
			break;

		case MAILESTD_TASK_GATHER:
			mailestd_gather(mailestd, (struct task_gather *)task);
			if (mailestd->db_sync_time == 0) {
				TAILQ_INSERT_TAIL(&mailestd->gather_pendings,
				    task, queue);
				task = NULL;
			}
			break;

		case MAILESTD_TASK_SUSPEND:
			_this->suspend = true;
			break;

		case MAILESTD_TASK_STOP:
			stop = true;
			if (thread_this == mailestd->dbworker.thread)
				task_worker_on_proc_db(_this, &dbctx, task);
			task_worker_stop(_this);
			break;

		case MAILESTD_TASK_RESUME:
			_this->suspend = false;
			break;

		case MAILESTD_TASK_INFORM:
			MAILESTD_ASSERT(thread_this ==
			    mailestd->mainworker.thread);
			inf = (struct task_inform *)task;
			TAILQ_FOREACH_SAFE(ce, &mailestd->ctls, queue, ct) {
				mailestc_task_inform(ce, inf->src_id,
				    inf->inform, inf->informsiz);
			}
			break;

		case MAILESTD_TASK_MONITOR_FOLDER:
			mailestd_monitor_folder(mailestd,
			    ((struct task_monitor *)task)->path);
			break;

		case MAILESTD_TASK_NONE:
			break;

		default:
			abort();
		}
		if (task)
			free(task);
	}
}

static bool
task_worker_on_proc_db(struct task_worker *_this,
    struct task_dbworker_context *ctx, struct task *task)
{
	struct rfc822		*msg;
	enum MAILESTD_TASK	 task_type;
	struct mailestd		*mailestd = _this->mailestd_this;
	struct task_search	*search;

	if (task == NULL)
		task_type = MAILESTD_TASK_NONE;
	else
		task_type = task->type;

	switch (task_type) {
	default:
		break;

	case MAILESTD_TASK_RFC822_PUTDB:
		msg = ((struct task_rfc822 *)task)->msg;
		if (mailestd_db_open_wr(mailestd) == NULL)
			break;
		ctx->puts++;
		ctx->resche++;
		if (msg->draft != NULL)
			mailestd_putdb(mailestd, msg);
		mailestd_gather_inform(mailestd, task, NULL);
		msg->ontask = false;
		TAILQ_INSERT_TAIL(&mailestd->rfc822_tasks, task, queue);
		mailestd->rfc822_ntask--;
		break;

	case MAILESTD_TASK_RFC822_DELDB:
		msg = ((struct task_rfc822 *)task)->msg;
		if (mailestd_db_open_wr(mailestd) == NULL)
			break;
		ctx->dels++;
		mailestd_gather_inform(mailestd, task, NULL);
		mailestd_deldb(mailestd, msg);
		RB_REMOVE(rfc822_tree, &mailestd->root, msg);
		rfc822_free(msg);
		break;

	case MAILESTD_TASK_SEARCH:
		search = (struct task_search *)task;
		if (mailestd_db_open_rd(mailestd) == NULL)
			break;
		mailestd_search(mailestd, task->id, search->cond,
		    search->outform);
		break;

	case MAILESTD_TASK_SMEW:
		mailestd_db_smew(mailestd, (struct task_smew *)task);
		break;

	case MAILESTD_TASK_NONE:
		if (ctx->resche)
			mailestd_reschedule_draft(mailestd);
		if (mailestd->db == NULL || !mailestd->db_wr)
			/* Keep the read only db connection */
			break;
		if (ctx->puts + ctx->dels > 0 &&
		    est_db_used_cache_size(mailestd->db) > MAILESTD_DBFLUSHSIZ){
			/*
			 * When we wrote somthing, flush the DB first.  Since
			 * closing DB takes long time.  Flush before close
			 * to make the other tasks can interrupt.
			 */
			if (debug > 1) {
				mailestd_log(LOG_DEBUG, "Flusing DB %d",
				    est_db_used_cache_size(mailestd->db));
				est_db_set_informer(mailestd->db,
				    mailestd_db_informer, NULL);
			}
			est_db_flush(mailestd->db, MAILESTD_DBFLUSHSIZ);
			if (debug > 1) {
				mailestd_log(LOG_DEBUG, "Flusinged DB %d",
				    est_db_used_cache_size(mailestd->db));
				est_db_set_informer(mailestd->db, NULL, NULL);
			}
			return (false);		/* check the other tasks
						   then call me again */
		}
		if (!ctx->optimized && ctx->puts + ctx->dels > 800) {
			mailestd_log(LOG_DEBUG, "Optimizing DB");
			est_db_optimize(mailestd->db,
			    ESTOPTNOPURGE | ESTOPTNODBOPT);
			mailestd_log(LOG_DEBUG, "Optimized DB");
			ctx->optimized = true;
			return (false);
		}
		/* Then close the DB */
		/* FALLTHROUGH */

	case MAILESTD_TASK_STOP:
		if (mailestd->db != NULL) {
			mailestd_log(LOG_INFO, "Closing DB");
			if (debug > 1)
				est_db_set_informer(mailestd->db,
				    mailestd_db_informer, NULL);
			mailestd_db_close(mailestd);
			mailestd_log(LOG_INFO, "Closed DB (put=%d delete=%d)",
			    ctx->puts, ctx->dels);
		}
		break;
	}

	if (ctx->resche && mailestd->rfc822_ntask >=
	    mailestd->rfc822_task_max / 2) {
		mailestd_reschedule_draft(mailestd);
		ctx->resche = 0;
	}

	return (true);
}

/***********************************************************************
 * mailestc
 ***********************************************************************/
static struct timeval mailestc_timeout = { MAILESTCTL_IDLE_TIMEOUT, 0 };

static void
mailestc_start(struct mailestc *_this, struct mailestd *mailestd, int sock)
{
	memset(_this, 0, sizeof(struct mailestc));
	_this->sock = sock;
	_this->mailestd_this = mailestd;
	_this->wbuf = bytebuffer_create(256);
	bytebuffer_flip(_this->wbuf);
	if (_this->wbuf == NULL)
		abort();
	EVENT_SET(&_this->evsock, _this->sock, EV_READ | EV_WRITE | EV_TIMEOUT,
	    mailestc_on_event, _this);
	event_add(&_this->evsock, &mailestc_timeout);
	TAILQ_INSERT_TAIL(&_this->mailestd_this->ctls, _this, queue);
}

static void
mailestc_stop(struct mailestc *_this)
{
	MAILESTD_ASSERT(_this->sock >= 0);
	bytebuffer_destroy(_this->wbuf);
	event_del(&_this->evsock);
	close(_this->sock);
	TAILQ_REMOVE(&_this->mailestd_this->ctls, _this, queue);
	free(_this);
}

static void
mailestc_on_event(int fd, short evmask, void *ctx)
{
	struct mailestc		*_this = ctx;
	struct mailestd		*mailestd = _this->mailestd_this;
	short			 nev;
	ssize_t			 siz;
	char			 msgbuf[MAILESTD_SOCK_MSGSIZ];
	struct {
		enum MAILESTCTL_CMD	 command;
		char			 space[BUFSIZ];
	} cmd;
	struct mailestctl_smew	*smew = (struct mailestctl_smew *)&cmd;
	struct mailestctl_update
				*update = (struct mailestctl_update *)&cmd;

	nev = EV_READ | EV_TIMEOUT;	/* next event */
	if (evmask & EV_READ) {
		if ((siz = read(_this->sock, &cmd, sizeof(cmd))) <= 0) {
			if (siz != 0) {
				if (errno == EINTR || errno == EAGAIN)
					return;
				mailestd_log(LOG_ERR, "%s(): read: %m",
				    __func__);
			}
			goto on_error;
		}
		switch (cmd.command) {
		case MAILESTCTL_CMD_NONE:
			break;

		case MAILESTCTL_CMD_DEBUGI:
			debug++;
			mailestd_log(LOG_INFO,
			    "debug++ requested.  debug=%d", debug);
			break;

		case MAILESTCTL_CMD_DEBUGD:
			debug--;
			mailestd_log(LOG_INFO,
			    "--debug requested.  debug=%d", debug);
			break;

		case MAILESTCTL_CMD_SUSPEND:
			mailestd_log(LOG_INFO, "suspend requested");
			mailestd_schedule_message_all(mailestd,
			    MAILESTD_TASK_SUSPEND);
			break;

		case MAILESTCTL_CMD_RESUME:
			mailestd_log(LOG_INFO, "resume requested");
			mailestd_schedule_message_all(mailestd,
			    MAILESTD_TASK_RESUME);
			break;

		case MAILESTCTL_CMD_STOP:
			mailestd_log(LOG_INFO, "Stop requested");
			mailestd_stop(mailestd);
			/* freed _this */
			return;

		case MAILESTCTL_CMD_SEARCH:
			if (mailestc_cmd_search(_this,
			    (struct mailestctl_search *)&cmd) != 0)
				goto on_error;
			break;

		case MAILESTCTL_CMD_SMEW:
			_this->monitoring_cmd = MAILESTCTL_CMD_SMEW;
			_this->monitoring_id =
			    mailestd_schedule_smew(mailestd,
				    smew->msgid, smew->folder);
			if (_this->monitoring_id == 0)
				goto on_error;
			break;

		case MAILESTCTL_CMD_UPDATE:
			_this->monitoring_cmd = MAILESTCTL_CMD_UPDATE;
			_this->monitoring_id = mailestd_schedule_gather(
			    mailestd, update->folder);
			if (_this->monitoring_id == 0)
				goto on_error;
			break;
		}
	}

	if (evmask & EV_WRITE || _this->wready) {
		_this->wready = true;
		if (bytebuffer_remaining(_this->wbuf) > 0) {
			siz = MINIMUM(sizeof(msgbuf),
			    bytebuffer_remaining(_this->wbuf));
			bytebuffer_get(_this->wbuf, msgbuf, siz);
			siz = write(_this->sock, msgbuf, siz);
			if (siz <= 0)
				goto on_error;
			_this->wready = false;
			if (_this->monitoring_stop &&
			    !bytebuffer_has_remaining(_this->wbuf)) {
				mailestc_stop(_this);
				return;
			}
		}
	} else if (evmask & EV_TIMEOUT) {
		/*
		 * Remark "else".  Skip run the timeout handler since the
		 * timer is also used to firing an event for writing.  See
		 * mailestc_send_message().
		 */
		if (_this->monitoring_cmd == MAILESTCTL_CMD_NONE)
			goto on_error;
	}

	if (!_this->wready)
		nev |= EV_WRITE;
	EVENT_SET(&_this->evsock, _this->sock, nev, mailestc_on_event, _this);
	event_add(&_this->evsock, &mailestc_timeout);

	return;
on_error:
	mailestc_stop(_this);
}

static int
mailestc_cmd_search(struct mailestc *_this, struct mailestctl_search *search)
{
	struct mailestd	*mailestd = _this->mailestd_this;
	ESTCOND		*cond;
	const char	*phrase = search->phrase;
	int		 i;

	cond = est_cond_new();
	if (phrase[0] != '\0') {
		while (*phrase == '\t' || *phrase == ' ')
			phrase++;
		est_cond_set_phrase(cond, phrase);
	}
	for (i = 0; i < (int)nitems(search->attrs) && search->attrs[i] != '\0';
	    i++)
		est_cond_add_attr(cond, search->attrs[i]);
	est_cond_set_order(cond, search->order);
	if (search->max > 0)
		est_cond_set_max(cond, search->max);
	_this->monitoring_id = mailestd_schedule_search(mailestd, cond);
	if (_this->monitoring_id == 0)
		return (-1);
	_this->monitoring_cmd = MAILESTCTL_CMD_SEARCH;

	return (0);
}

static void
mailestc_send_message(struct mailestc *_this, u_char *msg, size_t msgsiz)
{
	struct timeval soon = { 0, 0 };

	bytebuffer_compact(_this->wbuf);
	if (msgsiz > bytebuffer_remaining(_this->wbuf)) {
		if (bytebuffer_realloc(_this->wbuf,
		    bytebuffer_position(_this->wbuf) + msgsiz) != 0)
			abort();
	}
	bytebuffer_put(_this->wbuf, msg, msgsiz);
	bytebuffer_flip(_this->wbuf);
	event_add(&_this->evsock, &soon);	/* fire an I/O event */
}

static void
mailestc_task_inform(struct mailestc *_this, uint64_t task_id, u_char *inform,
    size_t informsiz)
{
	if (_this->monitoring_id != task_id)
		return;
	switch (_this->monitoring_cmd) {
	default:
		break;
	case MAILESTCTL_CMD_SMEW:
	case MAILESTCTL_CMD_SEARCH:
		if (informsiz == 0)
			mailestc_stop(_this);
		else
			mailestc_send_message(_this, inform, informsiz);
		_this->monitoring_stop = true;
		break;
	case MAILESTCTL_CMD_UPDATE:
	    {
		struct gather	*result = (struct gather *)inform;
		bool		 del_compl, put_compl;
		char		*msg = NULL, msg0[80];

		del_compl = (result->dels_done == result->dels)? true : false;
		put_compl = (result->puts_done == result->puts)? true : false;
		if (result->errmsg[0] != '\0') {
			strlcpy(msg0, result->errmsg, sizeof(msg0));
			strlcat(msg0, "...failed\n", sizeof(msg0));
			msg = msg0;
		} else if (put_compl && del_compl)
			msg = "new messages...done\n";
		else if (del_compl && result->dels_done > 0)
			msg = "old messages...done\n";
		if (msg != NULL) {
			mailestc_send_message(_this, (u_char *)msg,
			    strlen(msg));
		}
		if (result->errmsg[0] != '\0' || (
		    del_compl && put_compl &&
		    result->folders == result->folders_done))
			_this->monitoring_stop = true;
	    }
		break;
	}
}

/***********************************************************************
 * Monitoring
 ***********************************************************************/
static void
mailestd_monitor_init(struct mailestd *_this)
{
#ifdef MONITOR_KQUEUE
	if ((_this->monitor_kq = kqueue()) == -1)
		err(EX_OSERR, "kqueue");
	_this->monitor_kev = xcalloc(1, sizeof(struct kevent));
	_this->monitor_kev_siz = 1;
#endif
	RB_INIT(&_this->monitors);
}

#ifdef MAILESTD_MT
static void *
mailestd_monitor_start0(void *ctx)
{
	mailestd_monitor_start(ctx);
	return (NULL);
}
#endif

static void
mailestd_monitor_run(struct mailestd *_this)
{
#ifdef MONITOR_INOTIFY
	_this->monitor_in = inotify_init();	/* HERE? */
#endif
#ifdef MAILESTD_MT
	_thread_create(&_this->monitorworker.thread, NULL,
	    mailestd_monitor_start0, _this);
#endif
}

static void mailestd_monitor_stop(struct mailestd *);
static void
mailestd_monitor_start(struct mailestd *_this)
{
	MAILESTD_ASSERT(_this->monitor);

#ifdef MONITOR_INOTIFY
	EVENT_INIT();
	task_worker_start(&_this->monitorworker);
	EVENT_SET(&_this->monitor_inev, _this->monitor_in, EV_READ | EV_PERSIST,
	    mailestd_monitor_on_inotify, _this);
	event_add(&_this->monitor_inev, NULL);
	EVENT_SET(&_this->monitor_intimerev, -1, EV_TIMEOUT,
	    mailestd_monitor_on_inotify, _this);
	for (;;) {
		if (EVENT_LOOP(EVLOOP_ONCE) != 0)
			break;
		if (_this->monitorworker.sock < 0)
			mailestd_monitor_stop(_this);
	}
	EVENT_BASE_FREE();
#endif
#ifdef MONITOR_KQUEUE
	/* libevent can't handle kquque inode events, so treat them directly */
    {
	int		 i, ret, nkev, sock;
	struct kevent	 kev[64];
	struct folder	*flde;
	struct timespec	*ts, ts0;

	_this->monitorworker.thread = _thread_self();
	for (;;) {
		ts = NULL;
		nkev = 0;
		if ((sock = _this->monitorworker.sock) < 0)
			mailestd_monitor_stop(_this);
		else {
			EV_SET(&_this->monitor_kev[nkev], sock,
			    EVFILT_READ, EV_ADD, NOTE_EOF, 0, NULL);
			nkev++;
			if (mailestd_monitor_schedule(_this, &ts0) > 0)
				ts = &ts0;
		}

		RB_FOREACH(flde, folder_tree, &_this->monitors) {
			if (flde->fd >= 0) {
				if (_this->monitor_kev_siz <= nkev) {
					_this->monitor_kev_siz++;
					_this->monitor_kev = xreallocarray(
					    _this->monitor_kev,
					    _this->monitor_kev_siz,
					    sizeof(struct kevent));
				}
				EV_SET(&_this->monitor_kev[nkev], flde->fd,
				    EVFILT_VNODE, EV_ADD | EV_ONESHOT,
				    NOTE_WRITE | NOTE_DELETE |
				    NOTE_RENAME | NOTE_REVOKE, 0, flde);
				nkev++;
			}
		}
		if (nkev == 0)
			break;
		memset(kev, 0, sizeof(kev));
		if ((ret = kevent(_this->monitor_kq, _this->monitor_kev, nkev,
		    kev, nitems(kev), ts)) == -1) {
			if (errno == EINTR || errno == EAGAIN)
				continue;
			mailestd_log(LOG_ERR, "%s: kevent: %m", __func__);
			break;
		}
		for (i = 0; i < ret; i++) {
			if (kev[i].ident == sock)
				task_worker_on_itc_event(kev[i].ident,
				    EV_READ, &_this->monitorworker);
			else if (kev[i].udata != NULL) {
				flde = kev[i].udata;
				if ((kev[i].fflags & (NOTE_DELETE |
				    NOTE_RENAME | NOTE_REVOKE)) != 0) {
					close(flde->fd);
					flde->fd = -1;
				}
				clock_gettime(CLOCK_MONOTONIC, &flde->mtime);
			}
		}
	}
	return;
    }
#endif
}

#ifdef MONITOR_INOTIFY
static void
mailestd_monitor_on_inotify(int fd, short evmask, void *ctx)
{
	struct inotify_event	*inev;
	u_char			 buf[1024];
	ssize_t			 siz;
	struct mailestd *_this = ctx;
	struct folder		*flde;
	struct timespec		*ts = NULL, ts0;
	struct timeval		 tv;

mailestd_log(LOG_INFO, "%s: ", __func__);
	if (evmask & EV_READ) {
		if ((siz = read(_this->monitor_in, buf, sizeof(buf))) <= 0){
			if (siz != 0) {
				if (errno == EAGAIN || errno == EINTR)
					return;
				mailestd_log(LOG_ERR, "%s: read: %m", __func__);
			}
			mailestd_monitor_stop(_this);
		}
		inev = (struct inotify_event *)buf;
		RB_FOREACH(flde, folder_tree, &_this->monitors) {
			if (flde->fd == inev->wd)
				break;
		}
		MAILESTD_ASSERT(flde != NULL);
		if ((inev->mask & (IN_DELETE_SELF | IN_MOVE_SELF)) != 0) {
			inotify_rm_watch(_this->monitor_in, flde->fd);
			close(flde->fd);
			flde->fd = -1;
		}
		clock_gettime(CLOCK_MONOTONIC, &flde->mtime);
	}
	if (mailestd_monitor_schedule(_this, &ts0) > 0)
		ts = &ts0;
	if (ts != NULL) {
		TIMESPEC_TO_TIMEVAL(&tv, ts);
		event_add(&_this->monitor_intimerev, &tv);
	}
}
#endif

static void
mailestd_monitor_stop(struct mailestd *_this)
{
	struct folder	*flde, *fldt;

	RB_FOREACH_SAFE(flde, folder_tree, &_this->monitors, fldt) {
		RB_REMOVE(folder_tree, &_this->monitors, flde);
#ifdef MONITOR_INOTIFY
		inotify_rm_watch(_this->monitor_in, flde->fd);
#endif
		if (flde->fd >= 0)
			close(flde->fd);
		flde->fd = -1;
	}
#ifdef MONITOR_INOTIFY
	if (event_pending(&_this->monitor_intimerev, EV_TIMEOUT, NULL))
		event_del(&_this->monitor_intimerev);
	close(_this->monitor_in);
	event_del(&_this->monitor_inev);
#endif
}

static void
mailestd_monitor_fini(struct mailestd *_this)
{
#ifdef MONITOR_KQUEUE
	free(_this->monitor_kev);
#endif
}

static void
mailestd_monitor_folder(struct mailestd *_this, const char *dirpath)
{
	int		 fd = -1;
	struct folder	*fld, fld0;

	MAILESTD_ASSERT(_thread_self() == _this->monitorworker.thread);
	fld0.path = (char *)dirpath;
	if ((fld = RB_FIND(folder_tree, &_this->monitors, &fld0)) != NULL)
		return;
#ifdef MONITOR_KQUEUE
	if ((fd = open(dirpath, O_RDONLY)) < 0)
		return;
#endif
#ifdef MONITOR_INOTIFY
	if ((fd = inotify_add_watch(_this->monitor_in,
	    dirpath, IN_CREATE | IN_DELETE | IN_DELETE_SELF |
	    IN_MOVED_FROM | IN_MOVED_TO | IN_MOVE_SELF)) == -1) {
		mailestd_log(LOG_ERR, "%s() inotify_add_watch: %m", __func__);
	}
#endif
	fld = xcalloc(1, sizeof(struct folder));
	fld->fd = fd;
	fld->path = xstrdup(dirpath);
	RB_INSERT(folder_tree, &_this->monitors, fld);
	mailestd_log(LOG_DEBUG, "Start monitoring %s", dirpath);
}

static void
mailestd_monitor_maildir_changed(struct mailestd *_this)
{
	int		 lmaildir;
	DIR		*dp;
	struct dirent	*de;
	char		 path[PATH_MAX];
	struct folder	*fld, fld0;

	strlcpy(path, _this->maildir, sizeof(path));
	lmaildir = strlen(path);
	path[lmaildir++] = '/';
	path[lmaildir] = '\0';

	if ((dp = opendir(_this->maildir)) == NULL)
		return;
	while ((de = readdir(dp)) != NULL) {
		if (de->d_type != DT_DIR || strcmp(de->d_name, ".") == 0 ||
		    strcmp(de->d_name, "..") == 0)
			continue;
		if (!mailestd_folder_match(_this, de->d_name))
			continue;
		strlcpy(path + lmaildir, de->d_name, sizeof(path) - lmaildir);
		fld0.path = path;
		if ((fld = RB_FIND(folder_tree, &_this->monitors, &fld0))
		    != NULL)
			continue;
		/* new folder */
		mailestd_monitor_folder(_this, path);
		fld = RB_FIND(folder_tree, &_this->monitors, &fld0);
		MAILESTD_ASSERT(fld != NULL);
		clock_gettime(CLOCK_MONOTONIC, &fld->mtime);
	}
	closedir(dp);
}

static int
mailestd_monitor_schedule(struct mailestd *_this, struct timespec *wait)
{
	int			 pends;
	struct timespec		 currtime, diffts, maxts;
	struct folder		*dir0, *dir1, *maildir = NULL;
	struct dirpend {
		struct folder		*dir;
		TAILQ_ENTRY(dirpend)	 queue;
	}			*pnde, *pndt;
	TAILQ_HEAD(, dirpend)	 pend;
	char			 buf[PATH_MAX];

	TAILQ_INIT(&pend);
	RB_FOREACH(dir0, folder_tree, &_this->monitors) {
		if (dir0->mtime.tv_sec == 0 && dir0->mtime.tv_nsec == 0)
			continue;
		if (strcmp(dir0->path, _this->maildir) == 0) {
			maildir = dir0;
			continue;
		}
		/*
		 * pending entry
		 */
		TAILQ_FOREACH_SAFE(pnde, &pend, queue, pndt) {
			dir1 = pnde->dir;
			if (is_parent_dir(dir0->path, dir1->path)) {
				if (timespeccmp(&dir0->mtime, &dir1->mtime, <))
					dir0->mtime = dir1->mtime;
				timespecclear(&dir1->mtime);
				TAILQ_REMOVE(&pend, pnde, queue);
			}
			if (is_parent_dir(dir1->path, dir0->path)) {
				if (timespeccmp(&dir1->mtime, &dir0->mtime, <))
					dir1->mtime = dir0->mtime;
				timespecclear(&dir0->mtime);
			}
		}
		if (dir0->mtime.tv_sec != 0) {
			pnde = xcalloc(1, sizeof(struct dirpend));
			pnde->dir = dir0;
			TAILQ_INSERT_TAIL(&pend, pnde, queue);
		}
	}
	if (maildir != NULL) {
		pnde = xcalloc(1, sizeof(struct dirpend));
		pnde->dir = maildir;
		TAILQ_INSERT_TAIL(&pend, pnde, queue);
	}

	if (TAILQ_EMPTY(&pend))
		return (0);

	pends = 0;
	timespecclear(&maxts);
	clock_gettime(CLOCK_MONOTONIC, &currtime);
	TAILQ_FOREACH_SAFE(pnde, &pend, queue, pndt) {
		dir0 = pnde->dir;
		free(pnde);
		timespecsub(&currtime, &dir0->mtime, &diffts);
		if (timespeccmp(&diffts, &_this->monitor_delay, >=)) {
			MAILESTD_DBG((LOG_DEBUG, "FIRE %lld.%09lld %s",
			    (long long)diffts.tv_sec, (long long)
			    diffts.tv_nsec, dir0->path));
			timespecclear(&dir0->mtime);
			if (strcmp(dir0->path, _this->maildir) == 0) {
				mailestd_monitor_maildir_changed(_this);
				/* may scheduled again */
				pends++;
			} else {
				mailestd_log(LOG_INFO, "Gathering %s by "
				    "monitor", mailestd_folder_name(_this,
				    dir0->path, buf, sizeof(buf)));
				mailestd_schedule_gather(_this, dir0->path);
			}
			if (dir0->fd <= 0) {
				mailestd_log(LOG_DEBUG,
				    "Stop monitoring %s", dir0->path);
				RB_REMOVE(folder_tree, &_this->monitors, dir0);
				folder_free(dir0);
			}
		} else if (timespeccmp(&diffts, &maxts, >)) {
			MAILESTD_DBG((LOG_DEBUG, "PEND %lld.%09lld %s",
			    (long long)diffts.tv_sec,
			    (long long)diffts.tv_nsec, dir0->path));
			maxts = diffts;
			pends++;
		}
	}
	if (pends > 0)
		timespecsub(&_this->monitor_delay, &maxts, wait);

	return (pends);
}

/***********************************************************************
 * log
 ***********************************************************************/
static _thread_spinlock_t	mailestd_log_spin;
static FILE * volatile		mailestd_log_fp = NULL;
static int volatile		mailestd_log_initialized = 0;

static void
mailestd_log_init(void)
{
	_thread_spin_init(&mailestd_log_spin, 0);
	mailestd_log_initialized = 1;
}

static void
mailestd_log_fini(void)
{
	if (!foreground)
		fclose(mailestd_log_fp);
	_thread_spin_destroy(&mailestd_log_spin);
}

void
mailestd_log(int priority, const char *message, ...)
{
	va_list ap;

	if (debug < 1 && LOG_PRI(priority) >= LOG_DEBUG)
		return;
	va_start(ap, message);
	mailestd_vlog(priority, message, ap);
	va_end(ap);
}

void
mailestd_vlog(int priority, const char *message, va_list args)
{
	FILE			*fp = stderr;
	u_int			 i;
	int			 fmtoff = 0, state = 0, saved_errno;
	char			 fmt[1024];
	struct tm		*lt;
	time_t			 now;
	static const char	*prio_name_idx[64];
	static struct {
		int		 prio;
		const char	*name;
	}			 prio_names[] = {
#define VAL_NAME(x)	{ (x), #x }
		VAL_NAME(LOG_EMERG),
		VAL_NAME(LOG_ALERT),
		VAL_NAME(LOG_CRIT),
		VAL_NAME(LOG_ERR),
		VAL_NAME(LOG_WARNING),
		VAL_NAME(LOG_NOTICE),
		VAL_NAME(LOG_INFO),
		VAL_NAME(LOG_DEBUG)
#undef VAL_NAME
	};

	if (!foreground && mailestd_log_fp != NULL)
		fp = mailestd_log_fp;

	if (prio_name_idx[LOG_EMERG] == NULL) {
		for (i = 0; i < nitems(prio_names); i++)
			prio_name_idx[prio_names[i].prio] =
			    &prio_names[i].name[4];
	}

	time(&now);
	lt = localtime(&now);

	for (i = 0; i < strlen(message); i++) {
		switch (state) {
		case 0:
			switch (message[i]) {
			case '%':
				state = 1;
				goto copy_loop;
			case '\n':
				fmt[fmtoff++] = '\n';
				fmt[fmtoff++] = '\t';
				goto copy_loop;
			}
			break;
		case 1:
			switch (message[i]) {
			default:
			case '%':
				fmt[fmtoff++] = '%';
				state = 0;
				break;
			case 'm':
				saved_errno = errno;
				if (strerror_r(saved_errno, fmt + fmtoff,
				    sizeof(fmt) - fmtoff) == 0)
					fmtoff = strlen(fmt);
				errno = saved_errno;
				state = 0;
				goto copy_loop;
			}
		}
		fmt[fmtoff++] = message[i];
copy_loop:
		continue;
	}
	if (fmt[fmtoff-1] == '\t')
		fmtoff--;
	if (fmt[fmtoff-1] != '\n')
		fmt[fmtoff++] = '\n';

	fmt[fmtoff] = '\0';

	if (!mailestd_log_initialized) {
		extern char *__progname;
		fputs(__progname, fp);
		fputs(": ", fp);
		vfprintf(fp, fmt, args);
		fflush(fp);
		return;
	}
	_thread_spin_lock(&mailestd_log_spin);
	fprintf(fp, "%04d-%02d-%02d %02d:%02d:%02d:%s: ",
	    lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday,
	    lt->tm_hour, lt->tm_min, lt->tm_sec,
	    prio_name_idx[LOG_PRI(priority)]);
	vfprintf(fp, fmt, args);
	fflush(fp);
	_thread_spin_unlock(&mailestd_log_spin);
	fsync(fileno(fp));
}

static void
mailestd_log_rotation(const char *logfn, int siz, int max)
{
	int		 i;
	struct stat	 st;
	char		*fn = NULL, *ofn = NULL, fn0[PATH_MAX], fn1[PATH_MAX];
	bool		 turnover = false;
	mode_t		 oumask;

	if (foreground)
		return;
	if (siz != 0 && stat(logfn, &st) != -1 && st.st_size >= (ssize_t)siz) {
		for (i = max - 1; i >= 0; i--) {
			fn = (ofn != fn0)? fn0 : fn1;
			if (i == 0)
				strlcpy(fn, logfn, PATH_MAX);
			else
				snprintf(fn, PATH_MAX, "%s.%d", logfn, i - 1);
			if (i == max - 1)
				unlink(fn);
			else
				rename(fn, ofn);
			ofn = fn;
		}
		turnover = true;
	}
	_thread_spin_lock(&mailestd_log_spin);
	if (mailestd_log_fp != NULL)
		fclose(mailestd_log_fp);
	oumask = umask(077);
	mailestd_log_fp = fopen(logfn, "a");
	umask(oumask);
	_thread_spin_unlock(&mailestd_log_spin);
	if (turnover)
		mailestd_log(LOG_INFO, "logfile turned over");
}

/***********************************************************************
 * Miscellaneous functions
 ***********************************************************************/
static int
rfc822_compar(struct rfc822 *a, struct rfc822 *b)
{
	return strcmp(a->path, b->path);
}

static void
rfc822_free(struct rfc822 *msg)
{
	free(msg->path);
	if (msg->draft != NULL)
		est_doc_delete(msg->draft);
	free(msg);
}

static int
setnonblock(int sock)
{
	int flags;

	if ((flags = fcntl(sock, F_GETFL)) == -1 ||
	    (flags = fcntl(sock, F_SETFL, flags | O_NONBLOCK)) == -1)
		return (-1);

	return (0);
}

static void *
xcalloc(size_t nmemb, size_t size)
{
	void	*ptr;

	ptr = calloc(nmemb, size);
	if (ptr == NULL) {
		mailestd_log(LOG_CRIT, "calloc: %m");
		abort();
	}
	return (ptr);
}

static char *
xstrdup(const char *str)
{
	char	*ret;

	ret = strdup(str);
	if (ret == NULL) {
		mailestd_log(LOG_CRIT, "strdup: %m");
		abort();
	}

	return (ret);
}

static void *
xreallocarray(void *ptr, size_t nmemb, size_t size)
{
	void	*nptr;

	nptr = reallocarray(ptr, nmemb, size);
	if (nptr == NULL) {
		mailestd_log(LOG_CRIT, "calloc: %m");
		abort();
	}
	return (nptr);
}

static int
unlimit_data(void)
{
	rlim_t		 olim;
	struct rlimit	 rl;

	if (getrlimit(RLIMIT_DATA, &rl) == -1)
		return (-1);

	olim = rl.rlim_cur;
	rl.rlim_cur = rl.rlim_max;
	if (setrlimit(RLIMIT_DATA, &rl) == -1)
		return (-1);
	if (getrlimit(RLIMIT_DATA, &rl) == -1)
		return (-1);

	mailestd_log(LOG_DEBUG, "Unlimited datasize %dMB -> %dMB",
	    (int)(olim / 1024 / 1024), (int)(rl.rlim_cur / 1024 / 1024));

	return (0);
}

static int
unlimit_nofile(void)
{
	rlim_t		 olim;
	struct rlimit	 rl;

	if (getrlimit(RLIMIT_NOFILE, &rl) == -1)
		return (-1);

	olim = rl.rlim_cur;
	rl.rlim_cur = rl.rlim_max;
	if (setrlimit(RLIMIT_NOFILE, &rl) == -1)
		return (-1);
	if (getrlimit(RLIMIT_NOFILE, &rl) == -1)
		return (-1);

	mailestd_log(LOG_DEBUG, "Unlimited nofile %d -> %d",
	    (int)olim, (int)rl.rlim_cur);

	return (0);
}

static int
folder_compar(struct folder *a, struct folder *b)
{
	return strcmp(a->path, b->path);
}

static void
folder_free(struct folder *dir)
{
	free(dir->path);
	free(dir);
}

static void
estdoc_add_parid(ESTDOC *doc)
{
	int		 cinrp = 0;
	const char	*inrp0, *refs0;
	char		*t, *sp, *inrp = NULL, *refs = NULL, *parid = NULL;

	/*
	 * In cmew:
	 * > (1) The In-Reply-To contains one ID, use it.
	 * > (2) The References contains one or more IDs, use the last one.
	 * > (3) The In-Reply-To contains two or more IDs, use the first one.
	 */
	if ((inrp0 = est_doc_attr(doc, "in-reply-to")) != NULL) {
		inrp = xstrdup(inrp0);
		sp = inrp;
		while ((t = strsep(&sp, " \t")) != NULL) {
			if (valid_msgid(t)) {
				if (cinrp == 0)
					parid = t;
				cinrp++;
			}
		}
	}
	if (cinrp != 1) {
		if ((refs0 = est_doc_attr(doc, "references")) != NULL) {
			refs = xstrdup(refs0);
			sp = refs;
			while ((t = strsep(&sp, " \t")) != NULL) {
				if (valid_msgid(t))
					parid = t;
			}
		}
	}
	if (parid != NULL)
		est_doc_add_attr(doc, ATTR_PARID, parid);

	free(inrp);
	free(refs);
}

/* -a-zA-Z0-9!#$%&\'*+/=?^_`{}|~.@ is valid chars for Message-Id */
static int msgid_chars[] = {
         0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
         0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
         0, 1, 0, 1, 1, 1, 1, 1, 0, 0, 1, 1, 0, 1, 1, 1, /*  !"#$%&'()*+,-./ */
         1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 0, 1, /* 0123456789:;<=>? */
         1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, /* @ABCDEFGHIJKLMNO */
         1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 1, /* PQRSTUVWXYZ[\]^_ */
         1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, /* `abcdefghijklmno */
         1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0  /* pqrstuvwxyz{|}~  */
};

static bool
valid_msgid(const char *str)
{
	if (*(str++) != '<')
		return (false);

	for (;*str != '\0' && *str != '>'; str++) {
		if ((unsigned)(*str) >= 128 || msgid_chars[(int)*str] == 0)
			break;
	}

	return ((*str == '>')? true : false);
}

static bool
is_parent_dir(const char *dirp, const char *dir)
{
	int	ldirp;

	ldirp = strlen(dirp);
	return ((strncmp(dirp, dir, ldirp) == 0 && dir[ldirp] == '/')
	    ? true : false);
}

RB_GENERATE_STATIC(rfc822_tree, rfc822, tree, rfc822_compar);
RB_GENERATE_STATIC(folder_tree, folder, tree, folder_compar);
