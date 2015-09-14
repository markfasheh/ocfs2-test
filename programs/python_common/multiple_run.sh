#!/bin/bash
# vi: set ts=8 sw=8 autoindent noexpandtab :
################################################################################
#
# File :        multiple_run.sh
#
# Description:  The wrapper script help to organize all multi-nodes testcase
#		to perform a thorough test among multiple nodes.
#       
#
# Author:       Tristan Ye,     tristan.ye@oracle.com
#
# History:      22 Sep 2008
#

################################################################################
# Global Variables
################################################################################
if [ -f `dirname ${0}`/o2tf.sh ]; then
	. `dirname ${0}`/o2tf.sh
fi

PATH=$PATH:/sbin      # Add /sbin to the path for ocfs2 tools
export PATH=$PATH:.

USERNAME=`id -un`
GROUPNAME=`id -gn`
DATE=`/bin/date +%F-%H-%M`
DF=`which df`
DD=`which dd`
GREP=`which grep`
AWK=`which awk`
ECHO="`which echo` -e"

SUDO="`which sudo` -u root"
IFCONFIG_BIN="`which ifconfig`"

REMOTE_MOUNT_BIN="${BINDIR}/remote_mount.py"
REMOTE_UMOUNT_BIN="${BINDIR}/remote_umount.py"

NODE_LIST=
DEVICE=
MOUNT_POINT=
KERNELSRC=

ACCESS_METHOD="ssh"
ACCESS_METHOD_ARG=
INTERFACE="eth0"
INTERFACE_ARG=
SLOTS=4

DEFAULT_LOG_DIR=${O2TDIR}/log
LOG_DIR=
LOGFILE=
RUN_LOGFILE=

CLUSTERSIZE=
BLOCKSIZE=
LABELNAME=
FEATURES=
JOURNALSIZE=0
BLOCKS=0
MOUNT_OPTS=

set -o pipefail

################################################################################
# Utility Functions
################################################################################
f_usage()
{
    echo "usage: `basename ${0}` <-k kerneltarball> <-n nodes> [-i nic] \
[-a access_method] [-o logdir] <-d device> <mountpoint path>"
    echo "       -k kerneltarball should be path of tarball for kernel src."
    echo "       -n nodelist,should be comma separated."
    echo "       -o output directory for the logs"
    echo "       -i network interface name to be used for MPI messaging."
    echo "       -a access method for mpi execution,should be ssh or rsh"
    echo "       -d device name used for ocfs2 volume."
    echo "       <mountpoint path> path of mountpoint where test will be performed."
    echo 
    echo "Eaxamples:"
    echo "	 `basename ${0}` -k /kernel/linux-2.6.tgz -n \
node1.us.oracle.com,node2.us.oracle.com -d /dev/sdd1 /storage"
    exit 1;

}

f_getoptions()
{
	if [ $# -eq 0 ]; then
		f_usage;
		exit 1
	fi

	while getopts "n:d:i:a:o:k:h:" options; do
		case $options in
		n ) NODE_LIST="$OPTARG";;
		d ) DEVICE="$OPTARG";;
		i ) INTERFACE="$OPTARG";;
		a ) ACCESS_METHOD="$OPTARG";;
		o ) LOG_DIR="$OPTARG";;
		k ) KERNELSRC="$OPTARG";;
		h ) f_usage
		    exit 1;;
                * ) f_usage
		    exit 1;;
		esac
	done
	shift $(($OPTIND -1))
	MOUNT_POINT=${1}
}

