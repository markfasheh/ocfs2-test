#!/bin/bash
# vi: set ts=8 sw=8 autoindent noexpandtab :
################################################################################
#
# File :	xattr-single-run.sh
#
# Description:	The wrapper script help to run the xattr test with various settings
#               in terms of single-node,to perform utility,functionality and stress
#		test.
#
# Author:       Tristan Ye,	tristan.ye@oracle.com
#
# History:      14 Aug 2008
#
# Copyright (C) 2008 Oracle.  All rights reserved.
#
################################################################################
# Global Variables
################################################################################
PATH=$PATH:/sbin      # Add /sbin to the path for ocfs2 tools
export PATH=$PATH:.
GETXATTR="`which sudo` -u root `which getfattr`"
SETXATTR="`which sudo` -u root `which setfattr`"
RM="`which rm`"
MKDIR="`which mkdir`"
TOUCH_BIN="`which touch`"
MOUNT_BIN="`which sudo` -u root `which mount`"
UMOUNT_BIN="`which sudo` -u root `which umount`"
MKFS_BIN="`which sudo` -u root `which mkfs.ocfs2`"
XATTR_TEST_BIN=`which xattr-test`

DEFAULT_LOG="xattr-test-logs"
LOG_OUT_DIR=
DETAIL_LOG_FILE=
RUN_LOG_FILE=
MOUNT_POINT=
OCFS2_DEVICE=

BLOCKSIZE=
CLUSTERSIZE=
BLOCKNUMS=


WORKPLACE=

EXECUTE=1
KILL_TEST=0
GET_STATUS=0
declare -i ITERATIONS
declare -i EA_NUMS
declare -i EA_NAME_LEN
declare -i EA_VALUE_SIZE

set -o pipefail

BOOTUP=color
RES_COL=80
MOVE_TO_COL="echo -en \\033[${RES_COL}G"
SETCOLOR_SUCCESS="echo -en \\033[1;32m"
SETCOLOR_FAILURE="echo -en \\033[1;31m"
SETCOLOR_WARNING="echo -en \\033[1;33m"
SETCOLOR_NORMAL="echo -en \\033[0;39m"
LOGLEVEL=1

echo_success() {
  [ "$BOOTUP" = "color" ] && $MOVE_TO_COL
  echo -n "["
  [ "$BOOTUP" = "color" ] && $SETCOLOR_SUCCESS
  echo -n $" PASS "
  [ "$BOOTUP" = "color" ] && $SETCOLOR_NORMAL
  echo -n "]"
  return 0
}

echo_failure() {
  [ "$BOOTUP" = "color" ] && $MOVE_TO_COL
  echo -n "["
  [ "$BOOTUP" = "color" ] && $SETCOLOR_FAILURE
  echo -n $"FAILED"
  [ "$BOOTUP" = "color" ] && $SETCOLOR_NORMAL
  echo -n "]"
  return 1
}

echo_status()
{
        if [ "${1}" == "0" ];then
                echo_success
                echo
        else
                echo_failure
                echo
                exit 1
        fi


}

exit_or_not()
{
        if [ "${1}" != "0" ];then
                exit 1;
        fi
}


################################################################################
# Utility Functions
################################################################################
f_usage()
{
    echo "usage: `basename ${0}` [-o output_log_dir] <-d <device>> <mountpoint path>"
    echo "       -o output directory for the logs"
    echo "       -d specify the device which has been formated as an ocfs2 volume."
    echo "       <mountpoint path> path of mountpoint where the ocfs2 volume will be mounted on."
    exit 1;

}

