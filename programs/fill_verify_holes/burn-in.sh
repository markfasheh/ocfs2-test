#!/bin/bash
PATH=$PATH:/sbin	# Added sbin to the path for ocfs2-tools
USAGE=""
OPTIND=1
COUNT=10
ITER=1000
SIZE=1000000
SUDO="`which sudo` -u root"
DEBUGFS_BIN="`which sudo` -u root `which debugfs.ocfs2`"
MKFS_BIN="`which sudo` -u root `which mkfs.ocfs2`"
TUNEFS_BIN="`which sudo` -u root `which tunefs.ocfs2`"
GREP=`which grep`
DF=`which df`
ECHO="`which echo` -e"
AWK=`which awk`

BINPATH="."
LOGPATH="."
MMAPOPT=
UNWOPT=

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
    log_run "${BINPATH}/fill_holes" ${MMAPOPT} ${UNWOPT} -f -o "${logfile}" -i "${iter}" \
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
if [ "X${LABEL}" == "X" ]; then
	LABEL="testlabel";
fi;
UUID=`${DEBUGFS_BIN} -R stats ${DEVICE} | grep UUID|\
	${AWK} -F" " '{print $2}'`;
SLOTS=`${DEBUGFS_BIN} -R stats ${DEVICE} | grep Slots:|\
	${AWK} -F" " '{print $4}'`;
CLUSTERSIZE=`echo 2^${CLUSTERSIZE_BITS} |bc`;
BLOCKSIZE=`echo 2^${BLOCKSIZE_BITS} |bc`;
#
}
#
while getopts "c:d:i:b:l:s:muh?" args
do
  case "$args" in
    c) COUNT="$OPTARG";;
    d) DIRECTORY="$OPTARG";;
    i) ITER="$OPTARG";;
    b) BINPATH="$OPTARG";;
    l) LOGPATH="$OPTARG";;
    s) SIZE="$OPTARG";;
    m) MMAPOPT="-m";;
    u) UNWOPT="-u";;
    h) USAGE="yes";;
    ?) USAGE="yes";;
  esac
done

if [ -n "${USAGE}" ]; then
    echo "usage: burn-in.sh [ -m ] [ -u ] [ -b path-to-binaries ]  \
    	[ -l path-for-logfiles ] [ -c count ] [ -d  directory ]  \
	[ -i iteractions ] [ -s size ]";
    exit 0;
fi

GetDevInfo;
run100 ${ITER} ${SIZE} 
