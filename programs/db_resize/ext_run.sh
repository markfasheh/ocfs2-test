#!/bin/bash
#
# Script to test autoextend on ocfs.
#
# This script will start 2 sqlplus sessions in parallel on each node.
# The only argument accept by this script is to set how many times the
# insert job should be executed. If none is passed, it will execute
# the jobs 20 times and end.
#
Usage()
{
echo -e "$0 Usage: $0 -c <count> -n <nodename> -o <logfile> -h";
exit 1;
}
#
CreateObjects()
{
cd ${BASEDIR}/sqllib;
${ORACLE_HOME}/bin/sqlplus -s /nolog << EOF
conn / as sysdba
@${HOSTNAME}_define.sql
set echo on
set termout off
spool ${BASEDIR}/logs/${HOSTNAME}/ext_create_objects.log
@ext_create_tablespace.sql
@ext_create_tables.sql
spool off
exit
EOF
}
#
DropObjects()
{
cd ${BASEDIR}/sqllib;
${ORACLE_HOME}/bin/sqlplus -s /nolog << EOF
conn / as sysdba
@${HOSTNAME}_define.sql
set echo on
set termout off
spool ${BASEDIR}/logs/${HOSTNAME}/ext_drop_objects.log
@ext_drop_tablespace.sql
spool off
exit
EOF
}
GenScript()
{
NAME=/tmp/run$$_${1}.sh
echo "#!/bin/bash" > ${NAME};
echo "echo -e \"\n \`hostname -s\` - Starting run - ${NAME}.\"" >> ${NAME};
echo "ORACLE_HOME=${ORACLE_HOME};" >> ${NAME};
echo "ORACLE_SID=${ORACLE_SID};" >> ${NAME};
echo "PATH=${ORACLE_HOME}/bin:\$PATH;" >> ${NAME};
echo "export ORACLE_HOME ORACLE_SID PATH" >>${NAME};
echo ". ${OASTENV}" >>${NAME};
echo "cd ${BASEDIR}/sqllib">> ${NAME};
echo "$ORACLE_HOME/bin/sqlplus -s /nolog << EOF" >> ${NAME};
echo "connect / as sysdba " >> ${NAME};
echo "set echo on" >> ${NAME};
echo "set termout off" >> ${NAME};
echo "spool ${BASEDIR}/logs/${HOSTNAME}/ext_ins_test${1}.log" >> ${NAME};
i=0;
for (( i=0; i < ${COUNT}; i++))
do
   echo "@ext_ins_test${1}.sql" >> ${NAME};
done;
echo "spool off" >> ${NAME};
echo "exit" >> ${NAME};
echo "EOF" >> ${NAME};
echo "exit $?" >> ${NAME};
echo "echo -e \"\n \`hostname -s\` - Run Completed - ${NAME}.\"" >> ${NAME};
}
#
#
RunServer()
{
CreateObjects;
ssh ${LOCALNODE} "${BASEDIR}/bin/ext_run.sh -x -c ${COUNT} -o ${LOGFILE}" &
if [ "${SINGLE}" == "1" ]; then
   ssh ${NODE} "${BASEDIR}/bin/ext_run.sh -x -c ${COUNT} -o ${LOGFILE}" &
fi;
wait
sleep 30
DropObjects;
echo "Run Ended."
}
RunClient()
{
echo -e " `hostname` - Client run starting";
GenScript 1;
Run1=${NAME}
GenScript 2;
Run2=${NAME}
/bin/bash ${Run1} &
/bin/bash ${Run2} &
wait;
echo -e "`hostname` - Cleaning up."
rm -f ${Run1} ${Run2};
echo -e "`hostname` - Cleanup done."
}
#
LOCALDIR=`dirname ${0}`
if [ "${LOCALDIR}" == "." ]; then
   LOCALDIR=`pwd`;
fi;
BASEDIR=`dirname ${LOCALDIR}`
LOCALNODE=`hostname -s`
SINGLE=0
MASTER=0;
if [ ! -d ${BASEDIR}/logs/`hostname` ]; then
   mkdir -p ${BASEDIR}/logs/`hostname`;
fi;
LOGFILE=${BASEDIR}/logs/`hostname`/ext_run_$$.log

ID=$$;
#
echo -e " `hostname` - Starting test run - Datafile autoextension."
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
if [ ${MASTER} -eq 0 ]; then
   RunServer;
else
   RunClient;
fi;
exit $?;
