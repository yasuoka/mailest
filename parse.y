/*	$OpenBSD: parse.y,v 1.36 2014/11/20 05:51:21 jsg Exp $	*/

/*
 * Copyright (c) 2007, 2008, 2012 Reyk Floeter <reyk@openbsd.org>
 * Copyright (c) 2004, 2005 Esben Norby <norby@openbsd.org>
 * Copyright (c) 2004 Ryan McBride <mcbride@openbsd.org>
 * Copyright (c) 2002, 2003, 2004 Henning Brauer <henning@openbsd.org>
 * Copyright (c) 2001 Markus Friedl.  All rights reserved.
 * Copyright (c) 2001 Daniel Hartmeier.  All rights reserved.
 * Copyright (c) 2001 Theo de Raadt.  All rights reserved.
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

%{
#include "compat.h"

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/stat.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <syslog.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include "mailestd.h"
#include "defs.h"

void	 mailestd_log(int, const char *, ...)
	    __attribute__((__format__(__syslog__,2,3)));
void	 mailestd_vlog(int, const char *, va_list);

static struct mailestd_conf	*conf;

#define logit mailestd_log
static void log_warn(const char *, ...);
static void log_warnx(const char *, ...);
static void fatal(const char *);
static void fatalx(const char *);

TAILQ_HEAD(files, file)		 files = TAILQ_HEAD_INITIALIZER(files);
static struct file {
	TAILQ_ENTRY(file)	 entry;
	FILE			*stream;
	char			*name;
	int			 lineno;
	int			 errors;
} *file, *topfile;
struct file	*pushfile(const char *, int);
int		 popfile(void);
int		 check_file_secrecy(int, const char *);
int		 yyparse(void);
int		 yylex(void);
int		 yyerror(const char *, ...)
    __attribute__((__format__ (printf, 1, 2)))
    __attribute__((__nonnull__ (1)));
int		 kw_cmp(const void *, const void *);
int		 lookup(char *);
int		 lgetc(int);
int		 lungetc(int);
int		 findeol(void);

TAILQ_HEAD(symhead, sym)	 symhead = TAILQ_HEAD_INITIALIZER(symhead);
struct sym {
	TAILQ_ENTRY(sym)	 entry;
	int			 used;
	int			 persist;
	char			*nam;
	char			*val;
};
int		 symset(const char *, const char *, int);
char		*symget(const char *);

typedef struct {
	union {
		int64_t		  number;
		char		 *string;
		char		**strings;
	} v;
	int lineno;
} YYSTYPE;

%}

%token	INCLUDE ERROR
%token	COUNT DATABASE DEBUG DELAY DISABLE FOLDERS GUESSPARID LEVEL LOG
%token	MAILDIR MONITOR ROTATE PATH SOCKET SUFFIXES SIZE TASKS TRIMSIZE
%token	<v.string>	STRING
%token  <v.number>	NUMBER
%type	<v.strings>	strings

%%

grammar		: /* empty */
		| grammar include '\n'
		| grammar '\n'
		| grammar main '\n'
		| grammar varset '\n'		{ file->errors++; }
		| grammar error '\n'		{ file->errors++; }
		;

include		: INCLUDE STRING		{
			struct file	*nfile;

			if ((nfile = pushfile($2, 0)) == NULL) {
				yyerror("failed to include file %s", $2);
				free($2);
				YYERROR;
			}
			free($2);

			file = nfile;
			lungetc('\n');
		}
		;

varset		: STRING '=' STRING	{
			if (symset($1, $3, 0) == -1)
				fatal("cannot store variable");
			free($1);
			free($3);
		}
		;

main		: MAILDIR STRING	{
			conf->maildir = $2;
		}
		| SOCKET STRING		{
			conf->sock_path = $2;
		}
		| TASKS NUMBER		{
			conf->tasks = $2;
		}
		| TRIMSIZE NUMBER	{
			conf->trim_size = $2;
		}
		| SUFFIXES strings	{
			conf->suffixes = $2;
		}
		| FOLDERS strings	{
			conf->folders = $2;
		}
		| LOG log_opts
		| DATABASE database_opts
		| DEBUG LEVEL NUMBER	{
			conf->debug = $3;
		}
		| MONITOR		{
			conf->monitor = 1;
		}
		| MONITOR monitor_opts
		;
		| GUESSPARID {
			conf->paridguess = 1;
		}
		;

