#!/bin/bash
# vi: set ts=8 sw=8 autoindent noexpandtab :
################################################################################
#
# File :	xattr-multi-run.sh
#
# Description:	The wrapper script help to run the multi-nodes xattr test with 
# 		various settings,to perform the utility,fucntional,stress test.
#		NOTE:need to have openmpi configured and passwordless rsh/ssh 
#		access.
#
# Author:       Tristan Ye,	tristan.ye@oracle.com
#
# History:      6 July 2008
#
# Copyright (C) 2008 Oracle.  All rights reserved.
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
CHMOD_BIN="`which sudo` -u root `which chmod`"
CHOWN_BIN="`which sudo` -u root `which chown`"

XATTR_TEST_BIN=`which xattr-multi-test`

DEFAULT_LOG="multiple-xattr-test-logs"
LOG_OUT_DIR=
LOG_FILE=
RUN_LOG_FILE=
MOUNT_POINT=
OCFS2_DEVICE=

BLOCKSIZE=
CLUSTERSIZE=
BLOCKNUMS=

WORKPLACE=

TMP_DIR=/tmp
DEFAULT_HOSTFILE=".openmpi_hostfile"
CLUSTER_CONFIG_FILE="/etc/ocfs2/cluster.conf"
DEFAULT_RANKS=4

declare -i MPI_RANKS
MPI_HOSTFILE=
MPI_ACCESS_METHOD="ssh"
MPI_PLS_AGENT_ARG="-mca pls_rsh_agent ssh:rsh"

###for success/failure print
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
    echo "usage: `basename ${0}` [-r MPI_Ranks] [-f MPI_Hostfile] [-a access method] [-o output] <-d <device>> <mountpoint path>"
    echo "       -r size of MPI rank"
    echo "       -a access method for process propagation,should be ssh or rsh,set ssh as a default method when omited."
    echo "       -f MPI hostfile,script would generate one by referring /etc/ocfs2/cluster.conf by default"
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

	 while getopts "o:d:r:f:a:h:" options; do
                case $options in
		r ) MPI_RANKS="$OPTARG";;
                f ) MPI_HOSTFILE="$OPTARG";;
                o ) LOG_OUT_DIR="$OPTARG";;
                d ) OCFS2_DEVICE="$OPTARG";;
		a ) MPI_ACCESS_METHOD="$OPTARG";;
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
	LOG_FILE="`dirname ${LOG_OUT_DIR}`/`basename ${LOG_OUT_DIR}`/xattr-multiple-test-log-${LOG_POSTFIX}.log"
	RUN_LOG_FILE="`dirname ${LOG_OUT_DIR}`/`basename ${LOG_OUT_DIR}`/run-${LOG_POSTFIX}.log"
	
	if [ -z "$MPI_HOSTFILE" ];then
                f_create_hostfile
        else
                if [ ! -f "${MPI_HOSTFILE}" ];then
                        f_usage
                fi
        fi

	${CHMOD_BIN} -R 777 ${MOUNT_POINT}

        ${CHOWN_BIN} -R ${USER}:${USER} ${MOUNT_POINT}

        WORKPLACE="`dirname ${MOUNT_POINT}`/`basename ${MOUNT_POINT}`/multi_xattr_test_place"
	
}

