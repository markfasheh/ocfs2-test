#!/usr/bin/env python3
#
#
# Copyright (C) 2006 Oracle.    All rights reserved.
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
# Program		:	test_netfail.py
# Description	:	Start a buildkernel and after the test is going,
#						start to disable the NICs on each one of the nodes, 
#						leaving about 5 minutes gap to allow FS recovery.
#
# Author			:	Marcos E. Matsunaga 

#
import os, pwd, sys, optparse, socket, time, o2tf, pdb, config
#
DEBUGON = os.getenv('DEBUG',0)
#
logfile = config.LOGFILE
sleeptime = 600	# Default to 10 minutes
userid = pwd.getpwuid(os.getuid())[0]
hostname = str(socket.gethostname())
#
Usage = 'Usage: %prog [-c|--count count] \
[-d|--directorylist dirlist] \
[-h|--help] \
[-i|--initialize] \
[-l|-logfile logfilename] \
[-n|--nodes nodelist] \
[-s|--sleeptime sleeptime] \
[-t|--tarfile fullpath tar filename] \
[-u|--user username]'
#
if userid != 'root':
	o2tf.printlog('Not enough privileges to run. Need to be root.',
		logfile,
		0,
		'')
	sys.exit(1)
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
	parser.add_option('-d', 
		'--directorylist', 
		dest='dirlist',
		type='string',
		help='List of directories that will be used by the test.')
#
	parser.add_option('-i',
		'--initialize', 
		action="store_true",
		dest='initialize',
		default=False,
		help='Initialize directories before each run. Default=False.')
#
	parser.add_option('-l', 
		'--logfile', 
		dest='logfile',
		default='%s/log/test_netfail.log' % config.O2TDIR,
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
	parser.add_option('-s', 
		'--sleeptime', 
		dest='sleeptime',
		type='int',
		help='Wait time (in seconds) that it will wait to start disabling NICs.')
#
	parser.add_option('-t', 
		'--tarfile', 
		dest='tarfile', 
		type='string', 
		help='Fullpath filename of the tar file containing the kernel that \
			will be used.')
#
	parser.add_option('-u', 
		'--user', 
		dest='username', 
		type='string', 
		help='Username that will be used to run run_buildkernel.')

	(options, args) = parser.parse_args()
	if len(args) != 0:
		parser.error('incorrect number of arguments')
	if not options.tarfile:
		parser.error('Must provide a gzipped kernel tarball to run the test.')
	else:
		tarfile = options.tarfile
	if not options.username:
		parser.error('Must provide a username that will execute the test.')
	dirlist = options.dirlist.split(',')
	dirlen = len(dirlist)
	nodelist = options.nodelist.split(',')
	nodelen = len(nodelist)
	if nodelen < 3:
		parser.error('Must have at least 3 nodes to run this test.')
	if options.sleeptime:
		sleeptime = options.sleeptime
	if nodelen == 1:
		nodelist = nodelist.add(options.nodelist)
	else:
		nodelist = options.nodelist.split(',')
	if options.logfile:
		logfile = options.logfile
	if nodelen > config.NPROC:
	   nproc = nodelen
	else:
		nproc = config.NPROC
#
if DEBUGON:
	buildcmd=config.BINDIR+'/run_buildkernel.py --Debug'
else:
	buildcmd=config.BINDIR+'/run_buildkernel.py'
#
command = str('%s -c %s -d %s -l %s -n %s -t %s &' % (buildcmd,
	options.count,
	options.dirlist, 
	options.logfile, 
	options.nodelist,
	options.tarfile) )
#
if DEBUGON:
   o2tf.printlog('test_netfail: dirlist = (%s)' % dirlist, logfile, 0, '')
   o2tf.printlog('test_netfail: dirlen = (%s)' % dirlen, logfile, 0, '')
   o2tf.printlog('test_netfail: nodelist = (%s)' % nodelist, logfile, 0, '')
   o2tf.printlog('test_netfail: nodelen = (%s)' % nodelen, logfile, 0, '')
   o2tf.printlog('test_netfail: logfile = (%s)' % logfile, logfile, 0, '')
   o2tf.printlog('test_netfail: tarfile = (%s)' % tarfile, logfile, 0, '')
   o2tf.printlog('test_netfail: buildcmd = (%s)' % buildcmd, logfile, 0, '')
   o2tf.printlog('test_netfail: command = (%s)' % command, logfile, 0, '')
#
os.chmod(logfile,0777)
pid = os.spawnv(os.P_NOWAIT, '/bin/bash', ['/bin/bash', '-c', '/bin/su -l '+options.username+' -c \"'+command+'\"'])
#
cluster = o2tf.GetOcfs2Cluster()
#
for i in range(nodelen):
	o2tf.printlog('test_netfail: Sleeping for %s seconds' % sleeptime,
		logfile,
		0,
		'')
	time.sleep(sleeptime)
	x = o2tf.lrand(DEBUGON, nodelen)
	if DEBUGON:
		o2tf.printlog('test_netfail: Woke up from sleep',
			logfile,
			0,
			'')
		o2tf.printlog('test_netfail: Will disable NIC on %s' % (nodelist[x-1]),
			logfile,
			0,
			'')
	if nodelist[x-1] != hostname:
		os.system('ssh %s %s/ocfs2_nicdown.py -l %s' % (nodelist[x-1], 
			config.BINDIR,
			logfile))
o2tf.printlog('test_netfail: Test completed successfully.',
	logfile,
	3,
	'=')