strings		: strings STRING	{
			int n;
			char **strings;
			for (n = 0; $1[n] != NULL; n++)
				;
			strings = reallocarray($1, n + 2, sizeof(char *));
			if (strings == NULL)
				fatal("out of memory");
			$$ = strings;
			$$[n++] = $2;
			$$[n++] = NULL;
		}
		| STRING		{
			$$ = calloc(2, sizeof(char *));
			if ($$ == NULL)
				fatal("out of memory");
			$$[0] = $1;
			$$[1] = NULL;
		}
		;

log_opts	: log_opts log_opt
		| log_opt
		;

log_opt		: PATH STRING		{
			conf->log_path = $2;
		}
		| ROTATE log_rotate_opts
		;

log_rotate_opts : log_rotate_opts log_rotate_opt
		| log_rotate_opt
		;

log_rotate_opt	: COUNT NUMBER		{
			conf->log_count = $2;
		}
		| SIZE NUMBER		{
			conf->log_size = $2;
		}
		;

database_opt	: PATH STRING		{
			conf->db_path = $2;
		}
		;

database_opts	: database_opts database_opt
		| database_opt
		;

monitor_opt	: DISABLE		{
			conf->monitor = 0;
		}
		| DELAY NUMBER		{
			conf->monitor_delay = $2;
		}
monitor_opts	: monitor_opts monitor_opt
		| monitor_opt
		;
%%

struct keywords {
	const char	*k_name;
	int		 k_val;
};

int
yyerror(const char *fmt, ...)
{
	va_list		 ap;
	char		*msg;

	file->errors++;
	va_start(ap, fmt);
	if (vasprintf(&msg, fmt, ap) == -1)
		fatalx("yyerror vasprintf");
	va_end(ap);
	logit(LOG_CRIT, "%s:%d: %s", file->name, yylval.lineno, msg);
	free(msg);
	return (0);
}

int
kw_cmp(const void *k, const void *e)
{
	return (strcmp(k, ((const struct keywords *)e)->k_name));
}

int
lookup(char *s)
{
	/* this has to be sorted always */
	static const struct keywords keywords[] = {
		{ "count",		COUNT },
		{ "database",		DATABASE },
		{ "debug",		DEBUG },
		{ "delay",		DELAY },
		{ "disable",		DISABLE },
		{ "folders",		FOLDERS },
		{ "guess-parid",	GUESSPARID },
		{ "include",		INCLUDE },
		{ "level",		LEVEL },
		{ "log",		LOG },
		{ "maildir",		MAILDIR },
		{ "monitor",		MONITOR },
		{ "path",		PATH },
		{ "rotate",		ROTATE },
		{ "size",		SIZE },
		{ "socket",		SOCKET },
		{ "suffixes",		SUFFIXES },
		{ "tasks",		TASKS },
		{ "trim-size",		TRIMSIZE },
	};
	const struct keywords	*p;

	p = bsearch(s, keywords, sizeof(keywords)/sizeof(keywords[0]),
	    sizeof(keywords[0]), kw_cmp);

	if (p)
		return (p->k_val);
	else
		return (STRING);
}

#define MAXPUSHBACK	128

u_char	*parsebuf;
int	 parseindex;
u_char	 pushback_buffer[MAXPUSHBACK];
int	 pushback_index = 0;

int
lgetc(int quotec)
{
	int		c, next;

	if (parsebuf) {
		/* Read character from the parsebuffer instead of input. */
		if (parseindex >= 0) {
			c = parsebuf[parseindex++];
			if (c != '\0')
				return (c);
			parsebuf = NULL;
		} else
			parseindex++;
	}

	if (pushback_index)
		return (pushback_buffer[--pushback_index]);

	if (quotec) {
		if ((c = getc(file->stream)) == EOF) {
			yyerror("reached end of file while parsing quoted "
			    "string");
			if (file == topfile || popfile() == EOF)
				return (EOF);
			return (quotec);
		}
		return (c);
	}

	while ((c = getc(file->stream)) == '\\') {
		next = getc(file->stream);
		if (next != '\n') {
			c = next;
			break;
		}
		yylval.lineno = file->lineno;
		file->lineno++;
	}
	if (c == '\t' || c == ' ') {
		/* Compress blanks to a single space. */
		do {
			c = getc(file->stream);
		} while (c == '\t' || c == ' ');
		ungetc(c, file->stream);
		c = ' ';
	}

	while (c == EOF) {
		if (file == topfile || popfile() == EOF)
			return (EOF);
		c = getc(file->stream);
	}
	return (c);
}

