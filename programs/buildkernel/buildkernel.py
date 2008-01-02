#!/usr/bin/env python
#
#
# Copyright (C) 2006 Oracle.	All rights reserved.
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public
# License as published by the Free Software Foundation; either
# version 2 of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of 
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public
# License along with this program; if not, write to the
# Free Software Foundation, Inc., 59 Temple Place - Suite 330,
# Boston, MA 021110-1307, USA.
#
# XXX: Future improvements:
#	 
# Program	  : buildkernel.py
# Description : Run a build of the kernel on each one of the ocfs2
#					 partitions mounted, in parallel.
#					 i.e: The system has 7 partitions, it will run 7
#							builds in parallel, one for each partition.
# Author		  : Marcos E. Matsunaga 
# E-mail		  : Marcos.Matsunaga@oracle.com

#
import os, sys, time, optparse, socket, string, o2tf, pdb, timing, time, config
#
DEBUGON = os.getenv('DEBUG',0)
#
CHECKTHREAD_SLEEPTIME = 30
#
uname = os.uname()
lhostname = str(socket.gethostname())
logfile = config.LOGFILE
nodelist = ''
extract_tar = ''
#
# FUNCTIONS
#
def tbuild(thrid, command):
	'tbuild spawn process and build a process list'
	'tbuild takes 2 arguments:'
	'	 thrid - Thread number, which will index pidlist'
	'	 command - command to be executed'
	if DEBUGON:
		o2tf.printlog('buildkernel:tbuild - current directory %s' % os.getcwd(), 
			logfile, 
			0, 
			'')
		o2tf.printlog('buildkernel:tbuild - command	%s ' % command, 
			logfile, 
			0, 
			'')
	pidlist[thrid] = os.spawnv(os.P_NOWAIT, 
			'/bin/bash', 
			['bash', 
			'-c', 
			command])
#	 
def check_thread(logfile):
	'check_thread checks the pidlist for running tasks until the last one '
	'completes.'
	'check_thread takes 1 argument:'
	'	 logfile - logfile name'
	o2tf.printlog('buildkernel:check_thread: Waiting for processes to finish '
		'on thread ',
		logfile, 
		0, 
		'')
	while len(pidlist) > 0:
		o2tf.printlog('buildkernel:check_thread: Checking thread processes',
			logfile, 
			0, 
			'')
		o2tf.printlog('buildkernel:check_thread: pid list %s' % (pidlist), 
			logfile, 
			0, 
			'')
		for z in range(len(pidlist)):
			out = os.waitpid(pidlist[z],os.WNOHANG)
			o2tf.printlog('buildkernel:check_thread: z=%s, out=%s' % (z, out[1]), 
				logfile, 
				0, 
				'')
			if out[0] > 0:
				o2tf.printlog('buildkernel:check_thread: Removing pid '
					'%s from the list' %	pidlist[z], 
					logfile, 
					0, 
					'')
				t2 = time.time()
				o2tf.printlog('buildkernel:check_thread: %s [%s]: Build '
					'time is %f seconds' % 
					(str(socket.gethostname()), pidlist[z], t2 - t1), 
					logfile, 
					0, 
					'')
				pidlist.remove(out[0])
				break
		time.sleep(CHECKTHREAD_SLEEPTIME)
#
# MAIN
#
Usage = 'Usage: %prog [-c|--cleardir] \
[-d|--dirlist dirlist] \
[-e|--extract] \
[-l|-logfile logfilename] \
[-n | --nodelist nodename] \
[-t|--tarfile fullpath tar filename] \
[-h|--help]'
if __name__=='__main__':
	parser = optparse.OptionParser(Usage, conflict_handler="resolve")
#
	parser.add_option('-c', 
		'--cleardir', 
		action="store_true",
		dest='cleardir',
		default=False,
		help='Clear the node directories on the partitions. Default=False.')
#
	parser.add_option('-d', 
		'--directorylist', 
		dest='dirlist',
		type='string',
		help='List of directories that will be used by the test. ')
#
	parser.add_option('-D',
		'--debug', 
		action="store_true",
		dest='debug',
		default=False,
		help='Turn the debug option on. Default=False.')
#
	parser.add_option('-e',
		'--extract_tar', 
		action="store_true",
		dest='extract_tar',
		default=False,
		help='Extract Linux tree kernel. Default=False.')
#
	parser.add_option('-l', 
		'--logfile', 
		dest='logfile',
		type='string', 
		help='If logfile is specified, a single logfile will \
			be used by all processes, otherwise, \
			individual logfiles will be created per \
			process. Default will be to create a logfile \
			per process.')