f_do_mkfs_and_mount()
{
        echo -n "Mkfsing device(-b ${BLOCKSIZE} -C ${CLUSTERSIZE}): "|tee -a ${RUN_LOG_FILE}

        echo y|${MKFS_BIN} --fs-features=xattr -b ${BLOCKSIZE} -C ${CLUSTERSIZE} -N 4 ${OCFS2_DEVICE} ${BLOCKNUMS}>>${RUN_LOG_FILE} 2>&1

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

        ${MKDIR} -p ${WORKPLACE} || exit 1

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

f_runtest()
{
	echo >>${LOG_FILE}
	echo "==========================================================">>${LOG_FILE}
	echo -ne "Check Namespace&Filetype of Multinode Xattr on Ocfs2:"|tee -a ${RUN_LOG_FILE}
	echo -ne "Check Namespace&Filetype of Multinode Xattr on Ocfs2:">>${LOG_FILE}
	echo >>${LOG_FILE}
	echo "==========================================================">>${LOG_FILE}
	for namespace in user trusted
	do
		for filetype in normal directory symlink
		do
			echo -e "Testing Binary:\t\t${MPIRUN_BIN} ${MPI_PLS_AGENT_ARG} -mca btl tcp,self -mca btl_tcp_if_include eth0 -np ${MPI_RANKS} --hostfile ${MPI_HOSTFILE} ${XATTR_TEST_BIN} -i 1 -x 20 -n ${namespace} -t ${filetype} -l 50 -s 200 ${WORKPLACE}">>${LOG_FILE}
			echo "********${namespace} mode on ${filetype}********">>${LOG_FILE}

			${MPIRUN_BIN} ${MPI_PLS_AGENT_ARG} -mca btl tcp,self -mca btl_tcp_if_include eth0 -np ${MPI_RANKS} --hostfile ${MPI_HOSTFILE} ${XATTR_TEST_BIN} -i 1 -x 20 -n ${namespace} -t ${filetype} -l 50 -s 200 ${WORKPLACE}>>${LOG_FILE} 2>&1
			rc=$?
			if [ "$rc" != "0" ];then
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


	echo >>${LOG_FILE}
	echo "==========================================================">>${LOG_FILE}
        echo -ne "Check Utility of Multinode Xattr on Ocfs2:"|tee -a ${RUN_LOG_FILE}
        echo -ne "Check Utility of Multinode Xattr on Ocfs2:">>${LOG_FILE}
        echo >>${LOG_FILE}
        echo "==========================================================">>${LOG_FILE}
	for((i=0;i<4;i++));do
		echo -e "Testing Binary:\t\t${MPIRUN_BIN} ${MPI_PLS_AGENT_ARG} -mca btl tcp,self -mca btl_tcp_if_include eth0 -np ${MPI_RANKS} --hostfile ${MPI_HOSTFILE} ${XATTR_TEST_BIN} -i 1 -x 10 -n user -t normal -l 50 -s 100 ${WORKPLACE}">>${LOG_FILE}
		${MPIRUN_BIN} ${MPI_PLS_AGENT_ARG} -mca btl tcp,self -mca btl_tcp_if_include eth0 -np ${MPI_RANKS} --hostfile ${MPI_HOSTFILE} ${XATTR_TEST_BIN} -i 1 -x 10 -n user -t normal -l 50 -s 100 ${WORKPLACE}>>${LOG_FILE} 2>&1
		rc=$?
		if [ ! "$rc" == "0"  ];then
			echo_failure |tee -a ${RUN_LOG_FILE}
			echo | tee -a ${RUN_LOG_FILE}
			break
		fi
	done
	if [ "$rc" == "0" ];then
                echo_success |tee -a ${RUN_LOG_FILE}
                echo | tee -a ${RUN_LOG_FILE}
        fi
        ${RM} -rf ${WORKPLACE}/* || exit 1


	echo >>${LOG_FILE}
	echo "==========================================================">>${LOG_FILE}
	echo -ne "Check Max Multinode Xattr EA_Name_Length:"|tee -a ${RUN_LOG_FILE}
	echo -ne "Check Max Multinode Xattr EA_Name_Length:">> ${LOG_FILE}
	echo >>${LOG_FILE}
        echo "==========================================================">>${LOG_FILE}
	echo -e "Testing Binary:\t\t${MPIRUN_BIN} ${MPI_PLS_AGENT_ARG} -mca btl tcp,self -mca btl_tcp_if_include eth0 -np ${MPI_RANKS} --hostfile ${MPI_HOSTFILE} ${XATTR_TEST_BIN} -i 1 -x 4 -n user -t normal -l 255 -s 300 ${WORKPLACE}">>${LOG_FILE}
	${MPIRUN_BIN} ${MPI_PLS_AGENT_ARG} -mca btl tcp,self -mca btl_tcp_if_include eth0 -np ${MPI_RANKS} --hostfile ${MPI_HOSTFILE} ${XATTR_TEST_BIN} -i 1 -x 4 -n user -t normal -l 255 -s 300 ${WORKPLACE}>>${LOG_FILE} 2>&1
	RET=$?
        echo_status ${RET} |tee -a ${RUN_LOG_FILE}
        exit_or_not ${RET}
        ${RM} -rf ${WORKPLACE}/* || exit 1


	echo >>${LOG_FILE}
        echo "==========================================================">>${LOG_FILE}
        echo -ne "Check Max Multinode Xattr EA_Size:"|tee -a ${RUN_LOG_FILE}
        echo -ne "Check Max Multinode Xattr EA_Size:">> ${LOG_FILE}
        echo >>${LOG_FILE}
        echo "==========================================================">>${LOG_FILE}
        echo -e "Testing Binary:\t\t${MPIRUN_BIN} ${MPI_PLS_AGENT_ARG} -mca btl tcp,self -mca btl_tcp_if_include eth0 -np ${MPI_RANKS} --hostfile ${MPI_HOSTFILE} ${XATTR_TEST_BIN} -i 1 -x 1 -n user -t normal -l 50 -s 65536 ${WORKPLACE}">>${LOG_FILE}
        ${MPIRUN_BIN} ${MPI_PLS_AGENT_ARG} -mca btl tcp,self -mca btl_tcp_if_include eth0 -np ${MPI_RANKS} --hostfile ${MPI_HOSTFILE} ${XATTR_TEST_BIN} -i 1 -x 1 -n user -t normal -l 50 -s 65536 ${WORKPLACE}>>${LOG_FILE} 2>&1
        RET=$?
        echo_status ${RET} |tee -a ${RUN_LOG_FILE}
        exit_or_not ${RET}
        ${RM} -rf ${WORKPLACE}/* || exit 1


	echo >>${LOG_FILE}
        echo "==========================================================">>${LOG_FILE}
        echo -ne "Check Huge Multinode Xattr EA_Entry_Nums:"|tee -a ${RUN_LOG_FILE}
        echo -ne "Check Huge Multinode Xattr EA_Entry_Nums:">> ${LOG_FILE}
        echo >>${LOG_FILE}
        echo "==========================================================">>${LOG_FILE}
        echo -e "Testing Binary:\t\t${MPIRUN_BIN} ${MPI_PLS_AGENT_ARG} -mca btl tcp,self -mca btl_tcp_if_include eth0 -np ${MPI_RANKS} --hostfile ${MPI_HOSTFILE} ${XATTR_TEST_BIN} -i 1 -x 10000 -n user -t normal -l 100 -s 200 ${WORKPLACE}">>${LOG_FILE}
        ${MPIRUN_BIN} ${MPI_PLS_AGENT_ARG} -mca btl tcp,self -mca btl_tcp_if_include eth0 -np ${MPI_RANKS} --hostfile ${MPI_HOSTFILE} ${XATTR_TEST_BIN} -i 1 -x 10000 -n user -t normal -l 100 -s 200 ${WORKPLACE}>>${LOG_FILE} 2>&1
        RET=$?
        echo_status ${RET} |tee -a ${RUN_LOG_FILE}
        exit_or_not ${RET}
        ${RM} -rf ${WORKPLACE}/* || exit 1

	
	echo >>${LOG_FILE}
        echo "==========================================================">>${LOG_FILE}
        echo -ne "Check All Max Multinode Xattr Arguments Together:"|tee -a ${RUN_LOG_FILE}
        echo -ne "Check All Max Multinode Xattr Arguments Together:">> ${LOG_FILE}
        echo >>${LOG_FILE}
        echo "==========================================================">>${LOG_FILE}
        echo -e "Testing Binary:\t\t${MPIRUN_BIN} ${MPI_PLS_AGENT_ARG} -mca btl tcp,self -mca btl_tcp_if_include eth0 -np ${MPI_RANKS} --hostfile ${MPI_HOSTFILE} ${XATTR_TEST_BIN} -i 1 -x 1000 -n user -t normal -l 255 -s 65536 ${WORKPLACE}">>${LOG_FILE}
        ${MPIRUN_BIN} ${MPI_PLS_AGENT_ARG} -mca btl tcp,self -mca btl_tcp_if_include eth0 -np ${MPI_RANKS} --hostfile ${MPI_HOSTFILE} ${XATTR_TEST_BIN} -i 1 -x 1000 -n user -t normal -l 255 -s 65536 ${WORKPLACE}>>${LOG_FILE} 2>&1
        RET=$?
        echo_status ${RET} |tee -a ${RUN_LOG_FILE}
        exit_or_not ${RET}
        ${RM} -rf ${WORKPLACE}/* || exit 1


	echo >>${LOG_FILE}
        echo "==========================================================">>${LOG_FILE}
        echo -ne "Launch Concurrent Adding Test:"|tee -a ${RUN_LOG_FILE}
        echo -ne "Launch Concurrent Adding Test:">> ${LOG_FILE}
        echo >>${LOG_FILE}
        echo "==========================================================">>${LOG_FILE}
        echo -e "Testing Binary:\t\t${MPIRUN_BIN} ${MPI_PLS_AGENT_ARG} -mca btl tcp,self -mca btl_tcp_if_include eth0 -np ${MPI_RANKS} --hostfile ${MPI_HOSTFILE} ${XATTR_TEST_BIN} -i 1 -x 1000 -n user -t normal -l 255 -s 5000 -o -r -k ${WORKPLACE}">>${LOG_FILE}
        ${MPIRUN_BIN} ${MPI_PLS_AGENT_ARG} -mca btl tcp,self -mca btl_tcp_if_include eth0 -np ${MPI_RANKS} --hostfile ${MPI_HOSTFILE} ${XATTR_TEST_BIN} -i 1 -x 1000 -n user -t normal -l 255 -s 5000 -o -r -k ${WORKPLACE}>>${LOG_FILE} 2>&1
        RET=$?
        echo_status ${RET} |tee -a ${RUN_LOG_FILE}
        exit_or_not ${RET}
        ${RM} -rf ${WORKPLACE}/* || exit 1

	
	echo >>${LOG_FILE}
        echo "==========================================================">>${LOG_FILE}
        echo -ne "Launch MultiNode Xattr Stress Test:"|tee -a ${RUN_LOG_FILE}
        echo -ne "Launch MultiNode Xattr Stress Test:">> ${LOG_FILE}
        echo >>${LOG_FILE}
        echo "==========================================================">>${LOG_FILE}
        echo -e "Testing Binary:\t\t${MPIRUN_BIN} ${MPI_PLS_AGENT_ARG} -mca btl tcp,self -mca btl_tcp_if_include eth0 -np ${MPI_RANKS} --hostfile ${MPI_HOSTFILE} ${XATTR_TEST_BIN} -i 1 -x 2000 -n user -t normal -l 255 -s 5000  -r -k ${WORKPLACE}">>${LOG_FILE}
        ${MPIRUN_BIN} ${MPI_PLS_AGENT_ARG} -mca btl tcp,self -mca btl_tcp_if_include eth0 -np ${MPI_RANKS} --hostfile ${MPI_HOSTFILE} ${XATTR_TEST_BIN} -i 1 -x 2000 -n user -t normal -l 255 -s 5000  -r -k ${WORKPLACE}>>${LOG_FILE} 2>&1
        RET=$?
        echo_status ${RET} |tee -a ${RUN_LOG_FILE}
        exit_or_not ${RET}
        ${RM} -rf ${WORKPLACE}/* || exit 1


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
                echo "++++++++++Multiple node xattr test with \"-b ${BLOCKSIZE} -C ${CLUSTERSIZE}\"++++++++++" |tee -a ${RUN_LOG_FILE}
                echo "++++++++++Multiple node xattr test with \"-b ${BLOCKSIZE} -C ${CLUSTERSIZE}\"++++++++++">>${LOG_FILE}
                echo "======================================================================================="
                f_do_mkfs_and_mount
		f_runtest
                f_do_umount
                echo "======================================================================================="
                echo -e "\n\n\n">>${LOG_FILE}
        done
done

f_cleanup


