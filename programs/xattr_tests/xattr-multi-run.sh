#!/bin/bash
#
# Copyright (C) 2008 Oracle.  All rights reserved.
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public
# License, version 2,  as published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public
# License along with this program; if not, write to the
# Free Software Foundation, Inc., 59 Temple Place - Suite 330,
# Boston, MA 021110-1307, USA.
#
# vi: set ts=8 sw=8 autoindent noexpandtab :
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

################################################################################
# Global Variables
################################################################################
PATH=$PATH:/sbin      # Add /sbin to the path for ocfs2 tools
export PATH=$PATH:.

. `dirname ${0}`/config.sh

RM="`which rm`"
MKDIR="`which mkdir`"

RSH_BIN="`which rsh`"
SSH_BIN="`which ssh`"
REMOTE_SH_BIN=${SSH_BIN}

USERNAME=`id -nu`
GROUPNAME=`id -gn`

SUDO="`which sudo` -u root"
AWK_BIN="`which awk`"
TOUCH_BIN="`which touch`"
MOUNT_BIN="`which sudo` -u root `which mount`"
REMOTE_MOUNT_BIN="${BINDIR}/remote_mount.py"
UMOUNT_BIN="`which sudo` -u root `which umount`"
REMOTE_UMOUNT_BIN="${BINDIR}/remote_umount.py"
MKFS_BIN="`which sudo` -u root `which mkfs.ocfs2`"
CHMOD_BIN="`which sudo` -u root `which chmod`"
CHOWN_BIN="`which sudo` -u root `which chown`"
IFCONFIG_BIN="`which sudo` -u root `which ifconfig`"

XATTR_TEST_BIN="${BINDIR}/xattr-multi-test"

LABEL="ocfs2-xattr-multi-test-`uname -m`"
SLOTS=2
DEFAULT_LOG="multiple-xattr-test-logs"
LOG_OUT_DIR=
LOG_FILE=
RUN_LOG_FILE=
MOUNT_POINT=
CLUSTER_STACK=
CLUSTER_NAME=
OCFS2_DEVICE=

SLOTS=
BLOCKSIZE=
CLUSTERSIZE=
BLOCKNUMS=

WORKPLACE=

TMP_DIR=/tmp
DEFAULT_RANKS=4

declare -i MPI_RANKS
MPI_HOSTS=
MPI_ACCESS_METHOD="ssh"
MPI_PLS_AGENT_ARG="-mca plm_rsh_agent ssh:rsh"
MPI_MCA_BTL="-mca btl tcp,self"
MPI_MCA_BTL_IF=""

TEST_NO=0
TEST_PASS=0

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
    echo "usage: `basename ${0}` [-r MPI_Ranks] <-f MPI_Hosts> [-a access method] [-o output] [-i interface] <-d <device>> <-b blocksize> <-c clustersize> <-s cluster-stack> <-n cluster-name> <mountpoint path>"
    echo "       -r size of MPI rank"
    echo "       -a access method for process propagation,should be ssh or rsh,set ssh as a default method when omited."
    echo "       -f MPI hosts list,separated by comma,e.g -f node1.us.oracle.com,node2.us.oracle.com."
    echo "       -o output directory for the logs"
    echo "       -i Network Interface name to be used for MPI messaging."
    echo "       -d specify the device which has been formated as an ocfs2 volume."
    echo "	 -b block size."
    echo "	 -c cluster size."
    echo "       -s cluster stack."
    echo "       -n cluster name."
    echo "       <mountpoint path> path of mountpoint where the ocfs2 volume will be mounted on."
    exit 1;

}

