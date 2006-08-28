#!/bin/bash
#
RUN=0;
echo "Starting dirindex_rm.sh" |tee -a  /home/mmatsuna/LOGS/dirindex_rm.log;
while [ ${RUN} -eq 0 ]
do
   date >> /home/mmatsuna/LOGS/dirindex_rm.log;
   find /oastlog/dirindex/ -type f -exec rm -f {} \; -print >> /home/mmatsuna/LOGS/dirindex_rm.log;
   if [ -f /tmp/stop_run ]; then
      RUN=1;
   fi;
done;
