#!/bin/bash
# Create snapshot files repeatedly to the specified directory
#
# multi_reflink_snap.sh work_dir work_file
#
#

if [ ! -d "$1" ] ; then
	echo "$1 directory does not exist!"
	exit 1
fi

if [ ! -d "${1}/snapshots" ] ; then
	echo "${1}/snapshots directory does not exist!"
	exit 1
fi

if [ ! -f "$1/$2" ] ; then
	echo "$1/$2 file does not exist!"
	exit 1
fi

WORK_DIR="$1"
WORK_FILE="$2"
WORK_TIME=200
LOOP_COUT=0


function create_snapshot_files()
{
	for i in $(seq 1 100)
	do
		reflink "${WORK_DIR}/${WORK_FILE}" "${WORK_DIR}/snapshots/${WORK_FILE}.`date +%m%d%H%M%S`.$i.`hostname`" || exit 1
	done
}

function remove_snapshot_files()
{
	rm -f ${WORK_DIR}/snapshots/${WORK_FILE}.*.`hostname`
}

LOOP_START=$(date +%s)

while ((1)) ; do

	create_snapshot_files

	LOOP_COUT=`expr $LOOP_COUT + 1`
	LOOP_NOW=$(date +%s)
	LOOP_TIME=`expr $LOOP_NOW - $LOOP_START`
	if [ $LOOP_TIME -gt $WORK_TIME ] ; then
		break
	else
		usleep 10000
	fi

	remove_snapshot_files
done

echo "`hostname`: executed $LOOP_COUT reflink loop."
exit 0
