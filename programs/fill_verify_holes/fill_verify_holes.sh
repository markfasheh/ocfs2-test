#!/bin/bash
#
# fill_verify_holes.sh
#

APP=`basename ${0}`
PATH=$PATH:`dirname ${0}`:/sbin

AWK=`which awk`
CHOWN=`which chown`
DF=`which df`
ECHO="`which echo` -e"
FUSER=`which fuser`
GREP=`which grep`
SUDO="`which sudo` -u root"
WHOAMI=`which whoami`

DEBUGFS_BIN="${SUDO} `which debugfs.ocfs2`"
MKFS_BIN="${SUDO} `which mkfs.ocfs2`"
TUNEFS_BIN="${SUDO} `which tunefs.ocfs2`"
MOUNT="${SUDO} `which mount`"
UMOUNT="${SUDO} `which umount`"

USERID=`${WHOAMI}`

FILL_HOLES=`which fill_holes 2>/dev/null`
VERIFY_HOLES=`which verify_holes 2>/dev/null`

AIOOPT=

log_run() {
	${ECHO} "Run: $@"
	"$@"
}

do_mount() {
	if [ "$#" -lt "3" ]; then
      		${ECHO} "Error in do_mount()"
		exit 1
	fi

	device=$1
	mountpoint=$2
	mountopts=$3	

	${ECHO} "${MOUNT} -o ${mountopts} ${device} ${mountpoint}"
	${MOUNT} -o ${mountopts} ${device} ${mountpoint} >/dev/null
	if [ $? -ne 0 ]; then
		${ECHO} "ERROR: mount -o ${mountopts} ${device} ${mountpoint}"
		exit 1
	fi
	${SUDO} ${CHOWN} -R ${USERID} ${mountpoint}
}

do_umount() {
	if [ "$#" -lt "1" ]; then
		${ECHO} "Error in do_umount()"
		exit 1
	fi

	mountpoint=$1

	${ECHO} "${UMOUNT} ${mountpoint}"
	${UMOUNT} ${mountpoint}
	if [ $? -ne 0 ]; then
		${ECHO} "ERROR: umount ${mountpoint}"
		exit 1
	fi
}

usage()
{
	${ECHO} "${APP} [ -M ] [ -U ] [ -A ] [ -i iteractions ] [ -s size ] [-o mountopts] -c count -m mountpoint -l logdir -d device"
	exit 1
}

OPTIND=1
COUNT=1
ITER=100000
SIZE=10000000

MMAPOPT=
UNWOPT=

while getopts "c:d:i:l:s:m:o:MUAh?" args
do
	case "$args" in
		c) COUNT="$OPTARG";;
		d) DEVICE="$OPTARG";;
		i) ITER="$OPTARG";;
		l) LOGPATH="$OPTARG";;
		s) SIZE="$OPTARG";;
		m) MOUNTPOINT="$OPTARG";;
		o) MOUNTOPTS="$OPTARG";;
		M) MMAPOPT="-m";;
		U) UNWOPT="-u";;
		A) AIOOPT="-a";;
		h) USAGE="yes";;
		?) USAGE="yes";;
	esac
done

if [ ! -z "${USAGE}" ]; then
	usage
fi

if [ -z ${DEVICE} ] ; then
	${ECHO} "ERROR: No device"
	usage
elif [ ! -b ${DEVICE} ] ; then
	${ECHO} "ERROR: Invalid device ${DEVICE}"
	exit 1
fi

if [ -z ${MOUNTPOINT} ] ; then
	${ECHO} "ERROR: No mountpoint"
	usage
elif [ ! -d ${MOUNTPOINT} ] ; then
	${ECHO} "ERROR: Invalid mountpoint ${MOUNTPOINT}"
	exit 1
fi

if [ -z ${MOUNTOPTS} ] ; then
	MOUNTOPTS="defaults"
fi

if [ -z ${FILL_HOLES} -o -z ${VERIFY_HOLES} ] ; then
	${ECHO} "Error: fill_holes and/or verify_holes not in PATH"
	exit 1
fi

fnamebase="iter${ITER}.size${SIZE}"

do_mount ${DEVICE} ${MOUNTPOINT} ${MOUNTOPTS}

for i in `seq -w 0 ${COUNT}`
do
	outtxt="${MOUNTPOINT}/${fnamebase}.${i}.txt"
      	outlog="${LOGPATH}/${fnamebase}.${i}.log"

    	${ECHO} "Creating file..."
   	log_run "${FILL_HOLES}" ${MMAPOPT} ${UNWOPT} ${AIOOPT} -f -o "${outlog}" -i "${ITER}" "${outtxt}" "${SIZE}"
	if [ $? -ne 0 ]; then
		do_umount ${MOUNTPOINT}
		exit 1
	fi

    	sleep 5

    	${FUSER} -km ${MOUNTPOINT}

    	sleep 5

	do_umount ${MOUNTPOINT}

	do_mount ${DEVICE} ${MOUNTPOINT} ${MOUNTOPTS}

    	${ECHO} "Verifying..."
    	log_run "${VERIFY_HOLES}" "-v" "${outlog}" "${outtxt}"
    	if [ $? -ne 0 ]; then
		do_umount ${MOUNTPOINT}
		exit 1
    	fi
done

do_umount ${MOUNTPOINT}
