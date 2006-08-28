#!/bin/bash
#
if [ $# -eq 0 ]; then
   echo -e "Usage : ${0} <hostname>";
   exit 1;
fi;
/home/mmatsuna/Tests/OCFS/bin/cross_delete.sh -n ${1} -b /oastlog -l 16 -f /home/mmatsuna/Tests/OCFS/workfiles/linuxsrc.tar.gz &
/home/mmatsuna/Tests/OCFS/bin/cross_delete.sh -n ${1} -b /oastdbf2 -l 32 -f /home/mmatsuna/Tests/OCFS/workfiles/linuxsrc.tar.gz &
/home/mmatsuna/Tests/OCFS/bin/cross_delete.sh -n ${1} -b /oastdbf3 -l 64 -f /home/mmatsuna/Tests/OCFS/workfiles/linuxsrc.tar.gz &
/home/mmatsuna/Tests/OCFS/bin/cross_delete.sh -n ${1} -b /oastdbf5 -l 128 -f /home/mmatsuna/Tests/OCFS/workfiles/linuxsrc.tar.gz &
wait
rm -fr /oastlog/y1 /oastdbf2/y1 /oastdbf3/y1 /oastdbf5/y1
rm -fr /oastlog/x1 /oastdbf2/xx /oastdbf3/x1 /oastdbf5/x1