f_getoptions()
{
	 if [ $# -eq 0 ]; then
                f_usage;
                exit 1
         fi

	 while getopts "o:d:r:f:a:h:i:b:c:s:n:" options; do
                case $options in
		r ) MPI_RANKS="$OPTARG";;
                f ) MPI_HOSTS="$OPTARG";;
                o ) LOG_OUT_DIR="$OPTARG";;
                d ) OCFS2_DEVICE="$OPTARG";;
		a ) MPI_ACCESS_METHOD="$OPTARG";;
		i ) INTERFACE="$OPTARG";;
		b ) BLOCKSIZE="$OPTARG";;
		c ) CLUSTERSIZE="$OPTARG";;
		s ) CLUSTER_STACK="$OPTARG";;
		n ) CLUSTER_NAME="$OPTARG";;

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
		echo "Should not run tests as root."
		exit 1
	fi

	f_getoptions $*
	
	if [ "$MPI_ACCESS_METHOD" = "rsh" ];then
		MPI_PLS_AGENT_ARG="-mca plm_rsh_agent ssh:rsh"
		REMOTE_SH_BIN=${RSH_BIN}
	fi

	if [ ! -z "${INTERFACE}" ]; then
		${IFCONFIG_BIN} ${INTERFACE} >/dev/null 2>&1 || {
			echo "Invalid NIC";
			f_usage;
		} 
		MPI_MCA_BTL_IF="-mca btl_tcp_if_include ${INTERFACE}"
	fi;

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

	MPI_RANKS=${MPI_RANKS:-$DEFAULT_RANKS}

	LOG_OUT_DIR=${LOG_OUT_DIR:-$DEFAULT_LOG}

	${MKDIR} -p ${LOG_OUT_DIR} || exit 1

	
	LOG_SUFIX=$(date +%F-%H-%M-%S)
	LOG_FILE="`dirname ${LOG_OUT_DIR}`/`basename ${LOG_OUT_DIR}`/xattr-multiple-test-log-`uname -m`-${LOG_SUFIX}.log"
	RUN_LOG_FILE="`dirname ${LOG_OUT_DIR}`/`basename ${LOG_OUT_DIR}`/xattr-multiple-test-log-run-`uname -m`-${LOG_SUFIX}.log"
	
	if [ -z "$MPI_HOSTS" ];then
		f_usage
	fi

	# Use number of testing nodes as the default slot number.
	if [ -z "${SLOTS}" ];then
		echo $MPI_HOSTS|sed -e 's/,/\n/g' >/tmp/$$
		SLOTS=`cat /tmp/$$ |wc -l`
		rm -f /tmp/$$
	fi

	${CHMOD_BIN} -R 777 ${MOUNT_POINT}

	${CHOWN_BIN} -R ${USERNAME}:${GROUPNAME} ${MOUNT_POINT}

	WORKPLACE="`dirname ${MOUNT_POINT}`/`basename ${MOUNT_POINT}`/multi_xattr_test_place"
}

f_do_mkfs_and_mount()
{
        echo -n "Mkfsing device(-b ${BLOCKSIZE} -C ${CLUSTERSIZE}): "|tee -a ${RUN_LOG_FILE}

        echo y|${MKFS_BIN} --fs-features=xattr -b ${BLOCKSIZE} -C ${CLUSTERSIZE} --cluster-stack=${CLUSTER_STACK} --cluster-name=${CLUSTER_NAME} -N ${SLOTS} -L ${LABEL} ${OCFS2_DEVICE} ${BLOCKNUMS}>>${RUN_LOG_FILE} 2>&1

        RET=$?
        echo_status ${RET} |tee -a ${RUN_LOG_FILE}
        exit_or_not ${RET}

	echo -n "Mounting device ${OCFS2_DEVICE} to nodes(${MPI_HOSTS}):"|tee -a ${RUN_LOG_FILE}
	${REMOTE_MOUNT_BIN} -l ${LABEL} -m ${MOUNT_POINT} -n ${MPI_HOSTS}>>${RUN_LOG_FILE} 2>&1
	RET=$?
	echo_status ${RET} |tee -a ${RUN_LOG_FILE}
        exit_or_not ${RET}

	${SUDO} chown -R ${USERNAME}:${GROUPNAME} ${MOUNT_POINT}
	${SUDO} chmod -R 777 ${MOUNT_POINT}

        ${MKDIR} -p ${WORKPLACE} || exit 1

}

f_do_umount()
{
	echo -n "Umounting device ${OCFS2_DEVICE} from nodes(${MPI_HOSTS}):"|tee -a ${RUN_LOG_FILE}
	${REMOTE_UMOUNT_BIN} -m ${MOUNT_POINT} -n ${MPI_HOSTS}>>${RUN_LOG_FILE} 2>&1
	RET=$?
	echo_status ${RET} |tee -a ${RUN_LOG_FILE}
	exit_or_not ${RET}

}

