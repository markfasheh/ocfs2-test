#!/bin/bash
#
# Script to test datafile resize on ocfs.
#
# This script will start 2 sqlplus sessions in parallel on each node.
# The only argument accept by this script is to set how many times the
# rezise job should be executed. If none is passed, it will execute
# the jobs 5 times.
#
Usage()
{
echo -e "$0 Usage: $0 -c <count> -n <nodename> -min <min_size in MB> -max <max_size in MB> -h";
exit 1;
}
#
CreateObjects()
{
cd ${BASEDIR}/sqllib;
${ORACLE_HOME}/bin/sqlplus /nolog << EOF
conn / as sysdba
@${BASEDIR}/sqllib/${LOCALNODE}_define.sql 
set echo on
set termout off
spool ${BASEDIR}/logs/${HOSTNAME}_alterdbf_create_objects.log
@alterdbf_create_tablespace.sql
spool off
exit
EOF
RC=$?;
echo " ${HOSTNAME} - Create Objects - Start" >> ${LOGFILE};
cat ${BASEDIR}/logs/${HOSTNAME}_alterdbf_create_objects.log >> ${LOGFILE};
echo " ${HOSTNAME} - Create Objects - End" >> ${LOGFILE};
rm -f ${BASEDIR}/logs/${HOSTNAME}_alterdbf_create_objects.log;
if [ ${RC} -ne 0 ]; then
   echo -e "Failed to create tablespaces";
   exit ${RC}; 
fi;

}
#
DropObjects()
{
sleep 30
cd ${BASEDIR}/sqllib;
${ORACLE_HOME}/bin/sqlplus /nolog << EOF
conn / as sysdba
@${BASEDIR}/sqllib/${LOCALNODE}_define.sql 
set echo on
set termout off
spool ${BASEDIR}/logs/${HOSTNAME}_alterdbf_drop_objects.log
@alterdbf_drop_tablespace.sql
spool off
exit
EOF
echo " ${HOSTNAME} - Create Objects - Start" >> ${LOGFILE};
cat ${BASEDIR}/logs/${HOSTNAME}_alterdbf_drop_objects.log >> ${LOGFILE};
echo " ${HOSTNAME} - Create Objects - End" >> ${LOGFILE};
rm -f ${BASEDIR}/logs/${HOSTNAME}_alterdbf_drop_objects.log;
}
#
GenRandom()
{
RANDMAX=32767
DBFSIZE=$(( ${LO} + (${HI} * ${RANDOM}) / ($RANDMAX + 1) ))
}
#
GenScript()
{
NAME=/tmp/run$$_${1}.sh
echo "#!/bin/bash" > ${NAME};
echo "ORACLE_HOME=${ORACLE_HOME};" >> ${NAME};
echo "ORACLE_SID=${ORACLE_SID};" >> ${NAME};
echo "PATH=${ORACLE_HOME}/bin:\$PATH;" >> ${NAME};
echo "export ORACLE_HOME ORACLE_SID PATH" >>${NAME};
echo ". ${OASTENV}" >>${NAME};
echo "cd ${BASEDIR}/sqllib">> ${NAME};
echo "$ORACLE_HOME/bin/sqlplus -s /nolog << EOF" >> ${NAME};
echo "connect / as sysdba " >> ${NAME};
echo "@${BASEDIR}/sqllib/${LOCALNODE}_define.sql " >> ${NAME};
echo "set echo on " >> ${NAME};
echo "set termout off " >> ${NAME};
echo "whenever sqlerror exit failure;">> ${NAME};
echo "whenever oserror exit failure;">> ${NAME};
echo "spool ${BASEDIR}/logs/${HOSTNAME}_alerdbf_cli_${1}.log" >> ${NAME};
i=0;
for (( i=0; i < ${COUNT}; i++))
do
   GenRandom ${3} ${4};
   echo "alter database datafile '&&ALTERDBF_DBF${1}' resize ${DBFSIZE}M" >> ${NAME};
   echo "/" >> ${NAME};
done;
echo "spool off" >> ${NAME};
echo "exit" >> ${NAME};
echo "EOF" >> ${NAME};
echo "RC=$?";
echo "echo \" ${HOSTNAME} - Client ${1} - Start\" >> ${LOGFILE}" >>${NAME};
echo "cat ${BASEDIR}/logs/${HOSTNAME}_alerdbf_cli_${1}.log >> ${LOGFILE}" >>${NAME};
echo "echo \" ${HOSTNAME} - Client ${1} - End\" >> ${LOGFILE}" >>${NAME};
echo "rm -f ${BASEDIR}/logs/${HOSTNAME}_alerdbf_cli_${1}.log " >>${NAME};
echo "exit ${RC}" >> ${NAME};
}
RunServer()
{
CreateObjects;
ssh ${LOCALNODE} "${BASEDIR}/bin/alterdbf_run.sh -x -o ${LOGFILE} -c ${COUNT} -min ${LO} -max ${HI}" &
if [ ${SINGLE} -eq 1 ]; then
   ssh ${NODE} "${BASEDIR}/bin/alterdbf_run.sh -x -o ${LOGFILE} -c ${COUNT} -min ${LO} -max ${HI}" &
fi;
wait;
DropObjects;
echo "Run Ended.";
}
#
RunClient()
{
GenScript 1;
Run1=${NAME}
GenScript 2;
Run2=${NAME}
/bin/bash ${Run1} &
/bin/bash ${Run2} &
wait;
echo -e "\n Run Completed - `hostname -s`. Cleaning Up." |tee -a ${LOGFILE};
rm -f ${Run1} ${Run2};
echo -e "Cleanup done." |tee -a ${LOGFILE};
}
#
LOCALDIR=`dirname ${0}`
if [ "${LOCALDIR}" == "." ]; then
   LOCALDIR=`pwd`;
fi;
BASEDIR=`dirname ${LOCALDIR}`
LOCALNODE=`hostname -s`
COUNT=5
SINGLE=0
MASTER=0;
if [ ! -d ${BASEDIR}/logs/`hostname` ]; then
   mkdir -p ${BASEDIR}/logs/`hostname`;
fi;
LOGFILE=${BASEDIR}/logs/`hostname`/alterdbf_$$.log

ID=$$;
#
if [ $# -eq 0 ]; then
   Usage;
fi;
#
while [ $# -gt 0 ]
do
  case ${1} in
     -c ) COUNT=${2};
          shift 2;
          ;;
     -n ) NODE=${2};
          SINGLE=1;
          shift 2;
          ;;
     -min ) LO=${2};
          shift 2;
          ;;
     -max ) HI=${2};
          shift 2;
          ;;
     -o ) LOGFILE=${2};
          shift 2;
          ;;
     -x   ) MASTER=1;
          shift;
          ;;
     -h   ) Usage;
          ;;
     *    ) Usage;
          ;;
  esac;
done;
#
echo -e "Starting Datafile resize test.";
if [ ! -f ${LOGFILE} ]; then
   echo -e "Creating logfile ${LOGFILE}" |tee -a ${LOGFILE};
fi;
#
if [ ${MASTER} -eq 0 ]; then
   RunServer;
else
   RunClient;
fi;
exit;
