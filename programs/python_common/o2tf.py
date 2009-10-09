# *
# * o2tf.py
# *
# * Collection of functions used by ocfs2-test python programs.
# *
# * Copyright (C) 2004, 2008 Oracle.  All rights reserved.
# *
# * This program is free software; you can redistribute it and/or
# * modify it under the terms of the GNU General Public
# * License version 2 as published by the Free Software Foundation.
# *
# * This program is distributed in the hope that it will be useful,
# * but WITHOUT ANY WARRANTY; without even the implied warranty of
# * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# * General Public License for more details.
# *
# * Author : Marcos Matsunaga
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
#	- o2tf.py
#	- run_buildkernel.py
#	- buildkernel.py
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
			logfile, 0, '')
		printlog('o2tf.ClearDir: dirlist = (%s)' % dirlist,
			logfile, 0, '')
#
	if len(logfile) == 0:
		logfile=str(os.getcwd()  + '/ClearDir.log')
#
	from os import access,F_OK
	for i in range(dirlen):
		wdir=dirlist[i] + '/' + nodename
		if os.access(wdir, F_OK) == 0:
			if DEBUGON:
				printlog('o2tf.ClearDir: Directory %s does \
					not exists.' % wdir,
					logfile, 0, '')
			continue
		if DEBUGON:
			printlog('o2tf.ClearDir: Removing directory %s.' % \
				wdir, logfile, 0, '')
		os.system('rm -fr '+ wdir)
#
# extract_tar is used by :
#   - o2tf.py
#   - buildkernel.py
def extract_tar(DEBUGON, logfile, dirl, tarfile):
	'Extract the kernel source tarfile into each one of the node \
	directories, if they do not exist'
	nodename = str(socket.gethostname())
	dirlist=dirl.split(',')
	dirlen=len(dirlist)
	if DEBUGON:
		printlog('o2tf.extract_tar: logfile = (%s)' % \
			logfile, logfile, 0, '')
		printlog('o2tf.extract_tar: dirlist = (%s)' % \
			dirlist, logfile, 0, '')
		printlog('o2tf.extract_tar: tarfile = (%s)' % \
			tarfile, logfile, 0, '')
#
	if len(logfile) == 0:
		logfile=str(os.getcwd()  + '/extract_tar.log')
	CreateDir(DEBUGON, dirl, logfile)
	from os import access,F_OK
	for i in range(dirlen):
		wdir=dirlist[i] + '/' + nodename
		if DEBUGON:
			printlog('o2tf.extract_tar: wdir = %s' % wdir, 
				logfile, 0, '')
		if os.access(wdir, F_OK) == 1:
			if DEBUGON:
				printlog('o2tf.extract_tar: Directory %s \
					already exists.  Skipping...' % wdir,
					logfile, 0, '')
			continue
		if DEBUGON:
			printlog('o2tf.extract_tar: Creating directory %s.' \
				% wdir, logfile, 0, '')
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
			logfile, 0, '')
		printlog('o2tf.CreateDir: dirlen = (%s)' % ndir,
			logfile, 0, '')
	from os import access,F_OK
	for i in range(ndir):
#
		if os.access(dirlist[i],F_OK) == 0:
			if DEBUGON:
				printlog('o2tf.CreateDir: Directory '+
					'%s does not exist. Creating it.' % \
					dirlist[i], logfile, 0, '')
			os.makedirs(dirlist[i],0755)
		if DEBUGON:
			printlog('o2tf.CreateDir: Ended.',
				logfile, 0, '')

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
	options = 'xvf'
	compress = 'z'
	st = os.stat(destdir)
	if stat.S_ISREG(st.st_mode):
		o2tf.printlog('o2tf.untar: (%s) is a regular file. Can\'t \
			extract tarfile into a regular file.' % \
			destdir, logfile, 0, '')
	else:
		if DEBUGON:
			printlog('o2tf.untar: Extracting tar file %s into %s \
				directory.' %  (tarfile, destdir),
				logfile, 0, '')
		t1 = time.time()
		os.system('cd %s; tar %s %s 2>&1 1>> %s; du -sh * 1>> %s' % \
			(destdir, options + compress, tarfile, logfile,
				logfile))
		t2 = time.time()
		printlog('o2tf.untar: Extraction elapsed time = %s' % \
			(t2 - t1), logfile, 0, '')
		if DEBUGON:
			printlog('o2tf.untar: Extraction ended.', 
				logfile, 0, '')
