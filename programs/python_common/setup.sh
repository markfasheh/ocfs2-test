#!/bin/bash -x
#
if [ $# -ne 1 ]; then
   echo -e 'Usage: $0 <DESTDIR>';
   exit 1;
fi;
BINDIR=${1}/bin
sed "s;<DESTDIR>;${1};g" ${BINDIR}/config_py.skel >  ${BINDIR}/config.py
sed "s;<DESTDIR>;${1};g" ${BINDIR}/config_shell.skel >  ${BINDIR}/config.sh
