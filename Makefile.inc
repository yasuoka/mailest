LOCALBASE?=	/usr/local
PREFIX?=	${LOCALBASE}
BINDIR=		${PREFIX}/sbin
MANDIR=		${PREFIX}/man/man

CFLAGS+=	-I${.CURDIR}/.. -I${LOCALBASE}/include
LIBESTRAIER=	${LOCALBASE}/lib/libestraier.a
LIBQDBM=	${LOCALBASE}/lib/libqdbm.a

.include "${.CURDIR}/../compat.mk"

.PATH: ${.CURDIR}/..
