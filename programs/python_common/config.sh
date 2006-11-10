#!/bin/bash -x
#
if [ $# -ne 1 ]; then
   echo -e 'Usage: $0 <DESTDIR>';
   exit 1;
fi;
BINDIR=${1}/bin
/bin/cp ${BINDIR}/config.py ${BINDIR}/config.tmp
sed "s;<DESTDIR>;$1;g" ${BINDIR}/config.tmp >  ${BINDIR}/config.py
/bin/rm -f ${BINDIR}/config.tmp
