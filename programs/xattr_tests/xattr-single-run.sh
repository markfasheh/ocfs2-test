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

################################################################################
# Global Variables
################################################################################
PATH=$PATH:/sbin      # Add /sbin to the path for ocfs2 tools
export PATH=$PATH:.

. `dirname ${0}`/config.sh

GETXATTR="`which sudo` -u root `which getfattr`"
SETXATTR="`which sudo` -u root `which setfattr`"

USERNAME=`id -un`
GROUPNAME=`id -gn`

SUDO="`which sudo` -u root"
RM="`which rm`"
MKDIR="`which mkdir`"
TOUCH_BIN="`which touch`"
DD_BIN="`which dd`"
MOUNT_BIN="`which sudo` -u root `which mount`"
UMOUNT_BIN="`which sudo` -u root `which umount`"
MKFS_BIN="`which sudo` -u root `which mkfs.ocfs2`"
DEBUGFS_BIN="`which sudo` -u root `which debugfs.ocfs2`"
XATTR_TEST_BIN="`which sudo` -u root ${BINDIR}/xattr-test"

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

TEST_NO=0
TEST_PASS=0

COMBIN_TEST=
declare -i MAX_SMALL_INLINE_XATTR
declare -i MAX_LARGE_INLINE_XATTR
declare -i MAX_SMALL_BLOCK_XATTR
declare -i MAX_LARGE_BLOCK_XATTR

declare -i MAX_INLINE_DATA
declare -i MAX_INLINE_XATTR

declare -i ITERATIONS
declare -i EA_NUMS
declare -i EA_NAME_LEN
declare -i EA_VALUE_SIZE

declare -i i

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
    echo "usage: `basename ${0}` [-c] [-o output_log_dir] <-d <device>> <-b <block size>> <-C <cluster size>> <mountpoint path>"
    echo "	 -c enable the combination test for inline-data and inline-xattr."
    echo "       -o output directory for the logs"
    echo "       -d specify the device which has been formated as an ocfs2 volume."
    echo "	 -b block size."
    echo "	 -C cluster size."
    echo "       <mountpoint path> path of mountpoint where the ocfs2 volume will be mounted on."
    exit 1;

}

f_getoptions()
{
	 if [ $# -eq 0 ]; then
                f_usage;
                exit 1
         fi

	 while getopts "cho:d:b:C:" options; do
                case $options in
		c ) COMBIN_TEST="1";;
                o ) LOG_OUT_DIR="$OPTARG";;
                d ) OCFS2_DEVICE="$OPTARG";;
		b ) BLOCKSIZE="$OPTARG";;
		C ) CLUSTERSIZE="$OPTARG";;
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

	if [ -z "${COMBIN_TEST}" ];then
		echo y|${MKFS_BIN} --fs-features=xattr -b ${BLOCKSIZE} -C ${CLUSTERSIZE} -N 1 ${OCFS2_DEVICE} ${BLOCKNUMS}>>${RUN_LOG_FILE} 2>&1
	else
		echo y|${MKFS_BIN} --fs-features=xattr,inline-data -b ${BLOCKSIZE} -C ${CLUSTERSIZE} -N 1 ${OCFS2_DEVICE} ${BLOCKNUMS}>>${RUN_LOG_FILE} 2>&1
	fi
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

f_is_xattr_inlined()
{
	#${1} is test file
	#${2} is target volume

	${DEBUGFS_BIN} -R "xattr ${1}" ${2}|grep -qi "block" || {
		return 0
	}

	return 1
}

f_is_xattr_bucketed()
{
	#${1} is test file
	#${2} is target volume

	${DEBUGFS_BIN} -R "xattr ${1}" ${2}|grep -qi "bucket" || {
		return 0
	}

	return 1
}

#
# Insert xattr entries with given number and mode
# ${1} specify entry type(small or large)
# ${2} specify file name
# ${3} specify entry number
#
f_insert_xattrs()
{
	local XATTR_NAME_PREFIX=
	local XATTR_VALUE_PREFIX=
	local FILENAME=${2}
	local ENTRY_NUM=${3}
	local XATTR_NAME=
	local XATTR_VALUE=
	local LARGE_VALUE_LESS_THAN_80="largelargelargelargelargelargelargelargelargelargelargelargelargelargelarge."
	

	if [ "x${1}" = "xsmall" ];then
		XATTR_NAME_PREFIX="user."
		XATTR_VALUE_PREFIX=""
	else
		XATTR_NAME_PREFIX="user.large."
		XATTR_VALUE_PREFIX=${LARGE_VALUE_LESS_THAN_80}
		
	fi

	for i in $(seq ${ENTRY_NUM});do
		XATTR_NAME="${XATTR_NAME_PREFIX}${i}"
		XATTR_VALUE="${XATTR_VALUE_PREFIX}${i}"
		${SETXATTR} -n ${XATTR_NAME} -v ${XATTR_VALUE} ${FILENAME} >>${DETAIL_LOG_FILE} 2>&1
		exit_or_not $?
	done

	return $?
	
}

