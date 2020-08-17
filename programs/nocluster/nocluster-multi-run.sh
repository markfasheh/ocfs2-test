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
# File :	nocluster-multi-run.sh
#
# Description:	The wrapper script help to run the nocluster option test with
# 		various settings,to perform the utility,fucntional,stress test.
#		NOTE:need to have openmpi configured and passwordless rsh/ssh
#		access.
#
# Author:       Gang He,	ghe@suse.com
#
# History:      12 August 2020
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
HOSTNAME=`hostname`

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
IP_BIN="`which sudo` -u root `which ip`"

LABEL="ocfs2-nocluster-multi-test-`uname -m`"
SLOTS=2
DEFAULT_LOG="multiple-nocluster-test-logs"
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
		${IP_BIN} addr show ${INTERFACE} >/dev/null 2>&1 || {
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
	LOG_FILE="`dirname ${LOG_OUT_DIR}`/`basename ${LOG_OUT_DIR}`/nocluster-multiple-test-log-`uname -m`-${LOG_SUFIX}.log"
	RUN_LOG_FILE="`dirname ${LOG_OUT_DIR}`/`basename ${LOG_OUT_DIR}`/nocluster-multiple-test-log-run-`uname -m`-${LOG_SUFIX}.log"

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

	WORKPLACE="`dirname ${MOUNT_POINT}`/`basename ${MOUNT_POINT}`/multi_nocluster_test_place"
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

f_do_mount()
{
	if [ -n "$1" ] ; then
		echo -n "Mounting device ${OCFS2_DEVICE} with nocluster to node(${HOSTNAME}):"|tee -a ${RUN_LOG_FILE}
		echo y|${MOUNT_BIN} -L ${LABEL} -o nocluster ${MOUNT_POINT}>>${RUN_LOG_FILE} 2>&1
		RET=$?
	else
		echo -n "Mounting device ${OCFS2_DEVICE} to nodes(${MPI_HOSTS}):"|tee -a ${RUN_LOG_FILE}
		${REMOTE_MOUNT_BIN} -l ${LABEL} -m ${MOUNT_POINT} -n ${MPI_HOSTS}>>${RUN_LOG_FILE} 2>&1
		RET=$?
	fi
	echo_status ${RET} |tee -a ${RUN_LOG_FILE}
        exit_or_not ${RET}

	${SUDO} chown -R ${USERNAME}:${GROUPNAME} ${MOUNT_POINT}
	${SUDO} chmod -R 777 ${MOUNT_POINT}

}

f_do_umount()
{
	if [ -n "$1" ] ; then
		echo -n "Umounting device ${OCFS2_DEVICE} from node(${HOSTNAME}):"|tee -a ${RUN_LOG_FILE}
		${UMOUNT_BIN} ${MOUNT_POINT}>>${RUN_LOG_FILE} 2>&1
		RET=$?
	else
		echo -n "Umounting device ${OCFS2_DEVICE} from nodes(${MPI_HOSTS}):"|tee -a ${RUN_LOG_FILE}
		${REMOTE_UMOUNT_BIN} -m ${MOUNT_POINT} -n ${MPI_HOSTS}>>${RUN_LOG_FILE} 2>&1
		RET=$?
	fi
	echo_status ${RET} |tee -a ${RUN_LOG_FILE}
	exit_or_not ${RET}

}

f_create_files()
{
	local fname=
	local fblks=
	echo -n "Create random files:"|tee -a ${RUN_LOG_FILE}
	for((i=0;i<100;i++));do
		fname=${WORKPLACE}/dd.${i}
		fblks=`expr $RANDOM % 100 + 100`
		dd if=/dev/random iflag=fullblock of=$fname bs=4k count=$fblks >>${RUN_LOG_FILE} 2>&1
		RET=$?
		if [ ! "$RET" == "0"  ];then
			break
		fi
	done
	echo_status ${RET} |tee -a ${RUN_LOG_FILE}
	exit_or_not ${RET}

	ls ${WORKPLACE}/dd.* | xargs md5sum > ${TMP_DIR}/nocluster.md5
}

f_check_files()
{
	echo -n "Check random files:"|tee -a ${RUN_LOG_FILE}
	ls ${WORKPLACE}/dd.* | xargs md5sum > ${TMP_DIR}/nocluster.tmp
	diff ${TMP_DIR}/nocluster.tmp ${TMP_DIR}/nocluster.md5 >>${RUN_LOG_FILE} 2>&1
	RET=$?
	echo_status ${RET} |tee -a ${RUN_LOG_FILE}
	exit_or_not ${RET}
}

f_runtest()
{

	((TEST_NO++))
	echo >>${LOG_FILE}
	echo "==========================================================">>${LOG_FILE}
        echo -e "[${TEST_NO}] Check nocluster option on Ocfs2:"|tee -a ${RUN_LOG_FILE}
        echo -e "[${TEST_NO}] Check nocluster option on Ocfs2:">>${LOG_FILE}
        echo "==========================================================">>${LOG_FILE}

	for((k=0;k<10;k++));do

		# cluster mount
		f_create_files
		f_do_umount

		# nocluster mount
		f_do_mount 1
		f_check_files
		f_create_files
		f_do_umount 1

		# cluster mount again
		f_do_mount
		f_check_files
	done

	${RM} -rf ${WORKPLACE} || exit 1
	((TEST_PASS++))
	echo_status 0 >>${LOG_FILE}
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
echo "=====================Multiple nodes nocluster testing starts: `date`=====================" |tee -a ${RUN_LOG_FILE}
echo "=====================Multiple nodes nocluster testing starts: `date`=====================" >> ${LOG_FILE}

for BLOCKSIZE in $(echo "$bslist")
do
	for CLUSTERSIZE in $(echo "$cslist")
        do
                echo "++++++++++nocluster tests with \"-b ${BLOCKSIZE} -C ${CLUSTERSIZE}\"++++++++++" |tee -a ${RUN_LOG_FILE}
                echo "++++++++++nocluster tests with \"-b ${BLOCKSIZE} -C ${CLUSTERSIZE}\"++++++++++">>${LOG_FILE}
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
echo "=====================Multiple nodes nocluster testing ends: `date`=====================" |tee -a ${RUN_LOG_FILE}
echo "=====================Multiple nodes nocluster testing ends: `date`=====================" >> ${LOG_FILE}

echo "Time elapsed(s): $((${END_TIME}-${START_TIME}))" |tee -a ${RUN_LOG_FILE}
echo "Tests total: ${TEST_NO}" |tee -a ${RUN_LOG_FILE}
echo "Tests passed: ${TEST_PASS}" |tee -a ${RUN_LOG_FILE}

