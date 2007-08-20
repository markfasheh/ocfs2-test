#
# o2tf - OCFS2 Tests Functions.
#
import os, sys, signal, string, popen2, socket, time, pdb, shutil, platform
import config, stat, os.path
#from log import log
# Set time executable to use.
uname = os.uname()
#
class RunAlarm(Exception):
	pass
#
# printlog is used by :
#   - o2tf.py
#   - run_buildkernel.py
#   - buildkernel.py
def printlog(message, logfile, prflag=0, prsep=''):
	'print message to the logfile.'
	'takes 4 parameters: message, logfilename, prflag and prsep'
	'prflag can be:'
	'  0 : Do nothing. Just print the message'
	'  1 : Print prflag content before printing message'
	'  2 : Print prflag content after printing message'
	'  3 : Print prflag content before and after printing message'
	'prflag can be any single character used to make a separator'
	'The separator will be printed 10 times with no space between'
	hostname = str(socket.gethostname())
	datetime = time.strftime("%b %d %H:%M:%S", time.localtime())
	if len(prsep) == 0:
		prseplen = (80-(len(hostname)+len(datetime)+2))
	else:
		prseplen = (80-(len(hostname)+len(datetime)+2))/len(prsep)
	from os import access,F_OK
	if os.access(logfile,F_OK) == 0:
		os.system('touch ' + logfile)
   
#
	fd = open(logfile, 'a+', 0)
#
	if prflag == 1 or prflag == 3:
		fd.write(datetime+' '+hostname+' '+(prsep * prseplen)+'\n')
#
	fd.write(datetime+' '+hostname+' '+message+'\n')
	print '%s %s : %s ' % (datetime, hostname, message)
#
	if prflag == 2 or prflag == 3:
		fd.write(datetime+' '+hostname+' '+(prsep * prseplen)+'\n')
	fd.close
#
# ClearDir is used by :
#   - buildkernel.py
def ClearDir(DEBUGON, logfile, dirl):
	'Clear the node directories under dirlist'
	nodelist = []
	dirlist=dirl.split(',')
	dirlen=len(dirlist)
	nodename = str(socket.gethostname())
	if DEBUGON:
		printlog('o2tf.ClearDir: logfile = (%s)' % logfile, 
			logfile, 
			0, 
			'')
  		printlog('o2tf.ClearDir: dirlist = (%s)' % dirlist, 
			logfile, 
			0, 
			'')
#
	if len(logfile) == 0:
		logfile=str(os.getcwd()  + '/ClearDir.log')
#
	from os import access,F_OK
	for i in range(dirlen):
		wdir=dirlist[i] + '/' + nodename
		if os.access(wdir, F_OK) == 0:
			if DEBUGON:
				printlog('o2tf.ClearDir: Directory %s does not exists.' % wdir,
					logfile, 
					0, 
					'')
			continue
		if DEBUGON:
			printlog('o2tf.ClearDir: Removing directory %s.' % wdir,
				logfile, 
				0, 
				'')
		os.system('rm -fr '+ wdir)
#
# extract_tar is used by :
#   - o2tf.py
#   - buildkernel.py
def extract_tar(DEBUGON, logfile, dirl, tarfile):
	'Extract the kernel source tarfile into each one of the node directories, \
	if they do not exist'
	nodename = str(socket.gethostname())
	dirlist=dirl.split(',')
	dirlen=len(dirlist)
	if DEBUGON:
		printlog('o2tf.extract_tar: logfile = (%s)' % \
			logfile, 
			logfile, 
			0, 
			'')
		printlog('o2tf.extract_tar: dirlist = (%s)' % \
			dirlist, 
			logfile, 
			0, 
			'')
		printlog('o2tf.extract_tar: tarfile = (%s)' % \
			tarfile, 
			logfile, 
			0, 
			'')
#
	if len(logfile) == 0:
		logfile=str(os.getcwd()  + '/extract_tar.log')
	CreateDir(DEBUGON, dirl, logfile)
	from os import access,F_OK
	for i in range(dirlen):
		wdir=dirlist[i] + '/' + nodename
		if DEBUGON:
			printlog('o2tf.extract_tar: wdir = %s' % wdir, 
				logfile, 
				0, 
				'')
 		if os.access(wdir, F_OK) == 1:
			if DEBUGON:
				printlog('o2tf.extract_tar: Directory %s already exists. \
					Skipping...' % wdir, 
					logfile, 
					0, 
					'')
			continue
		if DEBUGON:
			printlog('o2tf.extract_tar: Creating directory %s.' % wdir,
				logfile, 
				0, 
				'')
		os.mkdir(wdir)
		os.chdir(wdir)
		untar(DEBUGON, wdir, tarfile, logfile)
