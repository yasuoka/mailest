LOCALBASE?=	/usr/local
BINDIR=		${LOCALBASE}/sbin
MANDIR=		${LOCALBASE}/man/man

CFLAGS+=	-I${.CURDIR}/.. -I${LOCALBASE}/include
LIBESTRAIER=	${LOCALBASE}/lib/libestraier.a
LIBQDBM=	${LOCALBASE}/lib/libqdbm.a
LIBICONV=	${LOCALBASE}/lib/libiconv.a

.include "${.CURDIR}/../compat.mk"

.PATH: ${.CURDIR}/..