#
# StartMPI for openmpi
#
def OpenMPIInit(DEBUGON, nodes, logfile, remote_sh):
	"""
	Since Openmpi no longer need startup until executions issued,
so just do a sanity check here to test if all nodes are available.
	"""
	from os import access,F_OK
	if os.access(config.MPIRUN, F_OK) == 0:
		printlog('o2tf.StartMPI: mpirun not found',
			logfile, 0, '')
		sys.exit(1)
	if os.access(config.MPIHOSTS, F_OK) == 1:
		os.system('rm -f ' + config.MPIHOSTS)
	nodelist = string.split(nodes,',')
	nodelen = len(nodelist)

	if remote_sh == '' or remote_sh == 'ssh':
		shopt = '-mca plm_rsh_agent ssh:rsh'
	else:
		shopt = '-mca plm_rsh_agent rsh:ssh'

	fd = open(config.MPIHOSTS,'w',0)
	for i in range(nodelen):
		fd.write(nodelist[i] + '\n')
	fd.close()

	try:
		if DEBUGON:
			printlog('o2tf.StartOpenMPI: Trying to execute %s with \
				 a simple command remotely among (%s)' \
				  % (config.MPIRUN, nodes),
				logfile, 0, '')
		os.system('%s  %s --hostfile %s %s' % (config.MPIRUN, shopt,
			  config.MPIHOSTS, 'echo -n'))

	except os.error,inst:
		printlog(str(inst), logfile, 0, '')
		pass
#
# Calls mpirun from openmpi
#
def openmpi_run(DEBUGON, nproc, cmd, nodes, remote_sh, logfile, w_flag):
	"""
	Execute commands in parallel using OpenMPI.'
	"""
	from os import access,F_OK
	pid = 0
        status = 0
	found = 0
	uname = os.uname()
	nodelen = len(string.split(nodes,','))
	if nproc == 'C':
		nprocopt=''
	else:
		nprocopt='-np ' + str(nproc)

	if remote_sh == '' or remote_sh == 'ssh':
		shopt = '-mca plm_rsh_agent ssh:rsh'
	else:
		shopt = '-mca plm_rsh_agent rsh:ssh'
	try:
		if DEBUGON:
			printlog('o2tf.mpi_run: MPIRUN = %s' % config.MPIRUN,
				logfile, 0, '')
			printlog('o2tf.mpi_run: nproc = %s' % nproc,
				logfile, 0, '')
			printlog('o2tf.mpi_run: nodelen = %d' % nodelen,
				logfile, 0, '')
			printlog('o2tf.mpi_run: shopt = %s' % shopt,
				logfile, 0, '')
			printlog('o2tf.mpi_run: cmd = %s' % cmd,
				logfile, 0, '')
		pid = os.spawnv(os.P_NOWAIT,
			'/bin/bash', ['bash', '-xc',
			config.MPIRUN + ' -mca btl tcp,self %s %s --host %s %s' % \
			(shopt, nprocopt, nodes, cmd)])
		if w_flag == 'NOWAIT':
			return pid
		else:
			(pid, status) = os.waitpid(pid, 0)
			return status

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
		configdir = '/config/cluster'
	elif (os.path.isdir('/sys/kernel/config/cluster')):
		configdir = '/sys/kernel/config/cluster'
	out = os.popen('ls %s' % configdir)
	CLUSTER = string.strip(out.read(),'\n')
	return(CLUSTER)
