#!/bin/sh
#
# wrapper script for mew
#
MAILESTCTL=${MAILESTCTL:-mailestctl}

update() {
	while getopts '+s:b:' ch $@; do
		case $ch in
		s)	echo -S; echo "\"$OPTARG\"" ;;
		b)	echo -m; echo "\"$OPTARG\"" ;;
		esac
	done
	shift $(expr $OPTIND - 1)
	echo update
	for _a in "$@"; do echo "\"$1\""; done
}

if [ "$1" = "search" ]; then
	shift
	eval set -- \
	    $(while [ $# -gt 2 ]; do echo "\"$1\""; shift; done; echo "\"$2\"")
	exec $MAILESTCTL csearch "$@"
else
	eval set -- $(update "$@")
	exec $MAILESTCTL "$@"
fi