f_getoptions()
{
	 if [ $# -eq 0 ]; then
                f_usage;
                exit 1
         fi

	 while getopts "i:x:n:l:s:ko:d:" options; do
                case $options in
                o ) LOG_OUT_DIR="$OPTARG";;
                d ) OCFS2_DEVICE="$OPTARG";;
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

	LOG_POSTFIX=$(date +%Y%m%d-%H%M%S)
        DETAIL_LOG_FILE="`dirname ${LOG_OUT_DIR}`/`basename ${LOG_OUT_DIR}`/xattr-single-test-${LOG_POSTFIX}.log"
        RUN_LOG_FILE="`dirname ${LOG_OUT_DIR}`/`basename ${LOG_OUT_DIR}`/run-single-${LOG_POSTFIX}.log"

	LOG_OUT_DIR=${LOG_OUT_DIR:-$DEFAULT_LOG}

	${MKDIR} -p ${LOG_OUT_DIR} || exit 1
	
	LOG_POSTFIX=$(date +%Y%m%d-%H%M%S)
	DETAIL_LOG_FILE="`dirname ${LOG_OUT_DIR}`/`basename ${LOG_OUT_DIR}`/xattr-single-test-${LOG_POSTFIX}.log"
        RUN_LOG_FILE="`dirname ${LOG_OUT_DIR}`/`basename ${LOG_OUT_DIR}`/run-single-${LOG_POSTFIX}.log"
	
	WORKPLACE="`dirname ${MOUNT_POINT}`/`basename ${MOUNT_POINT}`/xattr_test_place"
}

f_do_mkfs_and_mount()
{
	echo -n "Mkfsing device:"|tee -a ${RUN_LOG_FILE}

        echo y|${MKFS_BIN} --fs-features=xattr -b ${BLOCKSIZE} -C ${CLUSTERSIZE} -N 4 ${OCFS2_DEVICE} ${BLOCKNUMS}>>${RUN_LOG_FILE} 2>&1
        RET=$?
        echo_status ${RET} |tee -a ${RUN_LOG_FILE}
        exit_or_not ${RET}

        echo -n "Mounting device to ${MOUNT_POINT}:"|tee -a ${RUN_LOG_FILE}

        ${MOUNT_BIN} -t ocfs2 ${OCFS2_DEVICE}  ${MOUNT_POINT}>>${RUN_LOG_FILE} 2>&1
        RET=$?
        echo_status ${RET} |tee -a ${RUN_LOG_FILE}
        exit_or_not ${RET}

        ${MKDIR} -p ${WORKPLACE} || exit 1

}

f_do_umount()
{
	echo -n "Umounting device to ${MOUNT_POINT}:"|tee -a ${RUN_LOG_FILE}

	${RM} -rf ${WORKPLACE} || exit 1

        ${UMOUNT_BIN} ${MOUNT_POINT}>>${RUN_LOG_FILE} 2>&1

        RET=$?
        echo_status ${RET} |tee -a ${RUN_LOG_FILE}
        exit_or_not ${RET}
}