f_runtest()
{
	local xattr_nums=

	((TEST_NO++))
	echo >>${LOG_FILE}
	echo "==========================================================">>${LOG_FILE}
	echo -ne "[${TEST_NO}] Check Namespace&Filetype of Multinode Xattr on Ocfs2:"|tee -a ${RUN_LOG_FILE}
	echo -ne "[${TEST_NO}] Check Namespace&Filetype of Multinode Xattr on Ocfs2:">>${LOG_FILE}
	echo >>${LOG_FILE}
	echo "==========================================================">>${LOG_FILE}
	for namespace in user trusted
	do
		for filetype in normal directory symlink
		do
			echo -e "Testing Binary:\t\t${SUDO} ${MPIRUN} --allow-run-as-root ${MPI_PLS_AGENT_ARG} ${MPI_MCA_BTL} ${MPI_MCA_BTL_IF} -np ${MPI_RANKS} --host ${MPI_HOSTS} ${XATTR_TEST_BIN} -i 1 -x 20 -n ${namespace} -t ${filetype} -l 50 -s 200 ${WORKPLACE}">>${LOG_FILE}
			echo "********${namespace} mode on ${filetype}********">>${LOG_FILE}

			${SUDO} ${MPIRUN} --allow-run-as-root ${MPI_PLS_AGENT_ARG} ${MPI_MCA_BTL} ${MPI_MCA_BTL_IF} -np ${MPI_RANKS} --host ${MPI_HOSTS} ${XATTR_TEST_BIN} -i 1 -x 20 -n ${namespace} -t ${filetype} -l 50 -s 200 ${WORKPLACE}>>${LOG_FILE} 2>&1
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
	((TEST_PASS++))


	((TEST_NO++))
	echo >>${LOG_FILE}
	echo "==========================================================">>${LOG_FILE}
        echo -ne "[${TEST_NO}] Check Utility of Multinode Xattr on Ocfs2:"|tee -a ${RUN_LOG_FILE}
        echo -ne "[${TEST_NO}] Check Utility of Multinode Xattr on Ocfs2:">>${LOG_FILE}
        echo >>${LOG_FILE}
        echo "==========================================================">>${LOG_FILE}
	for((i=0;i<4;i++));do
		echo -e "Testing Binary:\t\t${MPIRUN} ${MPI_PLS_AGENT_ARG} ${MPI_MCA_BTL} ${MPI_MCA_BTL_IF} -np ${MPI_RANKS} --host ${MPI_HOSTS} ${XATTR_TEST_BIN} -i 1 -x 10 -n user -t normal -l 50 -s 100 ${WORKPLACE}">>${LOG_FILE}
		${MPIRUN} ${MPI_PLS_AGENT_ARG} ${MPI_MCA_BTL} ${MPI_MCA_BTL_IF} -np ${MPI_RANKS} --host ${MPI_HOSTS} ${XATTR_TEST_BIN} -i 1 -x 10 -n user -t normal -l 50 -s 100 ${WORKPLACE}>>${LOG_FILE} 2>&1
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
	((TEST_PASS++))


	((TEST_NO++))
	echo >>${LOG_FILE}
	echo "==========================================================">>${LOG_FILE}
	echo -ne "[${TEST_NO}] Check Max Multinode Xattr EA_Name_Length:"|tee -a ${RUN_LOG_FILE}
	echo -ne "[${TEST_NO}] Check Max Multinode Xattr EA_Name_Length:">> ${LOG_FILE}
	echo >>${LOG_FILE}
        echo "==========================================================">>${LOG_FILE}
	echo -e "Testing Binary:\t\t${MPIRUN} ${MPI_PLS_AGENT_ARG} ${MPI_MCA_BTL} ${MPI_MCA_BTL_IF} -np ${MPI_RANKS} --host ${MPI_HOSTS} ${XATTR_TEST_BIN} -i 1 -x 4 -n user -t normal -l 255 -s 300 ${WORKPLACE}">>${LOG_FILE}
	${MPIRUN} ${MPI_PLS_AGENT_ARG} ${MPI_MCA_BTL} ${MPI_MCA_BTL_IF} -np ${MPI_RANKS} --host ${MPI_HOSTS} ${XATTR_TEST_BIN} -i 1 -x 4 -n user -t normal -l 255 -s 300 ${WORKPLACE}>>${LOG_FILE} 2>&1
	RET=$?
        echo_status ${RET} |tee -a ${RUN_LOG_FILE}
        exit_or_not ${RET}
        ${RM} -rf ${WORKPLACE}/* || exit 1
	((TEST_PASS++))


	((TEST_NO++))
	echo >>${LOG_FILE}
        echo "==========================================================">>${LOG_FILE}
        echo -ne "[${TEST_NO}] Check Max Multinode Xattr EA_Size:"|tee -a ${RUN_LOG_FILE}
        echo -ne "[${TEST_NO}] Check Max Multinode Xattr EA_Size:">> ${LOG_FILE}
        echo >>${LOG_FILE}
        echo "==========================================================">>${LOG_FILE}
        echo -e "Testing Binary:\t\t${MPIRUN} ${MPI_PLS_AGENT_ARG} ${MPI_MCA_BTL} ${MPI_MCA_BTL_IF} -np ${MPI_RANKS} --host ${MPI_HOSTS} ${XATTR_TEST_BIN} -i 1 -x 1 -n user -t normal -l 50 -s 65536 ${WORKPLACE}">>${LOG_FILE}
        ${MPIRUN} ${MPI_PLS_AGENT_ARG} ${MPI_MCA_BTL} ${MPI_MCA_BTL_IF} -np ${MPI_RANKS} --host ${MPI_HOSTS} ${XATTR_TEST_BIN} -i 1 -x 1 -n user -t normal -l 50 -s 65536 ${WORKPLACE}>>${LOG_FILE} 2>&1
        RET=$?
        echo_status ${RET} |tee -a ${RUN_LOG_FILE}
        exit_or_not ${RET}
        ${RM} -rf ${WORKPLACE}/* || exit 1
	((TEST_PASS++))


	((TEST_NO++))
	echo >>${LOG_FILE}
        echo "==========================================================">>${LOG_FILE}
        echo -ne "[${TEST_NO}] Check Huge Multinode Xattr EA_Entry_Nums:"|tee -a ${RUN_LOG_FILE}
        echo -ne "[${TEST_NO}] Check Huge Multinode Xattr EA_Entry_Nums:">> ${LOG_FILE}
        echo >>${LOG_FILE}
        echo "==========================================================">>${LOG_FILE}
	[ ${OCFS2TEST_FASTMODE} -eq 1 ] && xattr_nums=10 || xattr_nums=10000
        echo -e "Testing Binary:\t\t${MPIRUN} ${MPI_PLS_AGENT_ARG} ${MPI_MCA_BTL} ${MPI_MCA_BTL_IF} -np ${MPI_RANKS} --host ${MPI_HOSTS} ${XATTR_TEST_BIN} -i 1 -x ${xattr_nums} -n user -t normal -l 100 -s 200 ${WORKPLACE}">>${LOG_FILE}
        ${MPIRUN} ${MPI_PLS_AGENT_ARG} ${MPI_MCA_BTL} ${MPI_MCA_BTL_IF} -np ${MPI_RANKS} --host ${MPI_HOSTS} ${XATTR_TEST_BIN} -i 1 -x ${xattr_nums} -n user -t normal -l 100 -s 200 ${WORKPLACE}>>${LOG_FILE} 2>&1
        RET=$?
        echo_status ${RET} |tee -a ${RUN_LOG_FILE}
        exit_or_not ${RET}
        ${RM} -rf ${WORKPLACE}/* || exit 1
	((TEST_PASS++))

	
	((TEST_NO++))
	echo >>${LOG_FILE}
        echo "==========================================================">>${LOG_FILE}
        echo -ne "[${TEST_NO}] Check All Max Multinode Xattr Arguments Together:"|tee -a ${RUN_LOG_FILE}
        echo -ne "[${TEST_NO}] Check All Max Multinode Xattr Arguments Together:">> ${LOG_FILE}
        echo >>${LOG_FILE}
        echo "==========================================================">>${LOG_FILE}
	[ ${OCFS2TEST_FASTMODE} -eq 1 ] && xattr_nums=10 || xattr_nums=1000
        echo -e "Testing Binary:\t\t${MPIRUN} ${MPI_PLS_AGENT_ARG} ${MPI_MCA_BTL} ${MPI_MCA_BTL_IF} -np ${MPI_RANKS} --host ${MPI_HOSTS} ${XATTR_TEST_BIN} -i 1 -x ${xattr_nums} -n user -t normal -l 255 -s 65536 ${WORKPLACE}">>${LOG_FILE}
        ${MPIRUN} ${MPI_PLS_AGENT_ARG} ${MPI_MCA_BTL} ${MPI_MCA_BTL_IF} -np ${MPI_RANKS} --host ${MPI_HOSTS} ${XATTR_TEST_BIN} -i 1 -x ${xattr_nums} -n user -t normal -l 255 -s 65536 ${WORKPLACE}>>${LOG_FILE} 2>&1
        RET=$?
        echo_status ${RET} |tee -a ${RUN_LOG_FILE}
        exit_or_not ${RET}
        ${RM} -rf ${WORKPLACE}/* || exit 1
	((TEST_PASS++))


	((TEST_NO++))
	echo >>${LOG_FILE}
        echo "==========================================================">>${LOG_FILE}
        echo -ne "[${TEST_NO}] Launch Concurrent Adding Test:"|tee -a ${RUN_LOG_FILE}
        echo -ne "[${TEST_NO}] Launch Concurrent Adding Test:">> ${LOG_FILE}
        echo >>${LOG_FILE}
        echo "==========================================================">>${LOG_FILE}
	[ ${OCFS2TEST_FASTMODE} -eq 1 ] && xattr_nums=10 || xattr_nums=1000
        echo -e "Testing Binary:\t\t${MPIRUN} ${MPI_PLS_AGENT_ARG} ${MPI_MCA_BTL} ${MPI_MCA_BTL_IF} -np ${MPI_RANKS} --host ${MPI_HOSTS} ${XATTR_TEST_BIN} -i 1 -x ${xattr_nums} -n user -t normal -l 255 -s 5000 -o -r -k ${WORKPLACE}">>${LOG_FILE}
        ${MPIRUN} ${MPI_PLS_AGENT_ARG} ${MPI_MCA_BTL} ${MPI_MCA_BTL_IF} -np ${MPI_RANKS} --host ${MPI_HOSTS} ${XATTR_TEST_BIN} -i 1 -x ${xattr_nums} -n user -t normal -l 255 -s 5000 -o -r -k ${WORKPLACE}>>${LOG_FILE} 2>&1
        RET=$?
        echo_status ${RET} |tee -a ${RUN_LOG_FILE}
        exit_or_not ${RET}
        ${RM} -rf ${WORKPLACE}/* || exit 1
	((TEST_PASS++))

	[ ${OCFS2TEST_FASTMODE} -eq 1 ] && return # no stress test in fastmode
	
	((TEST_NO++))
	echo >>${LOG_FILE}
        echo "==========================================================">>${LOG_FILE}
        echo -ne "[${TEST_NO}] Launch MultiNode Xattr Stress Test:"|tee -a ${RUN_LOG_FILE}
        echo -ne "[${TEST_NO}] Launch MultiNode Xattr Stress Test:">> ${LOG_FILE}
        echo >>${LOG_FILE}
        echo "==========================================================">>${LOG_FILE}
        echo -e "Testing Binary:\t\t${MPIRUN} ${MPI_PLS_AGENT_ARG} ${MPI_MCA_BTL} ${MPI_MCA_BTL_IF} -np ${MPI_RANKS} --host ${MPI_HOSTS} ${XATTR_TEST_BIN} -i 1 -x 2000 -n user -t normal -l 255 -s 5000  -r -k ${WORKPLACE}">>${LOG_FILE}
        ${MPIRUN} ${MPI_PLS_AGENT_ARG} ${MPI_MCA_BTL} ${MPI_MCA_BTL_IF} -np ${MPI_RANKS} --host ${MPI_HOSTS} ${XATTR_TEST_BIN} -i 1 -x 2000 -n user -t normal -l 255 -s 5000  -r -k ${WORKPLACE}>>${LOG_FILE} 2>&1
        RET=$?
        echo_status ${RET} |tee -a ${RUN_LOG_FILE}
        exit_or_not ${RET}
        ${RM} -rf ${WORKPLACE}/* || exit 1
	((TEST_PASS++))
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

if [ "$BLOCKSIZE" != "NONE" ];then
	bslist="$BLOCKSIZE"
else
	bslist="512 4096"
fi

if [ "$CLUSTERSIZE" != "NONE" ];then
	cslist="$CLUSTERSIZE"
else
	cslist="4096 1048576"
fi

START_TIME=${SECONDS}
echo "=====================Multiple nodes xattr testing starts: `date`=====================" |tee -a ${RUN_LOG_FILE}
echo "=====================Multiple nodes xattr testing starts: `date`=====================" >> ${LOG_FILE}

for BLOCKSIZE in $(echo "$bslist")
do
	for CLUSTERSIZE in $(echo "$cslist")
        do
                echo "++++++++++xattr tests with \"-b ${BLOCKSIZE} -C ${CLUSTERSIZE}\"++++++++++" |tee -a ${RUN_LOG_FILE}
                echo "++++++++++xattr tests with \"-b ${BLOCKSIZE} -C ${CLUSTERSIZE}\"++++++++++">>${LOG_FILE}
                echo "======================================================================================="
                f_do_mkfs_and_mount
		f_runtest
                f_do_umount
                echo "======================================================================================="
                echo -e "\n\n\n">>${LOG_FILE}
        done
done
f_cleanup

END_TIME=${SECONDS}
echo "=====================Multiple nodes xattr testing ends: `date`=====================" |tee -a ${RUN_LOG_FILE}
echo "=====================Multiple nodes xattr testing ends: `date`=====================" >> ${LOG_FILE}

echo "Time elapsed(s): $((${END_TIME}-${START_TIME}))" |tee -a ${RUN_LOG_FILE}
echo "Tests total: ${TEST_NO}" |tee -a ${RUN_LOG_FILE}
echo "Tests passed: ${TEST_PASS}" |tee -a ${RUN_LOG_FILE}


