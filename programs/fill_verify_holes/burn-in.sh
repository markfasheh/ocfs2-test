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
MOUNTED_BIN="`which sudo` -u root `which mounted.ocfs2`"
GREP=`which grep`
CHOWN=`which chown`
DF=`which df`
WC=`which wc`
ECHO="`which echo` -e"
AWK=`which awk`

USERID=`/usr/bin/whoami`
BINPATH="."
LOGPATH="."
MMAPOPT=
AIOOPT=
if [ -f `dirname ${0}`/config.sh ]; then
	. `dirname ${0}`/config.sh
fi;
NWOPT=

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
    log_run "${BINPATH}/fill_holes" ${MMAPOPT} ${UNWOPT} ${AIOOPT} -f -o "${logfile}" -i "${iter}" \
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

Run_corruption() {
	CORRUPTLOG=${O2TDIR}/workfiles/fill_holes_data/nosparsebug.dat
	FILESIZE=$[11*1024*1024*1024];
	FILENAME=${DIRECTORY}/fill_holes.txt;
	FS_FEATURES='--fs-features=nosparse,nounwritten'
	LOCAL_FEATURE=''
	if [ ${LOCALFLAG} -eq 0 ]; then
		LOCAL_FEATURE='-M local';
	fi;
	echo -e "Testing corruption";
	mounted=`mount|grep ${MOUNTPOINT}|${WC} -l`;
	if [ ${mounted} -eq 1 ]; then
		echo -e "umounting partition";
		sudo umount ${MOUNTPOINT};
	fi;
	mounted=`mount|grep ${MOUNTPOINT}|${WC} -l`;
	if [ ${mounted} -ne 0 ]; then
		echo -e "Device ${DEVICE} is mounted by some other node.";
		echo -e "Can't proceed with this test. Aborting.";
		exit 1;
	fi;
	echo "Formatting partition with nosparse,nounwritten options.";
	echo "y"|${MKFS_BIN} -b ${BLOCKSIZE} -C ${CLUSTERSIZE} -L ${LABEL} \
	-N ${SLOTS} ${LOCAL_FEATURE} ${FS_FEATURES} ${DEVICE};
	if [ $? -ne 0 ]; then
		echo -e "mkfs.ocfs2 failed $?";
		exit 1;
	fi;
	echo "mounting device with label ${LABEL} on ${MOUNTPOINT}";
	sudo mount LABEL=${LABEL} ${MOUNTPOINT};
	sudo mkdir -p ${DIRECTORY}
	${SUDO} ${CHOWN} --recursive ${USERID} ${MOUNTPOINT}

	mounted=`mount|grep ${MOUNTPOINT}|${WC} -l`;
	if [ ${mounted} -eq 0 ]; then
		echo -e "Device ${DEVICE} was not properly mounted.";
		echo -e "Can't proceed with this test. Aborting.";
		exit 1;
	fi;
	${BINDIR}/fill_holes  -r ${CORRUPTLOG} ${FILENAME} ${FILESIZE};
	${BINDIR}/verify_holes ${CORRUPTLOG} ${FILENAME};
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
${DEBUGFS_BIN} -R "stats" ${DEVICE}|grep "Feature Incompat"|grep -q 'local'
LOCALFLAG=$? 
UUID=`${DEBUGFS_BIN} -R stats ${DEVICE} | grep UUID|\
	${AWK} -F" " '{print $2}'`;
SLOTS=`${DEBUGFS_BIN} -R stats ${DEVICE} | grep Slots:|\
	${AWK} -F" " '{print $4}'`;
CLUSTERSIZE=`echo 2^${CLUSTERSIZE_BITS} |bc`;
BLOCKSIZE=`echo 2^${BLOCKSIZE_BITS} |bc`;
#
}
#
while getopts "c:d:i:b:l:s:muah?" args
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
    a) AIOOPT="-a";;
    h) USAGE="yes";;
    ?) USAGE="yes";;
  esac
done

if [ -n "${USAGE}" ]; then
    echo "usage: burn-in.sh [ -m ] [ -u ] [ -a ] [ -b path-to-binaries ]  \
    	[ -l path-for-logfiles ] [ -c count ] [ -d  directory ]  \
	[ -i iteractions ] [ -s size ]";
    exit 0;
fi

GetDevInfo;
run100 ${ITER} ${SIZE} 
Run_corruption