int
lungetc(int c)
{
	if (c == EOF)
		return (EOF);
	if (parsebuf) {
		parseindex--;
		if (parseindex >= 0)
			return (c);
	}
	if (pushback_index < MAXPUSHBACK-1)
		return (pushback_buffer[pushback_index++] = c);
	else
		return (EOF);
}

int
findeol(void)
{
	int	c;

	parsebuf = NULL;

	/* skip to either EOF or the first real EOL */
	while (1) {
		if (pushback_index)
			c = pushback_buffer[--pushback_index];
		else
			c = lgetc(0);
		if (c == '\n') {
			file->lineno++;
			break;
		}
		if (c == EOF)
			break;
	}
	return (ERROR);
}

int
yylex(void)
{
	u_char	 buf[8096];
	u_char	*p, *val;
	int	 quotec, next, c;
	int	 token;

top:
	p = buf;
	while ((c = lgetc(0)) == ' ' || c == '\t')
		; /* nothing */

	yylval.lineno = file->lineno;
	if (c == '#')
		while ((c = lgetc(0)) != '\n' && c != EOF)
			; /* nothing */
	if (c == '$' && parsebuf == NULL) {
		while (1) {
			if ((c = lgetc(0)) == EOF)
				return (0);

			if (p + 1 >= buf + sizeof(buf) - 1) {
				yyerror("string too long");
				return (findeol());
			}
			if (isalnum(c) || c == '_') {
				*p++ = c;
				continue;
			}
			*p = '\0';
			lungetc(c);
			break;
		}
		val = (u_char *)symget((const char *)buf);
		if (val == NULL) {
			yyerror("macro '%s' not defined", buf);
			return (findeol());
		}
		parsebuf = val;
		parseindex = 0;
		goto top;
	}

	switch (c) {
	case '\'':
	case '"':
		quotec = c;
		while (1) {
			if ((c = lgetc(quotec)) == EOF)
				return (0);
			if (c == '\n') {
				file->lineno++;
				continue;
			} else if (c == '\\') {
				if ((next = lgetc(quotec)) == EOF)
					return (0);
				if (next == quotec || c == ' ' || c == '\t')
					c = next;
				else if (next == '\n') {
					file->lineno++;
					continue;
				} else
					lungetc(next);
			} else if (c == quotec) {
				*p = '\0';
				break;
			} else if (c == '\0') {
				yyerror("syntax error");
				return (findeol());
			}
			if (p + 1 >= buf + sizeof(buf) - 1) {
				yyerror("string too long");
				return (findeol());
			}
			*p++ = c;
		}
		yylval.v.string = strdup((const char *)buf);
		if (yylval.v.string == NULL)
			err(1, "yylex: strdup");
		return (STRING);
	}

#define allowed_to_end_number(x) \
	(isspace(x) || x == ')' || x ==',' || x == '/' || x == '}' || x == '=')

	if (c == '-' || isdigit(c)) {
		do {
			*p++ = c;
			if ((unsigned)(p-buf) >= sizeof(buf)) {
				yyerror("string too long");
				return (findeol());
			}
		} while ((c = lgetc(0)) != EOF && isdigit(c));
		lungetc(c);
		if (p == buf + 1 && buf[0] == '-')
			goto nodigits;
		if (c == EOF || allowed_to_end_number(c)) {
			const char *errstr = NULL;

			*p = '\0';
			yylval.v.number = strtonum((const char *)buf, LLONG_MIN,
			    LLONG_MAX, &errstr);
			if (errstr) {
				yyerror("\"%s\" invalid number: %s",
				    buf, errstr);
				return (findeol());
			}
			return (NUMBER);
		} else {
nodigits:
			while (p > buf + 1)
				lungetc(*--p);
			c = *--p;
			if (c == '-')
				return (c);
		}
	}

#define allowed_in_string(x) \
	(isalnum(x) || (ispunct(x) && x != '(' && x != ')' && \
	x != '{' && x != '}' && \
	x != '!' && x != '=' && x != '#' && \
	x != ','))

	if (isalnum(c) || c == ':' || c == '_') {
		do {
			*p++ = c;
			if ((unsigned)(p-buf) >= sizeof(buf)) {
				yyerror("string too long");
				return (findeol());
			}
		} while ((c = lgetc(0)) != EOF && (allowed_in_string(c)));
		lungetc(c);
		*p = '\0';
		if ((token = lookup((char *)buf)) == STRING)
			if ((yylval.v.string = strdup((char *)buf)) == NULL)
				err(1, "yylex: strdup");
		return (token);
	}
	if (c == '\n') {
		yylval.lineno = file->lineno;
		file->lineno++;
	}
	if (c == EOF)
		return (0);
	return (c);
}

