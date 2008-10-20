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
DEBUGFS_BIN="`which sudo` -u root `which debugfs.ocfs2`"
TUNEFS_BIN="`which sudo` -u root `which tunefs.ocfs2`"
MKFS_BIN="`which sudo` -u root `which mkfs.ocfs2`"

REMOTE_MOUNT_BIN="${BINDIR}/remote_mount.py"
REMOTE_UMOUNT_BIN="${BINDIR}/remote_umount.py"

NODE_LIST=
DEVICE_NAME=
MOUNT_POINT=

################################################################################
# Utility Functions
################################################################################
f_usage()
{
    echo "usage: `basename ${0}` <-n nodes> <-d device> <mountpoint path>"
    echo "       -n nodelist,should be comma separated."
    echo "       -d device name used for ocfs2 volume."
    echo "       <mountpoint path> path of mountpoint where test will be performed."
    echo 
    echo "Eaxamples:"
    echo "	 `basename ${0}` -n node1.us.oracle.com,node2.us.oracle.com -d /dev/sdd1 /storage"
    exit 1;

}

f_getoptions()
{
         if [ $# -eq 0 ]; then
                f_usage;
                exit 1
         fi

         while getopts "n:d:h:" options; do
                case $options in
                n ) NODE_LIST="$OPTARG";;
                d ) DEVICE_NAME="$OPTARG";;
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
	f_getoptions $*

	if [ -z "${MOUNT_POINT}" ];then
                f_usage
        fi

	rpm -q --quiet openmpi ||{
		which mpirun &>/dev/null
		RET=$?
		if [ "$RET" != "0" ];then
			${ECHO} "Need to install openmpi first"|tee -a ${LOGFILE}
			exit 1
		fi
	}

	. ./config.sh

	LOGFILE=${O2TDIR}/log/multiple_run_${DATE}.log

	if [ ! -d ${MOUNT_POINT} ]; then
        	${ECHO} "Mount point ${MOUNT_POINT} does not exist" \
                	|tee -a ${LOGFILE};
		exit 1
	fi

	${DF} -h|${GREP} -q ${DEVICE_NAME}
        if [ "$?" == "0" ];then
                ${ECHO} "Partition has been mounted,should umount first to perform test" \
                        |tee -a ${LOGFILE};
		exit 1
        fi
}

LogRC()
{
	if [ ${1} -ne 0 ]; then
        	${ECHO} "Failed." >> ${LOGFILE}
	else
        	${ECHO} "Passed." >> ${LOGFILE}
	fi
	END=$(date +%s)
	DIFF=$(( ${END} - ${START} ))
	${ECHO} "Runtime ${DIFF} seconds.\n" >> ${LOGFILE}
}

