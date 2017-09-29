#!/usr/bin/env python3
# *
# * run_buildkernel.py
# *
# * Run a kernel build process per partition on each node.
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
# *

#
import os, sys, optparse, time, o2tf, pdb, config
#
DEBUGON = os.getenv('DEBUG',0)
#
logfile = config.LOGFILE
PID=os.getpid()
#
def CompSize(tarfile):
	global logfile, DEBUGON
	from os import access,F_OK
	tmpfile='/tmp/src_%s.dat' % PID
	if os.access(tarfile,F_OK) == 1:
		os.system('tar tvfz %s > %s' % (tarfile, tmpfile))
	fd=open(tmpfile, 'r', 0)
	size=0
	while True:
		line = fd.readline()
		if not line: break
		linelist = line.split(" ")
		for i in range(linelist.count('')):
			linelist.remove('')
		if DEBUGON:
			o2tf.printlog('size = %s, filesize = %s' % \
				(size, linelist[2]), logfile, 0, '')
		size = size + int(linelist[2])
	return size
#
def CheckDirs(nodes, dirs):
	'Check if the test directories at least exist'
	global tarfile, DEBUGON
	from subprocess import getoutput
	nodesl = nodes.split(',')
	dirl = dirs.split(',')
	MinSize = CompSize(tarfile)
	InitTrue = False
	for x in range(len(dirl)):
		if InitTrue:
			break
		for y in range(len(nodesl)):
			workdir=os.path.join(dirl[x], nodesl[y])
			from os import access,F_OK
			if os.access(workdir,F_OK) == 0:
				os.mkdir(workdir)
				InitTrue = True
				break
			size = getoutput('du -sb %s|cut -f1' % workdir)
			if DEBUGON:
				o2tf.printlog('workdir(%s) Minsize(%s) \
					 size (%s)' % \
					(workdir, MinSize, size), 
					logfile, 0, '')
			if int(MinSize) > int(size):
				InitTrue = True
				break
	if InitTrue:
		o2tf.printlog('At least one directory seems incomplete. \
			Will initialize them all',
			logfile, 0, '')
		Initialize()
#
def Initialize():
	global DEBUGON, logfile
	'Initialize the directories (remove and extract)'
#
	o2tf.printlog('Cleaning up directories.', logfile, 0, '')
	o2tf.OpenMPIInit(DEBUGON, options.nodelist, logfile, 'ssh')
	o2tf.openmpi_run(DEBUGON, nproc, str('%s -c -d %s -l %s' % \
			(buildcmd, 
			options.dirlist, 
			options.logfile) ),
			options.nodelist, 
			'ssh',
			options.interface,
			options.logfile,
			'WAIT')
#
	o2tf.printlog('Extracting tar file into directories.', logfile, 0, '')
	o2tf.openmpi_run(DEBUGON, nproc, str('%s -e -d %s -l %s -t %s' % \
			(buildcmd, 
			options.dirlist, 
			options.logfile,
			tarfile) ),
			options.nodelist, 
			'ssh',
			options.interface,
			options.logfile,
			'WAIT')
	o2tf.printlog('Directories initialization completed.', logfile, 0, '')
#
Usage = 'Usage: %prog [-c|--count count] \
[-d|--directorylist dirlist] \
[-h|--help] \
[--initialize] \
[-i|--if <Network Interface>] \
[-l|-logfile logfilename] \
[-n|--nodes nodelist] \
[--nocheck] \
[-t|--tarfile fullpath tar filename] \
[-u|--user username]'
#
if __name__=='__main__':
	parser = optparse.OptionParser(Usage)
#
	parser.add_option('-c', 
		'--count', 
		dest='count', 
		default='1',
		type='int', 
		help='Number of times it will build the kernel.  Default = 1.')
#
	parser.add_option('-u', 
		'--user', 
		dest='userid',
		type='string', 
		help='Userid used to open ssh connections.')
#
	parser.add_option('-d', 
		'--directorylist', 
		dest='dirlist',
		type='string',
		help='List of directories that will be used by the test.')