#
# CreateDir is used by :
#   - o2tf.py
#   - crdel_gen_files
def CreateDir(DEBUGON, dirl, logfile):
	'Create directories from a passed dirlist'
	if DEBUGON:
		printlog('o2tf.CreateDir: Started.', logfile, 0, '')
	dirlist = string.split(dirl,',')
	ndir = len(dirlist)
	if DEBUGON:
		printlog('o2tf.CreateDir: dirlist = (%s)' % dirlist, 
			logfile, 
			0, 
			'')
		printlog('o2tf.CreateDir: dirlen = (%s)' % ndir, 
			logfile, 
			0, 
			'')
	from os import access,F_OK
	for i in range(ndir):
#
		if os.access(dirlist[i],F_OK) == 0:
			if DEBUGON:
				printlog('o2tf.CreateDir: Directory %s does not exist. \
					Creating it.' % \
					dirlist[i], 
					logfile, 
					0, 
					'')
			os.makedirs(dirlist[i],0755)
		if DEBUGON:
			printlog('o2tf.CreateDir: Ended.', 
				logfile, 
				0, 
				'')

#
# untar is used by :
#   - o2tf.py
def untar(DEBUGON, destdir, tarfile, logfile):
	'Untar into a destdir, the tarfile, logging the output into a logfile'
	'For the moment, it only works with compressed tar files.'
	if DEBUGON:
		printlog('o2tf.untar: Started.', logfile, 0, '')
	import zipfile, os.path
	dirlog = os.path.dirname(logfile)
	nodename = str(socket.gethostname())
	pid = os.getpid()
	tarlog = dirlog+'/tar_'+nodename+'_'+str(pid)+'.log'
	options = 'xvf'
#    if zipfile.is_zipfile(tarfile): #for now, it is always compressed
	compress = 'z'
#    else:
#       compress = ''
	st = os.stat(destdir)
	if stat.S_ISREG(st.st_mode):
		o2tf.printlog('o2tf.untar: (%s) is a regular file. Can\'t \
			extract tarfile into a regular file.' % \
			destdir, 
			logfile, 
			0, 
			'')
	else:
		if DEBUGON:
			printlog('o2tf.untar: Extracting tar file %s into %s directory.' % \
				(tarfile, destdir), 
				logfile, 
				0, 
				'')
		os.system('cd %s; tar %s %s 2>&1 1>> %s; du -sh * 1>> %s' % \
			(destdir, 
			options + compress, 
			tarfile, 
			tarlog, 
			logfile) )
		if DEBUGON:
			printlog('o2tf.untar: Extraction ended.', logfile, 0, '')
#
# StartMPI is used by :
#   - o2tf.py
def StartMPI(DEBUGON, nodes, logfile):
	'Start LAM/MPI on all nodes, doing a sanity check before.'
	from os import access,F_OK
	if os.access(config.LAMBOOT, F_OK) == 0:
		printlog('o2tf.StartMPI: Lamboot not found', 
			logfile, 
			0, 
			'')
		sys.exit(1)
	if os.access(config.LAMHOSTS, F_OK) == 1:
		os.system('rm -f ' + config.LAMHOSTS)
	nodelist = string.split(nodes,',')
	nodelen = len(nodelist)
	fd = open(config.LAMHOSTS,'w',0)
	for i in range(nodelen):
		fd.write(nodelist[i] + '\n')
	fd.close()
# Check if all hosts are accessible
	try:
		if DEBUGON:
			printlog('o2tf.StartMPI: Trying to run %s with %s file.' % \
				(config.RECON, config.LAMHOSTS), 
				logfile, 
				0, 
				'')
		os.system('%s -v %s' % (config.RECON, config.LAMHOSTS))
	except os.error:
		pass
# Looks like everything is ok. So, run lamboot.
	try:
		if DEBUGON:
			printlog('o2tf.StartMPI: Trying to run %s with %s file.' % \
				(config.LAMBOOT, config.LAMHOSTS),
				logfile,
				0,
				'')
		os.system('%s -v %s' % (config.LAMBOOT, config.LAMHOSTS))
	except os.error:
		pass
#
# calls mpi-run-parts
# mpi_runparts is used by :
#   - o2tf.py
#   - run_buildkernel.py
def mpi_runparts(DEBUGON, nproc, cmd, nodes, logfile):
	'Execute commands in parallel using LAM/MPI.'
	from os import access,F_OK
	found = 0
	uname = os.uname()
	nodelen = len(string.split(nodes,','))
	try:
		if DEBUGON:
			printlog('o2tf.mpi_runparts: MPIRUN = %s' % config.MPIRUN, 
			logfile, 
			0, 
			'')
			printlog('o2tf.mpi_runparts: nproc = %s' % nproc, 
			logfile, 
			0, 
			'')
			printlog('o2tf.mpi_runparts: nodelen = %d' % nodelen, 
			logfile, 
			0, 
			'')
			printlog('o2tf.mpi_runparts: MPIRUNPARTS = %s' % config.MPIRUNPARTS, 
			logfile, 
			0, 
			'')
			printlog('o2tf.mpi_runparts: cmd = %s' % cmd, 
			logfile, 
			0, 
			'')
		pid = os.spawnv(os.P_NOWAIT, 
			'/bin/bash', 
			['bash', 
			'-xc', 
			config.MPIRUN + ' -sigs -ger -w n0-%d %s %s' % \
			( nodelen - 1, config.MPIRUNPARTS, cmd)])
		os.waitpid(pid,0)
	except os.error:
		pass