#
# Make inline-xattr firstly extented with small or large files increasing.
# ${1} stands for entry type(small or large)
# ${2} stands for file name
# ${3} should be block or bucket
#	block:	extend inlined xattr to block.
#	xattr: 	extend block to bucket.
#
f_extend_xattr()
{
	local XATTR_NAME_PREFIX=
	local XATTR_VALUE_PREFIX=
	local FILENAME=${2}
	local EXTEND_TYPE=${3}
	local XATTR_NAME=
	local XATTR_VALUE=
	local LARGE_VALUE_LESS_THAN_80="largelargelargelargelargelargelargelargelargelargelargelargelargelargelarge."
	

	if [ "x${1}" = "xsmall" ];then
		XATTR_NAME_PREFIX="user."
		XATTR_VALUE_PREFIX=""
	else
		XATTR_NAME_PREFIX="user.large."
		XATTR_VALUE_PREFIX=${LARGE_VALUE_LESS_THAN_80}
		
	fi

	i=1
	while : ;do
		XATTR_NAME="${XATTR_NAME_PREFIX}${i}"
		XATTR_VALUE="${XATTR_VALUE_PREFIX}${i}"
		${SETXATTR} -n ${XATTR_NAME} -v ${XATTR_VALUE} ${FILENAME} >>${DETAIL_LOG_FILE} 2>&1
		exit_or_not $?
		sync

		if [ "x${EXTEND_TYPE}" = "xblock" ];then
			f_is_xattr_inlined ${DEBUG_TEST_FILE} ${OCFS2_DEVICE} || {
				break
			}
		else

			f_is_xattr_bucketed ${DEBUG_TEST_FILE} ${OCFS2_DEVICE} || {
				break
			}
			
		fi

		((i++))
	done

	return $?
}

f_get_max_inline_size()
{
	TMP_FILE=${MOUNT_POINT}/.xattr-inline-data-tmp-file.$$
	DEBUG_TMP_FILE=/.xattr-inline-data-tmp-file.$$

	${TOUCH_BIN} ${TMP_FILE}

	${DD_BIN} if=/dev/zero of=${TMP_FILE} bs=1 count=1 &>/dev/null
	sync

	MAX_INLINE_DATA=`${DEBUGFS_BIN} -R "stat ${DEBUG_TMP_FILE}" ${OCFS2_DEVICE} | grep -i inline | grep -i data | grep -i max | awk '{print $4}'`

	${SETXATTR} -n "user.test" -v "test" ${TMP_FILE}
	sync

	MAX_INLINE_XATTR=`${DEBUGFS_BIN} -R "stat ${DEBUG_TMP_FILE}" ${OCFS2_DEVICE} | grep 'Extended Attributes Inline Size' | awk '{print $9}'`

	${RM} -rf ${TMP_FILE}
}

