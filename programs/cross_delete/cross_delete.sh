#!/bin/bash
#
# This script will run a test of cross deletion.
# 
# create a dir $BASEDIR/x1/x2/xn and extract contents on it
# on node 1. 
# create a dir $BASEDIR/y1/y2/yn and extrace contents on it
# on node 2.
# On node 1, remove the directory $BASEDIR/x1.
# On node 2, remove the directory $BASEDIR/y1.
#
# Removal of the directories are not performed in parallel.
#
# Parameters:
#    -n node name
#    -b base dir
#    -l sub-directory level deep
#    -f tar file for extraction
#
check_parm() 
{
LINE=`ssh -o 'PasswordAuthentication no' ${NODE_NAME} hostname`;
if [ ${?} -ne 0 ]; then
   printf "\nHost ${NODE_NAME} is not ssh enabled to accept commands from this host.";
   exit 1;
fi;
#
if [ ! -d ${BASEDIR} -o ! -w ${BASEDIR} ]; then
   printf "\n${BASEDIR} is not a directory or is not writable.";
   exit 1;
fi;
#
if [ ${DIRLEVEL} -lt 1 -a ${DIRLEVEL} -ge 30 ]; then
   printf "\nThe level ${DIRLEVEL} specified is invalid. Try a value from 1-30.";
   exit 1;
fi;
#
if [ ! -f ${LOADFILE} -o ! -r ${LOADFILE} ]; then
   printf "\nThe load file ${LOADFILE} is invalid or not readable.";
   exit 1;
fi;
}
#
CreateDir()
{
for (( i=1; i<=${DIRLEVEL}; i++ ))
  do
    TESTDIR1=${TESTDIR1}/x${i};
    TESTDIR2=${TESTDIR2}/y${i};
  done;
mkdir -p ${BASEDIR}${TESTDIR1};
ssh ${NODE_NAME} "mkdir -p ${BASEDIR}${TESTDIR2}";
if [ $? -ne 0 ]; then
   printf "\nDirectory creation on node ${NODE_NAME} failed.";
   exit 1;
fi;
}
#
ExtractDir()
{
EXT1DIR1=${BASEDIR}${TESTDIR1};
EXT1DIR2=${BASEDIR}${TESTDIR2};
EXT2DIR1=${BASEDIR};
EXT2DIR2=${BASEDIR};
LEVEL2=`echo ${DIRLEVEL}/2|bc`;
#
for (( i=1; i<=${LEVEL2}; i++ ))
  do
    EXT2DIR1=${EXT2DIR1}/x${i};
    EXT2DIR2=${EXT2DIR2}/y${i};
  done;
}
#
Usage()
{
printf "\n Usage:\n";
printf "\n    cross_delete.sh -n nodename -b basedir [-l dirlevel] [-f tar file].";
printf "\n\n     -n nodename      Node that will participate in the cross delete test.";
printf "\n     -b basedir       Base directory used by both nodes in the test.";
printf "\n     -l dirlevel      Level of subdirectories that will be created in";
printf "\n                      the test (1-30). Defaults to 8.";
printf "\n     -f tar file      Tar file to be used to populate the directories.\n";
exit 1;
}
DIRLEVEL=8
LOADFILE=/home/mmatsuna/TESTS_ARCHIVE/OAST_SERENA/oast3.0_9i_10i_030714.tar.gz
#LOADFILE=/home/mmatsuna/OCFSV2/tbin/tar/linux_src.tar.gz
LOCALDIR=`dirname ${0}`
if [ "${LOCALDIR}" == "." ]; then
   LOCALDIR=`pwd`;
fi;
BASEDIR=`dirname ${LOCALDIR}`

if [ ! -d ${BASEDIR}/logs/`hostname` ]; then
   mkdir -p ${BASEDIR}/logs/`hostname`;
fi;
LOGFILE=${BASEDIR}/logs/`hostname`/cross_delete_$$.log

if [ $# -lt 4 ]; then
   Usage;
fi;
#
while [ $# -gt 0 ]
do
  case ${1} in 
     -n ) NODE_NAME=${2};
          shift 2;
          ;;
     -b ) BASEDIR=${2};
          shift 2;
          ;;
     -l ) DIRLEVEL=${2};
          shift 2;
          ;;
     -f ) LOADFILE=${2};
          shift 2;
          ;;
     -debug ) set -x;
          shift;
          ;;
      * ) Usage;
          #exit
          ;;
  esac;
done;
echo ${NODE_NAME};
echo ${BASEDIR};
echo ${DIRLEVEL};
echo ${LOADFILE};
CreateDir;
ExtractDir;
printf "\n Starting file extractions  on directories :" >> ${LOGFILE};
printf "\n Local - ${EXT1DIR1}"  >> ${LOGFILE};
printf "\n Local - ${EXT2DIR1}"  >> ${LOGFILE};
printf "\n ${NODE_NAME} - ${EXT1DIR2}"  >> ${LOGFILE};
printf "\n ${NODE_NAME} - ${EXT2DIR2}"  >> ${LOGFILE};
ssh -f ${NODE_NAME} "cd ${EXT1DIR2}; /bin/tar xvfz ${LOADFILE} 2>&1 >> /dev/null ";
ssh -f ${NODE_NAME} "cd ${EXT2DIR2}; /bin/tar xvfz ${LOADFILE} 2>&1 >> /dev/null ";
cd ${EXT1DIR1};
/bin/tar xvfz ${LOADFILE} 2>&1 > /dev/null & 
cd ${EXT2DIR1}; 
/bin/tar xvfz ${LOADFILE} 2>&1 > /dev/null &
wait
ssh ${NODE_NAME} "cd ${BASEDIR}; time rm -fr y1 2>&1" >> ${LOGFILE};
cd ${BASEDIR};
time rm -fr x1 2>&1 >> ${LOGFILE};
