#!/bin/bash -x
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

#
if [ $# -ne 1 -a $# -ne 2 ]; then
   echo -e 'Usage: $0 <DESTDIR> [INSTALLDIR]';
   exit 1;
fi;

MPIRUN=`which mpirun`
RC=$?
if [ "$RC" != "0" ];then
	MPIRUN=`rpm -ql openmpi|grep bin|grep mpirun`
	MPIDIR=`dirname ${MPIRUN}`
else
	if [ -f ${MPIRUN} ];then
		MPIDIR=`dirname ${MPIRUN}`
	else
		MPIDIR=/usr/bin
	fi
fi;

if [ $# -eq 1 ]; then
	INSTALLDIR=${1};
else
	INSTALLDIR=${2};
fi;

ROOT_CONFIG_BIN=${1}/bin
sed "s;<DESTDIR>;${INSTALLDIR};g" ${ROOT_CONFIG_BIN}/config_py.skel >  ${ROOT_CONFIG_BIN}/config.py
mv ${ROOT_CONFIG_BIN}/config.py  ${ROOT_CONFIG_BIN}/config_py.skel
sed "s;<MPIDIR>;${MPIDIR};g" ${ROOT_CONFIG_BIN}/config_py.skel >  ${ROOT_CONFIG_BIN}/config.py
#
sed "s;<DESTDIR>;${INSTALLDIR};g" ${ROOT_CONFIG_BIN}/config_shell.skel >  ${ROOT_CONFIG_BIN}/config.sh
mv ${ROOT_CONFIG_BIN}/config.sh ${ROOT_CONFIG_BIN}/config_shell.skel
sed "s;<MPIDIR>;${MPIDIR};g" ${ROOT_CONFIG_BIN}/config_shell.skel >  ${ROOT_CONFIG_BIN}/config.sh
rm -f ${ROOT_CONFIG_BIN}/config_py.skel ${ROOT_CONFIG_BIN}/config_shell.skel
