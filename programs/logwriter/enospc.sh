#!/bin/sh
#
# enospc.sh <outdir> <DEVICE>
#
if [ `dirname ${0}` == '.' ]; then
   . ./config.sh
else
   . `dirname ${0}`/config.sh
fi;

if [ $# != 2 ]
then
	echo "usage: enospc.sh <outdir> <DEVICE>"
	exit 0
fi

MOUNT_DIR="/enospc_test"
OUT=$1/enospc_test.log
DEVICE=$2
PRG=enospc_test

echo starting test at $DATE > $OUT

echo formating DEVICE $DEVICE >> $OUT
mkfs.ocfs2 -x $DEVICE -b 4096 -C 4096 524288 >> $OUT 2>&1

if [ -d "$MOUNT_DIR" ]
then 
	echo directory $MOUNT_DIR exist >> $OUT
else
	mkdir $MOUNT_DIR
fi

echo mounting $DEVICE ... >> $OUT
mount -t ocfs2 -o datavolume $DEVICE $MOUNT_DIR
./$PRG $MOUNT_DIR
umount $MOUNT_DIR
rmdir $MOUNT_DIR
