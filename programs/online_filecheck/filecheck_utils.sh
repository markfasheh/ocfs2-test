#!/bin/bash

PATH=$PATH:/usr/sbin/

DEBUGFS_BIN="`which sudo` -u root `which debugfs.ocfs2`"
FSWRECK_BIN="`which sudo` -u root `dirname ${0}`/fswreck"
SUDO_BASH="`which sudo` -u root bash -c"
SUDO_CAT="`which sudo` -u root `which cat`"

f_check_file()
{
   local inode=$1
   local check_file=$2
   local error_type=$3
   local log_file=$4

   echo -e "${SUDO_BASH} \"echo ${inode} > ${check_file}\"" >> ${log_file} 2>&1
   ${SUDO_BASH} "echo ${inode} > ${check_file}"
   sleep 0.1
   ${SUDO_CAT} ${check_file} > .tmp 2>&1
   cat .tmp | head -n 1 >> ${log_file} 2>&1
   LINE=`cat .tmp | tail -n 1`
   rm -f -- .tmp
   echo -e "${LINE}" >> ${log_file}
   local ERROR=`echo ${LINE} | awk '{print $3}'`
   f_LogMsg ${log_file} "Error type: ${ERROR}"
   if [ ${ERROR} != "${error_type}" ];then
      f_LogMsg ${log_file} "Unexpected error type, exit..."
      false
   fi
}

f_fix_file()
{
   local inode=$1
   local fix_file=$2
   local log_file=$3

   echo -e "${SUDO_BASH} \"echo ${inode} > ${fix_file}\"" >> ${log_file} 2>&1
   ${SUDO_BASH} "echo ${inode} > ${fix_file}"
   sleep 0.1
   ${SUDO_CAT} ${fix_file} > .tmp 2>&1
   cat .tmp | head -n 1 >> ${log_file} 2>&1
   LINE=`cat .tmp | tail -n 1`
   rm -f -- .tmp
   echo -e "${LINE}" >> ${log_file}
   local RESULT=`echo ${LINE} | awk '{print $3}'`
   f_LogMsg ${log_file} "Fix result: ${RESULT}"
   if [ ${RESULT} != "SUCCESS" ];then
      f_LogMsg ${log_file} "Failed to fix! Exit..."
      false
   fi
}
