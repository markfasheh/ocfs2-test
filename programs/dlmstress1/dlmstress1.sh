#!/bin/bash
#
#
# Copyright (C) 2006 Oracle.  All rights reserved.
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public
# License as published by the Free Software Foundation; either
# version 2 of the License, or (at your option) any later version.
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
#/
#
# Description:  This is a simple test to stress the dlm.
#		It will run with 2 nodes performing a specified number of
#		loops that will issue a find, umount and mount on both nodes 
#		and in sequence.
#		It expects to find the partition mounted already and a number
# 		of files and directories created (good number would be about
#		80,000 files/directories or about 4 Linux Kernel source tree).
#
# 		This test also can test a slab corruption when the Linux kernel
#		is built with the option CONFIG_DEBUG_SLAB=y.
#
# Author: 	Marcos Matsunaga (Marcos.Matsunaga@oracle.com)
# 


FIND=`which find`
UMOUNT=`which umount`
MOUNT=`which mount`
ECHO=`which echo`
SSH=`which ssh`
WHOAMI=`which whoami`
# 
REMOTENODE=${1}
COUNT=${2}
LABEL=${3}
MOUNTPOINT=${4}
#
Usage()
{
echo -e "`basename ${0}` - Usage:"
echo -e "`basename ${0}` <Remote Node> <Count> <Dev LABEL> <Mountpoint>"
exit 1;
}
if [ $# -ne 4 ]; then
   Usage;
fi;
#
if [ "root" != `${WHOAMI}` ]; then
   ${ECHO} -e "This test has run as root."
   exit 1;
fi;
#
for (( i=1; i <= ${COUNT} ; i++ ))
do
   ${ECHO} -e "Starting run ..... $i"
   ${SSH} ${REMOTENODE} "${FIND} ${MOUNTPOINT} -exec ls -lR {} \;" >> /dev/null &
   ${FIND} ${MOUNTPOINT} -exec ls -lR {} \; >> /dev/null
   wait;
   ${UMOUNT} ${MOUNTPOINT}
   ${SSH} ${REMOTENODE} "${UMOUNT} ${MOUNTPOINT}"
   ${MOUNT} LABEL=${LABEL} ${MOUNTPOINT}
   ${SSH} ${REMOTENODE} "${MOUNT} LABEL=${LABEL} ${MOUNTPOINT}"
done

