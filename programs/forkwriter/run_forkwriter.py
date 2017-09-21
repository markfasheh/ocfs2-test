#!/usr/bin/env python3
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
# You should have received a c.of the GNU General Public
# License along with this program; if not, write to the
# Free Software Foundation, Inc., 59 Temple Place - Suite 330,
# Boston, MA 021110-1307, USA.
#
# XXX: Future improvements:
#	 
# Program	  : run_forkwriter.py
# Description : Interface to run run_forkwriter. Will validate parameters and
#					 properly configure LAM/MPI and start it, before starting
#					 the forkwriter.py.
#					 This progran will run on each
#		node.
# Author		  : Marcos E. Matsunaga 

#
import os, stat, sys, time, optparse, socket, string, o2tf, pdb, random, config
#
#pdb.set_trace()
#
#args = sys.argv[1:]
#
MINBLOCKSIZE = 512
MAXBLOCKSIZE = 8192
UNIQUEFILE = 0
#
DEBUGON = os.getenv('DEBUG',0)
#
uname = os.uname()
lhostname = str(socket.gethostname())
procs = 1
count = 5
logfile = config.LOGFILE
blocksize = '512,4096'
seconds = 60
filename = 'test_writetorture'
#
Usage = '\n	 %prog [-c|--count <count>] \
[-D | --Debug] \
[-f | --file <full path filename>] \
[-l | --logfile <logfile>] \
[-n | --nodelist nodelist] \
[-p | --procs procs] \
[-s | --sleep <ms in between each write (50000)>] \
[-h|--help]'
#
# FUNCTIONS
#
#
# MAIN
#
if __name__=='__main__':
	parser = optparse.OptionParser(usage=Usage)
#
	parser.add_option('-c', 
		'--count', 
		dest='count',
		type='int',
		help='Number of times that test will execute.')
#
	parser.add_option('-D', 
		'--Debug', 
		dest='Debug',
		action='store_true',
		default=False)
#
	parser.add_option('-f', 
		'--filename', 
		dest='filename',
		type='string',
		help='Full path filename that will be used by the test.')
#
	parser.add_option('-l', 
		'--logfile', 
		dest='logfile',
		type='string', 
		help='Logfile used by the process.')
#
	parser.add_option('-n', 
		'--nodelist', 
		dest='nodelist',
		type='string', 
		help='List of nodes where test will run.')
#
	parser.add_option('-p', 
		'--procs', 
		dest='procs',
		type='int', 
		help='Number of copies of the test program to run.')
#
	parser.add_option('-s', 
		'--sleep', 
		dest='sleep',
		type='int', 
		help='ms in between each write (50000)')
#
	(options, args) = parser.parse_args()
	if len(args) != 0:
		o2tf.printlog('args left %s' % len(args), 
			  logfile, 
		0, 
		'')
		parser.error('incorrect number of arguments')
#
	if options.Debug:
		DEBUGON=1
#
	if options.count:
		count = options.count
#
	if options.filename:
		filename = options.filename
#
	if options.logfile:
		logfile = options.logfile
#
	if options.nodelist:
		nodelist = options.nodelist
		nodelen = len(options.nodelist)
		if nodelen == 1:
			nodelist = nodelist.add(options.nodelist)
		else:
			nodelist = options.nodelist.split(',')
	else:
		parser.error('Invalid node list.')

#
	if options.procs:
		procs = options.procs
#
	if options.sleep:
		sleep = options.sleep
#
cmd = config.BINDIR+'/forkwriter'
#
if DEBUGON:
	o2tf.printlog('run_forkwriter: main - cmd = %s' % cmd,
		logfile, 
		0, 
		'')
#
o2tf.OpenMPIInit(DEBUGON, options.nodelist, logfile, 'ssh')
#
for z in range(options.count):
	o2tf.printlog('run_forkwriter: Running test# %s of %s' % (z, options.count), 
	logfile, 
	0, 
	'')
	ret = o2tf.openmpi_run(DEBUGON, options.procs, str('%s %s %s %s %s' %
		(cmd, 
		options.filename, 
		options.count, 
		options.procs, 
		options.sleep ) ), 
		options.nodelist, 
		'ssh',
		options.logfile,
		'WAIT')
	if not ret:
		o2tf.printlog('run_forkwriter: RUN# %s execution successful.' \
		% z, logfile, 0, '')
	else:
		o2tf.printlog('run_forkwriter: RUN# %s execution failed.' % z,
			logfile, 0, '')
		sys.exit(1)
