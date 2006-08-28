#!/bin/bash
#
# create files in a limited loop without waiting, trying to
# get files to be create at the exact same time and corrupt
# the dir index structure.
#
# Typical error is :
# ERROR: Block 0.86695936 (bit# 651) allocated to File Entries 0.10967040 and 0.10962432, fsck.c, 815
#
#
Usage()
{
echo -e "Usage:";
echo -e "${0}: -c [count] -d [directory]-p [logfile] -s [sleep time in miliseconds] -h";
exit;
}
#
LOCALDIR=`dirname ${0}`
if [ "${LOCALDIR}" == "." ]; then
   LOCALDIR=`pwd`;
fi;
BASEDIR=`dirname ${LOCALDIR}`
LOCALNODE=`hostname -s`
ARCH=`uname -m`
COUNT=100
if [ ! -d ${BASEDIR}/logs/`hostname` ]; then
   mkdir -p ${BASEDIR}/logs/`hostname`;
fi;
LOGFILE=${BASEDIR}/logs/`hostname`/dirindex_$$.log

#
if [ $# -eq 0 ]; then
   Usage;
fi;
#
while [ $# -gt 0 ]
do
  case ${1} in
     -c ) COUNT=${2};
          shift 2;
          ;;
     -d ) DIR=${2};
          shift 2;
          ;;
     -o ) LOGFILE=${2};
          shift 2;
          ;;
     -s ) SLEEP=${2};
          shift 2;
          SLEEP=${SLEEP} * 1000;
          ;;
     -h   ) Usage;
          ;;
     *    ) Usage;
          ;;
  esac;
done;
#
for (( i=0; i<= ${COUNT}; i++ ))
do
   echo -e "Creating file ${DIR}/file_${LOCALNODE}_${i}";
   ${BASEDIR}/bin/extendo.${ARCH} ${DIR}/file_${LOCALNODE}_${i} 2048 ${SLEEP} 1
   usleep ${SLEEP};
done