f_combin_test()
{
	SUB_TESTNO=1
	TEST_FILE=${WORKPLACE}/xattr-inline-data-combin-test-file
	DEBUG_TEST_FILE=/xattr_test_place/xattr-inline-data-combin-test-file

	LARGE_VALUE_LESS_THAN_80="largelargelargelargelargelargelargelargelargelargelargelargelargelargelarge"
	LARGE_VALUE_MORE_THAN_80="largelargelargelargelargelargelargelargelargelargelargelargelargelargelargelargelarge"

	echo "Test ${SUB_TESTNO}: Inline Data&Xattr Filling Up Test." >>${DETAIL_LOG_FILE}
	f_get_max_inline_size
	${TOUCH_BIN} ${TEST_FILE}

	${DD_BIN} if=/dev/zero of=${TEST_FILE} bs=1 count=$((${MAX_INLINE_DATA}-${MAX_INLINE_XATTR})) 2>>${DETAIL_LOG_FILE} >/dev/null
	exit_or_not $?

	f_insert_xattrs "small" ${TEST_FILE} ${MAX_SMALL_INLINE_XATTR}
	exit_or_not $?

	# Extend the inline-xattr and inline-data
	echo "e">>${TEST_FILE}
	${SETXATTR} -n "user.small" -v "SMALL" ${TEST_FILE}
	exit_or_not $?

	#Delete last EA entry to shrink.
	${SETXATTR} -x "user.small" ${TEST_FILE}
	exit_or_not $?

	#Truncate inline-data to shrink.
	${DD_BIN} if=/dev/zero of=${TEST_FILE} bs=1 count=$((${MAX_INLINE_DATA}-${MAX_INLINE_XATTR})) 2>>${DETAIL_LOG_FILE} >/dev/null
	exit_or_not $?

	#Replace last EA entry to extend.
	XATTR_NAME="user.${i}"
	${SETXATTR} -n "${XATTR_NAME}" -v "${LARGE_VALUE_LESS_THAN_80}" ${TEST_FILE}
	exit_or_not $?
	
	${RM} -rf ${TEST_FILE}
	${TOUCH_BIN} ${TEST_FILE}
	((SUB_TESTNO++))

	echo "Test ${SUB_TESTNO}: Disable&Enable Inline Xattr Test." >>${DETAIL_LOG_FILE}
	${DD_BIN} if=/dev/zero of=${TEST_FILE} bs=1 count=$((${MAX_INLINE_DATA}-${MAX_INLINE_XATTR}+1)) 2>>${DETAIL_LOG_FILE} >/dev/null
	${SETXATTR} -n "user.small" -v "SMALL" ${TEST_FILE}
	exit_or_not $?

	sync
	${DEBUGFS_BIN} -R "xattr ${DEBUG_TEST_FILE}" ${OCFS2_DEVICE}|grep -qi block || {
		echo "Xattr entry inserted here should be extended into outside block.">>${DETAIL_LOG_FILE}
		return 1
	}

	${DD_BIN} if=/dev/zero of=${TEST_FILE} bs=1 count=$((${MAX_INLINE_DATA}-${MAX_INLINE_XATTR})) 2>>${DETAIL_LOG_FILE} >/dev/null
	${SETXATTR} -n "user.small" -v "SMALL" ${TEST_FILE}
	exit_or_not $?

	sync
	${DEBUGFS_BIN} -R "stat ${DEBUG_TEST_FILE}" ${OCFS2_DEVICE}|grep -qi InlineXattr || {
		echo "Xattr entry inserted here should be inlined.">>${DETAIL_LOG_FILE}
		return 1
	}

	${RM} -rf ${TEST_FILE}
	${TOUCH_BIN} ${TEST_FILE}
	((SUB_TESTNO++))

	echo "Test ${SUB_TESTNO}: Inline Xattr Reservation Test." >>${DETAIL_LOG_FILE}
	# Reserve 256 bytes for inline-xattr
	${SETXATTR} -n "user.small" -v "SMALL" ${TEST_FILE}
	exit_or_not $?

	${DD_BIN} if=/dev/zero of=${TEST_FILE} bs=1 count=$((${MAX_INLINE_DATA}-${MAX_INLINE_XATTR})) 2>>${DETAIL_LOG_FILE} >/dev/null
	echo "a" >> ${TEST_FILE}

	sync
	${DEBUGFS_BIN} -R "stat ${DEBUG_TEST_FILE}" ${OCFS2_DEVICE}|grep -qi InlineData && {
		echo "Inline data should not invade reserved inline-xattr space.xxxxx">>${DETAIL_LOG_FILE}
		return 1
	}

	${RM} -rf ${TEST_FILE}
	${TOUCH_BIN} ${TEST_FILE}

	${SETXATTR} -n "user.small" -v "SMALL" ${TEST_FILE}
	exit_or_not $?

	${DD_BIN} if=/dev/zero of=${TEST_FILE} bs=$((${MAX_INLINE_DATA}-${MAX_INLINE_XATTR}+1))  count=1 2>>${DETAIL_LOG_FILE} >/dev/null

	sync
	${DEBUGFS_BIN} -R "stat ${DEBUG_TEST_FILE}" ${OCFS2_DEVICE}|grep -qi InlineData && {
		echo "Inline data should not invade a reserved inline-xattr space.">>${DETAIL_LOG_FILE}
		return 1
	}

	${RM} -rf ${TEST_FILE}
	${TOUCH_BIN} ${TEST_FILE}

	${SETXATTR} -n "user.small" -v "SMALL" ${TEST_FILE}
	exit_or_not $?

	${SETXATTR} -x "user.small" ${TEST_FILE}
	exit_or_not $?

	${DD_BIN} if=/dev/zero of=${TEST_FILE} bs=1 count=$((${MAX_INLINE_DATA}-${MAX_INLINE_XATTR}+1)) 2>>${DETAIL_LOG_FILE} >/dev/null
	exit_or_not $?

	sync
	${DEBUGFS_BIN} -R "stat ${DEBUG_TEST_FILE}" ${OCFS2_DEVICE}|grep -i InlineData && {
		echo "Inline data should not invade reserved inline-xattr space.yyyy">>${DETAIL_LOG_FILE}
		retun 1
	}

	${RM} -rf ${TEST_FILE}
	((SUB_TESTNO++))

	return 0
}