f_runtest()
{
	echo >>${DETAIL_LOG_FILE}
	echo "==========================================================">>${DETAIL_LOG_FILE}
	echo -ne "Check Namespace&Filetype of SingleNode Xattr on Ocfs2:"|tee -a ${RUN_LOG_FILE}
	echo -ne "Check Namespace&Filetype of SingleNode Xattr on Ocfs2:">>${DETAIL_LOG_FILE}
	echo >>${DETAIL_LOG_FILE}
	echo "==========================================================">>${DETAIL_LOG_FILE}
	for namespace in user trusted
	do
		for filetype in normal directory symlink
		do
			echo -e "Testing Binary:\t\t${XATTR_TEST_BIN} -i 1 -x 20 -n ${namespace} -t ${filetype} -l 50 -s 200 ${WORKPLACE}">>${DETAIL_LOG_FILE}
			echo "********${namespace} mode on ${filetype}********">>${DETAIL_LOG_FILE}

			${XATTR_TEST_BIN}  -i 1 -x 20 -n ${namespace} -t ${filetype} -l 50 -s 200  ${WORKPLACE} >>${DETAIL_LOG_FILE} 2>&1
			rc=$?
			if [ "$rc" != "0" ];then
				#currently,xattr only supported on symlink in terms of trusted mode
				if [ "$namespace" == "user" -a "$filetype" == "symlink" ]; then
					continue
				else
					rc=1
					echo_failure | tee -a ${RUN_LOG_FILE}
					echo | tee -a ${RUN_LOG_FILE}
					exit 1
				fi
			fi
			
			${RM} -rf ${WORKPLACE}/* || exit 1
		done
		if [ "$rc" != "0" ];then
			if [ "$namespace" == "user" -a "$filetype" == "symlink" ]; then
				continue
			else
				break
			fi
		fi
	done
	if [ "$rc" == "0" ];then
		echo_success |tee -a ${RUN_LOG_FILE}
		echo |tee -a ${RUN_LOG_FILE}
	fi
	${RM} -rf ${WORKPLACE}/* || exit 1


        echo >>${DETAIL_LOG_FILE}
	echo "==========================================================">>${DETAIL_LOG_FILE}
	echo -ne "Check Utility of SingleNode Xattr on Ocfs2:"|tee -a ${RUN_LOG_FILE}
        echo -ne "Check Utility of SingleNode Xattr on Ocfs2:">>${DETAIL_LOG_FILE}
        echo >>${DETAIL_LOG_FILE}
        echo "==========================================================">>${DETAIL_LOG_FILE}
	for((i=0;i<10;i++));do
		echo -e "Testing Binary:\t\t${XATTR_TEST_BIN} -i 2 -x 500 -n user -t normal -l 200 -s 2000 ${WORKPLACE}">>${DETAIL_LOG_FILE}
        	${XATTR_TEST_BIN}  -i 2 -x 500 -n user -t normal -l 200 -s 2000  ${WORKPLACE} >>${DETAIL_LOG_FILE} 2>&1
		rc=$?
		if [ ! "$rc" == "0"  ];then
			echo_failure |tee -a ${RUN_LOG_FILE}
			echo | tee -a ${RUN_LOG_FILE}
			exit 1
		fi
	done
	if [ "$rc" == "0" ];then
		echo_success |tee -a ${RUN_LOG_FILE}
		echo | tee -a ${RUN_LOG_FILE}
	fi
        ${RM} -rf ${WORKPLACE}/* || exit 1


	echo >>${DETAIL_LOG_FILE}
        echo "==========================================================">>${DETAIL_LOG_FILE}
        echo -ne "Check Max SingleNode Xattr EA_Name_Length:"|tee -a ${RUN_LOG_FILE}
        echo -ne "Check Max SingleNode Xattr EA_Name_Length:">>${DETAIL_LOG_FILE}
        echo >>${DETAIL_LOG_FILE}
        echo "==========================================================">>${DETAIL_LOG_FILE}
	echo -e "Testing Binary:\t\t${XATTR_TEST_BIN} -i 1 -x 1 -n user -t normal -l 255 -s 200 ${WORKPLACE}">>${DETAIL_LOG_FILE}
	${XATTR_TEST_BIN}  -i 1 -x 1 -n user -t normal -l 255 -s 200 ${WORKPLACE} >>${DETAIL_LOG_FILE} 2>&1
	RET=$?
        echo_status ${RET} |tee -a ${RUN_LOG_FILE}
        exit_or_not ${RET}
	${RM} -rf ${WORKPLACE}/* || exit 1


	echo >>${DETAIL_LOG_FILE}
        echo "==========================================================">>${DETAIL_LOG_FILE}
        echo -ne "Check Max SingleNode Xattr EA_Size:"|tee -a ${RUN_LOG_FILE}
        echo -ne "Check Max SingleNode Xattr EA_Size:">>${DETAIL_LOG_FILE}
        echo >>${DETAIL_LOG_FILE}
        echo "==========================================================">>${DETAIL_LOG_FILE}
        echo -e "Testing Binary:\t\t${XATTR_TEST_BIN} -i 1 -x 1 -n user -t normal -l 50 -s 65536 ${WORKPLACE}">>${DETAIL_LOG_FILE}
        ${XATTR_TEST_BIN}  -i 1 -x 1 -n user -t normal -l 50 -s 65536 ${WORKPLACE} >>${DETAIL_LOG_FILE} 2>&1
        RET=$?
        echo_status ${RET} |tee -a ${RUN_LOG_FILE}
        exit_or_not ${RET}
        ${RM} -rf ${WORKPLACE}/* || exit 1


	echo >>${DETAIL_LOG_FILE}
        echo "==========================================================">>${DETAIL_LOG_FILE}
        echo -ne "Check Huge SingleNode Xattr EA_Entry_Nums:"|tee -a ${RUN_LOG_FILE}
        echo -ne "Check Huge SingleNode Xattr EA_Entry_Nums:">>${DETAIL_LOG_FILE}
        echo >>${DETAIL_LOG_FILE}
        echo "==========================================================">>${DETAIL_LOG_FILE}
        echo -e "Testing Binary:\t\t${XATTR_TEST_BIN} -i 1 -x 20000 -n user -t normal -l 20 -s 100 ${WORKPLACE}">>${DETAIL_LOG_FILE}
	${XATTR_TEST_BIN}  -i 1 -x 20000 -n user -t normal -l 20 -s 100  ${WORKPLACE} >>${DETAIL_LOG_FILE} 2>&1
        RET=$?
        echo_status ${RET} |tee -a ${RUN_LOG_FILE}
        exit_or_not ${RET}
        ${RM} -rf ${WORKPLACE}/* || exit 1


        echo >>${DETAIL_LOG_FILE}
        echo "==========================================================">>${DETAIL_LOG_FILE}
        echo -ne "Check All Max SingleNode Xattr Arguments Together:"|tee -a ${RUN_LOG_FILE}
        echo -ne "Check All Max SingleNode Xattr Arguments Together:">>${DETAIL_LOG_FILE}
        echo >>${DETAIL_LOG_FILE}
        echo "==========================================================">>${DETAIL_LOG_FILE}
        echo -e "Testing Binary:\t\t${XATTR_TEST_BIN} -i 1 -x 5000 -n user -t normal -l 255 -s 65536 ${WORKPLACE}">>${DETAIL_LOG_FILE}
	${XATTR_TEST_BIN}  -i 1 -x 5000 -n user -t normal -l 255 -s 65536  ${WORKPLACE} >>${DETAIL_LOG_FILE} 2>&1
        RET=$?
        echo_status ${RET} |tee -a ${RUN_LOG_FILE}
        exit_or_not ${RET}
        ${RM} -rf ${WORKPLACE}/* || exit 1


        echo >>${DETAIL_LOG_FILE}
        echo "==========================================================">>${DETAIL_LOG_FILE}
        echo -ne "Launch Random SingleNode Xattr Test on Ocfs2:"|tee -a ${RUN_LOG_FILE}
        echo -ne "Launch Random SingleNode Xattr Test on Ocfs2:">>${DETAIL_LOG_FILE}
        echo >>${DETAIL_LOG_FILE}
        echo "==========================================================">>${DETAIL_LOG_FILE}
        echo -e "Testing Binary:\t\t${XATTR_TEST_BIN} -i 1 -x 200 -n user -t normal -l 100 -s 4000 -r ${WORKPLACE}">>${DETAIL_LOG_FILE}
	${XATTR_TEST_BIN}  -i 1 -x 200 -n user -t normal -l 100 -s 4000 -r ${WORKPLACE} >>${DETAIL_LOG_FILE} 2>&1
        RET=$?
        echo_status ${RET} |tee -a ${RUN_LOG_FILE}
        exit_or_not ${RET}
        ${RM} -rf ${WORKPLACE}/* || exit 1


	echo >>${DETAIL_LOG_FILE}
        echo "==========================================================">>${DETAIL_LOG_FILE}
        echo -ne "Launch Concurrent Update/Read SingleNode Xattr Test on Ocfs2:"|tee -a ${RUN_LOG_FILE}
        echo -ne "Launch Concurrent Update/Read SingleNode Xattr Test on Ocfs2:">>${DETAIL_LOG_FILE}
        echo >>${DETAIL_LOG_FILE}
        echo "==========================================================">>${DETAIL_LOG_FILE}
        echo -e "Testing Binary:\t\t${XATTR_TEST_BIN} -i 1 -x 2000 -n user -t normal -l 50 -s 1000 -m 2000 -r ${WORKPLACE}">>${DETAIL_LOG_FILE}
	${XATTR_TEST_BIN}  -i 1 -x 2000 -n user -t normal -l 50 -s 1000 -m 2000 -r  ${WORKPLACE} >>${DETAIL_LOG_FILE} 2>&1
        RET=$?
        echo_status ${RET} |tee -a ${RUN_LOG_FILE}
        exit_or_not ${RET}
        ${RM} -rf ${WORKPLACE}/* || exit 1
	
	
	echo >>${DETAIL_LOG_FILE}
        echo "==========================================================">>${DETAIL_LOG_FILE}
        echo -ne "Launch Multiple Files SingleNode Xattr Test on Ocfs2:"|tee -a ${RUN_LOG_FILE}
        echo -ne "Launch Multiple Files SingleNode Xattr Test on Ocfs2:">>${DETAIL_LOG_FILE}
        echo >>${DETAIL_LOG_FILE}
        echo "==========================================================">>${DETAIL_LOG_FILE}
        echo -e "Testing Binary:\t\t${XATTR_TEST_BIN} -i 1 -x 500 -n user -t normal -l 20 -s 800 -f 100 -r ${WORKPLACE}">>${DETAIL_LOG_FILE}
	${XATTR_TEST_BIN}  -i 1 -x 500 -n user -t normal -l 20 -s 800 -f 100 -r  ${WORKPLACE} >>${DETAIL_LOG_FILE} 2>&1
        RET=$?
        echo_status ${RET} |tee -a ${RUN_LOG_FILE}
        exit_or_not ${RET}
        ${RM} -rf ${WORKPLACE}/* || exit 1


	echo >>${DETAIL_LOG_FILE}
        echo "==========================================================">>${DETAIL_LOG_FILE}
        echo -ne "Launch Stress Test With Shell Command:"|tee -a ${RUN_LOG_FILE}
        echo -ne "">>${DETAIL_LOG_FILE}
        echo >>${DETAIL_LOG_FILE}
        echo "==========================================================">>${DETAIL_LOG_FILE}

	${TOUCH_BIN} ${WORKPLACE}/shell_commond_testfile
	for i in $(seq 1000) ; do
		value="value"
		for j in $(seq $i);do
			value="value${value}"
		done
		setfattr -n user.ea_name_${RANDOM}${i} -v ${value} ${WORKPLACE}/shell_commond_testfile >>${DETAIL_LOG_FILE} 2>&1
	done
        rc=$?
        if [ "$rc" == "0" ];then
		echo_success | tee -a ${RUN_LOG_FILE}
		echo | tee -a ${RUN_LOG_FILE}
        else
		echo_failure | tee -a ${RUN_LOG_FILE}
		echo | tee -a ${RUN_LOG_FILE}
		exit 1
        fi
        ${RM} -rf ${WORKPLACE}/* || exit 1


	echo >>${DETAIL_LOG_FILE}
        echo "==========================================================">>${DETAIL_LOG_FILE}
        echo -ne "Launch SingleNode Xattr Stress Test on Ocfs2:"|tee -a ${RUN_LOG_FILE}
        echo -ne "Launch SingleNode Xattr Stress Test on Ocfs2:">>${DETAIL_LOG_FILE}
        echo >>${DETAIL_LOG_FILE}
        echo "==========================================================">>${DETAIL_LOG_FILE}
        echo -e "Testing Binary:\t\t${XATTR_TEST_BIN} -i 100 -x 10000 -n user -t normal -l 200 -s 60000  -r ${WORKPLACE}">>${DETAIL_LOG_FILE}
        ${XATTR_TEST_BIN}  -i 100 -x 10000 -n user -t normal -l 200 -s 60000 -r  ${WORKPLACE} >>${DETAIL_LOG_FILE} 2>&1
        RET=$?
        echo_status ${RET} |tee -a ${RUN_LOG_FILE}
        exit_or_not ${RET}
        ${RM} -rf ${WORKPLACE}/* || exit 1


}

f_cleanup()
{
	:
}

################################################################################
# Main Entry
################################################################################

trap 'echo -ne "\n\n">>${RUN_LOG_FILE};echo  "Interrupted by Ctrl+C,Cleanuping... "|tee -a ${RUN_LOG_FILE}; f_cleanup;exit 1' SIGINT
trap ' : ' SIGTERM

f_setup $*

for BLOCKSIZE in 512 1024 4096
do
        for CLUSTERSIZE in 4096 32768 1048576
        do
                echo "++++++++++Single node xattr test with \"-b ${BLOCKSIZE} -C ${CLUSTERSIZE}\"++++++++++" |tee -a ${RUN_LOG_FILE}
                echo "++++++++++Single node xattr test with \"-b ${BLOCKSIZE} -C ${CLUSTERSIZE}\"++++++++++">>${DETAIL_LOG_FILE}
                echo "======================================================================================="
                f_do_mkfs_and_mount
                f_runtest
                f_do_umount
                echo "======================================================================================="
                echo -e "\n\n\n">>${DETAIL_LOG_FILE}
        done
done

f_cleanup