#
	parser.add_option('--initialize', 
		action="store_true",
		dest='initialize',
		default=False,
		help='Initialize directories before each run. Default=False.')
#
	parser.add_option('-i', 
		'--if', 
		dest='interface',
		type='string',
		help='Network Interface name to be used for MPI messaging.')
#
	parser.add_option('-l', 
		'--logfile', 
		dest='logfile',
		default='%s/log/run_buildkernel.log' % config.O2TDIR,
		type='string', 
		help='If logfile is specified, a single logfile will be used by \
			all processes, otherwise, individual logfiles will be created per \
			process. Default will be to create a logfile per process.')
#
	parser.add_option('-n', 
		'--nodes', 
		dest='nodelist',
		type='string',
		help='List of nodes where the test will be executed.')
#
	parser.add_option('--nocheck', 
		action="store_true",
		dest='nocheckdirs',
		default=False,
		help='Ignore directory check before the run. Default=False.')
#
	parser.add_option('-t', 
		'--tarfile', 
		dest='tarfile', 
		type='string', 
		help='Fullpath filename of the tar file containing the kernel that \
			will be used.')

	(options, args) = parser.parse_args()
	if len(args) != 0:
		parser.error('incorrect number of arguments')
	if not options.tarfile:
		parser.error('Must provide a gzipped kernel tarball to run the test.')
	dirlist = options.dirlist.split(',')
	dirlen = len(dirlist)
	nodelist = options.nodelist.split(',')
	nodelen = len(nodelist)
	if nodelen == 1:
		nodelist = nodelist.append(options.nodelist)
	else:
		nodelist = options.nodelist.split(',')
	logfile = options.logfile
	interface = options.interface
	tarfile = options.tarfile
	if nodelen > config.NPROC:
	   nproc = nodelen
	else:
		nproc = config.NPROC
#
if DEBUGON:
	buildcmd= config.BINDIR+'/buildkernel.py -D'
else:
	buildcmd= config.BINDIR+'/buildkernel.py'
#
if DEBUGON:
   o2tf.printlog('run_buildkernel: dirlist = (%s)' % dirlist, logfile, 0, '')
   o2tf.printlog('run_buildkernel: dirlen = (%s)' % dirlen, logfile, 0, '')
   o2tf.printlog('run_buildkernel: nodelist = (%s)' % nodelist, logfile, 0, '')
   o2tf.printlog('run_buildkernel: interface = (%s)' % interface, logfile, 0, '')
   o2tf.printlog('run_buildkernel: nodelen = (%s)' % nodelen, logfile, 0, '')
   o2tf.printlog('run_buildkernel: logfile = (%s)' % logfile, logfile, 0, '')
   o2tf.printlog('run_buildkernel: tarfile = (%s)' % tarfile, logfile, 0, '')
   o2tf.printlog('run_buildkernel: buildcmd = (%s)' % buildcmd, logfile, 0, '')
#
if options.initialize:
	Initialize()
elif not options.nocheckdirs:
	CheckDirs(options.nodelist, options.dirlist)
#
for i in range(options.count):
	r = i+1
	o2tf.printlog('run_buildkernel: Starting RUN# %s of %s' % (r, options.count),
		logfile, 3, '=')
	o2tf.OpenMPIInit(DEBUGON, options.nodelist, logfile, 'ssh')
	ret = o2tf.openmpi_run(DEBUGON, nproc, str('%s -d %s -l %s -n %s' % \
			(buildcmd, 
			options.dirlist, 
			options.logfile, 
			options.nodelist) ), 
			options.nodelist, 
			'ssh',
			options.interface,
			options.logfile,
			'WAIT' )
if not ret:
	o2tf.printlog('run_buildkernel: main - execution successful.',
		logfile, 0, '')
	sys.exit(0)
else:
	o2tf.printlog('run_buildkernel: main - execution failed.',
		logfile, 0, '')
	sys.exit(1)
