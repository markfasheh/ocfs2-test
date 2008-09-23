#!/bin/bash
# vi: set ts=8 sw=8 autoindent noexpandtab :
################################################################################
#
# File :       	multiple-inline-run.sh
#
# Description:  The wrapper script help to run the multiple-node inline-data test for both files and
#		dirs.
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

MPI_RUN=`rpm -ql openmpi|grep bin|grep mpirun`
MPI_BIN_PATH=`dirname ${MPI_RUN}`
export PATH=$PATH:$MPI_BIN_PATH

MPIRUN_BIN="`which mpirun`"

RM="`which rm`"
MKDIR="`which mkdir`"

RSH_BIN="`which rsh`"
SSH_BIN="`which ssh`"
REMOTE_SH_BIN=${SSH_BIN}

AWK_BIN="`which awk`"
TOUCH_BIN="`which touch`"
MOUNT_BIN="`which sudo` -u root `which mount`"
UMOUNT_BIN="`which sudo` -u root `which umount`"
MKFS_BIN="`which sudo` -u root `which mkfs.ocfs2`"
INLINE_DATA_BIN=`which multi-inline-data`
INLINE_DIRS_BIN=`which multi-inline-dirs`
DEFAULT_LOG="multiple-inline-data-test-logs"
LOG_OUT_DIR=
DATA_LOG_FILE=
DIRS_LOG_FILE=
RUN_LOG_FILE=
MOUNT_POINT=
OCFS2_DEVICE=

BLOCKSIZE=
CLUSTERSIZE=
BLOCKNUMS=

TMP_DIR=/tmp
DEFAULT_HOSTFILE=".openmpi_hostfile"
CLUSTER_CONFIG_FILE="/etc/ocfs2/cluster.conf"
DEFAULT_RANKS=4

declare -i MPI_RANKS
MPI_HOSTFILE=
MPI_ACCESS_METHOD="ssh"
MPI_PLS_AGENT_ARG="-mca pls_rsh_agent ssh:rsh"

set -o pipefail

###for success/failure print
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
    echo "usage: `basename ${0}` [-r MPI_ranks] [-f MPI_hostfile] [-a access_method] [-o output] <-d <device>> <mountpoint path>"
    echo "       -r size of MPI rank"
    echo "       -a access method for process propagation,should be ssh or rsh,set ssh as a default method when omited."
    echo "       -f MPI hostfile,by default,script would generate one by referring to /etc/ocfs2/cluster.conf"
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

         while getopts "o:hd:r:a:f:" options; do
                case $options in
		a ) MPI_ACCESS_METHOD="$OPTARG";;
		r ) MPI_RANKS="$OPTARG";;
		f ) MPI_HOSTFILE="$OPTARG";;
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

f_create_hostfile()
{
        MPI_HOSTFILE="${TMP_DIR}/${DEFAULT_HOSTFILE}"

        TMP_FILE="${TMP_DIR}/.tmp_openmpi_hostfile"

        if [ -f "$MPI_HOSTFILE" ];then
                ${RM} -rf ${MPI_HOSTFILE}
        fi

        cat ${CLUSTER_CONFIG_FILE}|grep name|sed '$ d'|${AWK_BIN} '{print $3}'>$TMP_FILE

        while read line
        do
                echo "$line      slots=2">>$MPI_HOSTFILE

        done<$TMP_FILE

        ${RM} -rf $TMP_FILE

}


f_setup()
{
	rpm -q --quiet openmpi ||{
                echo "Need to install openmpi in advance"
                exit 1
        }

        f_getoptions $*

	if [ "$MPI_ACCESS_METHOD" = "rsh" ];then
                MPI_PLS_AGENT_ARG="-mca pls_rsh_agent rsh:ssh"
                REMOTE_SH_BIN=${RSH_BIN}
        fi

        if [ -z "${MOUNT_POINT}" ];then
                f_usage
        fi

	MPI_RANKS=${MPI_RANKS:-$DEFAULT_RANKS}

        LOG_OUT_DIR=${LOG_OUT_DIR:-$DEFAULT_LOG}

        ${MKDIR} -p ${LOG_OUT_DIR} || exit 1


        LOG_POSTFIX=$(date +%Y%m%d-%H%M%S)
        DATA_LOG_FILE="`dirname ${LOG_OUT_DIR}`/`basename ${LOG_OUT_DIR}`/multiple-inline-data-test-${LOG_POSTFIX}.log"
        DIRS_LOG_FILE="`dirname ${LOG_OUT_DIR}`/`basename ${LOG_OUT_DIR}`/multiple-inline-dirs-test-${LOG_POSTFIX}.log"
        RUN_LOG_FILE="`dirname ${LOG_OUT_DIR}`/`basename ${LOG_OUT_DIR}`/run-${LOG_POSTFIX}.log"

	if [ -z "$MPI_HOSTFILE" ];then
                f_create_hostfile
        else
                if [ ! -f "${MPI_HOSTFILE}" ];then
                        f_usage
                fi
        fi

}

