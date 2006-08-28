#!/bin/bash
#
LOGFILE=`dirname ${0}`/double_cross.log
DEPTH=16
SOURCE="/home/mmatsuna/Tests/OCFS/workfiles/linuxsrc.tar.gz"
if [ "X${1}" == "X" ]; then
   echo "Enter the loop count for this job.";
   exit 1;
fi;
#
echo "Starting double_cross job on `hostname -s`" |tee -a ${LOGFILE};
if [ `hostname -s` == "ca-test32" -o `hostname -s` == "ca-test30" ]; then
   if [ `hostname -s` == "ca-test32" ]; then
      REMOTE="ca-test31";
      TDIR=/oastdbf1/oradata/mmatsuna/t32
   else
      REMOTE="ca-test33";
      TDIR=/oastdbf1/oradata/mmatsuna/t30
   fi;  
fi;  
if [ ! -d ${TDIR} ]; then
   mkdir -p ${TDIR};
fi;
#
# LOOP
#
for (( i=1; i < ${1}; i++ ))
do
   echo "Run #${i}" |tee -a ${LOGFILE};
   /home/mmatsuna/Tests/OCFS/bin/cross_delete.sh -n ${REMOTE} -b ${TDIR} -l ${DEPTH} -f ${SOURCE} |tee -a ${LOGFILE};
done;
