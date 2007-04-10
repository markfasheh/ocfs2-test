#!/bin/bash
#
# Copyright (C) 2007 Oracle.  All rights reserved.
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
#
#
# Description: 	This script will perform a sanity check on tunefs.ocfs2
#
# Author: 	Marcos Matsunga (Marcos.Matsunaga@oracle.com)
#

. `dirname ${0}`/config.sh

MKFS_BIN=`which mkfs.ocfs2`
FSCK_BIN=`which fsck.ocfs2`
DEBUGFS_BIN=`which debugfs.ocfs2`
TUNEFS_BIN=`which tunefs.ocfs2`
MOUNTED_BIN=`which mounted.ocfs2`
TEE_BIN=`which tee`
GAWK=`which gawk`
GREP=`which grep`
LOG_DIR=${O2TDIR}/log
MKFSLOG=${LOG_DIR}/$$_mkfs.log
FSCKLOG=${LOG_DIR}/$$_fsck.log
TUNEFSLOG=${LOG_DIR}/$$_tunefs.log

BLOCKDEV=`which blockdev`
DEVICE=""

BLOCKSIZE=4k
CLUSTERSIZE=8k

LABEL1=ocfs2test
LABEL2=ocfs2test1
LABEL3=ocfs2test2

JOURNAL1=4194304
JOURNAL2=67108864
JOURNAL3=268435456

NNODES1=2
NNODES2=8
NNODES3=32

BLKCNT1=262144		# 1st blk count 1GB
BLKCNT2=1048576		# 2nd blk count 4GB
BLKCNT3=3145728		# 3rd blk count 12GB