#
# Calls mpirun (Original from the LAM/MPI Package)
# mpi_run is used by :
#   - open_delete.py
def mpi_run(DEBUGON, nproc, cmd, nodes, logfile):
	'Execute commands in parallel using LAM/MPI.'
	from os import access,F_OK
	found = 0
	uname = os.uname()
	nodelen = len(string.split(nodes,','))
	try:
		if DEBUGON:
			printlog('o2tf.mpi_run: MPIRUN = %s' % config.MPIRUN, 
				logfile, 
				0, 
				'')
			printlog('o2tf.mpi_run: nproc = %s' % nproc, 
				logfile, 
				0, 
				'')
			printlog('o2tf.mpi_run: nodelen = %d' % nodelen, 
				logfile, 
				0, 
				'')
			printlog('o2tf.mpi_run: cmd = %s' % cmd, 
				logfile, 
				0, 
				'')
		pid = os.spawnv(os.P_NOWAIT, 
			'/bin/bash', 
			['bash', 
			'-xc', 
			config.MPIRUN + ' -sigs -ger -w n0-%d %s' % \
			( nodelen - 1, cmd)])
		os.waitpid(pid,0)
	except os.error:
		pass
#
# lamexec is used by :
#   - 
def lamexec(DEBUGON, nproc, wait, cmd, nodes, logfile):
	'Execute commands in parallel using LAM/MPI.'
	from os import access,F_OK
	found = 0
	uname = os.uname()
	nodelen = len(string.split(nodes,','))
	try:
		if DEBUGON:
			printlog('o2tf.lamexec: LAMEXEC = %s' % config.LAMEXEC, 
				logfile, 
				0, 
				'')
			printlog('o2tf.lamexec: nproc = %s' % nproc, 
				logfile, 
				0, 
				'')
			printlog('o2tf.lamexec: cmd = %s' % cmd, 
				logfile, 
				0, 
				'')
			printlog('o2tf.lamexec: nodelen = %d' % nodelen, 
				logfile, 
				0, 
				'')
			printlog('o2tf.lamexec: nodes = %s' % nodes, 
				logfile, 
				0, 
				'')
		pid = os.spawnv(os.P_NOWAIT, 
			'/bin/bash', 
			['bash', 
			'-xc', 
			config.LAMEXEC + ' -np %s %s n0-%d %s' % \
			( nproc, wait, nodelen - 1, cmd)])
		os.waitpid(pid,0)
	except os.error:
		pass
#
# lrand is used by :
#   - 
def lrand(DEBUGON, max):
#
	'Generate and return an integer random number. Takes the max number \
	as parameter.'
	import time, random
	random.seed(time.time())
	return(random.randint(1,max))
#
# GetOcfs2Cluster is used by:
#
def GetOcfs2Cluster():
	""" Find and return the OCFS2 cluster name"""
	if (os.path.isdir('/config/cluster')):
		out = os.popen('ls /config/cluster')
		CLUSTER = string.strip(out.read(),'\n')
		return(CLUSTER)
#
# GetOcfs2NIC is used by:
#
def GetOcfs2NIC(DEBUGON, Cluster):
	""" Find and return the NIC used by OCFS2"""
	hostname = str(socket.gethostname())
	nodedir=os.path.join('/config/cluster', Cluster, 'node', hostname)
	if (os.path.isdir(nodedir)):
		os.chdir(nodedir)
		from os import access,F_OK
   	if os.access('ipv4_address',F_OK) == 1:
			fd = open('ipv4_address','r',0)
			IPAddress=string.strip(fd.read(), '\n')
			if DEBUGON:
				print 'GetOcfs2NIC: IPAddress = %s' % IPAddress
			fd.close()
			out = os.popen('/sbin/ifconfig | awk \'/^eth/{eth=$1}/inet addr:'+
				IPAddress+'/{print eth;exit}\'')
			NIC=string.strip(out.read(), '\n')
			out.close()
			if not NIC:
				out = os.popen('/sbin/ifconfig | awk \'/^bond/{eth=$1}/inet addr:'+
					IPAddress+'/{print eth;exit}\'')
				NIC=string.strip(out.read(), '\n')
				out.close()

			if DEBUGON:
				print 'GetOcfs2NIC: NIC = %s' % NIC
			return(NIC)