#
# GetOcfs2NIC is used by:
#
def GetOcfs2NIC(DEBUGON, Cluster):
	""" Find and return the NIC used by OCFS2"""
	hostname = str(socket.gethostname())
	if (os.path.isdir('/config/cluster')):
		configdir = '/config/cluster'
	elif (os.path.isdir('/sys/kernel/config/cluster')):
		configdir = '/sys/kernel/config/cluster'
	nodedir=os.path.join(configdir, Cluster, 'node', hostname)
	if (os.path.isdir(nodedir)):
		os.chdir(nodedir)
		from os import access,F_OK
	if os.access('ipv4_address',F_OK) == 1:
			fd = open('ipv4_address','r',0)
			IPAddress=string.strip(fd.read(), '\n')
			if DEBUGON:
				print 'GetOcfs2NIC: IPAddress = %s' % \
					IPAddress
			fd.close()
			out = os.popen('/sbin/ifconfig | awk \' \
				/^eth/{eth=$1}/inet addr:'+ 
				IPAddress+'/{print eth;exit}\'')
			NIC=string.strip(out.read(), '\n')
			out.close()
			if not NIC:
				out = os.popen('/sbin/ifconfig | awk \' \
					/^bond/{eth=$1}/inet addr:'+
					IPAddress+'/{print eth;exit}\'')
				NIC=string.strip(out.read(), '\n')
				out.close()

			if DEBUGON:
				print 'GetOcfs2NIC: NIC = %s' % NIC
			return(NIC)
#
# CheckMounted is used by:
#
def CheckMounted(DEBUGON, logfile, keyword):
	'''Check if a partition is mounted based on device or label'''
	from commands import getoutput 
	count = getoutput('df -k |grep -v Filesystem| grep %s|wc -l' % keyword)
	if DEBUGON:
		printlog('o2tf.CheckMounted:  count %s)' % count,
			logfile, 0, '')
	if int(count) == 0:
		return False
	else:
		return True
#
# FindMountPoint is used by:
#
def FindMountPoint(DEBUGON, logfile, filedir):
	'''Find and return mountpoint/device based on file/directory'''
	from commands import getoutput 
	line = getoutput('df -k %s|grep -v Filesystem' % filedir)
	linelist = line.split(" ")
	for i in range(linelist.count('')):
		linelist.remove('')
	if linelist[0] == 'df:':
		return 1
	if DEBUGON:
		printlog('o2tf.FindMountPoint:  linelist %s)' % linelist,
			logfile, 0, '')
	return linelist[0],linelist[5]
#
# GetLabel is used by:
#
def GetLabel(DEBUGON, logfile, devname):
	'''Find and return device Label based on devicename'''
	from commands import getoutput 
	line = getoutput('sudo /sbin/mounted.ocfs2 -d %s|grep -v UUID' % \
		devname)
	linelist = line.split(" ")
	if DEBUGON:
		printlog('o2tf.GetLabel:  linelist %s)' % linelist,
			logfile, 0, '')     
	for i in range(linelist.count('')):
		linelist.remove('')
	if DEBUGON:
		printlog('o2tf.GetLabel:  linelist %s)' % linelist,
			logfile, 0, '')
	return linelist[3]
#
# SudoUmount is used by:
#
def SudoUmount(DEBUGON, logfile, mountpoint):
	'''Find and return device Label based on devicename'''
	from commands import getstatusoutput
	status = getstatusoutput('sudo umount %s' % mountpoint)
	if status[0] == 0:
		return
	else:
		printlog('o2tf.SudoUmount:  Umount failed (RC=%s)' % status[0],
			logfile, 0, '')
		printlog('o2tf.SudoUmount:  %s' % status[1],
			logfile, 0,'')
#
# SudoMount is used by:
#
def SudoMount(DEBUGON, logfile, mountpoint, label, options):
	'''Find and return device Label based on devicename'''
	from commands import getstatusoutput
	status = getstatusoutput('sudo mount LABEL=%s %s %s' % (label, options,
		mountpoint))
	if status[0] == 0:
		return
	else:
		printlog('o2tf.SudoMount:  Mount failed (RC=%s)' % status[0],
			logfile, 0, '')
		printlog('o2tf.SudoMount:  %s' % status[1],
			logfile, 0, '')
