#!/bin/bash
#
# vim: noexpandtab sw=8 ts=8 sts=0:
#
# reflink_files.sh
#
# Description:  It's a simple script that takes a list of files to run reflink
#		in a loop.
#
# Copyright (C) 2009 Oracle.  All rights reserved.
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public
# License, version 2,  as published by the Free Software Foundation.

################################################################################
# Global Variables
################################################################################
if [ -f `dirname ${0}`/o2tf.sh ]; then
        . `dirname ${0}`/o2tf.sh
fi

REFLINK_BIN=`which reflink`

DEFAULT_LOG_DIR=./logs
LOG_DIR=
RUN_LOG_FILE=
LOG_FILE=
WORK_PLACE=

ITERATION=
SLEEP_INTERVAL=

declare -a FILES

DIR_PREFIX=
DIR_COUNT=0
CUR_DIR=

set -o pipefail

################################################################################
# Utility Functions
################################################################################

function f_usage()
{
	echo "usage: `basename ${0}` [-o logdir] <-w working place > <-i iterations> <-s sleep_interval> <list of files>"
	echo "	     -o output directory for the logs"
	echo "	     -w specify your working place where snaphosts reside"
	echo "	     -i specify the iterations of loop"
	echo "	     -s specify the sleeping intervals between loops"
	echo "	     The rest of args sepecifed are the original files for snapshoting"
        exit 1;
}

function f_getoptions()
{
	if [ $# -eq 0 ]; then
		f_usage;
		exit 1
	fi

	while getopts "o:hw:i:s:" options; do
		case $options in
		o ) LOG_DIR="$OPTARG";;
		w ) WORK_PLACE="$OPTARG";;
		i ) ITERATION="$OPTARG";;
		s ) SLEEP_INTERVAL="$OPTARG";;
		h ) f_usage;;
		* ) f_usage;;
		esac
	done
	shift $(($OPTIND -1))
        FILES=${*}
}

function f_check()
{
	f_getoptions $*

	if [ -z "${WORK_PLACE}" ];then
		f_usage
	else
		if [ ! -d ${WORK_PLACE} ]; then
			echo "working place ${WORK_PLACE} does not exist."
			exit 1
		else
			if [ "`dirname ${WORK_PLACE}`" = "/" ]; then
				WORK_PLACE="`dirname ${WORK_PLACE}``basename ${WORK_PLACE}`"
			else
				WORK_PLACE="`dirname ${WORK_PLACE}`/`basename ${WORK_PLACE}`"
			fi
		fi
	fi

	LOG_DIR=${LOG_DIR:-$DEFAULT_LOG}
	${MKDIR_BIN} -p ${LOG_DIR} || exit 1

	LOG_FILE="`dirname ${LOG_DIR}`/`basename ${LOG_DIR}`/`date +%F-%H-%M-%S`-reflink-files.log"

	RUN_LOG_FILE="`dirname ${LOG_DIR}`/`basename ${LOG_DIR}`/`date +%F-%H-%M-%S`-reflink-files-run.log"

	DIR_PREFIX=${WORK_PLACE}/reflink_files
}

function f_create_dir()
{
	((DIR_COUNT++))
	DIR_NAME="${DIR_PREFIX}${DIR_COUNT}"

	${MKDIR_BIN} -p ${DIR_NAME}>>${LOG_FILE} 2>&1

	CUR_DIR=${DIR_NAME}
}

function f_remove_dir()
{
	DIR_INDEX=${1}
	DIR_NAME="${DIR_PREFIX}${DIR_INDEX}"

	for file in ${FILES};do
		${RM_BIN} -rf ${DIR_NAME}/${reflink_file} >>${LOG_FILE} 2>&1
	done

	${RM_BIN} -rf ${DIR_NAME}>>${LOG_FILE} 2>&1
}

