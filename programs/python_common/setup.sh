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
if [ $# -ne 1 ]; then
   echo -e 'Usage: $0 <DESTDIR>';
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
fi

BINDIR=${1}/bin
sed "s;<DESTDIR>;${1};g" ${BINDIR}/config_py.skel >  ${BINDIR}/config.py
mv ${BINDIR}/config.py  ${BINDIR}/config_py.skel
sed "s;<MPIDIR>;${MPIDIR};g" ${BINDIR}/config_py.skel >  ${BINDIR}/config.py
#
sed "s;<DESTDIR>;${1};g" ${BINDIR}/config_shell.skel >  ${BINDIR}/config.sh
mv ${BINDIR}/config.sh ${BINDIR}/config_shell.skel
sed "s;<MPIDIR>;${MPIDIR};g" ${BINDIR}/config_shell.skel >  ${BINDIR}/config.sh
rm -f ${BINDIR}/config_py.skel ${BINDIR}/config_shell.skel