f_setup()
{
	if [ "${UID}" = "0" ];then
		echo "Should not run tests as root"
		exit 1
	fi

	f_getoptions $*

	if [ -z "${MOUNT_POINT}" ];then
                f_usage
        fi

	if [ -z ${KERNELSRC} ]; then
		echo "No kernel source"
		f_usage
	fi

	if [ ! -f ${KERNELSRC} ]; then
        	${ECHO} "Not a kernel source"
		f_usage
	fi

	if [ ! -d ${MOUNT_POINT} ]; then
        	${ECHO} "Mount point ${MOUNT_POINT} does not exist"
		exit 1
	fi

	if [ -n "${ACCESS_METHOD}" ];then
		if [ "$ACCESS_METHOD" != "rsh" -a "$ACCESS_METHOD" != "ssh" ];then
			echo "access method should be 'rsh' or 'ssh'"
			f_usage
		fi
	fi

	ACCESS_METHOD_ARG=" -a ${ACCESS_METHOD} "

	rpm -q --quiet openmpi ||{
		which mpirun &>/dev/null
		RET=$?
		if [ "$RET" != "0" ];then
			${ECHO} "Need to install openmpi first"
			exit 1
		fi
	}

	LOGFILE=${O2TDIR}/log/multiple_run_${DATE}.log

	if [ -z "$NODE_LIST" ];then
		f_usage
	else
		echo $NODE_LIST|sed -e 's/,/\n/g' >/tmp/$$
		SLOTS=`cat /tmp/$$ |wc -l`
		rm -f /tmp/$$
	fi

	${DF} -h|${GREP} -q ${DEVICE}
        if [ "$?" == "0" ];then
                ${ECHO} "Partition has been mounted,should umount first to perform test" \
                        |tee -a ${LOGFILE};
		exit 1
        fi

	if [ ! -z "${INTERFACE}" ]; then
		${IFCONFIG_BIN} ${INTERFACE} >/dev/null 2>&1 || {
			echo "Invalid NIC";
			f_usage;
		}

		INTERFACE_ARG=" -i ${INTERFACE} "
	fi

        LOG_DIR=${LOG_DIR:-$DEFAULT_LOG_DIR}
        ${MKDIR_BIN} -p ${LOG_DIR} || exit 1

        LOGFILE="`dirname ${LOG_DIR}`/`basename ${LOG_DIR}`/multiple-\
`uname -m`-`date +%F-%H-%M-%S`.log"

        RUN_LOGFILE="`dirname ${LOG_DIR}`/`basename ${LOG_DIR}`/multiple-run-\
`uname -m`-`date +%F-%H-%M-%S`.log"
}

LogRC()
{
	if [ ${1} -ne 0 ]; then
        	${ECHO} "Failed." | ${TEE_BIN} -a ${RUN_LOGFILE}
	else
        	${ECHO} "Passed." | ${TEE_BIN} -a ${RUN_LOGFILE}
	fi
	END=$(date +%s)
	DIFF=$(( ${END} - ${START} ))
	${ECHO} "Runtime ${DIFF} seconds.\n" >> ${LOGFILE}
	${ECHO} "Runtime ${DIFF} seconds.\n" | ${TEE_BIN} -a ${RUN_LOGFILE}
}

