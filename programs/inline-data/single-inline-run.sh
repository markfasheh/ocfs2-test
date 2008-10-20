#!/bin/bash
# vi: set ts=8 sw=8 autoindent noexpandtab :
################################################################################
#
# File :        inline-run.sh
#
# Description:  The wrapper script help to run the single node inline-data test
#		for both files and dirs.
#       
#
# Author:       Tristan Ye,     tristan.ye@oracle.com
#
# History:      25 Aug 2008
#

################################################################################
# Global Variables
################################################################################
PATH=$PATH:/sbin      # Add /sbin to the path for ocfs2 tools
export PATH=$PATH:.

. ./config.sh

USERNAME=`id -un`
GROUPNAME=`id -gn`

SUDO="`which sudo` -u root"
RM="`which rm`"
MKDIR="`which mkdir`"
TOUCH_BIN="`which touch`"
MOUNT_BIN="`which sudo` -u root `which mount`"
UMOUNT_BIN="`which sudo` -u root `which umount`"
MKFS_BIN="`which sudo` -u root `which mkfs.ocfs2`"
INLINE_DATA_BIN="`which sudo` -u root ${BINDIR}/inline-data"
INLINE_DIRS_BIN="`which sudo` -u root ${BINDIR}/inline-dirs"
DEFAULT_LOG="inline-data-test-logs"
LOG_OUT_DIR=
DATA_LOG_FILE=
DIRS_LOG_FILE=
RUN_LOG_FILE=
MOUNT_POINT=
OCFS2_DEVICE=

BLOCKSIZE=
CLUSTERSIZE=
BLOCKNUMS=


set -o pipefail

BOOTUP=color
RES_COL=80
MOVE_TO_COL="echo -en \\033[${RES_COL}G"
SETCOLOR_SUCCESS="echo -en \\033[1;32m"
SETCOLOR_FAILURE="echo -en \\033[1;31m"
SETCOLOR_WARNING="echo -en \\033[1;33m"
SETCOLOR_NORMAL="echo -en \\033[0;39m"
LOGLEVEL=1

echo_success()
{
  [ "$BOOTUP" = "color" ] && $MOVE_TO_COL
  echo -n "["
  [ "$BOOTUP" = "color" ] && $SETCOLOR_SUCCESS
  echo -n $" PASS "
  [ "$BOOTUP" = "color" ] && $SETCOLOR_NORMAL
  echo -n "]"
  return 0
}

echo_failure()
{
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
    echo "usage: `basename ${0}` [-o output] <-d <device>> <mountpoint path>"
    echo "       -o output directory for the logs"
    echo "       -d device name used for ocfs2 volume"
    echo "       <mountpoint path> path of mountpoint where the ocfs2 volume will be mounted on."
    exit 1;

}