function f_do_reflinks()
{
	DIR_INDEX=${1}
	DIR_NAME="${DIR_PREFIX}${DIR_INDEX}"
	for file in ${FILES};do
		${REFLINK_BIN} $file ${DIR_NAME}/`basename ${file}` >>${LOG_FILE} 2>&1
	done
}

function f_run_test()
{

	f_LogRunMsg ${RUN_LOG_FILE} "Creating dir ${DIR_PREFIX}$((${DIR_COUNT}+1)) and doing reflinks\n"
	f_LogMsg ${RUN_LOG_FILE} "Creating dir ${DIR_PREFIX}$((${DIR_COUNT}+1)) and doing reflinks"
	f_create_dir
	f_do_reflinks ${DIR_COUNT}

	for i in `seq ${ITERATION}`;do

		if [ "$((${RANDOM}%2))" -eq "1" ];then
			f_LogRunMsg ${RUN_LOG_FILE} "removing dir ${DIR_PREFIX}${DIR_COUNT} and unlinking reflinks\n"
			f_LogMsg ${RUN_LOG_FILE} "removing dir ${DIR_PREFIX}${DIR_COUNT} and unlinking reflinks"
			f_remove_dir ${DIR_COUNT}
			f_LogRunMsg ${RUN_LOG_FILE} "Creating dir ${DIR_PREFIX}$((${DIR_COUNT}+1)) and doing reflinks\n"
			f_LogMsg ${RUN_LOG_FILE} "Creating dir ${DIR_PREFIX}$((${DIR_COUNT}+1)) and doing reflinks"
			f_create_dir
			f_do_reflinks ${DIR_COUNT}
		else
			f_LogRunMsg ${RUN_LOG_FILE} "Creating dir ${DIR_PREFIX}$((${DIR_COUNT}+1)) and doing reflinks\n"
			f_LogMsg ${RUN_LOG_FILE} "Creating dir ${DIR_PREFIX}$((${DIR_COUNT}+1)) and doing reflinks"
			f_create_dir
			f_do_reflinks ${DIR_COUNT}
			f_LogRunMsg ${RUN_LOG_FILE} "removing dir ${DIR_PREFIX}$((${DIR_COUNT}-1)) and unlinking reflinks\n"
			f_LogMsg ${RUN_LOG_FILE} "removing dir ${DIR_PREFIX}$((${DIR_COUNT}-1)) and unlinking reflinks"
			f_remove_dir $((${DIR_COUNT}-1))
		fi

		sleep ${SLEEP_INTERVAL}
	done

	#Make sure tests end up with one reference
	f_LogRunMsg ${RUN_LOG_FILE} "removing dir ${DIR_PREFIX}${DIR_COUNT} and unlinking reflinks\n"
	f_LogMsg ${RUN_LOG_FILE} "removing dir ${DIR_PREFIX}${DIR_COUNT} and unlinking reflinks"
	f_remove_dir ${DIR_COUNT}
}

################################################################################
# Main Entry
################################################################################

#redfine the int signal hander
trap 'echo -ne "\n\n">>${LOG_FILE};echo  "Interrupted by Ctrl+C,Cleanuping... "|tee -a ${LOG_FILE}; f_cleanup;exit 1' SIGINT

f_check $*

START_TIME=${SECONDS}
f_LogRunMsg ${RUN_LOG_FILE} "=====================Reflink files test start:  `date`=====================\n"
f_LogMsg ${LOG_FILE} "=====================Reflink files test start:  `date`====================="

f_run_test

END_TIME=${SECONDS}
f_LogRunMsg ${RUN_LOG_FILE} "=====================Reflink files tests end: `date`=====================\n"
f_LogMsg ${LOG_FILE} "=====================Reflink files tests end: `date`====================="

f_LogRunMsg ${RUN_LOG_FILE} "Time elapsed(s): $((${END_TIME}-${START_TIME}))\n"
f_LogRunMsg ${RUN_LOG_FILE} "Tests total: ${TEST_NO}\n"
f_LogRunMsg ${RUN_LOG_FILE} "Tests passed: ${TEST_PASS}\n"
