#!/bin/sh
#
# crfiles.sh <sleep> <size in 256kb> <count> <filename>
#
Usage()
{
echo -e "$0:"
echo -e "     crfiles.sh -b <blocksize in bytes> -i <interval> -s <size in 256kb> -c <count> ";
echo -e "                -d <directory> -f <filename> -o <logfile>";
echo -e "\n     -b <blocksize> If not specified, defaults to 256k";
echo -e "     -i <interval>  If not specified, defaults to 5 Seconds.";
echo -e "     -s <Size>      Number of blocks. IF not specified, defaults to 4 (1Mb file size).";
echo -e "     -c <count>     Number of files to be created. Defaults to 10.";
echo -e "     -f <filename>  Filename prefix to be used while creating the tests files.";
echo -e "     -o <logfile>   logfile name for the run.";
echo -e "     -d <directory> Directory where the test files will be created.";
exit 1;
}
LOCALDIR=`dirname ${0}`
if [ "${LOCALDIR}" == "." ]; then
   LOCALDIR=`pwd`;
fi;
BASEDIR=`dirname ${LOCALDIR}`
if [ ! -d ${BASEDIR}/logs/`hostname` ]; then
   mkdir -p ${BASEDIR}/logs/`hostname`;
fi;
LOGFILE=${BASEDIR}/logs/`hostname`/crfiles_$$.log
HOST=`hostname`
INTERVAL=5;
BLOCKSIZE=262144;
SIZE=4;
COUNT=10;
FILENAME=test
DIRECTORY=0;
ARCH=`uname -m`;
if [ $# -eq 0 ]; then
   Usage;
fi;
#
while [ $# -gt 0 ]
do
  case ${1} in
     -b ) BLOCKSIZE=${2};
          shift 2;
          ;;
     -c ) COUNT=${2};
          shift 2;
          ;;
     -d ) DIRECTORY=${2};
          shift 2;
          ;;
     -i ) INTERVAL=${2};
          shift 2;
          ;;
     -f ) FILENAME=${2};
          shift 2;
          ;;
     -h   ) Usage;
          ;;
     -s ) SIZE=${2};
          shift 2;
          ;;
     *    ) Usage;
          ;;
  esac;
done;
if [ "X${DIRECTORY}" == "X" ]; then
   echo -e "\n\nDirectory must be specified.\n\n".
   Usage;
fi;
#
if [ ! -f ${LOGFILE} ]; then
   echo -e "Creating logfile ${LOGFILE}";
else
   echo -e "Appending logfile ${LOGFILE}";
fi;
#
if [ -d ${DIRECTORY} ]; then
   echo -e "Directory already exists.";
else
   if [ -e ${DIRECTORY} ]; then
      echo -e "\nInformation on directory (${DIRECTORY}) is invalid. A file with same name exists.";
      exit 1;
   else
      mkdir -p ${DIRECTORY};
   fi;
fi;
#
for (( i=0; i<${COUNT}; i++))
do
   echo -e "Creating file ${DIRECTORY}/${FILENAME}_${i}" | tee -a ${LOGFILE}
#   if [ `uname -m` == "ia64" ]; then
      ${BASEDIR}/bin/extendo.${ARCH} ${DIRECTORY}/${FILENAME}_${i} 262 100 ${SIZE};
#   else
#      dd if=/dev/zero of=${DIRECTORY}/${FILENAME}_${i} bs=${BLOCKSIZE} count=${SIZE} 2>&1 >> ${LOGFILE}
#      dd if=/dev/zero of=${DIRECTORY}/${FILENAME}_${i} bs=${BLOCKSIZE} count=${SIZE} o_direct=yes 2>&1 >> ${LOGFILE}
#   fi;
   sleep ${INTERVAL};
done;