f_do_mkfs_and_mount()
{
	echo -n "Mkfsing device(-b ${BLOCKSIZE} -C ${CLUSTERSIZE}): "|tee -a ${RUN_LOG_FILE}

	echo y|${MKFS_BIN} --fs-features=inline-data -b ${BLOCKSIZE} -C ${CLUSTERSIZE} -N 4 ${OCFS2_DEVICE} ${BLOCKNUMS}>>${RUN_LOG_FILE} 2>&1

	RET=$?
	echo_status ${RET} |tee -a ${RUN_LOG_FILE}
	exit_or_not ${RET}

	while read node_line ; do
		host_node=`echo ${node_line}|${AWK_BIN} '{print $1}'`
		echo -n "Mounting device to ${MOUNT_POINT} on ${host_node}:"|tee -a ${RUN_LOG_FILE}
		RET=$(${REMOTE_SH_BIN} -n ${host_node} "sudo /bin/mount -t ocfs2 -o rw,nointr ${OCFS2_DEVICE} ${MOUNT_POINT};echo \$?" 2>>${RUN_LOG_FILE})
		echo_status ${RET} |tee -a ${RUN_LOG_FILE}
		exit_or_not ${RET}
			
	done<${MPI_HOSTFILE}

} 

f_do_umount()
{
	while read node_line;do
		host_node=`echo ${node_line}|awk '{print $1}'`
		echo -ne "Unmounting device from ${MOUNT_POINT} on ${host_node}:"|tee -a ${RUN_LOG_FILE}
		RET=$(${REMOTE_SH_BIN} -n ${host_node} "sudo /bin/umount ${MOUNT_POINT};echo \$?" 2>>${RUN_LOG_FILE})
		echo_status ${RET} |tee -a ${RUN_LOG_FILE}
		exit_or_not ${RET}
		
	done<${MPI_HOSTFILE}

}


f_run_data_test()
{
	echo >>${DATA_LOG_FILE}
        echo "==========================================================">>${DATA_LOG_FILE}
        echo -ne "Functionality Test For Regular File Among Nodes:"|tee -a ${RUN_LOG_FILE}
        echo -ne "Functionality Test For Regular File Among Nodes:">> ${DATA_LOG_FILE}
        echo >>${DATA_LOG_FILE}
        echo "==========================================================">>${DATA_LOG_FILE}
	echo -e "Testing Binary:\t\t${INLINE_DATA_BIN} -i 1 -d ${OCFS2_DEVICE} ${MOUNT_POINT}">>${DATA_LOG_FILE}

	${MPIRUN_BIN} ${MPI_PLS_AGENT_ARG} -mca btl tcp,self -mca btl_tcp_if_include eth0 -np ${MPI_RANKS} --hostfile ${MPI_HOSTFILE} ${INLINE_DATA_BIN} -i 1 -d ${OCFS2_DEVICE} ${MOUNT_POINT}>>${DATA_LOG_FILE} 2>&1
	RET=$?
	echo_status ${RET} |tee -a ${RUN_LOG_FILE}
	exit_or_not ${RET}


	echo >>${DATA_LOG_FILE}
        echo "==========================================================">>${DATA_LOG_FILE}
        echo -ne "Stress Test For Regular File Among Nodes:"|tee -a ${RUN_LOG_FILE}
        echo -ne "Stress Test For Regular File Among Nodes:">> ${DATA_LOG_FILE}             
        echo >>${DATA_LOG_FILE}
        echo "==========================================================">>${DATA_LOG_FILE}
	echo -e "Testing Binary:\t\t${INLINE_DATA_BIN} -i 50 -d ${OCFS2_DEVICE} ${MOUNT_POINT}">>${DATA_LOG_FILE}

        ${MPIRUN_BIN} ${MPI_PLS_AGENT_ARG} -mca btl tcp,self -mca btl_tcp_if_include eth0 -np ${MPI_RANKS} --hostfile ${MPI_HOSTFILE} ${INLINE_DATA_BIN} -i 200  -d ${OCFS2_DEVICE} ${MOUNT_POINT}>>${DATA_LOG_FILE} 2>&1
	RET=$?
        echo_status ${RET} |tee -a ${RUN_LOG_FILE}
        exit_or_not ${RET}

}

