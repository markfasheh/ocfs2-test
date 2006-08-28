#!/bin/bash
#
REMOTE=ca-test13
LOGFILE=/tmp/cross14.log
LOOP=30
TDIR=/oastdbf7/oradata/mmatsuna/crossdelete;
DEPTH=256
SOURCE=/home/mmatsuna/Tests/OCFS/workfiles/linuxsrc.tar.gz
for (( i=1; i < ${LOOP}; i++ ))
do
   echo "Run #${i}" |tee -a ${LOGFILE};
   /home/mmatsuna/Tests/OCFS/bin/cross_delete.sh -n ${REMOTE} -b ${TDIR} -l ${DEPTH} -f ${SOURCE} |tee -a ${LOGFILE};
done;

