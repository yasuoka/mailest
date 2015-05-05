MAILESTD_MT=	#

PREFIX=		/usr/local
BINDIR=		${PREFIX}/sbin
MANDIR=		${PREFIX}/man/man

PROG=		mailestd
SRCS=		bytebuf.c mailestd.c mailestctl.c parse.y parser.c
LINKS=		${BINDIR}/mailestd ${PREFIX}/bin/mailestctl
CFLAGS+=	-I/usr/local/include -DHAVE_LIBESTDRAFT
LDFLAGS=	-L/usr/local/lib
LDADD=		-lestraier -levent
DPADD=		${LIBESTRAIER} ${LIBEVENT}
MAN=		mailestctl.1 mailestd.8 mailestd.conf.5 mew-mailest.1

.ifdef DEBUG
CFLAGS+=	-DMAILESTD_DEBUG
.endif
.ifdef MAILESTD_MT
CFLAGS+=	-DMAILESTD_MT
LDADD+=		-lpthread
DPADD+=		${LIBPTHREAD}
.endif

beforeinstall:
	${INSTALL} ${INSTALL_COPY} -o ${BINOWN} -g ${BINGRP} -m ${BINMODE} \
	    ${.CURDIR}/../mew-mailest.sh ${DESTDIR}${PREFIX}/bin/mew-mailest

.ifdef .FreeBSD
LIBESTDRAFTOBJ!=cd ${.CURDIR}/../libestdraft; ${MAKE} whereobj

# __FreeBSD_version < 1100072 doesn't have reallocarray()
SRCS+=		reallocarray.c
.PATH:		${.CURDIR}/../replace

.else
.include <bsd.obj.mk>
.if exists(${.CURDIR}/../libestdraft/${__objdir})
LIBESTDRAFTOBJ=	${.CURDIR}/../libestdraft/${__objdir}
.else
LIBESTDRAFTOBJ=	${.CURDIR}/../libestdraft
.endif
.endif

LDADD+=         -L${LIBESTDRAFTOBJ} -lestdraft
DPADD+=         -L${LIBESTDRAFTOBJ}/libestdraft.a

.include <bsd.prog.mk>
