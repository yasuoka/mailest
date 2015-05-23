#!/bin/sh
#
# wrapper script for mew
#

MAILESTCTL=${MAILESTCTL:-mailestctl}

usage() {
	echo "usage: smew-mailestd [-cph] msgid id.db mydir" >&2
}

cmd=smew
while getopts "pch" ch "$@"; do
	case $ch in
	'p')	cmd='parent-id'
		;;
	'c')	cmd='message-id'
		;;
	'h')	usage
		exit 64
		;;
	esac
done

shift $(($OPTIND - 1))
if [ $# -le 0 ]; then
	usage
	exit 64
fi
msgid=$1
mydir=$3

if [ $cmd = 'smew' ]; then
	exec $MAILESTCTL $cmd "$msgid" "$mydir"
else
	exec $MAILESTCTL $cmd "$msgid"
fi
