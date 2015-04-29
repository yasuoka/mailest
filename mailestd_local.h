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
#define MAILESTD_TIMEFMT		"%a, %d %b %Y %H:%M:%S +0000"
#define MAILESTCTL_CMD_MAX	1024
#define MAILESTCTL_IDLE_TIMEOUT	60

#include "thread.h"

static int	 debug = 0;
static int	 foreground = 0;

struct mailestd;
struct task;
struct rfc822;
struct mailestc;
struct gather;
TAILQ_HEAD(task_queue, task);
RB_HEAD(rfc822_tree, rfc822);
TAILQ_HEAD(rfc822_queue, rfc822);
TAILQ_HEAD(mailestc_queue, mailestc);
TAILQ_HEAD(gather_queue, gather);

struct task_worker {
	struct mailestd		*mailestd_this;
	int			 sock;
	struct event		 evsock;
	int			 sock_itc;	/* for main thread */
	_thread_t		 thread;
	struct task_queue	 head;
	_thread_mutex_t		 lock;
	bool			 suspend;
};

struct mailestd {
	char			  maildir[PATH_MAX];
	char			  dbpath[PATH_MAX];
	char			  logfn[PATH_MAX];
	int			  logsiz;
	int			  logmax;
	char			**suffix;
	char			**ignore;
	int			  doc_trimsize;
	int			  rfc822_task_max;
	ESTDB			 *db;
	bool			  db_wr;
	char			 *sync_prev;

	time_t			  curr_time;
	struct event		  evsigterm;
	struct event		  evsigint;
	struct event		  evtimer;

	_thread_spinlock_t	  id_seq_lock;
	uint64_t		  id_seq;
	struct rfc822_tree	  root;
	struct rfc822_queue	  rfc822_pendings;
	struct task_queue	  rfc822_tasks;
	int			  rfc822_ntask;
	struct task_worker	  dbworker;
	struct task_worker	  mainworker;
	struct task_worker	 *workers[3];
	struct gather_queue	  gathers;

	int			  sock_ctl;
	struct event		  evsock_ctl;
	struct mailestc_queue	  ctls;
};

struct rfc822 {
	char			*path;
	ESTDOC			*draft;
	int			 db_id;
	time_t			 mtime;
	off_t			 size;
	time_t			 fstime;
	RB_ENTRY(rfc822)	 tree;
	TAILQ_ENTRY(rfc822)	 queue;
	bool			 ontask;
	uint64_t		 gather_id;	/* gather of the task */
};

enum MAILESTD_TASK {
	/* order is matter if highprio */
	MAILESTD_TASK_NONE = 0,
	MAILESTD_TASK_STOP,
	MAILESTD_TASK_SUSPEND,
	MAILESTD_TASK_RESUME,
	MAILESTD_TASK_INFORM,
	MAILESTD_TASK_SEARCH,
	MAILESTD_TASK_SYNCDB,
	MAILESTD_TASK_GATHER,
	MAILESTD_TASK_RFC822_DRAFT,
	MAILESTD_TASK_RFC822_PUTDB,
	MAILESTD_TASK_RFC822_DELDB
};

struct task {
	uint64_t		 id;
	enum MAILESTD_TASK	 type;
	TAILQ_ENTRY(task)	 queue;
	bool			 highprio;
};

struct task_rfc822 {
	uint64_t		 id;
	enum MAILESTD_TASK	 type;
	TAILQ_ENTRY(task)	 queue;
	bool			 highprio;
	struct rfc822		*msg;
};

struct task_gather {
	uint64_t		 id;
	enum MAILESTD_TASK	 type;
	TAILQ_ENTRY(task)	 queue;
	bool			 highprio;
	uint64_t		 gather_id;
	char			 folder[PATH_MAX];
};

enum SEARCH_OUTFORM {
	SEARCH_OUTFORM_COMPAT_VU
};

struct task_search {
	uint64_t		 id;
	enum MAILESTD_TASK	 type;
	TAILQ_ENTRY(task)	 queue;
	bool			 highprio;
	enum SEARCH_OUTFORM	 outform;
	ESTCOND			*cond;
};