f_add_func_test()
{
	SUB_TESTNO=1
	TEST_FILE=${WORKPLACE}/additional-func-test-file
	DEBUG_TEST_FILE=/xattr_test_place/additional-func-test-file
	LARGE_VALUE_LESS_THAN_80="largelargelargelargelargelargelargelargelargelargelargelargelargelargelarge"
	LARGE_VALUE_MORE_THAN_80="largelargelargelargelargelargelargelargelargelargelargelargelargelargelargelargelarge"
	
	${TOUCH_BIN} ${TEST_FILE}

	echo "Test ${SUB_TESTNO}: None Xattr Test.">>${DETAIL_LOG_FILE}

	DUMMY_NAME="user.dummy"
	EMPTY_NAME=" "
	INVALID_NAME="user."

	for ea_name in ${DUMMY_NAME} ${EMPTY_NAME} ${INVALID_NAME};do
		${GETXATTR} -n ${ea_name} ${TEST_FILE} >>${DETAIL_LOG_FILE} 2>&1
		RET=$?
	done;

	((SUB_TESTNO++))
	
	echo "Test ${SUB_TESTNO}: Simple In-inode-xattr Test.">>${DETAIL_LOG_FILE}
	# Add a small ea in inode
	XATTR_NAME="user.small"
	XATTR_VALUE="smallvalue"

	${SETXATTR} -n ${XATTR_NAME} -v ${XATTR_VALUE} ${TEST_FILE} >>${DETAIL_LOG_FILE} 2>&1
	exit_or_not $?

	${SETXATTR} -x ${XATTR_NAME} ${TEST_FILE} >>${DETAIL_LOG_FILE} 2>&1
	exit_or_not $?

	# Add a large ea in inode
	XATTR_NAME="user.large"
	XATTR_VALUE=${LARGE_VALUE_LESS_THAN_80}

	${SETXATTR} -n ${XATTR_NAME} -v ${XATTR_VALUE} ${TEST_FILE} >>${DETAIL_LOG_FILE} 2>&1
	exit_or_not $?

	${SETXATTR} -x ${XATTR_NAME} ${TEST_FILE} >>${DETAIL_LOG_FILE} 2>&1
	exit_or_not $?

	((SUB_TESTNO++))

	echo "Test ${SUB_TESTNO}: In inode EA extension test.">>${DETAIL_LOG_FILE}
	# Get very ready for extension after a small ea insertion
	f_extend_xattr "small" ${TEST_FILE} "block"
	MAX_SMALL_INLINE_XATTR=$((${i}-1))
	exit_or_not $?

	${RM} -rf ${TEST_FILE}
	${TOUCH_BIN} ${TEST_FILE}

	# Get very ready for extension after a large ea insertion
	f_extend_xattr "large" ${TEST_FILE} "block"
	MAX_LARGE_INLINE_XATTR=$((${i}-1))
	exit_or_not $?

	${RM} -rf ${TEST_FILE}
	${TOUCH_BIN} ${TEST_FILE}

	# Replace a small ea to a large one to extend 
	f_extend_xattr "small" ${TEST_FILE} "block"
	exit_or_not $?

	XATTR_NAME="user.${i}"

	${SETXATTR} -x ${XATTR_NAME} ${TEST_FILE} >>${DETAIL_LOG_FILE} 2>&1
	exit_or_not $?

	#Large xattr value less than 80
	XATTR_VALUE=${LARGE_VALUE_LESS_THAN_80}${i}

	${SETXATTR} -n ${XATTR_NAME} -v ${XATTR_VALUE} ${TEST_FILE} >>${DETAIL_LOG_FILE} 2>&1
	exit_or_not $?

	${RM} -rf ${TEST_FILE}
	${TOUCH_BIN} ${TEST_FILE}
	
	f_extend_xattr "small" ${TEST_FILE} "block"
	exit_or_not $?

	XATTR_NAME="user.${i}"

	${SETXATTR} -x ${XATTR_NAME} ${TEST_FILE} >>${DETAIL_LOG_FILE} 2>&1
	exit_or_not $?

	#Large xattr value more than 80
	XATTR_VALUE=${LARGE_VALUE_MORE_THAN_80}${i}
	${SETXATTR} -n ${XATTR_NAME} -v ${XATTR_VALUE} ${TEST_FILE} >>${DETAIL_LOG_FILE} 2>&1
	exit_or_not $?

	${RM} -rf ${TEST_FILE}
	${TOUCH_BIN} ${TEST_FILE}

	((SUB_TESTNO++))

	echo "Test ${SUB_TESTNO}: Outside Xattr Shrink Test.">>${DETAIL_LOG_FILE}
	f_extend_xattr "small" ${TEST_FILE} "block"
	exit_or_not $?

	# Add a small ea in external block
	XATTR_NAME="user.small"
	XATTR_VALUE="SMALL"

	${SETXATTR} -n ${XATTR_NAME} -v ${XATTR_VALUE} ${TEST_FILE} >>${DETAIL_LOG_FILE} 2>&1
	exit_or_not $?

	# Add a large ea in external block
	XATTR_NAME="user.large"
	XATTR_VALUE=${LARGE_VALUE_LESS_THAN_80}

	${SETXATTR} -n ${XATTR_NAME} -v ${XATTR_VALUE} ${TEST_FILE} >>${DETAIL_LOG_FILE} 2>&1
	exit_or_not $?

	${RM} -rf ${TEST_FILE}
	${TOUCH_BIN} ${TEST_FILE}

	# Replace a large entry with a small one to shrink
	f_extend_xattr "large" ${TEST_FILE} "block"

	# Here we verify a bug when updating a boundary entry without shrinking.
	XATTR_NAME="user.large.${i}"
	XATTR_VALUE="${LARGE_VALUE_LESS_THAN_80}.${i}"

	${SETXATTR} -n ${XATTR_NAME} -v ${XATTR_VALUE} ${TEST_FILE} >>${DETAIL_LOG_FILE} 2>&1
	exit_or_not $?

	XATTR_NAME="user.large.${i}"
	XATTR_VALUE="${i}"

	${SETXATTR} -n ${XATTR_NAME} -v ${XATTR_VALUE} ${TEST_FILE} >>${DETAIL_LOG_FILE} 2>&1
	exit_or_not $?

	${RM} -rf ${TEST_FILE}
	${TOUCH_BIN} ${TEST_FILE}

	# Replace a large entry with also a large one(value size > 80) to shrink
	# Since it should reserve the space on inode more the 48 bytes, we need more elaborate operations.
	${SETXATTR} -n "user.1" -v "1" ${TEST_FILE}
	${SETXATTR} -n "user.2" -v "2" ${TEST_FILE}
	${SETXATTR} -n "user.large.1" -v ${LARGE_VALUE_LESS_THAN_80} ${TEST_FILE} 
	${SETXATTR} -n "user.large.2" -v ${LARGE_VALUE_LESS_THAN_80} ${TEST_FILE} 
	exit_or_not $?

	XATTR_NAME="user.large.2"
	# Make value size more than 80 here
	XATTR_VALUE=${LARGE_VALUE_MORE_THAN_80}

	${SETXATTR} -n ${XATTR_NAME} -v ${XATTR_VALUE} ${TEST_FILE} >>${DETAIL_LOG_FILE} 2>&1
	exit_or_not $?

	${RM} -rf ${TEST_FILE}
	${TOUCH_BIN} ${TEST_FILE}

	((SUB_TESTNO++))

	echo "Test ${SUB_TESTNO}: Basic Bucket Extension Test.">>${DETAIL_LOG_FILE}
	echo "Extension test by small xattr increament.">>${DETAIL_LOG_FILE}
	f_extend_xattr "small" ${TEST_FILE} "bucket"
	MAX_SMALL_BLOCK_XATTR=$((${i}-1))
	exit_or_not $?

	echo "Clean up file">>${DETAIL_LOG_FILE}

	${RM} -rf ${TEST_FILE}
	${TOUCH_BIN} ${TEST_FILE}

	echo "Extension test by large xattr increament.">>${DETAIL_LOG_FILE}
	f_extend_xattr "large" ${TEST_FILE} "bucket"
	MAX_LARGE_BLOCK_XATTR=$((${i}-1))
	exit_or_not $?

	echo "Clean up file">>${DETAIL_LOG_FILE}

	${RM} -rf ${TEST_FILE}
	${TOUCH_BIN} ${TEST_FILE}

	# Get very ready to extend block to bucket.
	echo " Get very ready to extend block to bucket.">>${DETAIL_LOG_FILE}

	f_insert_xattrs "small" ${TEST_FILE} ${MAX_SMALL_BLOCK_XATTR}
	exit_or_not $?

	# Replace a small entry with a large one,less than 80
	echo "Replace a small entry with a large one,less than 80">>${DETAIL_LOG_FILE}
	XATTR_NAME=user.${i}
	XATTR_VALUE=${LARGE_VALUE_LESS_THAN_80}

	${SETXATTR} -n ${XATTR_NAME} -v ${XATTR_VALUE} ${TEST_FILE} >>${DETAIL_LOG_FILE} 2>&1
	exit_or_not $?

	echo "Clean up file">>${DETAIL_LOG_FILE}

	${RM} -rf ${TEST_FILE}
	${TOUCH_BIN} ${TEST_FILE}

	echo " Get very ready to extend block to bucket again.">>${DETAIL_LOG_FILE}
	f_insert_xattrs "small" ${TEST_FILE} ${MAX_SMALL_BLOCK_XATTR}
	exit_or_not $?

	# Replace a small entry with a large one, more than 80
	echo "Replace a small entry with a large one, more than 80">>${DETAIL_LOG_FILE}
	XATTR_NAME=user.${i}
	XATTR_VALUE=${LARGE_VALUE_MORE_THAN_80}

	${SETXATTR} -n ${XATTR_NAME} -v ${XATTR_VALUE} ${TEST_FILE} >>${DETAIL_LOG_FILE} 2>&1
	exit_or_not $?

	echo "Clean up file">>${DETAIL_LOG_FILE}

	${RM} -rf ${TEST_FILE}
	${TOUCH_BIN} ${TEST_FILE}

	((SUB_TESTNO++))

	echo "Test ${SUB_TESTNO}: Basic Bucket Insert & Shrink Test.">>${DETAIL_LOG_FILE}

	f_extend_xattr "small" ${TEST_FILE} "bucket"
	exit_or_not $?

	XATTR_NAME="user.small"
	XATTR_VALUE="SMALL"

	${SETXATTR} -n ${XATTR_NAME} -v ${XATTR_VALUE} ${TEST_FILE} >>${DETAIL_LOG_FILE} 2>&1
	exit_or_not $?

	XATTR_NAME="user.large"
	XATTR_VALUE=${LARGE_VALUE_LESS_THAN_80}

	${SETXATTR} -n ${XATTR_NAME} -v ${XATTR_VALUE} ${TEST_FILE} >>${DETAIL_LOG_FILE} 2>&1
	exit_or_not $?

	${RM} -rf ${TEST_FILE}
	${TOUCH_BIN} ${TEST_FILE}

	f_extend_xattr "large" ${TEST_FILE} "bucket"
	exit_or_not $?

	# Remove one random entry in inode-block to leave space for further replacing
	RANDOM_SLOT=$((${RANDOM}%${MAX_LARGE_INLINE_XATTR}+1))
	XATTR_NAME=user.large.${RANDOM_SLOT}

	${SETXATTR} -x ${XATTR_NAME} ${TEST_FILE} >>${DETAIL_LOG_FILE} 2>&1
	exit_or_not $?

	# Replace a large xattr with small one in bucket to make it stored in inode-block.
	RANDOM_SLOT=$((${RANDOM}%$((${MAX_LARGE_BLOCK_XATTR}-${MAX_LARGE_INLINE_XATTR}+1))+${MAX_LARGE_INLINE_XATTR}))
	XATTR_NAME=user.large.${RANDOM_SLOT}
	XATTR_VALUE="SMALL"

	${SETXATTR} -n ${XATTR_NAME} -v ${XATTR_VALUE} ${TEST_FILE} >>${DETAIL_LOG_FILE} 2>&1
	exit_or_not $?

	${RM} -rf ${TEST_FILE}
	${TOUCH_BIN} ${TEST_FILE}

	f_extend_xattr "large" ${TEST_FILE} "bucket"
	exit_or_not $?

	RANDOM_SLOT=$((${RANDOM}%${MAX_LARGE_INLINE_XATTR}+1))
	XATTR_NAME=user.large.${RANDOM_SLOT}

	${SETXATTR} -x ${XATTR_NAME} ${TEST_FILE} >>${DETAIL_LOG_FILE} 2>&1
	exit_or_not $?

	# Replace a large xattr with large one(more than 80) in bucket to make it stored in inode-block.
	RANDOM_SLOT=$((${RANDOM}%$((${MAX_LARGE_BLOCK_XATTR}-${MAX_LARGE_INLINE_XATTR}+1))+${MAX_LARGE_INLINE_XATTR}))
	XATTR_NAME=user.large.${RANDOM_SLOT}
	XATTR_VALUE=${LARGE_VALUE_MORE_THAN_80}

	${SETXATTR} -n ${XATTR_NAME} -v ${XATTR_VALUE} ${TEST_FILE} >>${DETAIL_LOG_FILE} 2>&1
	exit_or_not $?
	
	return $?
}

