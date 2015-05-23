/*
 * Copyright (c) 2008 Reyk Floeter <reyk@openbsd.org>
 * Copyright (c) 2004 Esben Norby <norby@openbsd.org>
 * Copyright (c) 2003, 2004 Henning Brauer <henning@openbsd.org>
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>

#include "parser.h"

enum token_type {
	NOTOKEN,
	ENDTOKEN,
	FOLDER,
	KEYWORD,
	SEARCH_FLAG,
	SEARCH_PARAM,
	SEARCH_MAX,
	SEARCH_ORD,
	SEARCH_IC,
	SEARCH_ATTR,
	SEARCH_PHRASE,
	MSGID,
	PARID
};

struct token {
	enum token_type		 type;
	const char		*keyword;
	int			 value;
	const struct token	*next;
};

static const struct token t_main[];
static const struct token t_folder[];
static const struct token t_search[];
static const struct token t_search_max[];
static const struct token t_search_ord[];
static const struct token t_search_ic[];
static const struct token t_search_attr[];
static const struct token t_smew[];
static const struct token t_msgid[];

static const struct token t_main[] = {
	{KEYWORD,	"start",	START,		NULL},
	{KEYWORD,	"stop",		STOP,		NULL},
	{KEYWORD,	"restart",	RESTART,	NULL},
	{KEYWORD,	"csearch",	CSEARCH,	t_search},
	{KEYWORD,	"smew",		SEARCH_SMEW,	t_smew},
	{KEYWORD,	"message-id",	MESSAGE_ID,	t_msgid},
	{KEYWORD,	"parent-id",	PARENT_ID,	t_msgid},
	{KEYWORD,	"update",	UPDATE,		t_folder},
	{KEYWORD,	"suspend",	SUSPEND,	NULL},
	{KEYWORD,	"resume",	RESUME,		NULL},
	{KEYWORD,	"debug",	DEBUGI,		NULL},
	{KEYWORD,	"-debug",	DEBUGD,		NULL},
	{ENDTOKEN,	"",		NONE,		NULL}
};
static const struct token t_folder[] = {
	{NOTOKEN,	"",		NONE,		NULL},
	{FOLDER,	"",		NONE,		NULL},
	{ENDTOKEN,	"",		NONE,		NULL}
};
static const struct token t_search[] = {
	{SEARCH_FLAG,	"-vu",		SEARCH_FLAG_VU,		t_search},
	{SEARCH_PARAM,	"-max",		SEARCH_FLAG_MAX,	t_search_max},
	{SEARCH_PARAM,	"-ord",		SEARCH_FLAG_ORD,	t_search_ord},
	{SEARCH_PARAM,	"-ic",		SEARCH_FLAG_IC,		t_search_ic},
	{SEARCH_PARAM,	"-attr",	SEARCH_FLAG_ATTR,	t_search_attr},
	{SEARCH_PHRASE,	"",		NONE,		NULL},
	{ENDTOKEN,	"",		NONE,		NULL}
};
static const struct token t_search_max[] = {
	{SEARCH_MAX,	"",		NONE,		t_search},
	{ENDTOKEN,	"",		NONE,		NULL}
};
static const struct token t_search_ord[] = {
	{SEARCH_ORD,	"",		NONE,		t_search},
	{ENDTOKEN,	"",		NONE,		NULL}
};
static const struct token t_search_ic[] = {
	{SEARCH_IC,	"",		NONE,		t_search},
	{ENDTOKEN,	"",		NONE,		NULL}
};
static const struct token t_search_attr[] = {
	{SEARCH_ATTR,	"",		NONE,		t_search},
	{ENDTOKEN,	"",		NONE,		NULL}
};
static const struct token t_smew[] = {
	{MSGID,		"",		NONE,		t_folder},
	{ENDTOKEN,	"",		NONE,		NULL}
};
static const struct token t_msgid[] = {
	{MSGID,		"",		NONE,		NULL},
	{ENDTOKEN,	"",		NONE,		NULL}
};

static struct parse_result	 res;

const struct token		*match_token(char *, const struct token []);
void				 show_valid_args(const struct token []);
static int			 parse_result_add_attr(struct parse_result *,
				    const char *);

struct parse_result *
parse(int argc, char *argv[])
{
	const struct token	*table = t_main;
	const struct token	*match;

	bzero(&res, sizeof(res));

	while (argc >= 0) {
		if ((match = match_token(argv[0], table)) == NULL) {
			fprintf(stderr, "valid commands/args:\n");
			show_valid_args(table);
			return (NULL);
		}

		argc--;
		argv++;

		if (match->type == NOTOKEN || match->next == NULL)
			break;

		table = match->next;
	}

	if (argc > 0) {
		fprintf(stderr, "superfluous argument: %s\n", argv[0]);
		return (NULL);
	}

	return (&res);
}

const struct token *
match_token(char *word, const struct token table[])
{
	u_int			  i, match = 0;
	const struct token	 *t = NULL;
	int			  terminal = 0;
	const char		 *errstr;

	for (i = 0; table[i].type != ENDTOKEN; i++) {
		switch (table[i].type) {
		case NOTOKEN:
			if (word == NULL || strlen(word) == 0) {
				match++;
				t = &table[i];
			}
			break;

		case KEYWORD:
			if (word != NULL && strncmp(word, table[i].keyword,
			    strlen(word)) == 0) {
				match++;
				t = &table[i];
				if (t->value)
					res.action = t->value;
			}
			break;

		case FOLDER:
			if (match == 0 && word != NULL && strlen(word) > 0) {
				if (word[0] == '-' && word[1] == 'h')
					break;
				t = &table[i];
				res.folder = strdup(word);
				match++;
			}
			break;

		case SEARCH_PHRASE:
			if (res.search.phrase == NULL && match == 0 &&
			    word != NULL && strlen(word) > 0) {
				match++;
				t = &table[i];
				res.search.phrase = strdup(word);
			}
			break;
		case SEARCH_FLAG:
		case SEARCH_PARAM:
			if (match == 0 && word != NULL &&
			    strcmp(word, table[i].keyword) == 0) {
				match++;
				t = &table[i];
				if (t->value)
					res.search.flags |= t->value;
			}
			break;
		case SEARCH_MAX:
			if (match == 0 && word != NULL && strlen(word) > 0) {
				match++;
				t = &table[i];
				res.search.max =
				    strtonum(word, -1, INT_MAX, &errstr);
				if (errstr) {
					fprintf(stderr, "%s: %s\n", errstr,
					    word);
					return (NULL);
				}
			}
			break;
		case SEARCH_ATTR:
			if (match == 0 && word != NULL && strlen(word) > 0) {
				match++;
				t = &table[i];
				if (parse_result_add_attr(&res, word) != 0)
					return (NULL);
			}
			break;
		case SEARCH_ORD:
			if (res.search.ord == NULL && match == 0 &&
			    word != NULL && strlen(word) > 0) {
				match++;
				t = &table[i];
				res.search.ord = strdup(word);
			}
			break;
		case SEARCH_IC:
			if (res.search.ic == NULL && match == 0 &&
			    word != NULL && strlen(word) > 0) {
				match++;
				t = &table[i];
				res.search.ic = strdup(word);
			}
			break;
		case MSGID:
			if (res.search.ic == NULL && match == 0 &&
			    word != NULL && strlen(word) > 0) {
				match++;
				t = &table[i];
				res.msgid = strdup(word);
			}
			break;
		case ENDTOKEN:
			break;
		}
		if (terminal)
			break;
	}

	if (terminal) {
		t = &table[i];
	} else if (match != 1) {
		if (word == NULL)
			fprintf(stderr, "missing argument:\n");
		else if (match > 1)
			fprintf(stderr, "ambiguous argument: %s\n", word);
		else if (match < 1)
			fprintf(stderr, "unknown argument: %s\n", word);
		return (NULL);
	}

	return (t);
}

void
show_valid_args(const struct token table[])
{
	int	i;

	for (i = 0; table[i].type != ENDTOKEN; i++) {
		switch (table[i].type) {
		case NOTOKEN:
			fprintf(stderr, "  <cr>\n");
			break;
		case KEYWORD:
			fprintf(stderr, "  %s\n", table[i].keyword);
			break;
		case SEARCH_FLAG:
			if ((res.search.flags & table[i].value) == 0)
				fprintf(stderr, "  %s\n", table[i].keyword);
			break;
		case SEARCH_PARAM:
			if ((res.search.flags & table[i].value) == 0)
				fprintf(stderr, "  %s <%s>\n",
				    table[i].keyword, table[i].keyword + 1);
			break;
		case FOLDER:
			fprintf(stderr, "  <folder>\n");
			break;
		case SEARCH_MAX:
			fprintf(stderr, "  <max>\n");
			break;
		case SEARCH_ORD:
			fprintf(stderr, "  <order>\n");
			break;
		case SEARCH_IC:
			fprintf(stderr, "  <ic>\n");
			break;
		case SEARCH_ATTR:
			fprintf(stderr, "  <attr>\n");
			break;
		case SEARCH_PHRASE:
			fprintf(stderr, "  <phrase>\n");
			break;
		case MSGID:
			fprintf(stderr, "  <message-id>\n");
			break;
		case ENDTOKEN:
			break;
		}
	}
}

static int
parse_result_add_attr(struct parse_result *result, const char *word)
{
	char	**attrs;
	int	  n = 0;

	n = 0;
	if (result->search.attrs != NULL) {
		for (n = 0; result->search.attrs[n]; n++)
			;
	}
	attrs = reallocarray(result->search.attrs, n + 1, sizeof(char *));
	if (attrs == NULL) {
		fprintf(stderr, "realloc_array(): %s", strerror(errno));
		return (-1);
	} else {
		attrs[n++] = strdup(word);
		attrs[n] = NULL;
		result->search.attrs = attrs;
	}
	result->search.flags &= ~SEARCH_FLAG_ATTR;

	return (0);
}