#
	parser.add_option('-n', 
		'--nodelist', 
		dest='nodelist',
		type='string', 
		help='Nodename owner of the directory it will run the \
			find command. Default to local hostname.')
#
	parser.add_option('-t',
		'--tarfile',
		dest='tarfile',
		type='string',
		help='Fullpath filename of the tar file containing \
			the kernel that will be used.')

#
	(options, args) = parser.parse_args()
	if len(args) != 0:
		parser.error('incorrect number of arguments')
#
	if options.debug:
		DEBUGON=1
	cleardir = options.cleardir
	extract_tar = options.extract_tar
	if options.extract_tar:
		if not options.tarfile:
			parser.error('Must provide a gzipped kernel tarball to run the test.')
		else:
			tarfile = options.tarfile
	if not options.dirlist:
		parser.error('Must provide a directory list where the test will run.')
	dirlist = options.dirlist.split(',')
	dirlen = len(dirlist)
	if options.logfile:
		logfile = options.logfile
	if not options.cleardir and not options.extract_tar:
		if not options.nodelist:
			parser.error('Must provide a list of nodes where the test will run.')
		nodelen = len(options.nodelist)
		if nodelen == 1:
			nodelist = nodelist.add(options.nodelist)
		else:
			nodelist = options.nodelist.split(',')
			nodelen = len(nodelist)
#
if DEBUGON:
	o2tf.printlog('buildkernel: cleardir = (%s)' % cleardir, 
		logfile, 
		0, 
		'')
	o2tf.printlog('buildkernel: dirlist = (%s)' % dirlist, 
		logfile, 
		0, 
		'')
	o2tf.printlog('buildkernel: dirlen = (%s)' % dirlen, 
		logfile, 
		0, 
		'')
	o2tf.printlog('buildkernel: logfile = (%s)' % logfile, 
		logfile, 
		0, 
		'')
	o2tf.printlog('buildkernel: nodelist = (%s)' % nodelist, 
		logfile, 
		0, 
		'')
	if options.extract_tar:
		o2tf.printlog('buildkernel: tarfile = (%s)' % tarfile, 
			logfile, 
			0, 
			'')
	o2tf.printlog('buildkernel: extract_tar = (%s)' % extract_tar, 
		logfile, 
		0, 
		'')
#
if cleardir:
	o2tf.ClearDir(DEBUGON, logfile, options.dirlist)
	sys.exit()
else:
	if extract_tar:
		o2tf.extract_tar(DEBUGON, logfile, options.dirlist, options.tarfile)
		sys.exit()
#
# For each entry in dirlist, fork a process to run the build and a find on the
# directory owned by another node.
#
# First find which node to run the find. This is the only reason a nodelist
# is passed in the arguments list.
#
for y in range(nodelen):
	if lhostname == nodelist[y]:
		if y == 0:
			nodefind = nodelist[ -1 ]
		else:
			nodefind = nodelist[ y - 1 ]
		if DEBUGON:
			o2tf.printlog('buildkernel:Main - Node to use on find %s' % nodefind, 
				logfile, 
				0, 
				'')
	else:
		nodefind = nodelist[y]

#
import os.path
logdir = os.path.dirname(logfile)
pidlist = [0] * dirlen * 2
for x in range(dirlen):
	buildlog = logdir + '/build_' + lhostname + '_' + str(x) + '.log'
	findlog = logdir + '/find_' + lhostname + '_' + str(x) + '.log'
	wdir = dirlist[x] + '/' + str(socket.gethostname()) +'/'+ config.KERNELDIR
	cmd = 'cd ' + wdir + ';  make mrproper 2>&1 1>> %s; \
			make defconfig 2>&1 1>> %s; /usr/bin/make -j2 V=1 2>&1 1>> %s' %  \
			(buildlog, buildlog, buildlog)
#
	if DEBUGON:
		o2tf.printlog('buildkernel:Main - current directory %s' % os.getcwd(),
			logfile,
			0,
			'')
		o2tf.printlog('buildkernel:Main - working directory %s' % dirlist[x],
			logfile,
			0,
			'')
		o2tf.printlog('buildkernel:Main - wdir =  %s' %  wdir,
			logfile,
			0,
			'')
		o2tf.printlog('buildkernel:Main - cmd = %s' % cmd,
			logfile,
			0,
			'')
	t1 = time.time()
	tbuild(x, cmd)
	cmd = 'cd ' + dirlist[x] + '/' + nodefind + '; find . -print \
		2>&1 1>> %s' % findlog
	tbuild(int(x + dirlen), cmd)
	if DEBUGON:
		o2tf.printlog('buildkernel:Main - cmd = %s' % cmd, logfile, 0, '')
check_thread(logfile)
