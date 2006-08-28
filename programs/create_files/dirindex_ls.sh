#!/bin/bash
#
RUN=0;
echo "Starting dirindex_ls.sh" |tee -a  /home/mmatsuna/LOGS/dirindex_ls.log;
while [ ${RUN} -eq 0 ]
do
   date >> /home/mmatsuna/LOGS/dirindex_ls.log;
   ls /oastlog/dirindex/* >> /home/mmatsuna/LOGS/dirindex_ls.log;
   if [ -f /tmp/stop_run ]; then
      RUN=1;
   fi;
   sleep 10;
done;
