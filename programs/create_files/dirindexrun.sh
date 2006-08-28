#!/bin/bash
# 
# This script will run a test with 3 nodes (ca-test2, test6 and test7)
#
# First thing that it will do is to submit a job via "at" command to be
# executed in the future (2 minutes aproximately). 
# Second thing is to wait for some of the files to be create (>1000) and 
# then start a delete process in ca-test2.
# Third thing is to keep a count process on ca-test6, that will keep 
# monitoring the number of files in the directory.
# Forth thing is to issue some ls commands sporadically on ca-test7 to 
# check the directory being tested.
# Last thing, is to run a fsck.ocfs in the filesystem and check if there
# are any errors reported.
GetTime()
{
HOUR=`date +%H`;
MIN=`date +%M`;
if [ ${MIN} -ge 00 -a ${MIN} -lt 58 ]; then
   MIN=`expr $MIN + 2`
else
   if [ ${HOUR} -eq 23 ]; then
      HOUR=00;
   else
      HOUR=`expr $HOUR + 1`
   fi;
   if [ ${MIN} -eq 58 ]; then
      MIN=00;
   else
      MIN=01;
   fi;
fi;
if [ ${MIN} -gt 1 -a ${MIN} -le 9 ]; then
   ATTIME="${HOUR}:0${MIN}";
else
   ATTIME="${HOUR}:${MIN}";
fi;
}
#
CheckTasks()
{
TASK2=`ssh ${RUNUSER}@ca-test2 atq`;
TASK6=`ssh ${RUNUSER}@ca-test6 atq`;
TASK7=`ssh ${RUNUSER}@ca-test7 atq`;
if [ "X$TASK2" == "X" -a "X$TASK6" == "X" -a "X$TASK7" == "X" ]; then
   RUNNING=0;
fi;
sleep 5
}
#
CheckFilesCreated()
{
ENOUGHFILES=0;
while [ ${ENOUGHFILES} -eq 0 ]
do
   COUNT=`find /oastlog/dirindex |wc -l`;
   if [ ${COUNT} -gt 1000 ]; then
      ENOUGHFILES=1;
   fi;
   sleep 5;
done;
}
#
# Main
#
# GLOBAL VARIABLES
#
RUNUSER=mmatsuna;
RUNNING=1;
rm -f /tmp/stop_dirindex;
if [ -f /tmp/stop_run ]; then
   ssh ${RUNUSER}@ca-test2 'rm -f /tmp/stop_run';
   ssh ${RUNUSER}@ca-test6 'rm -f /tmp/stop_run';
   ssh ${RUNUSER}@ca-test7 'rm -f /tmp/stop_run';
fi;
#
if [ `hostname -s` != 'ca-test7' -a "${USER}" != "root" ]; then
   echo "Must run as root on ca-test7";
   exit 1;
fi;
#
while true
do
   if [ -f /tmp/stop_dirindex ]; then
      exit;
   fi;
   GetTime;
#
# schedute tasks on all 3 nodes;
#
   echo -e "`date` - Scheduling tasks at ${ATTIME}";
   ssh ${RUNUSER}@ca-test2 at -f /home/mmatsuna/TESTS/OCFS/bin/runtest ${ATTIME}
   ssh ${RUNUSER}@ca-test6 at -f /home/mmatsuna/TESTS/OCFS/bin/runtest ${ATTIME}
   ssh ${RUNUSER}@ca-test7 at -f /home/mmatsuna/TESTS/OCFS/bin/runtest ${ATTIME}
#
   CheckFilesCreated;
#
# Start removal on ca-test2;
#
   ssh -f ${RUNUSER}@ca-test2 '/home/mmatsuna/TESTS/OCFS/bin/dirindex_background.sh /home/mmatsuna/TESTS/OCFS/bin/dirindex_rm.sh';
#
# Start count on ca-test6
#
   ssh -f ${RUNUSER}@ca-test6 '/home/mmatsuna/TESTS/OCFS/bin/dirindex_background.sh /home/mmatsuna/TESTS/OCFS/bin/dirindex_count.sh';
#
# Start count on ca-test7
#
   ssh -f ${RUNUSER}@ca-test7 '/home/mmatsuna/TESTS/OCFS/bin/dirindex_background.sh /home/mmatsuna/TESTS/OCFS/bin/dirindex_ls.sh';
#
   while [ ${RUNNING} -eq 1 ]
   do
      CheckTasks;
   done;
   ssh ${RUNUSER}@ca-test2 'touch /tmp/stop_run';
   ssh ${RUNUSER}@ca-test6 'touch /tmp/stop_run';
   ssh ${RUNUSER}@ca-test7 'touch /tmp/stop_run';
   sleep 30;
   echo "Running fsck on /dev/sdd1" >> /home/mmatsuna/LOGS/dirindex_fsck.log;
   fsck.ocfs -v /dev/sdb1 >> /home/mmatsuna/LOGS/dirindex_fsck.log;
   find /oastlog/dirindex/ -type f -exec rm -f {} \; -print ;
done;