LogRunMsg()
{
	${ECHO} "`date`" >> ${LOGFILE}
	${ECHO} "`date`" | ${TEE_BIN} -a ${RUN_LOGFILE}
	${ECHO} ${1} >> ${LOGFILE}
	${ECHO} "${1}\c" | ${TEE_BIN} -a ${RUN_LOGFILE}
	i=${#1}
	while (( i < 60 )) ;do
		${ECHO} ".\c" | ${TEE_BIN} -a ${RUN_LOGFILE}
		(( ++i ))
	done
}

LogMsg()
{
	${ECHO} "$(date +%Y/%m/%d,%H:%M:%S)  $@" >> ${LOGFILE}
}

#
# Common template to run the majority of all testcases:
# Arguments are described as follows:
# $1	Name for testcase
# $2	FS features
# $3	Testing CMD
# Example:
#	run_common_testcase "open_delete" "sparse,unwritten,inline-data" \
#	"open_delete.py -f /storage/open_delete_test -i 10000 -l /tmp/logfile \
#	-n node1.us.oracle.com,node2.us.oracle.com"
run_common_testcase()
{
	local testname=$1
	local features=$2
	shift 2
	local cmd=$@

	LogRunMsg "${testname}"

	local logdir=${LOG_DIR}/${testname}
        local logfile=${logdir}/${testname}_${DATE}.log
	local workplace=${MOUNT_POINT}/${testname}_test

	${MKDIR_BIN} -p ${logdir}
	${CHMOD_BIN} -R 777 ${logdir}
        ${CHOWN_BIN} -R ${USERNAME}:${GROUPNAME} ${logdir}
	${TOUCH_BIN} ${logfile}

        LABELNAME="multi-${testname}-test"
	FEATURES="${features}"

	LogMsg "Mkfs device ${DEVICE}:"
	f_mkfs ${LOGFILE} ${BLOCKSIZE} ${CLUSTERSIZE} ${LABELNAME} ${SLOTS} \
${DEVICE} ${FEATURES} ${JOURNALSIZE} ${BLOCKS}
        RET=$?
        f_exit_or_not ${RET}

	LogMsg "Mount volume from all nodes"
	f_remote_mount ${LOGFILE} ${LABELNAME} ${MOUNT_POINT} ${NODE_LIST} ${MOUNT_OPTS}
	RET=$?
	f_exit_or_not ${RET}

	${CHMOD_BIN} -R 777 ${MOUNT_POINT}
	${CHOWN_BIN} -R ${USERNAME}:${GROUPNAME} ${MOUNT_POINT}
	${MKDIR_BIN} -p ${workplace}

	LogMsg "Run ${testname}, CMD: $cmd"
	eval "${cmd}" >> ${LOGFILE} 2>&1
	LogRC $?

	LogMsg "Umount volume from all nodes."
	f_remote_umount ${LOGFILE} ${MOUNT_POINT} ${NODE_LIST}
	RET=$?
	f_exit_or_not ${RET}
}

run_open_delete_test()
{
	local workplace=${MOUNT_POINT}/open_delete_test
	local testfile=${workplace}/open_delete_test_file
	local logdir=${LOG_DIR}/open_delete
	local logfile=${logdir}/open_delete_${DATE}.log

	run_common_testcase "open_delete" "sparse,unwritten,inline-data" \
"${BINDIR}/open_delete.py -f ${testfile} -i 10000 -I ${INTERFACE} -l ${logfile} -n ${NODE_LIST}"

}

run_cross_delete_test()
{
	local logdir=${LOG_DIR}/cross_delete
	local logfile=${logdir}/cross_delete_${DATE}.log
	local workplace=${MOUNT_POINT}/cross_delete_test

	run_common_testcase "cross_delete" "sparse,unwritten,inline-data" \
"${BINDIR}/cross_delete.py -c 1 -i ${INTERFACE} -d ${workplace} -n ${NODE_LIST} -t ${KERNELSRC}"
}

run_write_append_truncate_test()
{
	local logdir=${LOG_DIR}/write_append_truncate
	local logfile=${logdir}/write_append_truncate_${DATE}.log
	local workplace=${MOUNT_POINT}/write_append_truncate_test
	local testfile=${workplace}/write_append_truncate_test_file

	run_common_testcase "write_append_truncate" "sparse,unwritten,inline-data" \
"${BINDIR}/run_write_append_truncate.py -i 20000 -I ${INTERFACE} -l ${logfile} -n ${NODE_LIST} -f ${testfile}"
}

run_multi_mmap_test()
{
	local logdir=${LOG_DIR}/multi_mmap
	local logfile=${logdir}/multi_mmap_${DATE}.log
	local workplace=${MOUNT_POINT}/multi_mmap_test
	local testfile=${workplace}/multi_mmap_test_file

	run_common_testcase "multi_mmap" "sparse,unwritten,inline-data" \
"${BINDIR}/run_multi_mmap.py -i 20000 -I ${INTERFACE} -n ${NODE_LIST} -c -b 6000 --hole -f ${testfile}"
}

run_create_racer_test()
{
	local logdir=${LOG_DIR}/create_racer
	local logfile=${logdir}/create_racer_${DATE}.log
	local workplace=${MOUNT_POINT}/create_racer_test

	run_common_testcase "create_racer" "sparse,unwritten,inline-data" \
"${BINDIR}/run_create_racer.py -c 40000 -i ${INTERFACE} -l ${logfile} -n ${NODE_LIST} -p ${workplace}"
}

run_xattr_test()
{
	local logdir=${LOG_DIR}/multi-xattr-test

	LogRunMsg "xattr-test"
	${BINDIR}/xattr-multi-run.sh -r 4 -f ${NODE_LIST} -a rsh -o ${logdir} \
-d ${DEVICE} ${MOUNT_POINT} >> ${LOGFILE} 2>&1
	LogRC $?
}

run_inline_test()
{
	local logdir=${LOG_DIR}/multi-inline-test

	LogRunMsg "inline-test"
	${BINDIR}/multi-inline-run.sh -r 4 -f ${NODE_LIST} -a rsh -o ${logdir} \
-d ${DEVICE} ${MOUNT_POINT} >> ${LOGFILE} 2>&1
	LogRC $?
}

run_reflink_test()
{
	local logdir=${LOG_DIR}/multi-reflink-test

	LogRunMsg "reflink-test"
	LogMsg "reflink 'data=ordered' mode test"
	${BINDIR}/multi_reflink_test_run.sh -r 4 -f ${NODE_LIST} -a rsh -o \
${logdir} -d ${DEVICE} ${MOUNT_POINT} >> ${LOGFILE} 2>&1 || {
	RET=$?
	LogRC $RET
	return $RET
}
	LogMsg "reflink 'data=writeback' mode test"
	${BINDIR}/multi_reflink_test_run.sh -r 4 -f ${NODE_LIST} -a rsh -o \
${logdir} -W -d ${DEVICE} ${MOUNT_POINT} >> ${LOGFILE} 2>&1
	LogRC $?
}

run_lvb_torture_test()
{
	LogRunMsg "lvb_torture"

	local logdir=${LOG_DIR}/lvb_torture
        local logfile=${logdir}/lvb_torture_${DATE}.log
        local workplace=${MOUNT_POINT}
	local testfile=${workplace}/lvb_torture_test_file

	${MKDIR_BIN} -p ${logdir}
	${CHMOD_BIN} -R 777 ${logdir}
	${CHOWN_BIN} -R ${USERNAME}:${GROUPNAME} ${logdir}
	${TOUCH_BIN} ${logfile}

	LABELNAME="multi-lvb-torture-test"
	FEATURES="sparse,unwritten,inline-data"

	LogMsg "Mkfs device ${DEVICE}:"
	f_mkfs ${LOGFILE} ${BLOCKSIZE} ${CLUSTERSIZE} ${LABELNAME} ${SLOTS} \
${DEVICE} ${FEATURES} ${JOURNALSIZE} ${BLOCKS}
        RET=$?
        f_exit_or_not ${RET}

	LogMsg "Mount volume from all nodes"
	f_remote_mount ${LOGFILE} ${LABELNAME} ${MOUNT_POINT} ${NODE_LIST} ${MOUNT_OPTS}
	RET=$?
	f_exit_or_not ${RET}

	${CHMOD_BIN} -R 777 ${MOUNT_POINT}
	${CHOWN_BIN} -R ${USERNAME}:${GROUPNAME} ${MOUNT_POINT}

	#dd 2G file for testing.
	LogMsg "dd 2G testfile under mount point."
	${DD} if=/dev/zero of=${testfile} bs=1024 count=2097152 >> ${LOGFILE} 2>&1
	local UUID="`${DEBUGFS_BIN} -R stats ${DEVICE} |grep UUID|cut -d: -f 2`"
	local LOCK="`${DEBUGFS_BIN} -R 'encode lvb_torture_test_file' ${DEVICE}`"

	LogMsg "Run lvb_torture, CMD: ${BINDIR}/run_lvb_torture.py -d /dlm/ -i 60000 \
-H ${DEVICE} -l ${logfile} -n ${NODE_LIST} "${UUID}" "${LOCK}""
	${BINDIR}/run_lvb_torture.py -d /dlm/ -c 60000 -i ${INTERFACE} -H ${DEVICE} -l \
${logfile} -n ${NODE_LIST} "${UUID}" "${LOCK}" >> ${LOGFILE} 2>&1
	LogRC $?

	LogMsg "Umount volume from all nodes."
	f_remote_umount ${LOGFILE} ${MOUNT_POINT} ${NODE_LIST}
	RET=$?
	f_exit_or_not ${RET}
}

run_flock_unit_test()
{
	local logdir=${LOG_DIR}/flock_unit
	local logfile=${logdir}/flock_unit_${DATE}.log

	local flock_logfile=${logdir}/flock_unit_test_${DATE}.log
	local fcntl_logfile=${logdir}/fcntl_unit_test_${DATE}.log

	local workplace=${MOUNT_POINT}/flock_unit_test
	local testfile1=${workplace}/flock_unit_test_file1
	local testfile2=${workplace}/flock_unit_test_file2

	run_common_testcase "flock_unit" "sparse,unwritten,inline-data" \
"${TOUCH_BIN} ${testfile1} && ${TOUCH_BIN} ${testfile2} && ${BINDIR}/run_flock_unit_test.py \
-l ${fcntl_logfile} -n ${NODE_LIST} -t fcntl -e ${testfile1} -f ${testfile2} \
&& ${BINDIR}/run_flock_unit_test.py -l ${flock_logfile} -i ${INTERFACE} -n ${NODE_LIST} -t \
flock -e ${testfile1} -f ${testfile2}"
}

f_cleanup()
{
	:
}
################################################################################
# Main Entry
################################################################################

trap 'echo -ne "\n\n">>${LOGFILE};echo  "Interrupted by Ctrl+C,Cleanuping... "|tee -a ${LOGFILE}; f_cleanup;exit 1' SIGINT
trap ' : ' SIGTERM

f_setup $*

STARTRUN=$(date +%s)
${ECHO} "`date` - Starting Multiple Nodes Regress test" > ${LOGFILE}

START=$(date +%s)
run_xattr_test 

START=$(date +%s)
run_inline_test

START=$(date +%s)
run_reflink_test

for BLOCKSIZE in 512 1024 4096;do
	for CLUSTERSIZE in 4096 32768 1048576;do
		${ECHO} "Tests with \"-b ${BLOCKSIZE} -C ${CLUSTERSIZE}\"" | \
${TEE_BIN} -a ${LOGFILE}
		START=$(date +%s)
		run_write_append_truncate_test

		START=$(date +%s)
		run_multi_mmap_test

		START=$(date +%s)
		run_create_racer_test

		START=$(date +%s)
		run_flock_unit_test

		START=$(date +%s)
		run_cross_delete_test

		START=$(date +%s)
		run_open_delete_test

		START=$(date +%s)
		run_lvb_torture_test
	done
done

END=$(date +%s)
DIFF=$(( ${END} - ${STARTRUN} ));
${ECHO} "Total Runtime ${DIFF} seconds.\n" >> ${LOGFILE}
${ECHO} "`date` - Ended Multiple Nodes Regress test" >> ${LOGFILE}