int
check_file_secrecy(int fd, const char *fname)
{
	struct stat	st;

	if (fstat(fd, &st)) {
		log_warn("cannot stat %s", fname);
		return (-1);
	}
	if (st.st_uid != 0 && st.st_uid != getuid()) {
		log_warnx("%s: owner not root or current user", fname);
		return (-1);
	}
	if (st.st_mode & (S_IWGRP | S_IXGRP | S_IRWXO)) {
		log_warnx("%s: group writable or world read/writable", fname);
		return (-1);
	}
	return (0);
}

struct file *
pushfile(const char *name, int secret)
{
	struct file	*nfile;

	if ((nfile = calloc(1, sizeof(struct file))) == NULL) {
		log_warn("malloc");
		return (NULL);
	}
	if ((nfile->name = strdup(name)) == NULL) {
		log_warn("malloc");
		free(nfile);
		return (NULL);
	}
	if ((nfile->stream = fopen(nfile->name, "r")) == NULL) {
		log_warn("%s", nfile->name);
		free(nfile->name);
		free(nfile);
		return (NULL);
	} else if (secret &&
	    check_file_secrecy(fileno(nfile->stream), nfile->name)) {
		fclose(nfile->stream);
		free(nfile->name);
		free(nfile);
		return (NULL);
	}
	nfile->lineno = 1;
	TAILQ_INSERT_TAIL(&files, nfile, entry);
	return (nfile);
}

int
popfile(void)
{
	struct file	*prev;

	if ((prev = TAILQ_PREV(file, files, entry)) != NULL)
		prev->errors += file->errors;

	TAILQ_REMOVE(&files, file, entry);
	fclose(file->stream);
	free(file->name);
	free(file);
	file = prev;
	return (file ? 0 : EOF);
}

int
symset(const char *nam, const char *val, int persist)
{
	struct sym	*sym;

	for (sym = TAILQ_FIRST(&symhead); sym && strcmp(nam, sym->nam);
	    sym = TAILQ_NEXT(sym, entry))
		;	/* nothing */

	if (sym != NULL) {
		if (sym->persist == 1)
			return (0);
		else {
			free(sym->nam);
			free(sym->val);
			TAILQ_REMOVE(&symhead, sym, entry);
			free(sym);
		}
	}
	if ((sym = calloc(1, sizeof(*sym))) == NULL)
		return (-1);

	sym->nam = strdup(nam);
	if (sym->nam == NULL) {
		free(sym);
		return (-1);
	}
	sym->val = strdup(val);
	if (sym->val == NULL) {
		free(sym->nam);
		free(sym);
		return (-1);
	}
	sym->used = 0;
	sym->persist = persist;
	TAILQ_INSERT_TAIL(&symhead, sym, entry);
	return (0);
}

int
cmdline_symset(char *s)
{
	char	*sym, *val;
	int	ret;
	size_t	len;

	if ((val = strrchr(s, '=')) == NULL)
		return (-1);

	len = strlen(s) - strlen(val) + 1;
	if ((sym = malloc(len)) == NULL)
		errx(1, "cmdline_symset: malloc");

	(void)strlcpy(sym, s, len);

	ret = symset(sym, val + 1, 1);
	free(sym);

	return (ret);
}

char *
symget(const char *nam)
{
	struct sym	*sym;

	TAILQ_FOREACH(sym, &symhead, entry)
		if (strcmp(nam, sym->nam) == 0) {
			sym->used = 1;
			return (sym->val);
		}
	return (NULL);
}

void
free_config(struct mailestd_conf *c)
{
	int	 i;

	if (c->folders != NULL) {
		for (i = 0; c->folders[i] != NULL; i++)
			free(c->folders[i]);
	}
	free(c->folders);
	if (c->suffixes != NULL) {
		for (i = 0; c->suffixes[i] != NULL; i++)
			free(c->suffixes[i]);
	}
	free(c->suffixes);
	free(c->log_path);
	free(c->db_path);
	free(c->sock_path);
	free(c->maildir);
	free(c);
}