LogMsg()
{
	${ECHO} `date` >> ${LOGFILE}
	${ECHO} "${1}\c" >> ${LOGFILE}
	i=${#1}
	while (( i < 60 )) ;do
	        ${ECHO} ".\c" >> ${LOGFILE}
        	(( ++i ))
	done
}

IsDeviceMounted()
{
	local OCFS2_DEVICE=${1}
	${DF} -h | ${GREP} -q ${OCFS2_DEVICE}
	
	if [ "$?" == "0" ]; then
		return 0
	else
		return 1
	fi
}

run_xattr_test()
{
	LogMsg "xattr-test"
	${BINDIR}/xattr-multi-run.sh -r 8 -f ${NODE_LIST} -a rsh -o ${O2TDIR}/log/xattr-tests-log -d ${DEVICE_NAME} ${MOUNT_POINT}
	LogRC $?
}

run_inline_test()
{
	LogMsg "inline-test"
	${BINDIR}/multi-inline-run.sh -r 4 -f ${NODE_LIST} -a rsh -o ${O2TDIR}/log/inline-tests-log -d ${DEVICE_NAME} ${MOUNT_POINT}
	LogRC $?
	
}

run_write_append_truncate_test()
{
	LogMsg "write-append-truncate-test"

	local logdir=${O2TDIR}/log/write_append_truncate_log
	local logfile=${logdir}/write_append_truncate_${DATE}.log

	local testfile=${MOUNT_POINT}/write_append_truncate_test_file

	#${SUDO} mkdir -p ${logdir}
	
	mkdir -p ${logdir}
	chmod 777 ${logdir}
	touch ${logfile}
	chmod 777 ${logfile}

	#force to umount volume from all nodes
	${ECHO} "Try to umount volume from all nodes before test."|tee -a ${logfile}
	${REMOTE_UMOUNT_BIN} -m ${MOUNT_POINT} -n ${NODE_LIST}>>${logfile} 2>&1
	
	CLUSTERSIZE=32k
	BLOCKSIZE=4k
	SLOTS=4
	LABEL=ocfs2test

	${ECHO} "Format volume to launch new test"|tee -a ${logfile}
	echo y|${MKFS_BIN} -C ${CLUSTERSIZE} -b ${BLOCKSIZE} -N ${SLOTS} -L ${LABEL} ${DEVICE_NAME} || {
		${ECHO} "Can not format ${DEVICE_NAME}"
		return 1
	}

	${ECHO} "Mount volume to all nodes"|tee -a ${logfile}
	${REMOTE_MOUNT_BIN} -l ${LABEL} -m ${MOUNT_POINT} -n ${NODE_LIST}>>${logfile} 2>&1

	${SUDO} chown -R ${USERNAME}:${GROUPNAME} ${MOUNT_POINT}
	${SUDO} chmod -R 777  ${MOUNT_POINT}

	${BINDIR}/run_write_append_truncate.py -i 20000 -l ${logfile} -n ${NODE_LIST} -f ${testfile}

	LogRC $?

	${ECHO} "Umount volume from all nodes after test."|tee -a ${logfile}
        ${REMOTE_UMOUNT_BIN} -m ${MOUNT_POINT} -n ${NODE_LIST}>>${logfile} 2>&1
}

run_multi_mmap_test()
{
	LogMsg "multi-mmap-test"
	
	local logdir=${O2TDIR}/log/multi_mmap_log
        local logfile=${logdir}/multi_mmap_test_${DATE}.log

        local testfile=${MOUNT_POINT}/multi_mmap_test_file

        #${SUDO} mkdir -p ${logdir}
	
	mkdir -p ${logdir}
        chmod 777 ${logdir}
        touch ${logfile}
        chmod 777 ${logfile}

        #force to umount volume from all nodes
        ${ECHO} "Try to umount volume from all nodes before test."|tee -a ${logfile}
        ${REMOTE_UMOUNT_BIN} -m ${MOUNT_POINT} -n ${NODE_LIST}>>${logfile} 2>&1

        CLUSTERSIZE=32k
        BLOCKSIZE=4k
        SLOTS=4
        LABEL=ocfs2test

        ${ECHO} "Format volume to launch new test"|tee -a ${logfile}
        echo y|${MKFS_BIN} -C ${CLUSTERSIZE} -b ${BLOCKSIZE} -N ${SLOTS} -L ${LABEL} ${DEVICE_NAME} || {
                ${ECHO} "Can not format ${DEVICE_NAME}"
                return 1
        }

        ${ECHO} "Mount volume to all nodes"|tee -a ${logfile}
        ${REMOTE_MOUNT_BIN} -l ${LABEL} -m ${MOUNT_POINT} -n ${NODE_LIST}>>${logfile} 2>&1

	${SUDO} chown -R ${USERNAME}:${GROUPNAME} ${MOUNT_POINT} 
	${SUDO} chmod -R 777  ${MOUNT_POINT}

        ${BINDIR}/run_multi_mmap.py -i 20000 -n ${NODE_LIST} -c -b 6000 --hole -f ${testfile} | tee -a ${logfile}

	LogRC $?

	${ECHO} "Umount volume from all nodes after test."|tee -a ${logfile}
        ${REMOTE_UMOUNT_BIN} -m ${MOUNT_POINT} -n ${NODE_LIST}>>${logfile} 2>&1
}

run_lvb_torture_test()
{
	LogMsg "lvb_torture_test"

	local logdir=${O2TDIR}/log/lvb_torture_log
        local logfile=${logdir}/lvb_torture_test_${DATE}.log

	#${SUDO} mkdir -p ${logdir}

	mkdir -p ${logdir}
        chmod 777 ${logdir}
        touch ${logfile}
        chmod 777 ${logfile}

	#force to umount volume from all nodes
	${ECHO} "Try to umount volume from all nodes before test."|tee -a ${logfile}
	${REMOTE_UMOUNT_BIN} -m ${MOUNT_POINT} -n ${NODE_LIST}>>${logfile} 2>&1

	CLUSTERSIZE=32k
	BLOCKSIZE=4k
	SLOTS=4
	LABEL=ocfs2test

	${ECHO} "Format volume to launch new test"|tee -a ${logfile}
	echo y|${MKFS_BIN} -C ${CLUSTERSIZE} -b ${BLOCKSIZE} -N ${SLOTS} -L ${LABEL} ${DEVICE_NAME} || {
		${ECHO} "Can not format ${DEVICE_NAME}"
		return 1
	}

	${ECHO} "Mount volume to all nodes"|tee -a ${logfile}
	${REMOTE_MOUNT_BIN} -l ${LABEL} -m ${MOUNT_POINT} -n ${NODE_LIST}>>${logfile} 2>&1

	${SUDO} chown -R ${USERNAME}:${GROUPNAME} ${MOUNT_POINT}
	${SUDO} chmod -R 777  ${MOUNT_POINT}

	#dd 2G file for testing.
	${ECHO} "dd 2G testfile under mount point."|tee -a ${logfile}
	${DD} if=/dev/zero of=${MOUNT_POINT}/testfile bs=1024 count=2097152
	local UUID="`${DEBUGFS_BIN} -R stats ${DEVICE_NAME} |grep UUID|cut -d: -f 2`"
	local LOCK="`${DEBUGFS_BIN} -R 'encode testfile' ${DEVICE_NAME}`"

	${BINDIR}/run_lvb_torture.py -d /dlm/ -i 60000 -H ${DEVICE_NAME} -l ${logfile} -n ${NODE_LIST} "${UUID}" "${LOCK}"

	LogRC $?

	${ECHO} "Umount volume from all nodes after test."|tee -a ${logfile}
	${REMOTE_UMOUNT_BIN} -m ${MOUNT_POINT} -n ${NODE_LIST}>>${logfile} 2>&1
}

run_create_racer_test()
{
	LogMsg "create-racer-test"

	local logdir=${O2TDIR}/log/create_racer_log
        local logfile=${logdir}/create_racer_test_${DATE}.log

        local testpath=${MOUNT_POINT}

        #${SUDO} mkdir -p ${logdir}
	mkdir -p ${logdir}
	chmod 777 ${logdir}
	touch ${logfile}
	chmod 777 ${logfile}

        #force to umount volume from all nodes
        ${ECHO} "Try to umount volume from all nodes before test."|tee -a ${logfile}
        ${REMOTE_UMOUNT_BIN} -m ${MOUNT_POINT} -n ${NODE_LIST}>>${logfile} 2>&1

        CLUSTERSIZE=32k
        BLOCKSIZE=4k
        SLOTS=4
        LABEL=ocfs2test

        ${ECHO} "Format volume to launch new test"|tee -a ${logfile}
        echo y|${MKFS_BIN} -C ${CLUSTERSIZE} -b ${BLOCKSIZE} -N ${SLOTS} -L ${LABEL} ${DEVICE_NAME} || {
                ${ECHO} "Can not format ${DEVICE_NAME}"
                return 1
        }

        ${ECHO} "Mount volume to all nodes"|tee -a ${logfile}
        ${REMOTE_MOUNT_BIN} -l ${LABEL} -m ${MOUNT_POINT} -n ${NODE_LIST}>>${logfile} 2>&1

	${SUDO} chown -R ${USERNAME}:${GROUPNAME} ${MOUNT_POINT} 
	${SUDO} chmod -R 777  ${MOUNT_POINT}

        ${BINDIR}/run_create_racer.py -i 40000 -l ${logfile} -n ${NODE_LIST} -p ${testpath}

	LogRC $?

	${ECHO} "Umount volume from all nodes after test."|tee -a ${logfile}
        ${REMOTE_UMOUNT_BIN} -m ${MOUNT_POINT} -n ${NODE_LIST}>>${logfile} 2>&1
}

run_flock_tests()
{
	LogMsg "flock-tests"

	local logdir=${O2TDIR}/log/flock_log
        local flock_logfile=${logdir}/flock_test_${DATE}.log
        local fcntl_logfile=${logdir}/fcntl_test_${DATE}.log

        local testfile1=${MOUNT_POINT}/flock_test_file1
	local testfile2=${MOUNT_POINT}/flock_test_file2

        #${SUDO} mkdir -p ${logdir}

	mkdir -p ${logdir}
        chmod 777 ${logdir}
        touch ${logfile}
        chmod 777 ${logfile}
        #force to umount volume from all nodes
        ${ECHO} "Try to umount volume from all nodes before test."|tee -a ${flock_logfile}
        ${REMOTE_UMOUNT_BIN} -m ${MOUNT_POINT} -n ${NODE_LIST}>>${flock_logfile} 2>&1

        CLUSTERSIZE=32k
        BLOCKSIZE=4k
        SLOTS=4
        LABEL=ocfs2test

        ${ECHO} "Format volume to launch new test"|tee -a ${flock_logfile}
        echo y|${MKFS_BIN} -C ${CLUSTERSIZE} -b ${BLOCKSIZE} -N ${SLOTS} -L ${LABEL} ${DEVICE_NAME} || {
                ${ECHO} "Can not format ${DEVICE_NAME}"
                return 1
        }

        ${ECHO} "Mount volume to all nodes"|tee -a ${flock_logfile}
        ${REMOTE_MOUNT_BIN} -l ${LABEL} -m ${MOUNT_POINT} -n ${NODE_LIST}>>${flock_logfile} 2>&1

	${SUDO} chown -R ${USERNAME}:${GROUPNAME} ${MOUNT_POINT} 
	${SUDO} chmod -R 777  ${MOUNT_POINT}

	touch ${testfile1}
	touch ${testfile2}

        ${BINDIR}/run_flock_unit_test.py -l ${fcntl_logfile} -n ${NODE_LIST} -t fcntl -e ${testfile1} -f ${testfile2} \
	&& \
	${BINDIR}/run_flock_unit_test.py -l ${flock_logfile} -n ${NODE_LIST} -t flock -e ${testfile1} -f ${testfile2}

	LogRC $?

	${ECHO} "Umount volume from all nodes after test."|tee -a ${logfile}
        ${REMOTE_UMOUNT_BIN} -m ${MOUNT_POINT} -n ${NODE_LIST}>>${logfile} 2>&1

}

run_open_delete_test()
{
	LogMsg "open-delete-test"

	local logdir=${O2TDIR}/log/open_delete_log
        local logfile=${logdir}/open_delete_test_${DATE}.log

        local testfile=${MOUNT_POINT}/open_delete_test_file

        #${SUDO} mkdir -p ${logdir}

	mkdir -p ${logdir}
        chmod 777 ${logdir}
        touch ${logfile}
        chmod 777 ${logfile}

        #force to umount volume from all nodes
        ${ECHO} "Try to umount volume from all nodes before test."|tee -a ${logfile}
        ${REMOTE_UMOUNT_BIN} -m ${MOUNT_POINT} -n ${NODE_LIST}>>${logfile} 2>&1


        CLUSTERSIZE=32k
        BLOCKSIZE=4k
        SLOTS=4
        LABEL=ocfs2test

        ${ECHO} "Format volume to launch new test"|tee -a ${logfile}
        echo y|${MKFS_BIN} -C ${CLUSTERSIZE} -b ${BLOCKSIZE} -N ${SLOTS} -L ${LABEL} ${DEVICE_NAME} || {
                ${ECHO} "Can not format ${DEVICE_NAME}"
                return 1
        }

        ${ECHO} "Mount volume to all nodes"|tee -a ${logfile}
        ${REMOTE_MOUNT_BIN} -l ${LABEL} -m ${MOUNT_POINT} -n ${NODE_LIST}>>${logfile} 2>&1

	${SUDO} chown -R ${USERNAME}:${GROUPNAME} ${MOUNT_POINT} 
	${SUDO} chmod -R 777  ${MOUNT_POINT}

        ${BINDIR}/open_delete.py -f ${testfile} -i 10000 -l ${logfile} -n ${NODE_LIST}

	LogRC $?

	${ECHO} "Umount volume from all nodes after test."|tee -a ${logfile}
        ${REMOTE_UMOUNT_BIN} -m ${MOUNT_POINT} -n ${NODE_LIST}>>${logfile} 2>&1
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
run_write_append_truncate_test

START=$(date +%s)
run_multi_mmap_test

START=$(date +%s)
run_lvb_torture_test

START=$(date +%s)
run_create_racer_test

START=$(date +%s)
run_flock_tests

START=$(date +%s)
run_open_delete_test

END=$(date +%s)
DIFF=$(( ${END} - ${STARTRUN} ));
${ECHO} "Total Runtime ${DIFF} seconds.\n" >> ${LOGFILE}
${ECHO} "`date` - Ended Multiple Nodes Regress test" >> ${LOGFILE}