struct task_inform {
	uint64_t		 id;
	enum MAILESTD_TASK	 type;
	TAILQ_ENTRY(task)	 queue;
	bool			 highprio;
	uint64_t		 src_id;
	size_t			 informsiz;
	u_char			 inform[0];
};

struct mailestc {
	int			 sock;
	TAILQ_ENTRY(mailestc)	 queue;
	struct event		 evsock;
	bytebuffer		*wbuf;
	bool			 wready;
	struct mailestd		*mailestd_this;
	enum MAILESTCTL_CMD	 monitoring_cmd;
	uint64_t		 monitoring_id;
};

struct task_dbworker_context {
	int	 puts;
	int	 resche;
	int	 dels;
};

struct gather {
	uint64_t		 id;
	u_int			 puts;
	u_int			 dels;
	u_int			 puts_done;
	u_int			 dels_done;
	char			 errmsg[80];
	TAILQ_ENTRY(gather)	 queue;
};

#ifdef	MAILESTD_DEBUG
#define MAILESTD_DBG(_msg)	 mailestd_log _msg;
#define MAILESTD_ASSERT(_cond)						\
	do {								\
		if (!(_cond)) {						\
			mailestd_log(LOG_CRIT,				\
			    "ASSERT(%s) failed in %s():%s:%d",		\
			    #_cond, __func__, __FILE__, __LINE__);	\
			abort();					\
		}							\
	} while (0/*CONSTCOND*/)
#else
#define MAILESTD_DBG(_msg)	do {/* do nothing */} while(0/*CONSTCOND*/)
#define MAILESTD_ASSERT(_cond)	do {/* do nothing */} while(0/*CONSTCOND*/)
#endif

RB_PROTOTYPE_STATIC(rfc822_tree, rfc822, tree, rfc822_compar);

static void	 mailestd_init(struct mailestd *, struct mailestd_conf *,
		    const char **);
static void	 mailestd_start(struct mailestd *, bool);
static uint64_t	 mailestd_new_id(struct mailestd *);
static void	 mailestd_stop(struct mailestd *);
static void	 mailestd_fini(struct mailestd *);
static struct gather *
		 mailestd_get_gahter(struct mailestd *, uint64_t);

static void	 mailestd_on_timer(int, short, void *);
static void	 mailestd_on_sigterm(int, short, void *);
static void	 mailestd_on_sigint(int, short, void *);

static ESTDB	*mailestd_db_open_rd(struct mailestd *);
static ESTDB	*mailestd_db_open_wr(struct mailestd *);
static void	 mailestd_db_close(struct mailestd *);
static int	 mailestd_syncdb(struct mailestd *);
static int	 mailestd_gather(struct mailestd *, struct task_gather *);
static void	 mailestd_gather_inform(struct mailestd *,
		    struct task_rfc822 *, struct gather *);
static int	 mailestd_fts(struct mailestd *, struct gather *, time_t,
		    FTS *, FTSENT *);
static void	 mailestd_draft(struct mailestd *, struct rfc822 *msg);
static void	 mailestd_putdb(struct mailestd *, ESTDB *, struct rfc822 *);
static void	 mailestd_deldb(struct mailestd *, ESTDB *, struct rfc822 *);
static void	 mailestd_search(struct mailestd *, ESTDB *, uint64_t,
		    ESTCOND *, enum SEARCH_OUTFORM);
static void	 mailestd_db_informer(const char *, void *);
static void	 mailestd_db_error(struct mailestd *);

static uint64_t	 mailestd_schedule_syncdb(struct mailestd *);
static uint64_t  mailestd_schedule_gather(struct mailestd *, const char *);
static uint64_t	 mailestd_schedule_draft(struct mailestd *, struct gather *,
		    struct rfc822 *);
static uint64_t  mailestd_schedule_putdb(struct mailestd *, struct task *,
		    struct rfc822 *);
static uint64_t	 mailestd_schedule_deldb(struct mailestd *, struct rfc822 *);
static uint64_t	 mailestd_schedule_search(struct mailestd *, ESTCOND *);
static uint64_t	 mailestd_schedule_inform(struct mailestd *, uint64_t,
		    u_char *, size_t);

static void	 task_worker_init(struct task_worker *, struct mailestd *);
static void	 task_worker_start(struct task_worker *);
static void	 task_worker_run(struct task_worker *) __used;
static void	 task_worker_stop(struct task_worker *);
static void	 task_worker_on_itc_event(int, short, void *);
static void	 task_worker_on_proc(struct task_worker *_this);
static uint64_t	 task_worker_add_task(struct task_worker *, struct task *);
static void	 task_worker_message_all(struct mailestd *, enum MAILESTD_TASK);
static bool	 task_dbworker(struct task_worker *,
		    struct task_dbworker_context *, struct task *task);
static int	 task_compar(struct task *, struct task *);

static void	 mailestc_on_event(int, short, void *);
static void	 mailestd_ctl_sock_reset_event(struct mailestd *);
static void	 mailestc_start(struct mailestc *, struct mailestd *, int);
static void	 mailestc_stop(struct mailestc *);
static void	 mailestd_ctl_on_event(int, short, void *);
static int	 mailestc_cmd_search(struct mailestc *,
		    struct mailestctl_search *);
static void	 mailestc_send_message(struct mailestc *, u_char *, size_t);
static void	 mailestc_task_inform(struct mailestc *, uint64_t, u_char *,
		    size_t);

static void	 mailestd_log_init(void);
static void	 mailestd_log_fini(void);
static void	 mailestd_log_rotation(const char *, int, int);
void		 mailestd_log(int, const char *, ...)
		    __attribute__((__format__(__syslog__,2,3)));
void		 mailestd_vlog(int, const char *, va_list);

static int	 rfc822_compar(struct rfc822 *, struct rfc822 *);
static void	 rfc822_free(struct rfc822 *msg);
static void	*xcalloc(size_t, size_t);
static char	*xstrdup(const char *);

#ifndef	nitems
#define nitems(_n)	(sizeof((_n)) / sizeof((_n)[0]))
#endif
#define MINIMUM(_a, _b)	(((_a) < (_b))? (_a) : (_b))
#define MAXIMUM(_a, _b)	(((_a) > (_b))? (_a) : (_b))
#define isnull(_str)	((_str) == NULL || (_str)[0] == '\0')

#define TAILQ_FIRST_ITEM(_head) \
    ((TAILQ_EMPTY((_head)))? NULL : TAILQ_FIRST((_head)))

#ifdef MAILESTD_MT
/*
 * Per thread event(4) base helper.  It makes events sure they happen always
 * on the thread called these functions.
 */
static pthread_key_t	 evbase_key;
static int		 evbase_key_initialized = 0;

#define EVENT_INIT()							\
	do {								\
		struct event_base	*evbase;			\
									\
		evbase = event_init();					\
		if (evbase_key_initialized == 0) {			\
			pthread_key_create(&evbase_key, NULL);		\
			evbase_key_initialized = 1;			\
		}							\
		pthread_setspecific(evbase_key, evbase);		\
	} while (0 /* CONSTCOND */)
#define EVENT_SET(_ev, _fd, _event, _fn, _ctx)				\
	do {								\
		event_set((_ev), (_fd), (_event), (_fn), (_ctx));	\
		event_base_set(pthread_getspecific(evbase_key), (_ev));	\
	} while (0 /* CONSTCOND */)
#define EVENT_LOOP(_flags)						\
	do {								\
		event_base_loop(pthread_getspecific(evbase_key),	\
		    (_flags));						\
	} while (0 /* CONSTCOND */)
#define BUFFEREVENT_ENABLE(_bufev, _event)				\
	do {								\
		bufferevent_base_set(pthread_getspecific(evbase_key),	\
		    (_bufev));						\
		bufferevent_enable((_bufev), (_event));			\
	} while (0 /* CONSTCOND */)
#else
#define EVENT_INIT		event_init
#define EVENT_SET		event_set
#define EVENT_LOOP		event_loop
#define BUFFEREVENT_ENABLE	bufferevent_enable
#endif
