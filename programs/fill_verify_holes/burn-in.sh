#!/bin/bash

BINPATH="."
LOGPATH="."
MMAPOPT=

log_run() {
    echo "Run: $@"
    "$@"
}

run_fill() {
    iter="$1";
    filename="$2";
    size="$3";
    logfile="$4"

    echo "Creating file..."
    log_run "${BINPATH}/fill_holes" ${MMAPOPT} -f -o "${logfile}" -i "${iter}" \
	    "${filename}" "${size}"
    sleep 10
    sudo /sbin/fuser -km ${MOUNTPOINT}
    sleep 10
    echo "umounting partition"
    sudo umount ${MOUNTPOINT}
    sleep 10
    echo "mounting partition"
    sudo mount LABEL=${LABEL} ${MOUNTPOINT}
    sleep 10
    echo "Verifying..."
    log_run "${BINPATH}/verify_holes" "-v" "${logfile}" "${filename}"
    

    RET=$?
    if [ $RET -ne 0 ]; then
	exit 1;
    fi;
}

run100() {
    iter="$1"
    size="$2"

    fnamebase="iter${iter}.size${size}"

    for i in `seq -w 0 ${COUNT}`
    do
      f="${DIRECTORY}/${fnamebase}.${i}.txt"
      l="${LOGPATH}/${fnamebase}.${i}.log"
      run_fill "${iter}" "${f}" "${size}" "${l}"
    done
}

#
# GetDevInfo
#
GetDevInfo()
{
line=`${DF} -h ${DIRECTORY} | ${GREP} -v Filesystem`
DEVICE=`echo ${line} | ${AWK} -F" " '{print $1}'`
MOUNTPOINT=`echo ${line} | ${AWK} -F" " '{print $6}'`
if [ ${MOUNTPOINT} == "/" -o  ${MOUNTPOINT} == "" ]; then
	${ECHO} "Specified partition is not mounted.\n"
	${ECHO} "Aborting....\n"
	exit 1;
fi;
CLUSTERSIZE_BITS=`${DEBUGFS_BIN} -R stats ${DEVICE} | grep Bits|\
	${AWK} -F" " '{print $8}'`;
BLOCKSIZE_BITS=`${DEBUGFS_BIN} -R stats ${DEVICE} | grep Bits|\
	${AWK} -F" " '{print $4}'`;
LABEL=`${DEBUGFS_BIN} -R stats ${DEVICE} | grep Label:|\
	${AWK} -F" " '{print $2}'`;
SLOTS=`${DEBUGFS_BIN} -R stats ${DEVICE} | grep Slots:|\
	${AWK} -F" " '{print $4}'`;
CLUSTERSIZE=`echo 2^${CLUSTERSIZE_BITS} |bc`;
BLOCKSIZE=`echo 2^${BLOCKSIZE_BITS} |bc`;
#
}

USAGE=""
OPTIND=1
COUNT=10
SIZE=1000000
SUDO="/usr/bin/sudo -u root"
DEBUGFS_BIN="/usr/bin/sudo -u root /sbin/debugfs.ocfs2"
MKFS_BIN="/usr/bin/sudo -u root /sbin/mkfs.ocfs2"
GREP="/bin/grep"
DF="/bin/df"
ECHO="/bin/echo -e"
AWK="/bin/awk"
#
while getopts "c:d:i:mb:l:s:h?" args
do
  case "$args" in
    c) COUNT="$OPTARG";;
    d) DIRECTORY="$OPTARG";;
    i) ITER="$OPTARG";;
    m) MMAPOPT="-m";;
    b) BINPATH="$OPTARG";;
    l) LOGPATH="$OPTARG";;
    s) SIZE="$OPTARG";;
    h) USAGE="yes";;
    ?) USAGE="yes";;
  esac
done

if [ -n "${USAGE}" ]; then
    echo "usage: burn-in.sh [ -b path-to-binaries ] [ -l path-for-logfiles ] \
    	[ -c count ] [ -d  directory ] [ -i iteractions ] [ -s size ]";
    exit 0;
fi

GetDevInfo;
run100 ${ITER} ${SIZE} 
