MAILESTDIR=	${.CURDIR}/../mailest
MAN!=		cd ${MAILESTDIR}; ls *.[0-8]
MANHTML=	${MAN:S/$/.html/}
CLEANFILES=	${MAN:S/$/.html/}

all:		${MANHTML}

clean:
	rm ${CLEANFILES}

.for _man in ${MAN}
${_man}.html: ${MAILESTDIR}/${_man}
	mandoc -Thtml ${MAILESTDIR}/${_man} > ${_man}.html
.endfor