f_run_dirs_test()
{
	echo >>${DIRS_LOG_FILE}
        echo "==========================================================">>${DIRS_LOG_FILE}
        echo -ne "Functionality Test For Directory Among Nodes:"|tee -a ${RUN_LOG_FILE}
        echo -ne "Functionality Test For Directory Among Nodes:">> ${DIRS_LOG_FILE}
        echo >>${DIRS_LOG_FILE}
        echo "==========================================================">>${DIRS_LOG_FILE}
	echo -e "Testing Binary:\t\t${INLINE_DIRS_BIN} -i 1 -s 20 -d ${OCFS2_DEVICE} ${MOUNT_POINT}">>${DIRS_LOG_FILE}

	${MPIRUN_BIN} ${MPI_PLS_AGENT_ARG} -mca btl tcp,self -mca btl_tcp_if_include eth0 -np ${MPI_RANKS} --hostfile ${MPI_HOSTFILE}  ${INLINE_DIRS_BIN} -i 1 -s 20 -d ${OCFS2_DEVICE} ${MOUNT_POINT}>>${DIRS_LOG_FILE} 2>&1
	RET=$?
        echo_status ${RET} |tee -a ${RUN_LOG_FILE}
        exit_or_not ${RET}


	echo >>${DIRS_LOG_FILE}
        echo "==========================================================">>${DIRS_LOG_FILE}
        echo -ne "Stress Test I Test For Directory Among Nodes:"|tee -a ${RUN_LOG_FILE}
        echo -ne "Stress Test I Test For Directory Among Nodes:">> ${DIRS_LOG_FILE}
        echo >>${DIRS_LOG_FILE}
        echo "==========================================================">>${DIRS_LOG_FILE}
	echo -e "Testing Binary:\t\t${INLINE_DIRS_BIN} -i 1 -s 100 -d ${OCFS2_DEVICE} ${MOUNT_POINT}">>${DIRS_LOG_FILE}

        ${MPIRUN_BIN} ${MPI_PLS_AGENT_ARG} -mca btl tcp,self -mca btl_tcp_if_include eth0 -np ${MPI_RANKS} --hostfile ${MPI_HOSTFILE}  ${INLINE_DIRS_BIN} -i 1 -s 100 -d ${OCFS2_DEVICE} ${MOUNT_POINT}>>${DIRS_LOG_FILE} 2>&1
	RET=$?
        echo_status ${RET} |tee -a ${RUN_LOG_FILE}
        exit_or_not ${RET}


	echo >>${DIRS_LOG_FILE}
        echo "==========================================================">>${DIRS_LOG_FILE}
        echo -ne "Stress Test II Test For Directory Among Nodes:"|tee -a ${RUN_LOG_FILE}
        echo -ne "Stress Test II Test For Directory Among Nodes:">> ${DIRS_LOG_FILE}
        echo >>${DIRS_LOG_FILE}
        echo "==========================================================">>${DIRS_LOG_FILE}
	echo -e "Testing Binary:\t\t${INLINE_DIRS_BIN} -i 5 -s 20 -d ${OCFS2_DEVICE} ${MOUNT_POINT}">>${DIRS_LOG_FILE}

        ${MPIRUN_BIN} ${MPI_PLS_AGENT_ARG} -mca btl tcp,self -mca btl_tcp_if_include eth0 -np ${MPI_RANKS} --hostfile ${MPI_HOSTFILE}  ${INLINE_DIRS_BIN} -i 1 -s 20 -d ${OCFS2_DEVICE} ${MOUNT_POINT}>>${DIRS_LOG_FILE} 2>&1
	RET=$?
        echo_status ${RET} |tee -a ${RUN_LOG_FILE}
        exit_or_not ${RET}

}

f_cleanup()
{
	if [ -f "$TMP_FILE" ];then
		${RM} -rf $TMP_FILE
	fi
}

################################################################################
# Main Entry
################################################################################

trap 'echo -ne "\n\n">>${RUN_LOG_FILE};echo  "Interrupted by Ctrl+C,Cleanuping... "|tee -a ${RUN_LOG_FILE}; f_cleanup;exit 1' SIGINT
trap ' : ' SIGTERM

f_setup $*

for BLOCKSIZE in 512 1024 4096
do
	for CLUSTERSIZE in  4096 32768 1048576
	do
		echo "++++++++++Multiple node inline-data test with \"-b ${BLOCKSIZE} -C ${CLUSTERSIZE}\"++++++++++" |tee -a ${RUN_LOG_FILE}
		echo "++++++++++Multiple node inline-data test with \"-b ${BLOCKSIZE} -C ${CLUSTERSIZE}\"++++++++++">>${DATA_LOG_FILE}
		echo "++++++++++Multiple node inline-data test with \"-b ${BLOCKSIZE} -C ${CLUSTERSIZE}\"++++++++++">>${DIRS_LOG_FILE}
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
