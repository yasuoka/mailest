MAILESTD_MT=	1
PREFIX=		/usr/local
BINDIR=		${PREFIX}/sbin

PROG=		mailestd
SRCS=		bytebuf.c mailestd.c mailestctl.c parse.y parser.c
LINKS=		${BINDIR}/mailestd ${PREFIX}/bin/mailestctl
NOMAN=		#
CPPFLAGS=	-I/usr/local/include
LDFLAGS=	-L/usr/local/lib
LDADD=		-lestraier -levent
DPADD=		${LIBESTRAIER} ${LIBEVENT}

.ifdef DEBUG
CPPFLAGS+=	-DMAILESTD_DEBUG
.endif
.ifdef MAILESTD_MT
CPPFLAGS+=	-DMAILESTD_MT
LDADD+=		-lpthread
DPADD+=		${LIBPTHREAD}
.endif

beforeinstall:
	${INSTALL} ${INSTALL_COPY} -o ${BINOWN} -g ${BINGRP} -m ${BINMODE} \
	    ${.CURDIR}/../mew-mailest.sh ${DESTDIR}${PREFIX}/bin/mew-mailest

.include <bsd.prog.mk>