struct mailestd_conf *
parse_config(const char *filename, const char *maildir)
{
	struct sym	*sym, *next;
	int		 errors;
	char		 tmppath[PATH_MAX];
	struct stat	 st;

	conf = calloc(1, sizeof(struct mailestd_conf));
	conf->tasks = MAILESTD_NTASKS;
	conf->log_size = MAILESTD_LOGSIZ;
	conf->log_count = MAILESTD_LOGROTMAX;
	conf->trim_size = MAILESTD_TRIMSIZE;
	conf->monitor = 1;
	conf->monitor_delay = MAILESTD_MONITOR_DELAY;

	if (stat(filename, &st) == 0) {
		if ((file = pushfile(filename, 0)) == NULL) {
			free(conf);
			return (NULL);
		}
		topfile = file;

		yyparse();
		errors = file->errors;
		popfile();

		/* Free macros and check which have not been used. */
		TAILQ_FOREACH_SAFE(sym, &symhead, entry, next) {
			if (!sym->persist) {
				TAILQ_REMOVE(&symhead, sym, entry);
				free(sym->nam);
				free(sym->val);
				free(sym);
			}
		}
		if (errors) {
			free_config(conf);
			return (NULL);
		}
	} else if (errno != ENOENT) {
		log_warn("open(%s)", filename);
		return (NULL);
	}
	if (conf->maildir == NULL || conf->maildir[0] != '/') {
		strlcpy(tmppath, getenv("HOME"), sizeof(tmppath));
		strlcat(tmppath, "/", sizeof(tmppath));
		if (conf->maildir != NULL) {
			strlcat(tmppath, conf->maildir, sizeof(tmppath));
			free(conf->maildir);
		} else
			strlcpy(tmppath, maildir, sizeof(tmppath));
		conf->maildir = strdup(tmppath);
	}
	if (conf->log_path == NULL || conf->log_path[0] != '/') {
		strlcpy(tmppath, conf->maildir, sizeof(tmppath));
		strlcat(tmppath, "/", sizeof(tmppath));
		if (conf->log_path != NULL) {
			strlcat(tmppath, conf->log_path, sizeof(tmppath));
			free(conf->log_path);
		} else
			strlcat(tmppath, MAILESTD_LOG_PATH, sizeof(tmppath));
		conf->log_path = strdup(tmppath);
	}
	if (conf->sock_path == NULL || conf->sock_path[0] != '/') {
		strlcpy(tmppath, conf->maildir, sizeof(tmppath));
		strlcat(tmppath, "/", sizeof(tmppath));
		if (conf->sock_path != NULL) {
			strlcat(tmppath, conf->sock_path, sizeof(tmppath));
			free(conf->sock_path);
		} else
			strlcat(tmppath, MAILESTD_SOCK_PATH, sizeof(tmppath));
		conf->sock_path = strdup(tmppath);
	}
	if (conf->db_path == NULL || conf->db_path[0] != '/') {
		strlcpy(tmppath, conf->maildir, sizeof(tmppath));
		strlcat(tmppath, "/", sizeof(tmppath));
		if (conf->db_path != NULL) {
			strlcat(tmppath, conf->db_path, sizeof(tmppath));
			free(conf->db_path);
		} else
			strlcat(tmppath, MAILESTD_DBNAME, sizeof(tmppath));
		conf->db_path = strdup(tmppath);
	}

	return (conf);
}

static void
log_warn(const char *fmt, ...)
{
	char	fmt0[BUFSIZ];
	va_list ap;

	strlcpy(fmt0, fmt, sizeof(fmt0));
	strlcat(fmt0, ": %m", sizeof(fmt0));

	va_start(ap, fmt);
	mailestd_vlog(LOG_WARNING, fmt0, ap);
	va_end(ap);
}

static void
log_warnx(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	mailestd_vlog(LOG_WARNING, fmt, ap);
	va_end(ap);
}

static void
fatal(const char *msg)
{
	mailestd_log(LOG_CRIT, "%s: %m", msg);
	abort();
}

static void
fatalx(const char *msg)
{
	mailestd_log(LOG_CRIT, "%s", msg);
	abort();
}