f_runtest()
{
	((TEST_NO++))
	echo >>${DETAIL_LOG_FILE}
	echo "==========================================================">>${DETAIL_LOG_FILE}
	echo -ne "[${TEST_NO}] Check Namespace&Filetype of SingleNode Xattr on Ocfs2:"|tee -a ${RUN_LOG_FILE}
	echo -ne "[${TEST_NO}] Check Namespace&Filetype of SingleNode Xattr on Ocfs2:">>${DETAIL_LOG_FILE}
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
					echo "failed in $namespace mode with $filetype file"
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
	echo >>${DETAIL_LOG_FILE}
	echo "==========================================================">>${DETAIL_LOG_FILE}
	echo -ne "[${TEST_NO}] Perform Additional Func Test:"|tee -a ${RUN_LOG_FILE}
	echo -ne "[${TEST_NO}] Perform Additional Func Test::">>${DETAIL_LOG_FILE}
	echo >>${DETAIL_LOG_FILE}
	echo "==========================================================">>${DETAIL_LOG_FILE}
	if [ ! ${BLOCKSIZE} -eq 512 ];then
		f_add_func_test
	fi
	RET=$?
	echo_status ${RET} |tee -a ${RUN_LOG_FILE}
	exit_or_not ${RET}
	${RM} -rf ${WORKPLACE}/* || exit 1
	((TEST_PASS++))

	((TEST_NO++))
	echo >>${DETAIL_LOG_FILE}
	echo "==========================================================">>${DETAIL_LOG_FILE}
	echo -ne "[${TEST_NO}] Perform Inline Data&Xattr Combination Test:"|tee -a ${RUN_LOG_FILE}
	echo -ne "[${TEST_NO}] Perform Inline Data&Xattr Combination Test::">>${DETAIL_LOG_FILE}
	echo >>${DETAIL_LOG_FILE}
	echo "==========================================================">>${DETAIL_LOG_FILE}
	if [ ! -z "${COMBIN_TEST}" ] && [ ! ${BLOCKSIZE} -eq 512 ];then
		f_combin_test
	fi
	RET=$?
	echo_status ${RET}|tee -a ${RUN_LOG_FILE}
	exit_or_not ${RET}
	${RM} -rf ${WORKPLACE}/* || exit 1
	((TEST_PASS++))

	((TEST_NO++))
        echo >>${DETAIL_LOG_FILE}
	echo "==========================================================">>${DETAIL_LOG_FILE}
	echo -ne "[${TEST_NO}] Check Utility of SingleNode Xattr on Ocfs2:"|tee -a ${RUN_LOG_FILE}
        echo -ne "[${TEST_NO}] Check Utility of SingleNode Xattr on Ocfs2:">>${DETAIL_LOG_FILE}
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
	((TEST_PASS++))


	((TEST_NO++))
	echo >>${DETAIL_LOG_FILE}
        echo "==========================================================">>${DETAIL_LOG_FILE}
        echo -ne "[${TEST_NO}] Check Max SingleNode Xattr EA_Name_Length:"|tee -a ${RUN_LOG_FILE}
        echo -ne "[${TEST_NO}] Check Max SingleNode Xattr EA_Name_Length:">>${DETAIL_LOG_FILE}
        echo >>${DETAIL_LOG_FILE}
        echo "==========================================================">>${DETAIL_LOG_FILE}
	echo -e "Testing Binary:\t\t${XATTR_TEST_BIN} -i 1 -x 1 -n user -t normal -l 255 -s 200 ${WORKPLACE}">>${DETAIL_LOG_FILE}
	${XATTR_TEST_BIN}  -i 1 -x 1 -n user -t normal -l 255 -s 200 ${WORKPLACE} >>${DETAIL_LOG_FILE} 2>&1
	RET=$?
        echo_status ${RET} |tee -a ${RUN_LOG_FILE}
        exit_or_not ${RET}
	${RM} -rf ${WORKPLACE}/* || exit 1
	((TEST_PASS++))


	((TEST_NO++))
	echo >>${DETAIL_LOG_FILE}
        echo "==========================================================">>${DETAIL_LOG_FILE}
        echo -ne "[${TEST_NO}] Check Max SingleNode Xattr EA_Size:"|tee -a ${RUN_LOG_FILE}
        echo -ne "[${TEST_NO}] Check Max SingleNode Xattr EA_Size:">>${DETAIL_LOG_FILE}
        echo >>${DETAIL_LOG_FILE}
        echo "==========================================================">>${DETAIL_LOG_FILE}
        echo -e "Testing Binary:\t\t${XATTR_TEST_BIN} -i 1 -x 1 -n user -t normal -l 50 -s 65536 ${WORKPLACE}">>${DETAIL_LOG_FILE}
        ${XATTR_TEST_BIN}  -i 1 -x 1 -n user -t normal -l 50 -s 65536 ${WORKPLACE} >>${DETAIL_LOG_FILE} 2>&1
        RET=$?
        echo_status ${RET} |tee -a ${RUN_LOG_FILE}
        exit_or_not ${RET}
        ${RM} -rf ${WORKPLACE}/* || exit 1
	((TEST_PASS++))


	((TEST_NO++))
	echo >>${DETAIL_LOG_FILE}
        echo "==========================================================">>${DETAIL_LOG_FILE}
        echo -ne "[${TEST_NO}] Check Huge SingleNode Xattr EA_Entry_Nums:"|tee -a ${RUN_LOG_FILE}
        echo -ne "[${TEST_NO}] Check Huge SingleNode Xattr EA_Entry_Nums:">>${DETAIL_LOG_FILE}
        echo >>${DETAIL_LOG_FILE}
        echo "==========================================================">>${DETAIL_LOG_FILE}
        echo -e "Testing Binary:\t\t${XATTR_TEST_BIN} -i 1 -x 20000 -n user -t normal -l 20 -s 100 ${WORKPLACE}">>${DETAIL_LOG_FILE}
	${XATTR_TEST_BIN}  -i 1 -x 20000 -n user -t normal -l 20 -s 100  ${WORKPLACE} >>${DETAIL_LOG_FILE} 2>&1
        RET=$?
        echo_status ${RET} |tee -a ${RUN_LOG_FILE}
        exit_or_not ${RET}
        ${RM} -rf ${WORKPLACE}/* || exit 1
	((TEST_PASS++))


	((TEST_NO++))
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
        echo -ne "[${TEST_NO}] Launch Random SingleNode Xattr Test on Ocfs2:"|tee -a ${RUN_LOG_FILE}
        echo -ne "[${TEST_NO}] Launch Random SingleNode Xattr Test on Ocfs2:">>${DETAIL_LOG_FILE}
        echo >>${DETAIL_LOG_FILE}
        echo "==========================================================">>${DETAIL_LOG_FILE}
        echo -e "Testing Binary:\t\t${XATTR_TEST_BIN} -i 1 -x 200 -n user -t normal -l 100 -s 4000 -r ${WORKPLACE}">>${DETAIL_LOG_FILE}
	${XATTR_TEST_BIN}  -i 1 -x 200 -n user -t normal -l 100 -s 4000 -r ${WORKPLACE} >>${DETAIL_LOG_FILE} 2>&1
        RET=$?
        echo_status ${RET} |tee -a ${RUN_LOG_FILE}
        exit_or_not ${RET}
        ${RM} -rf ${WORKPLACE}/* || exit 1
	((TEST_PASS++))


	((TEST_NO++))
	echo >>${DETAIL_LOG_FILE}
        echo "==========================================================">>${DETAIL_LOG_FILE}
        echo -ne "[${TEST_NO}] Launch Concurrent Update/Read SingleNode Xattr Test on Ocfs2:"|tee -a ${RUN_LOG_FILE}
        echo -ne "[${TEST_NO}] Launch Concurrent Update/Read SingleNode Xattr Test on Ocfs2:">>${DETAIL_LOG_FILE}
        echo >>${DETAIL_LOG_FILE}
        echo "==========================================================">>${DETAIL_LOG_FILE}
        echo -e "Testing Binary:\t\t${XATTR_TEST_BIN} -i 1 -x 2000 -n user -t normal -l 50 -s 1000 -m 2000 -r ${WORKPLACE}">>${DETAIL_LOG_FILE}
	${XATTR_TEST_BIN}  -i 1 -x 2000 -n user -t normal -l 50 -s 1000 -m 2000 -r  ${WORKPLACE} >>${DETAIL_LOG_FILE} 2>&1
        RET=$?
        echo_status ${RET} |tee -a ${RUN_LOG_FILE}
        exit_or_not ${RET}
        ${RM} -rf ${WORKPLACE}/* || exit 1
	((TEST_PASS++))
	
	
	((TEST_NO++))
	echo >>${DETAIL_LOG_FILE}
        echo "==========================================================">>${DETAIL_LOG_FILE}
        echo -ne "[${TEST_NO}] Launch Multiple Files SingleNode Xattr Test on Ocfs2:"|tee -a ${RUN_LOG_FILE}
        echo -ne "[${TEST_NO}] Launch Multiple Files SingleNode Xattr Test on Ocfs2:">>${DETAIL_LOG_FILE}
        echo >>${DETAIL_LOG_FILE}
        echo "==========================================================">>${DETAIL_LOG_FILE}
        echo -e "Testing Binary:\t\t${XATTR_TEST_BIN} -i 1 -x 500 -n user -t normal -l 20 -s 800 -f 100 -r ${WORKPLACE}">>${DETAIL_LOG_FILE}
	${XATTR_TEST_BIN}  -i 1 -x 500 -n user -t normal -l 20 -s 800 -f 100 -r  ${WORKPLACE} >>${DETAIL_LOG_FILE} 2>&1
        RET=$?
        echo_status ${RET} |tee -a ${RUN_LOG_FILE}
        exit_or_not ${RET}
        ${RM} -rf ${WORKPLACE}/* || exit 1
	((TEST_PASS++))


	((TEST_NO++))
	echo >>${DETAIL_LOG_FILE}
        echo "==========================================================">>${DETAIL_LOG_FILE}
        echo -ne "[${TEST_NO}] Launch Stress Test With Shell Command:"|tee -a ${RUN_LOG_FILE}
        echo -ne "[${TEST_NO}] Launch Stress Test With Shell Command:">>${DETAIL_LOG_FILE}
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
	((TEST_PASS++))


	((TEST_NO++))
	echo >>${DETAIL_LOG_FILE}
        echo "==========================================================">>${DETAIL_LOG_FILE}
        echo -ne "[${TEST_NO}] Launch SingleNode Xattr Stress Test on Ocfs2:"|tee -a ${RUN_LOG_FILE}
        echo -ne "[${TEST_NO}] Launch SingleNode Xattr Stress Test on Ocfs2:">>${DETAIL_LOG_FILE}
        echo >>${DETAIL_LOG_FILE}
        echo "==========================================================">>${DETAIL_LOG_FILE}
        echo -e "Testing Binary:\t\t${XATTR_TEST_BIN} -i 100 -x 10000 -n user -t normal -l 200 -s 60000  -r ${WORKPLACE}">>${DETAIL_LOG_FILE}
        ${XATTR_TEST_BIN}  -i 20 -x 10000 -n user -t normal -l 200 -s 6000   ${WORKPLACE} >>${DETAIL_LOG_FILE} 2>&1
        RET=$?
        echo_status ${RET} |tee -a ${RUN_LOG_FILE}
        exit_or_not ${RET}
        ${RM} -rf ${WORKPLACE}/* || exit 1
	((TEST_PASS++))
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

if [ "$BLOCKSIZE" != "NONE" ];then
	bslist="$BLOCKSIZE"
else
	bslist="512 1024 2048 4096"
fi

if [ "$CLUSTERSIZE" != "NONE" ];then
	cslist="$CLUSTERSIZE"
else
	cslist="4096 32768 1048576"
fi

START_TIME=${SECONDS}
echo "=====================Single node xattr testing starts: `date`=====================" |tee -a ${RUN_LOG_FILE}
echo "=====================Single node xattr testing starts: `date`=====================" >> ${DETAIL_LOG_FILE}

for BLOCKSIZE in $(echo "$bslist")
do
	for CLUSTERSIZE in $(echo "$cslist")
        do
                echo "++++++++++xattr tests with \"-b ${BLOCKSIZE} -C ${CLUSTERSIZE}\"++++++++++" |tee -a ${RUN_LOG_FILE}
                echo "++++++++++xattr tests with \"-b ${BLOCKSIZE} -C ${CLUSTERSIZE}\"++++++++++">>${DETAIL_LOG_FILE}
                echo "======================================================================================="
                f_do_mkfs_and_mount
                f_runtest
                f_do_umount
                echo "======================================================================================="
                echo -e "\n\n\n">>${DETAIL_LOG_FILE}
        done
done
f_cleanup

END_TIME=${SECONDS}
echo "=====================Single node xattr testing ends: `date`=====================" |tee -a ${RUN_LOG_FILE}
echo "=====================Single node xattr testing ends: `date`=====================" >> ${DETAIL_LOG_FILE}

echo "Time elapsed(s): $((${END_TIME}-${START_TIME}))" |tee -a ${RUN_LOG_FILE}
echo "Tests total: ${TEST_NO}" |tee -a ${RUN_LOG_FILE}
echo "Tests passed: ${TEST_PASS}" |tee -a ${RUN_LOG_FILE}