#
def Del(DEBUGON, logfile, deldir, dirlist):
	import config
	ForbitMntPts = [ '/', '/usr', '/home', '*', '/var', '/opt', 
		'/usr/local']
	if not logfile:
		logfile = config.logfile
	if dirlist:
		dirl = string.split(dirlist, ',')
	# See if it is deleting everything, including under root.
	if deldir == '*' or deldir == '/*':
		printlog('o2tf.del - Cannot perform generic delete (* or /*)',
			logfile, 0, '')
		return
	# Before proceeding, check if dir/file to be deleted exists
	if not os.path.exists(deldir):
		printlog('%s : file or directory not found.' %
			deldir, logfile, 0, '')
		printlog('Aborting.', logfile, 0, '')
		return 1
	# First check if the dirlist is valid
		for i in range(len(dirl)):
			if not os.path.exists(dirl[i]):
				printlog('%s : file or directory not found.' %
					dirl[i], logfile, 0, '')
				printlog('Aborting.', logfile, 0, '')
				return 1
			if os.path.isfile(dirl[i]):
				printlog('%s must be a direcftory' %
					dirl[i], logfile, 0, '')
				printlog('Aborting.', logfile, 0, '')
				return 1
			if not os.path.isabs(dirl[i]):
				printlog('%s directory must be absolut path.' %
					dirl[i], logfile, 0, '')
				printlog('Aborting.', logfile, 0, '')
				return 1

	# Choose which list to be used. Allowed or forbid.
	if dirlist:
		if DEBUGON:
			printlog('o2tf.Del - dirlist %s' % dirlist, logfile, 0, '')
		AllowMntPt = BldAllowedMtPts(DEBUGON, logfile, dirlist)
		mntpt = GetMntPt(DEBUGON, logfile, deldir)
		if DEBUGON:
			printlog('AllowMntPt (%s) - mntpt (%s)' % 
				(AllowMntPt, mntpt), logfile, 0, '')
		if mntpt not in AllowMntPt:
			printlog('o2tf.Del - Cannot delete dir %s' % deldir,
				logfile, 0, '')
			return 1
	else:
		mntpt = GetMntPt(DEBUGON, logfile, deldir)
		if mntpt in ForbitMntPts:
			printlog('o2tf.Del - Cannot delete file/dir in the \
				forbid list (%s)' % ForbitMntPts,
				logfile, 0, '')
			return 1
	if DEBUGON:
		printlog('o2tf.Del - Deleting dir %s' % deldir,
			logfile, 0, '')
	os.system('rm -fr '+deldir)
#
def BldAllowedMtPts(DEBUGON, logfile, allowlist):
	from os import access,F_OK
	WorkingDirs = string.split(allowlist, ',')
	if DEBUGON:
		printlog('working lenght = %s, (%s)' % (len(WorkingDirs),
			WorkingDirs), logfile, 0, '')
	AllowMntPt=[None]*len(WorkingDirs)
	if len(WorkingDirs) == 1:
		if DEBUGON:
			printlog('Only one working dir',
				logfile, 0, '')
		AllowMntPt[0]=GetMntPt(DEBUGON, logfile, allowlist)
	else:
		for i in range(len(WorkingDirs)):
			if os.access(WorkingDirs[i],F_OK) == 0:
				os.system('touch ' + logfile)
			if DEBUGON:
				printlog('workingdirs[%s] = (%s)' % 
					(i, WorkingDirs[i]), logfile, 0, '')
			AllowMntPt[i]=GetMntPt(DEBUGON, logfile, WorkingDirs[i])
	if DEBUGON:
		printlog('Allow Mount Points list (%s)' % AllowMntPt,
			logfile, 0, '')
	return AllowMntPt

#
def GetMntPt(DEBUGON, logfile, dirname):
	import os, os.path
	if (os.path.isdir(dirname)):
		if os.path.ismount(dirname):
			return dirname
	xx =os.path.split(dirname)
	if DEBUGON:
		printlog('GetMntPt : Starting - xx[0]=(%s)' % xx[0],
			logfile, 0, '')
	while True:
		if os.path.ismount(xx[0]):
			break
		else:
			if DEBUGON:
				printlog('GetMntPt : Looking - xx[0]=(%s)' % \
				xx[0], logfile, 0, '')
			xx=os.path.split(xx[0])
	if DEBUGON:
		printlog('GetMntPt : Exiting - xx[0]=(%s)' % xx[0],
			logfile, 0, '')
	return xx[0]
#