f_getoptions()
{
         if [ $# -eq 0 ]; then
                f_usage;
                exit 1
         fi

         while getopts "o:hd:" options; do
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
	else
		if [ ! -d ${MOUNT_POINT} ]; then
			echo "Mount point ${MOUNT_POINT} does not exist."
			exit 1
		else
			#To assure that mount point will not end with a trailing '/'
			if [ "`dirname ${MOUNT_POINT}`" = "/" ]; then
				MOUNT_POINT="`dirname ${MOUNT_POINT}``basename ${MOUNT_POINT}`"
			else
				MOUNT_POINT="`dirname ${MOUNT_POINT}`/`basename ${MOUNT_POINT}`"
			fi
		fi
        fi

        LOG_OUT_DIR=${LOG_OUT_DIR:-$DEFAULT_LOG}

        ${MKDIR} -p ${LOG_OUT_DIR} || exit 1


        LOG_POSTFIX=$(date +%Y%m%d-%H%M%S)
        DATA_LOG_FILE="`dirname ${LOG_OUT_DIR}`/`basename ${LOG_OUT_DIR}`/inline-data-test-${LOG_POSTFIX}.log"
        DIRS_LOG_FILE="`dirname ${LOG_OUT_DIR}`/`basename ${LOG_OUT_DIR}`/inline-dirs-test-${LOG_POSTFIX}.log"
        RUN_LOG_FILE="`dirname ${LOG_OUT_DIR}`/`basename ${LOG_OUT_DIR}`/run-${LOG_POSTFIX}.log"

}

f_do_mkfs_and_mount()
{
	echo -n "Mkfsing device:"|tee -a ${RUN_LOG_FILE}

	echo y|${MKFS_BIN} --fs-features=inline-data -b ${BLOCKSIZE} -C ${CLUSTERSIZE} -N 4 ${OCFS2_DEVICE} ${BLOCKNUMS} >>${RUN_LOG_FILE} 2>&1
	RET=$?
	echo_status ${RET} |tee -a ${RUN_LOG_FILE}
	exit_or_not ${RET}

	echo -n "Mounting device to ${MOUNT_POINT}:"|tee -a ${RUN_LOG_FILE}

	${MOUNT_BIN} -t ocfs2 ${OCFS2_DEVICE}  ${MOUNT_POINT}>>${RUN_LOG_FILE} 2>&1

	RET=$?
        echo_status ${RET} |tee -a ${RUN_LOG_FILE}
        exit_or_not ${RET}

	${SUDO} chown -R ${USERNAME}:${GROUPNAME} ${MOUNT_POINT}
	${SUDO} chmod -R 777 ${MOUNT_POINT}
} 

f_run_data_test()
{
	echo >>${DATA_LOG_FILE}
        echo "==========================================================">>${DATA_LOG_FILE}
        echo -ne "Functionality Test For Regular File:"|tee -a ${RUN_LOG_FILE}
        echo -ne "Functionality Test For Regular File:">> ${DATA_LOG_FILE}
        echo >>${DATA_LOG_FILE}
        echo "==========================================================">>${DATA_LOG_FILE}
	echo -e "Testing Binary:\t\t${INLINE_DATA_BIN} -i 1 -d ${OCFS2_DEVICE} ${MOUNT_POINT}">>${DATA_LOG_FILE}

	${INLINE_DATA_BIN} -i 1 -d ${OCFS2_DEVICE} ${MOUNT_POINT}>>${DATA_LOG_FILE} 2>&1
	RET=$?
        echo_status ${RET} |tee -a ${RUN_LOG_FILE}
        exit_or_not ${RET}


	echo >>${DATA_LOG_FILE}
        echo "==========================================================">>${DATA_LOG_FILE}
        echo -ne "Concurrent R/W Test For Regular File:"|tee -a ${RUN_LOG_FILE}
        echo -ne "Concurrent R/W Test For Regular File:">> ${DATA_LOG_FILE} 
        echo >>${DATA_LOG_FILE}
        echo "==========================================================">>${DATA_LOG_FILE}
	echo -e "Testing Binary:\t\t${INLINE_DATA_BIN} -i 1 -c 100 -d ${OCFS2_DEVICE} ${MOUNT_POINT}">>${DATA_LOG_FILE}

        ${INLINE_DATA_BIN} -i 1 -c 100 -d ${OCFS2_DEVICE} ${MOUNT_POINT}>>${DATA_LOG_FILE} 2>&1
	RET=$?
        echo_status ${RET} |tee -a ${RUN_LOG_FILE}
        exit_or_not ${RET}

	
	echo >>${DATA_LOG_FILE}
        echo "==========================================================">>${DATA_LOG_FILE}
        echo -ne "Multiple File Test For Regular File:"|tee -a ${RUN_LOG_FILE}
        echo -ne "Multiple File Test For Regular File:">> ${DATA_LOG_FILE}    
        echo >>${DATA_LOG_FILE}
        echo "==========================================================">>${DATA_LOG_FILE}
	echo -e "Testing Binary:\t\t${INLINE_DATA_BIN} -i 1 -m 50 -d ${OCFS2_DEVICE} ${MOUNT_POINT}">>${DATA_LOG_FILE}

        ${INLINE_DATA_BIN} -i 1 -m 50 -d ${OCFS2_DEVICE} ${MOUNT_POINT}>>${DATA_LOG_FILE} 2>&1
	RET=$?
        echo_status ${RET} |tee -a ${RUN_LOG_FILE}
        exit_or_not ${RET}


	echo >>${DATA_LOG_FILE}
        echo "==========================================================">>${DATA_LOG_FILE}
        echo -ne "Stress Test I For Regular File:"|tee -a ${RUN_LOG_FILE}
        echo -ne "Stress Test I For Regular File:">> ${DATA_LOG_FILE}             
        echo >>${DATA_LOG_FILE}
        echo "==========================================================">>${DATA_LOG_FILE}
	echo -e "Testing Binary:\t\t${INLINE_DATA_BIN} -i 50 -m 20 -d ${OCFS2_DEVICE} ${MOUNT_POINT}">>${DATA_LOG_FILE}

        ${INLINE_DATA_BIN} -i 50 -m 20 -d ${OCFS2_DEVICE} ${MOUNT_POINT}>>${DATA_LOG_FILE} 2>&1
	RET=$?
        echo_status ${RET} |tee -a ${RUN_LOG_FILE}
        exit_or_not ${RET}


	echo >>${DATA_LOG_FILE}
        echo "==========================================================">>${DATA_LOG_FILE}
        echo -ne "Stress Test II For Regular File:"|tee -a ${RUN_LOG_FILE}
        echo -ne "Stress Test II For Regular File:">> ${DATA_LOG_FILE}     
        echo >>${DATA_LOG_FILE}
        echo "==========================================================">>${DATA_LOG_FILE}
	echo -e "Testing Binary:\t\t${INLINE_DATA_BIN} -i 50 -c 500 -d ${OCFS2_DEVICE} ${MOUNT_POINT}">>${DATA_LOG_FILE}

        ${INLINE_DATA_BIN} -i 50 -c 500 -d ${OCFS2_DEVICE} ${MOUNT_POINT}>>${DATA_LOG_FILE} 2>&1
	RET=$?
        echo_status ${RET} |tee -a ${RUN_LOG_FILE}
        exit_or_not ${RET}


}

f_run_dirs_test()
{
	echo >>${DIRS_LOG_FILE}
        echo "==========================================================">>${DIRS_LOG_FILE}
        echo -ne "Functionality Test For Directory:"|tee -a ${RUN_LOG_FILE}
        echo -ne "Functionality Test For Directory:">> ${DIRS_LOG_FILE}
        echo >>${DIRS_LOG_FILE}
        echo "==========================================================">>${DIRS_LOG_FILE}
	echo -e "Testing Binary:\t\t${INLINE_DIRS_BIN} -i 1 -s 20 -d ${OCFS2_DEVICE} ${MOUNT_POINT}">>${DIRS_LOG_FILE}

	${INLINE_DIRS_BIN} -i 1 -s 20 -d ${OCFS2_DEVICE} ${MOUNT_POINT}>>${DIRS_LOG_FILE} 2>&1
	RET=$?
        echo_status ${RET} |tee -a ${RUN_LOG_FILE}
        exit_or_not ${RET}


	echo >>${DIRS_LOG_FILE}
        echo "==========================================================">>${DIRS_LOG_FILE}
        echo -ne "Concurrent R/W Test For Directory:"|tee -a ${RUN_LOG_FILE}
        echo -ne "Concurrent R/W Test For Directory:">> ${DIRS_LOG_FILE}
        echo >>${DIRS_LOG_FILE}
        echo "==========================================================">>${DIRS_LOG_FILE}
	echo -e "Testing Binary:\t\t${INLINE_DIRS_BIN} -i 1 -s 20 -c 100 -d ${OCFS2_DEVICE} ${MOUNT_POINT}">>${DIRS_LOG_FILE}

        ${INLINE_DIRS_BIN} -i 1 -s 20 -c 100 -d ${OCFS2_DEVICE} ${MOUNT_POINT}>>${DIRS_LOG_FILE} 2>&1
	RET=$?
        echo_status ${RET} |tee -a ${RUN_LOG_FILE}
        exit_or_not ${RET}


	echo >>${DIRS_LOG_FILE}
        echo "==========================================================">>${DIRS_LOG_FILE}
        echo -ne "Multiple Dirs Test For Directory:"|tee -a ${RUN_LOG_FILE}
        echo -ne "Multiple Dirs Test For Directory:">> ${DIRS_LOG_FILE}
        echo >>${DIRS_LOG_FILE}
        echo "==========================================================">>${DIRS_LOG_FILE}
	echo -e "Testing Binary:\t\t${INLINE_DIRS_BIN} -i 1 -s 20 -m 50 -d ${OCFS2_DEVICE} ${MOUNT_POINT}">>${DIRS_LOG_FILE}

        ${INLINE_DIRS_BIN} -i 1 -s 20 -m 50 -d ${OCFS2_DEVICE} ${MOUNT_POINT}>>${DIRS_LOG_FILE} 2>&1
	RET=$?
        echo_status ${RET} |tee -a ${RUN_LOG_FILE}
        exit_or_not ${RET}


	echo >>${DIRS_LOG_FILE}
        echo "==========================================================">>${DIRS_LOG_FILE}
        echo -ne "Stress Test I Test For Directory:"|tee -a ${RUN_LOG_FILE}
        echo -ne "Stress Test I Test For Directory:">> ${DIRS_LOG_FILE}
        echo >>${DIRS_LOG_FILE}
        echo "==========================================================">>${DIRS_LOG_FILE}
	echo -e "Testing Binary:\t\t${INLINE_DIRS_BIN} -i 1 -s 20 -m 100 -d ${OCFS2_DEVICE} ${MOUNT_POINT}">>${DIRS_LOG_FILE}

        ${INLINE_DIRS_BIN} -i 1 -s 20 -m 100 -d ${OCFS2_DEVICE} ${MOUNT_POINT}>>${DIRS_LOG_FILE} 2>&1
	RET=$?
        echo_status ${RET} |tee -a ${RUN_LOG_FILE}
        exit_or_not ${RET}


	echo >>${DIRS_LOG_FILE}
        echo "==========================================================">>${DIRS_LOG_FILE}
        echo -ne "Stress Test II Test For Directory:"|tee -a ${RUN_LOG_FILE}
        echo -ne "Stress Test II Test For Directory:">> ${DIRS_LOG_FILE}
        echo >>${DIRS_LOG_FILE}
        echo "==========================================================">>${DIRS_LOG_FILE}
	echo -e "Testing Binary:\t\t${INLINE_DIRS_BIN} -i 1 -s 20 -c 500 -d ${OCFS2_DEVICE} ${MOUNT_POINT}">>${DIRS_LOG_FILE}

        ${INLINE_DIRS_BIN} -i 1 -s 20 -c 500 -d ${OCFS2_DEVICE} ${MOUNT_POINT}>>${DIRS_LOG_FILE} 2>&1
	RET=$?
        echo_status ${RET} |tee -a ${RUN_LOG_FILE}
        exit_or_not ${RET}


	echo >>${DIRS_LOG_FILE}
        echo "==========================================================">>${DIRS_LOG_FILE}
        echo -ne "Stress Test III Test For Directory:"|tee -a ${RUN_LOG_FILE}
        echo -ne "Stress Test III Test For Directory:">> ${DIRS_LOG_FILE}
        echo >>${DIRS_LOG_FILE}
        echo "==========================================================">>${DIRS_LOG_FILE}
	echo -e "Testing Binary:\t\t${INLINE_DIRS_BIN} -i 100 -s 100 -d ${OCFS2_DEVICE} ${MOUNT_POINT}">>${DIRS_LOG_FILE}

        ${INLINE_DIRS_BIN} -i 100 -s 100 -d ${OCFS2_DEVICE} ${MOUNT_POINT}>>${DIRS_LOG_FILE} 2>&1
	RET=$?
        echo_status ${RET} |tee -a ${RUN_LOG_FILE}
        exit_or_not ${RET}

}
f_do_umount()
{
	echo -ne "Umounting device to ${MOUNT_POINT}:"|tee -a ${RUN_LOG_FILE}

	${UMOUNT_BIN} ${MOUNT_POINT}

	RET=$?
        echo_status ${RET} |tee -a ${RUN_LOG_FILE}
        exit_or_not ${RET}

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
		echo "++++++++++Single node inline-data test with \"-b ${BLOCKSIZE} -C ${CLUSTERSIZE}\"++++++++++" |tee -a ${RUN_LOG_FILE}
		echo "++++++++++Single node inline-data test with \"-b ${BLOCKSIZE} -C ${CLUSTERSIZE}\"++++++++++">>${DATA_LOG_FILE}
		echo "++++++++++Single node inline-data test with \"-b ${BLOCKSIZE} -C ${CLUSTERSIZE}\"++++++++++">>${DIRS_LOG_FILE}
		echo "======================================================================================="
		f_do_mkfs_and_mount
		f_run_data_test
		f_run_dirs_test
		f_do_umount
		echo "======================================================================================="
		echo -e "\n\n\n">>${DATA_LOG_FILE}
		echo -e "\n\n\n">>${DIRS_LOG_FILE}
	done
done

f_cleanup
