#!/bin/bash
#
RUN=0;
echo "Starting dirindex_count.sh" |tee -a  /home/mmatsuna/LOGS/dirindex_count.log;
while [ ${RUN} -eq 0 ]
do
   date >> /home/mmatsuna/LOGS/dirindex_count.log;
   find /oastlog/dirindex/ -print |wc -l >> /home/mmatsuna/LOGS/dirindex_count.log;
   if [ -f /tmp/stop_run ]; then
      RUN=1;
   fi;
done;