#
# usage			Display help information and exit.
#
function usage()
{
	local script="${0##*/}"
	cat <<EOF
	Usage: ${script} [options] device

	Options:
	      --help                       display this help and exit
	      --log-dir=DIRECTORY          use the DIRECTORY to store the log
	      --with-fsck=PROGRAM          use the PROGRAM as fsck.ocfs2
	      --with-mkfs=PROGRAM          use the PROGRAM as mkfs.ocfs2
	      --with-mounted=PROGRAM       use the PROGRAM as mounted.ocfs2
	      --with-debugfs=PROGRAM       use the PROGRAM as debugfs.ocfs2
	      --with-tunefs=PROGRAM        use the PROGRAM as tunefs.ocfs2

	Examples:

	  ${script} --with-debugfs=../debugfs.ocfs2/debugfs.ocfs2 /dev/sde2
	  ${script} --with-mkfs=/sbin/mkfs.ocfs2 --log-dir=/tmp /dev/sde2
EOF
}
#
# check_executes
#
function check_executes()
{
	LogMsg "checking the programs we need in the test...";
	for PROGRAM in ${MKFS_BIN} ${FSCK_BIN} ${DEBUGFS_BIN} ${TUNEFS_BIN} $MOUNTED_BIN
	do
		which ${PROGRAM} 2>&1 >> ${LOGFILE}
		if [ "$?" != "0" ]; then
			LogMsg "${PROGRAM} not exist" 
			usage
			exit 1
		fi
	done
}
# 
# set_log_file
#
function set_log_file()
{
        if [ ! -d ${LOG_DIR} ]; then
	   mkdir -p ${LOG_DIR}
        fi;
	if [ ! -d ${LOG_DIR} ]; then
		LogMsg "log_dir[${LOG_DIR}] not exist, \c";
		LogMsg "use [${PWD}] instead.";
		LOG_DIR=${PWD}
	fi
	
	LOGFILE="${LOG_DIR}/`date +%F-%H-%M-%S`-tunefs_test.log"
#	exec 3>&1
#	exec 1>${LOGFILE} 2>&1

}
#
# LogMsg - Log messages to console and logfile.
#
LogMsg()
{
        echo -e "$@"| ${TEE_BIN} -a ${LOGFILE}
}
#
#       $1      The testcase output information 
#
function test_info()
{
        LogMsg "${CURRENT_TEST}\c";
        LogMsg " \c";
        local -i i=${#CURRENT_TEST} 
        while (( i < 60 ))
        do
                LogMsg ".\c" 
                (( ++i ))
        done
        LogMsg " \c"
        LogMsg "$@"
}
#
# test_pass     Testcase pass
#
function test_pass()
{
        (( ++NUM_OF_PASS ))
        test_info "PASS"
        CURRENT_TEST=""
}

#
# test_fail     Testcase fail and print out the stdout and stderr
#
function test_fail()
{
        (( ++NUM_OF_FAIL ))
        test_info "FAIL" 
        CURRENT_TEST=""
}
#
# test_summary
#
function test_summary()
{
        [ 0 -eq "${NUM_OF_TESTS}" ] && return
        [ "${CURRENT_TEST}" ] && test_broken

        LogMsg "=====================================\c"
	LogMsg "========================================" 
        LogMsg "Test Summary" 
        LogMsg "------------------------------------" 
        LogMsg "Number of tests:        ${NUM_OF_TESTS}" 
        LogMsg "Number of passed tests: ${NUM_OF_PASS}" 
        LogMsg "Number of failed tests: ${NUM_OF_FAIL}" 
        LogMsg "Number of broken tests: ${NUM_OF_BROKEN}" 
        LogMsg "=====================================\c"  
	LogMsg "========================================\n" 
	LogMsg "Logfiles for this execution can be found at :\n"
	LogMsg "General Logfile : ${LOGFILE}";
	LogMsg "FSCK Logfile    : ${FSCKLOG}";
	LogMsg "MKFS Logfile    : ${MKFSLOG}";
	LogMsg "TUNEFS Logfile  : ${TUNEFSLOG}";
}
#
# Set_Volume_For_Test - Initialize volume for tests. 
#     	S1 = blocks count (partition size)
#       S2 = "" or "--no-backup-super"
#
Set_Volume_For_Test()
{
	LogMsg "tunefs_test : Initializing volume for test"
        echo "y"| ${MKFS_BIN} -b ${BLOCKSIZE} -C ${CLUSTERSIZE} -L ${LABEL1} -N ${NNODES1} \
	        -J size=${JOURNAL1} ${2} ${DEVICE} ${1} 2>&1 >> ${MKFSLOG}
	Check_Volume;
}
#
# Check_Volume - run fsck on device and save some information
#
Check_Volume()
{
    	rm -f /tmp/fsck.$$
	${FSCK_BIN} -yf ${DEVICE} 2>&1 > /tmp/fsck.$$
	RC=$?;
   	cat /tmp/fsck.$$ >> ${FSCKLOG};
   	if [ ${RC} -ne 0 ]; then
    	   LogMsg "fsck.ocfs2 failed."
           LogMsg "Stopping test."
	   LogMsg "Current test (${CURRENT_TEST})"
	   (( ++NUM_OF_BROKEN ))
           exit;
	fi;
	SB_NBLOCKS=`${GREP} blocks: /tmp/fsck.$$ | ${GAWK} '{print \$4; exit}'`
	SB_BLOCKSIZE=`${GREP} block: /tmp/fsck.$$ | ${GAWK} '{print \$4; exit}'`
	SB_NCLUSTERS=`${GREP} clusters: /tmp/fsck.$$ | ${GAWK} '{print \$4; exit}'`
	SB_CLUSTERSIZE=`${GREP} cluster: /tmp/fsck.$$ | ${GAWK} '{print \$4; exit}'`
    	rm -f /tmp/fsck.$$
}
#
# Change_Volume_Label - Test Volume Label Change
#
Change_Volume_Label()
{
        LogMsg "tunefs_test : Testing Volume label change"
	(( ++NUM_OF_TESTS ))
	Set_Volume_For_Test ${BLKCNT1};
	CURRENT_TEST="Label to $LABEL2";
	echo "y"|${TUNEFS_BIN} -L $LABEL2 ${DEVICE} 2>&1 >> ${TUNEFSLOG};
	Check_Volume;
	SB_LABEL=`${DEBUGFS_BIN} -n -R "stats" ${DEVICE}|${GREP} Label|  \
		${GAWK} '{print \$2; exit}'`;
	if [ `basename ${SB_LABEL}` != "$LABEL2" ]; then
	   test_fail;
	   LogMsg "tunefs_test : Label change failed. \c"
	   LogMsg "Superblock Label (${SB_LABEL})"
	else
	   test_pass;
	fi;
#
	CURRENT_TEST="Label to ${LABEL3}";
	(( ++NUM_OF_TESTS ))
	echo "y"|${TUNEFS_BIN} -L ${LABEL3} ${DEVICE} 2>&1 >> ${TUNEFSLOG};
	Check_Volume;
	SB_LABEL=`${DEBUGFS_BIN} -n -R "stats" ${DEVICE}|${GREP} Label| \
		${GAWK} '{print \$2; exit}'`;
	if [ `basename ${SB_LABEL}` != "${LABEL3}" ]; then
	   test_fail;
	   LogMsg "tunefs_test : Label change failed. \c"
	   LogMsg "Superblock Label (${SB_LABEL})"
	else
	   test_pass;
	fi;
}
#
# Change_Number_of_Nodes - Test Number of nodes change
#
Change_Number_of_Nodes()
{
        LogMsg "tunefs_test : Testing Number of Nodes change"
	(( ++NUM_OF_TESTS ))
	Set_Volume_For_Test ${BLKCNT1};
	CURRENT_TEST="#of nodes to ${NNODES2}";
	echo "y"|${TUNEFS_BIN} -N ${NNODES2} ${DEVICE} 2>&1 >> ${TUNEFSLOG};
	Check_Volume;
	SB_NNODES=`${DEBUGFS_BIN} -n -R "stats" ${DEVICE}|${GREP} Slots| \
		 ${GAWK} '{print \$4; exit}'`;
	if [ ${SB_NNODES} -ne ${NNODES2} ]; then
	   test_fail;
	   LogMsg "tunefs_test : #of nodes change failed. \c"
	   LogMsg "Superblock number of nodes (${SB_NNODES})"
	else
	   test_pass;
	fi;
#
	CURRENT_TEST="#of nodes to ${NNODES3}";
	(( ++NUM_OF_TESTS ))
	echo "y"|${TUNEFS_BIN} -N ${NNODES3} ${DEVICE} 2>&1 >> ${TUNEFSLOG};
	Check_Volume;
	SB_NNODES=`${DEBUGFS_BIN} -n -R "stats" ${DEVICE}|${GREP} Slots| \
		 ${GAWK} '{print \$4; exit}'`;
	if [ ${SB_NNODES} -ne ${NNODES3} ]; then
	   test_fail;
	   LogMsg "tunefs_test : #of nodes change failed. \c"
	   LogMsg "Superblock number of nodes (${SB_NNODES})"
	else
	   test_pass;
	fi;
}
#
# Change_Journal_Size - Test Journal Size change.
#
Change_Journal_Size()
{
        LogMsg "tunefs_test : Testing Journal Size change"
	Set_Volume_For_Test ${BLKCNT2};
	CURRENT_TEST="Journal Size to ${JOURNAL2}";
	(( ++NUM_OF_TESTS ))
	echo "y"|${TUNEFS_BIN} -J size=${JOURNAL2} ${DEVICE} 2>&1 >> ${TUNEFSLOG};
	Check_Volume;
	SB_JSIZE=`${DEBUGFS_BIN} -n -R "ls -l //" ${DEVICE}|${GREP} -i Journal:0001| 
		${GAWK} '{print \$6; exit}'`;
	if [ ${SB_JSIZE} -ne ${JOURNAL2} ]; then
	   test_fail;
	   LogMsg "tunefs_test : Journal size change failed.\c"
	   LogMsg " Superblock Journal Size (${SB_JSIZE})"
	else
	   test_pass;
	fi;
#
	CURRENT_TEST="Journal Size to ${JOURNAL3}";
	(( ++NUM_OF_TESTS ))
	echo "y"|${TUNEFS_BIN} -J size=${JOURNAL3} ${DEVICE} 2>&1 >> ${TUNEFSLOG};
	Check_Volume;
	SB_JSIZE=`${DEBUGFS_BIN} -n -R "ls -l //" ${DEVICE}|${GREP} -i Journal:0001| 
		${GAWK} '{print \$6; exit}'`;
	if [ ${SB_JSIZE} -ne ${JOURNAL3} ]; then
	   test_fail;
	   LogMsg "tunefs_test : Journal size change failed. Superblock \c"
	   LogMsg "Journal Size (${SB_JSIZE})";
	else
	   test_pass;
	fi;
}
#
# Change_Partition_Size - Test Partition size change
#
Change_Partition_Size()
{
        LogMsg "tunefs_test : Testing Partition Size change"
	Set_Volume_For_Test ${BLKCNT1};
	CURRENT_TEST="Partition Size to ${BLKCNT2}";
	(( ++NUM_OF_TESTS ))
	echo "y"|${TUNEFS_BIN} -S ${DEVICE} ${BLKCNT2} 2>&1 >> ${TUNEFSLOG};
	Check_Volume;
	if [ ${SB_NBLOCKS} -ne ${BLKCNT2} ]; then
	   test_fail;
	   LogMsg "tunefs_test : Partition size change failed. Superblock \c"
	   LogMsg "Partition Size (${SB_NBLOCKS})";
	else
	   test_pass;
	fi;
#
	CURRENT_TEST="Partition Size to ${BLKCNT3}";
	(( ++NUM_OF_TESTS ))
	echo "y"|${TUNEFS_BIN} -S ${DEVICE} ${BLKCNT3} 2>&1 >> ${TUNEFSLOG};
	Check_Volume;
	if [ ${SB_NBLOCKS} -ne ${BLKCNT3} ]; then
	   test_fail;
	   LogMsg "tunefs_test : Partition size change failed. Superblock \c"
	   LogMsg "Partition Size (${SB_NBLOCKS})";
	else
	   test_pass;
	fi;
}
#
# Change_UUID - Test UUID Generation
#
Change_UUID()
{
        LogMsg "tunefs_test : UUID change"
	Set_Volume_For_Test ${BLKCNT1};
	CURRENT_TEST="UUID Change 1";
	(( ++NUM_OF_TESTS ))
	OLD_UUID=`${DEBUGFS_BIN} -n -R "stats" ${DEVICE}|${GREP} UUID:| \
		${GAWK} '{print \$2; exit}'`;
	echo "y"|${TUNEFS_BIN} -U ${DEVICE} 2>&1 >> ${TUNEFSLOG};
	Check_Volume;
	SB_UUID=`${DEBUGFS_BIN} -n -R "stats" ${DEVICE}|${GREP} UUID:| \
		${GAWK} '{print \$2; exit}'`;
	if [ "${SB_UUID}" == "${OLDUUID}" ]; then
	   test_fail;
	   LogMsg "tunefs_test : UUID change failed. Superblock \c"
	   LogMsg "UUID (${SB_UUID})";
	   LogMsg "OLD_UUID ($OLD_UUID)";
	else
	   test_pass;
	fi;
#
   	OLD_UUID=${SB_UUID};
	CURRENT_TEST="UUID Change 2";
	(( ++NUM_OF_TESTS ))
	echo "y"|${TUNEFS_BIN} -U ${DEVICE} 2>&1 >> ${TUNEFSLOG};
	Check_Volume;
	SB_UUID=`${DEBUGFS_BIN} -n -R "stats" ${DEVICE}|${GREP} UUID:| \
		${GAWK} '{print \$2; exit}'`;
	if [ "${SB_UUID}" == "${OLDUUID}" ]; then
	   test_fail;
	   LogMsg "tunefs_test : UUID change failed. Superblock \c"
	   LogMsg "UUID (${SB_UUID})";
	   LogMsg "OLD_UUID ($OLD_UUID)";
	else
	   test_pass;
	fi;
}
#
# Change_Mount_Type - Test change of mount type to local.
#
Change_Mount_Type()
{
        LogMsg "tunefs_test : Mount Type change"
	Set_Volume_For_Test ${BLKCNT1};
	CURRENT_TEST="Mount Type change to local";
	(( ++NUM_OF_TESTS ))
	echo "y"|${TUNEFS_BIN} -M local ${DEVICE} 2>&1 >> ${TUNEFSLOG};
	Check_Volume;
	SB_MTYPE=`${DEBUGFS_BIN} -n -R "stats" ${DEVICE}|${GREP} Incompat:| \
		${GAWK} '{print \$3; exit}'`;
	if [ ${SB_MTYPE} -ne 8 ]; then
	   test_fail;
	   LogMsg "tunefs_test : Mount Type change failed. Superblock \c"
	   LogMsg "Feature Incompat (${SB_MTYPE})";
	else
	   test_pass;
	fi;
}
#
# Add_Backup_Super - Add backup super to a device created without it.
#
Add_Backup_Super()
{
        LogMsg "tunefs_test : Testing Adding backup-super"
	Set_Volume_For_Test "" "--no-backup-super";
	CURRENT_TEST="Add backup-super"
	(( ++NUM_OF_TESTS ))
	echo "y"|${TUNEFS_BIN} --backup-super ${DEVICE} 2>&1 >> ${TUNEFSLOG};
	Check_Volume;
	SB_BSUPER=`${DEBUGFS_BIN} -n -R "stats" ${DEVICE}|${GREP} Compat:| \
		${GAWK} '{print \$3; exit}'`;
	if [ ${SB_BSUPER} -ne 1 ]; then
	   test_fail;
	   LogMsg "tunefs_test : Change to backup super failed. Superblock \c"
	   LogMsg "Feature Compat (${SB_BSUPER})";
	else
	   test_pass;
	fi;
}
################################################################

#
# main
#
#
if [ "$#" -eq "0" ]
then
	usage
	exit 255
fi

while [ "$#" -gt "0" ]
do
	case "$1" in
	"--help")
		usage
		exit 255
		;;
	"--log-dir="*)
		LOG_DIR="${1#--log-dir=}"
		;;
	"--with-fsck="*)
		FSCK_BIN="${1#--with-fsck=}"
		;;
	"--with-mkfs="*)
		MKFS_BIN="${1#--with-mkfs=}"
		;;
	"--with-debugfs="*)
		DEBUGFS_BIN="${1#--with-debugfs=}"
		;;
	"--with-tunefs="*)
		TUNEFS_BIN="${1#--with-tunefs=}"
		;;
	*)
		DEVICE="$1"
		;;
	esac
	shift
done

if [ ! -b "${DEVICE}" ]; then
	LogMsg "invalid block device - ${DEVICE}" 
	usage
	exit 1
fi

sect_total=`${BLOCKDEV} --getsize ${DEVICE}`
byte_total=`expr ${sect_total} \* 512`

# We must have at least a 5GB volume size
min_vol_size=`expr ${FIRST_BACKUP_OFF} + 1048576`
if [ `expr ${byte_total}` -lt `expr ${min_vol_size}` ]; then
	LogMsg "${DEVICE} is too small for our test. \c"
	LogMsg "We need ${min_vol_size} at least"
	exit 1
fi

declare -i NUM_OF_TESTS=0
declare -i NUM_OF_PASS=0
declare -i NUM_OF_FAIL=0
declare -i NUM_OF_BROKEN=0


set_log_file

LogMsg "\ntunefs_test: Starting test \c"
LogMsg "(`date +%F-%H-%M-%S`)\n\n"

check_executes

Change_Volume_Label

Change_Number_of_Nodes

Change_Journal_Size

Change_Partition_Size

Change_UUID

Change_Mount_Type

Add_Backup_Super

test_summary

LogMsg "\ntunefs_test: Ending test (`date +%F-%H-%M-%S`)\n" 

exit 0
