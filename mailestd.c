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
#include <sys/queue.h>
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
#include <pthread.h>
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

	fprintf(stderr, "usage: %s [-dh] [-s suffix] [maildir]\n", __progname);
}

int
main(int argc, char *argv[])
{
	int			 ch, suffixcount = 0;
	struct mailestd		 mailestd_s;
	const char		*maildir = NULL, *home;
	char			 pathtmp[PATH_MAX], maildir0[PATH_MAX];
	const char		*conf_file = NULL, *suffix[11];
	struct mailestd_conf	*conf;
	bool			 noaction = false;
	extern char	*__progname;

	if (strcmp(__progname, "mailestctl") == 0)
		return (mailestctl_main(argc, argv));

	memset(suffix, 0, sizeof(suffix));
	while ((ch = getopt(argc, argv, "dhs:nf:")) != -1)
		switch (ch) {
		case 's':
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
	if (!foreground)
		daemon(1, 0);
	mailestd_log_init();
	EVENT_INIT();
	mailestd_start(&mailestd_s, foreground);

	EVENT_LOOP(0);

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
	const char		*defignore[] = { MAILESTD_DEFAULT_IGNORES };

	memset(_this, 0, sizeof(struct mailestd));

	strlcpy(_this->maildir, conf->maildir, sizeof(_this->maildir));

	if (debug == 0)
		debug = conf->debug;
	RB_INIT(&_this->root);
	TAILQ_INIT(&_this->rfc822_pendings);
	TAILQ_INIT(&_this->rfc822_tasks);
	_this->rfc822_task_max = conf->tasks;
	TAILQ_INIT(&_this->ctls);
	TAILQ_INIT(&_this->gathers);
	strlcpy(_this->logfn, conf->log_path, sizeof(_this->logfn));
	strlcpy(_this->dbpath, conf->db_path, sizeof(_this->dbpath));
	_this->logsiz = conf->log_size;
	_this->logmax = conf->log_count;
	_this->doc_trimsize = conf->trim_size;
	_this->ignore = conf->ignores;
	if (conf->ignores == NULL) {
		_this->ignore = xcalloc(nitems(defignore), sizeof(char *));
		for (i = 0; i < (int)nitems(defignore); i++)
			_this->ignore[i] = xstrdup(defignore[i]);
		_this->ignore[i] = NULL;
	}
	conf->ignores = NULL;

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
	memcpy(sun.sun_path, conf->sock_path, sizeof(sun.sun_path));

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

	_this->workers[0] = &_this->mainworker;
	_this->workers[1] = &_this->dbworker;
	_this->workers[2] = NULL;
	/* start the task workers */
	for (i = 0; _this->workers[i] != NULL; i++) {
		task_worker_init(_this->workers[i], _this);
#ifndef MAILESTD_MT
		task_worker_start(_this->workers[i]);
	}
#else
	}
	task_worker_start(&_this->mainworker);	/* this thread */
	task_worker_run(&_this->dbworker);	/* another thread */
#endif

	if (listen(_this->sock_ctl, 5) == -1)
		mailestd_log(LOG_ERR, "listen(): %m");
	mailestd_ctl_sock_reset_event(_this);

	mailestd_log(LOG_INFO, "Started mailestd.  Process-Id=%d",
	    (int)getpid());
	mailestd_schedule_syncdb(_this);
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
	task_worker_message_all(_this, MAILESTD_TASK_STOP);
	close(_this->dbworker.sock_itc);
	close(_this->mainworker.sock_itc);

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

	if (_this->suffix != NULL) {
		for (i = 0; !isnull(_this->suffix[i]); i++)
			free(_this->suffix[i]);
	}
	free(_this->suffix);
	if (_this->ignore != NULL) {
		for (i = 0; !isnull(_this->ignore[i]); i++)
			free(_this->ignore[i]);
	}
	free(_this->ignore);

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
		mailestd_log(LOG_DEBUG, "Opened DB");
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

static int
mailestd_syncdb(struct mailestd *_this)
{
	ESTDB		*db;
	int		 i, id;
	const char	*prev, *fn, *uri, *errstr;
	ESTDOC		*doc;
	struct rfc822	*msg, msg0;
	struct tm	 tm;
	struct stat	 st;

	MAILESTD_ASSERT(_thread_self() == _this->dbworker.thread);
	MAILESTD_DBG((LOG_DEBUG, "START %s()", __func__));

	if (lstat(_this->dbpath, &st) == -1 && errno == ENOENT) {
		mailestd_log(LOG_INFO, "Database %s doesn't exist",
		    _this->dbpath);
		return (0);
	}

	if ((db = mailestd_db_open_rd(_this)) == NULL)
		return (-1);

	prev = _this->sync_prev;
	est_db_iter_init(db, prev);
	for (i = 0; (id = est_db_iter_next(db)) > 0; i++) {
		doc = est_db_get_doc(db, id, ESTGDNOTEXT | ESTGDNOTEXT);
		if (doc == NULL)
			continue;

		uri = est_doc_attr(doc, ESTDATTRURI);
		if (uri == NULL || strncmp(uri, "file:///", 8) != 0)
			continue;
		fn = uri + 7;

		msg0.path = (char *)fn;
		msg = RB_FIND(rfc822_tree, &_this->root, &msg0);
		if (msg == NULL) {
			msg = xcalloc(1, sizeof(struct rfc822));
			msg->path = xstrdup(fn);
			RB_INSERT(rfc822_tree, &_this->root, msg);
		}
		strptime(est_doc_attr(doc, ESTDATTRMDATE), MAILESTD_TIMEFMT,
		    &tm);
		msg->db_id = id;
		msg->mtime = timegm(&tm);
		msg->size = strtonum(est_doc_attr(doc, ESTDATTRSIZE), 0,
		    INT64_MAX, &errstr);

		if (i >= MAILESTD_DBSYNC_NITER) {
			free(_this->sync_prev);
			_this->sync_prev = xstrdup(uri);
			est_doc_delete(doc);
			mailestd_schedule_syncdb(_this); /* schedule again */
			return (0);
		}
		est_doc_delete(doc);
	}
	_this->sync_prev = NULL;
	free(_this->sync_prev);

	mailestd_log(LOG_INFO, "Database cache updated");
	return (0);
}

static int
mailestd_gather(struct mailestd *_this, struct task_gather *task)
{
	int		 lrdir, update = 0, delete = 0, total = 0;
	char		 rdir[PATH_MAX], *paths[2];
	const char	*folder = task->folder;
	struct gather	*ctx;
	FTS		*fts;
	struct rfc822	*msge, *msgt, msg0;
	time_t		 curr_time;

	ctx = mailestd_get_gather(_this, task->gather_id);
	if (folder[0] == '/') {
		mailestd_log(LOG_DEBUG, "Gathering %s ...", folder);
		strlcpy(rdir, folder, sizeof(rdir));
	} else {
		mailestd_log(LOG_DEBUG, "Gathering +%s ...", folder);
		strlcpy(rdir, _this->maildir, sizeof(rdir));
		strlcat(rdir, "/", sizeof(rdir));
		strlcat(rdir, folder, sizeof(rdir));
	}
	paths[0] = rdir;
	paths[1] = NULL;
	lrdir = strlen(rdir);

	if ((fts = fts_open(paths, FTS_LOGICAL, NULL)) == NULL) {
		mailestd_log(LOG_ERR, "fts_open(%s): %m", folder);
		return (-1);
	}
	curr_time = _this->curr_time;
	update = mailestd_fts(_this, ctx, curr_time, fts, fts_read(fts));
	fts_close(fts);

	MAILESTD_ASSERT(lrdir + 1 < (int)sizeof(rdir));
	rdir[lrdir++] = '/';
	rdir[lrdir] = '\0';

	msg0.path = rdir;
	for (msge = RB_NFIND(rfc822_tree, &_this->root, &msg0);
	    msge != NULL; msge = msgt) {
		msgt = RB_NEXT(rfc822_tree, &_this->root, msge);

		if (strncmp(msge->path, rdir, lrdir) != 0)
			break;

		total++;
		if (msge->fstime != curr_time) {
			RB_REMOVE(rfc822_tree, &_this->root, msge);
			delete++;
			if (!msge->ontask) {
				if (msge->db_id != 0) {
					if (ctx != NULL)
						ctx->dels++;
					mailestd_schedule_deldb(_this, msge);
				} else
					rfc822_free(msge);
			}
		}
	}

	mailestd_log(LOG_INFO,
	    "Gathered %s%s (Total: %d Remove: %d Update: %d)",
	    (folder[0] != '/')? "+" : "", folder, total, delete, update);

	if (ctx != NULL) {
		if (ctx->puts == ctx->puts_done &&
		    ctx->dels == ctx->dels_done) {
			if (update > 0 || delete > 0)
				strlcpy(ctx->errmsg,
				    "other task is running",
				    sizeof(ctx->errmsg));
			mailestd_gather_inform(_this, NULL, ctx);
		} else if (_this->dbworker.suspend) {
			strlcpy(ctx->errmsg,
			    "database tasks are suspended",
			    sizeof(ctx->errmsg));
			mailestd_gather_inform(_this, NULL, ctx);
		}
	}

	return (0);
}

static void
mailestd_gather_inform(struct mailestd *_this, struct task_rfc822 *task,
    struct gather *gat)
{
	int		 notice = 0;
	struct gather	*gather = gat;

	if (task != NULL) {
		if (gather == NULL)
			gather = mailestd_get_gather(_this,
			    task->msg->gather_id);
		if (gather == NULL)
			return;
		switch (task->type) {
		default:
			break;

		case MAILESTD_TASK_RFC822_DELDB:
			if (++gather->dels_done == gather->dels)
				notice++;
			break;

		case MAILESTD_TASK_RFC822_PUTDB:
			if (++gather->puts_done == gather->puts)
				notice++;
			break;
		}
		if (notice > 0)
			mailestd_schedule_inform(_this, gather->id,
			    (u_char *)gather, sizeof(struct gather));
		if (gather->dels_done != gather->dels ||
		    gather->puts_done != gather->puts)
			return;
	}

	mailestd_schedule_inform(_this, gather->id,
	    (u_char *)gather, sizeof(struct gather));

	TAILQ_REMOVE(&_this->gathers, gather, queue);
	free(gather);
}

static int
mailestd_fts(struct mailestd *_this, struct gather *ctx, time_t curr_time,
    FTS *fts, FTSENT *ftse)
{
	int		 i, j, update = 0;
	const char	*bn;
	struct rfc822	*msg, msg0;
	bool		 needupdate = false;

	do {
		needupdate = false;
		if (ftse == NULL)
			break;
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
			needupdate = true;
		} else {
			if (msg->db_id == 0 ||
			    msg->mtime != ftse->fts_statp->st_mtime ||
			    msg->size != ftse->fts_statp->st_size)
				needupdate = true;
		}
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
	if ((msgs = mmap(0, st.st_size, PROT_READ, MAP_FILE, fd, 0)) ==
	    MAP_FAILED) {
		mailestd_log(LOG_WARNING, "mmap(%s): %m", msg->path);
		goto on_error;
	}
	msg->draft = est_doc_new_from_mime(msgs, st.st_size, "UTF-8",
	    ESTLANGEN, 0);
	if (msg->draft == NULL) {
		mailestd_log(LOG_WARNING, "est_doc_new_from_mime(%s) failed",
		    msg->path);
		goto on_error;
	}
	est_doc_slim(msg->draft, MAILESTD_TRIMSIZE);
	strlcpy(buf, "file://", sizeof(buf));
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
mailestd_putdb(struct mailestd *_this, ESTDB *db, struct rfc822 *msg)
{
	int		 ecode;

	MAILESTD_ASSERT(_thread_self() == _this->dbworker.thread);
	MAILESTD_ASSERT(msg->draft != NULL);

	if (est_db_put_doc(db, msg->draft, 0)) {
		msg->db_id = est_doc_id(msg->draft);
		if (debug > 2)
			mailestd_log(LOG_DEBUG, "put %s successfully.  id=%d",
			    msg->path, msg->db_id);
	} else {
		ecode = est_db_error(db);
		mailestd_log(LOG_WARNING,
		    "putting %s failed: %s", msg->path, est_err_msg(ecode));
		mailestd_db_error(_this);
	}

	est_doc_delete(msg->draft);
	msg->draft = NULL;
}

static void
mailestd_deldb(struct mailestd *_this, ESTDB *db, struct rfc822 *msg)
{
	int		 ecode;

	MAILESTD_ASSERT(_thread_self() == _this->dbworker.thread);
	MAILESTD_ASSERT(msg->db_id != 0);
	if (est_db_out_doc(db, msg->db_id, 0)) {
		if (debug > 2)
			mailestd_log(LOG_DEBUG, "delete %s(%d) successfully",
			    msg->path, msg->db_id);
	} else {
		ecode = est_db_error(db);
		mailestd_log(LOG_WARNING, "deleting %s(%d) failed: %s",
		    msg->path, msg->db_id, est_err_msg(ecode));
		mailestd_db_error(_this);
	}
}

static void
mailestd_search(struct mailestd *_this, ESTDB *db, uint64_t task_id,
    ESTCOND *cond, enum SEARCH_OUTFORM outform)
{
	int	 i, rnum, *res, ecode;
	char	*bufp = NULL;
	size_t	 bufsiz = 0;
	ESTDOC	*doc;
	FILE	*out;

	if ((out = open_memstream(&bufp, &bufsiz)) == NULL)
		abort();
	res = est_db_search(db, cond, &rnum, NULL);
	if (res == NULL) {
		ecode = est_db_error(db);
		mailestd_log(LOG_INFO,
		    "Search(%s) failed: %s", cond->phrase, est_err_msg(ecode));
		mailestd_schedule_inform(_this, task_id, NULL, 0);
	} else {
		mailestd_log(LOG_INFO,
		    "Searched(%s) successfully.  Hit %d", cond->phrase, rnum);
		for (i = 0; i < rnum; i++) {
			doc = est_db_get_doc(db, res[i], ESTGDNOKWD);
			if (doc == NULL) {
				ecode = est_db_error(db);
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
		mailestd_schedule_inform(_this, task_id, bufp, bufsiz);
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
	task_worker_message_all(_this, MAILESTD_TASK_SUSPEND);
}

static uint64_t
mailestd_schedule_syncdb(struct mailestd *_this)
{
	struct task	*task;

	task = xcalloc(1, sizeof(struct task));
	task->type = MAILESTD_TASK_SYNCDB;
	task->highprio = true;

	return (task_worker_add_task(&_this->dbworker, task));
}

static uint64_t
mailestd_schedule_gather0(struct mailestd *_this, struct gather *ctx,
    const char *folder)
{
	int			 i;
	struct task_gather	*task;
	const char		*pat;
	bool			 neg;

	for (i = 0; _this->ignore != NULL && !isnull(_this->ignore[i]); i++) {
		neg = false;
		pat = _this->ignore[i];
		if (*pat == '!') {
			pat++;
			neg = true;
		}
		if ((fnmatch(pat, folder, 0) == 0)? !neg : neg)
			return (0);
	}
	task = xcalloc(1, sizeof(struct task_gather));
	task->type = MAILESTD_TASK_GATHER;
	task->highprio = true;
	task->gather_id = ctx->id;
	strlcpy(task->folder, folder, sizeof(task->folder));

	return (task_worker_add_task(&_this->dbworker, (struct task *)task));
}

static uint64_t
mailestd_schedule_gather(struct mailestd *_this, const char *folder)
{
	int		 i, nfolders = 0;
	char		 path[PATH_MAX];
	glob_t		 gl;
	struct stat	 st;
	DIR		*dp;
	struct dirent	*de;
	ssize_t		 lmaildir;
	struct gather	*ctx;

	ctx = xcalloc(1, sizeof(struct gather));
	ctx->id = mailestd_new_id(_this);
	TAILQ_INSERT_TAIL(&_this->gathers, ctx, queue);

	if (folder[0] != '\0') {
		if (folder[0] != '/') {
			memset(&gl, 0, sizeof(gl));
			strlcpy(path, _this->maildir, sizeof(path));
			lmaildir = strlcat(path, "/", sizeof(path));
			strlcat(path, folder, sizeof(path));
			if (glob(path, GLOB_BRACE, NULL, &gl) != 0) {
				if (errno == GLOB_NOMATCH)
					errno = ENOENT;
				mailestd_log(LOG_WARNING, "%s: %m", path);
			} else {
				for (i = 0; i < gl.gl_pathc; i++) {
					if (lstat(gl.gl_pathv[i], &st) != 0 ||
					    !S_ISDIR(st.st_mode))
						continue;
					mailestd_schedule_gather0(_this, ctx,
					    gl.gl_pathv[i] + lmaildir);
					nfolders++;
				}
				globfree(&gl);
			}
		} else {
			if (lstat(folder, &st) != 0)
				mailestd_log(LOG_ERR, "%s: %m", folder);
			else if (!S_ISDIR(st.st_mode))
				mailestd_log(LOG_ERR, "%s: Not a directory",
				    folder);
			else {
				mailestd_schedule_gather0(_this, ctx, folder);
				nfolders++;
			}
		}
	} else {
		if ((dp = opendir(_this->maildir)) == NULL)
			mailestd_log(LOG_ERR, "%s: %m", _this->maildir);
		else {
			while ((de = readdir(dp)) != NULL) {
				if (de->d_type != DT_DIR ||
				    strcmp(de->d_name, ".") == 0 ||
				    strcmp(de->d_name, "..") == 0)
					continue;
				mailestd_schedule_gather0(_this, ctx,
				    de->d_name);
				nfolders++;
			}
			closedir(dp);
		}
	}
	if (nfolders == 0) {
		strlcpy(ctx->errmsg, "grabing folders", sizeof(ctx->errmsg));
		mailestd_gather_inform(_this, NULL, ctx);
	}

	return (ctx->id);
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

	task = xcalloc(1, sizeof(struct task_search));
	task->type = MAILESTD_TASK_SEARCH;
	task->cond = cond;
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

/***********************************************************************
 * Tasks
 ***********************************************************************/
static void
task_worker_init(struct task_worker *_this, struct mailestd *mailestd)
{
	memset(_this, 0, sizeof(struct task_worker));
	TAILQ_INIT(&_this->head);
	_thread_mutex_init(&_this->lock, NULL);
	_this->mailestd_this = mailestd;
}

static void
task_worker_start(struct task_worker *_this)
{
	int	 on, pairsock[2];

	_this->thread = _thread_self();
	if (socketpair(PF_UNIX, SOCK_SEQPACKET, 0, pairsock) == -1)
		err(EX_OSERR, "socketpair()");
	_this->sock = pairsock[1];
	_this->sock_itc = pairsock[0];
	on = 1;
	fcntl(_this->sock, O_NONBLOCK, &on);
	on = 1;
	fcntl(_this->sock_itc, O_NONBLOCK, &on);
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
	MAILESTD_ASSERT(_thread_self() == _this->thread);
	if (_this->sock >= 0) {
		event_del(&_this->evsock);
		close(_this->sock);
	}
	_thread_mutex_destroy(&_this->lock);
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
task_worker_message_all(struct mailestd *_this, enum MAILESTD_TASK tsk_type)
{
	int			 i;
	struct task		*task;
	struct task_worker	*workers[] = {
	    &_this->dbworker, &_this->mainworker
	};

	for (i = 0; i < (int)nitems(workers); i++) {
		task = xcalloc(1, sizeof(struct task));
		task->type = tsk_type;
		task->highprio = true;
		task_worker_add_task(workers[i], task);
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

	memset(&dbctx, 0, sizeof(dbctx));
	while (!stop) {
		_thread_mutex_lock(&_this->lock);
		task = TAILQ_FIRST_ITEM(&_this->head);
		if (task != NULL) {
			if (!_this->suspend || task->highprio)
				TAILQ_REMOVE(&_this->head, task, queue);
			else
				task = NULL;
		}
		_thread_mutex_unlock(&_this->lock);
		if (task == NULL) {
			if (!stop && thread_this == mailestd->dbworker.thread) {
				if (!task_dbworker(_this, &dbctx, NULL))
					continue;
			}
			break;
		}

		switch (task->type) {
		case MAILESTD_TASK_RFC822_DRAFT:
			msg = ((struct task_rfc822 *)task)->msg;
			MAILESTD_ASSERT(msg->draft == NULL);
			mailestd_draft(mailestd, msg);
			/*
			 * This thread can't return the task even if creating
			 * a draft failed.  (since it's not dbworker)
			 */
			mailestd_schedule_putdb(mailestd, task, msg);
			task = NULL;	/* recycled */
			break;

		case MAILESTD_TASK_RFC822_PUTDB:
			MAILESTD_ASSERT(thread_this ==
			    mailestd->dbworker.thread);
			task_dbworker(_this, &dbctx, task);
			task = NULL;	/* recycled */
			break;
		case MAILESTD_TASK_SEARCH:
		case MAILESTD_TASK_RFC822_DELDB:
			MAILESTD_ASSERT(thread_this ==
			    mailestd->dbworker.thread);
			task_dbworker(_this, &dbctx, task);
			break;

		case MAILESTD_TASK_SYNCDB:
			mailestd_syncdb(mailestd);
			break;

		case MAILESTD_TASK_GATHER:
			mailestd_gather(mailestd, (struct task_gather *)task);
			break;

		case MAILESTD_TASK_SUSPEND:
			_this->suspend = true;
			/* FALLTHROUGH */
		case MAILESTD_TASK_STOP:
			stop = true;
			if (thread_this == mailestd->dbworker.thread)
				task_dbworker(_this, &dbctx, task);
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
task_dbworker(struct task_worker *_this, struct task_dbworker_context *ctx,
    struct task *task)
{
	struct rfc822		*msg;
	ESTDB			*db = NULL;
	enum MAILESTD_TASK	 task_type;
	struct mailestd		*mailestd = _this->mailestd_this;
	struct task_search	*search;

	if (task == NULL)
		task_type = MAILESTD_TASK_NONE;
	else {
		task_type = task->type;
		msg = ((struct task_rfc822 *)task)->msg;
	}

	switch (task_type) {
	default:
		break;

	case MAILESTD_TASK_RFC822_PUTDB:
		if ((db = mailestd_db_open_wr(mailestd)) == NULL)
			break;
		ctx->puts++;
		ctx->resche++;
		if (msg->draft != NULL)
			mailestd_putdb(mailestd, db, msg);
		mailestd_gather_inform(mailestd, (struct task_rfc822 *)task,
		    NULL);
		msg->ontask = false;
		TAILQ_INSERT_TAIL(&mailestd->rfc822_tasks, task, queue);
		mailestd->rfc822_ntask--;
		break;

	case MAILESTD_TASK_RFC822_DELDB:
		if ((db = mailestd_db_open_wr(mailestd)) == NULL)
			break;
		ctx->dels++;
		mailestd_gather_inform(mailestd, (struct task_rfc822 *)task,
		    NULL);
		mailestd_deldb(mailestd, db, msg);
		rfc822_free(msg);
		break;

	case MAILESTD_TASK_SEARCH:
		search = (struct task_search *)task;
		if ((db = mailestd_db_open_rd(mailestd)) == NULL)
			break;
		mailestd_search(mailestd, db, task->id, search->cond,
		    search->outform);
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

static uint64_t
task_worker_add_task(struct task_worker *_this, struct task *task)
{
	uint64_t	 id;
	struct task	*tske;

	task->id = mailestd_new_id(_this->mailestd_this);
	id = task->id;

	_thread_mutex_lock(&_this->lock);
	if (task->highprio) {
		TAILQ_FOREACH(tske, &_this->head, queue) {
			if (task_compar(tske, task) < 0)
				break;
		}
		if (tske == NULL)
			TAILQ_INSERT_TAIL(&_this->head, task, queue);
		else
			TAILQ_INSERT_BEFORE(tske, task, queue);
	} else
		TAILQ_INSERT_TAIL(&_this->head, task, queue);
	_thread_mutex_unlock(&_this->lock);

	write(_this->sock_itc, "A", 1);		/* wakeup */

	return (id);
}

static int
task_compar(struct task *a, struct task *b)
{
	int  cmp;

	cmp = (a->highprio - b->highprio);
	if (cmp)
		return (cmp);

	return (b->type - a->type);
}

/***********************************************************************
 * mailestc
 ***********************************************************************/
static struct timeval mailestc_timeout = { MAILESTCTL_IDLE_TIMEOUT, 0 };

static void
mailestd_ctl_sock_reset_event(struct mailestd *_this)
{
	MAILESTD_ASSERT(_this->sock_ctl >= 0);

	EVENT_SET(&_this->evsock_ctl, _this->sock_ctl, EV_READ,
	    mailestd_ctl_on_event, _this);
	event_add(&_this->evsock_ctl, NULL);
}

static void
mailestd_ctl_on_event(int fd, short evmask, void *ctx)
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
	mailestd_ctl_sock_reset_event(_this);
	return;
}

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
	struct mailestctl_update *update = (struct mailestctl_update *)&cmd;

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
			task_worker_message_all(mailestd,
			    MAILESTD_TASK_SUSPEND);
			break;

		case MAILESTCTL_CMD_RESUME:
			mailestd_log(LOG_INFO, "resume requested");
			task_worker_message_all(mailestd, MAILESTD_TASK_RESUME);
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
			if (!bytebuffer_has_remaining(_this->wbuf)) {
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
	while (*phrase == '\t' || *phrase == ' ')
		phrase++;
	est_cond_set_phrase(cond, phrase);
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
	case MAILESTCTL_CMD_SEARCH:
		if (informsiz == 0)
			mailestc_stop(_this);
		else
			mailestc_send_message(_this, inform, informsiz);
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
		else if (del_compl)
			msg = "old messages...done\n";
		if (msg != NULL)
			mailestc_send_message(_this, msg, strlen(msg));
	    }
		break;
	}
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
				strerror_r(saved_errno, fmt + fmtoff,
				    sizeof(fmt) - fmtoff);
				errno = saved_errno;
				fmtoff = strlen(fmt);
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
	mailestd_log_fp = fopen(logfn, "a");
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
	free(msg->draft);
	free(msg);
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

RB_GENERATE_STATIC(rfc822_tree, rfc822, tree, rfc822_compar);
