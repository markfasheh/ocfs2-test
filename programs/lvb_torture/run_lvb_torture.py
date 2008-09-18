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
# You should have received a c.of the GNU General Public
# License along with this program; if not, write to the
# Free Software Foundation, Inc., 59 Temple Place - Suite 330,
# Boston, MA 021110-1307, USA.
#
# XXX: Future improvements:
#	 
# Program	: run_lvb_torture.py
# Description 	: Interface to run lvb_torture. Will validate parameters and
#		  properly configure LAM/MPI and start it, before starting
#		  the test.
#		  This progran will run on each node.
# Author  	: Marcos E. Matsunaga 

#
import os, stat, sys, time, optparse, socket, string, o2tf, pdb, random, config
#
#pdb.set_trace()
#
#args = sys.argv[1:]
#
DEBUGON = os.getenv('DEBUG',0)
#
pgm='run_lvb_torture'
uname = os.uname()
lhostname = str(socket.gethostname())
logfile = config.LOGFILE
iteractions = 10000
path = ''
procs = 2
dlmfs = '/dlm'
cmd = config.BINDIR+'/lvb_torture'
#
Usage = '\n	 %prog [-d | --dlmfs <dlmfs path>] \
[-i | --iteractions <iteractions>] \
[-H | --hbdev <heartbeat device>] \
[-l | --logfile <logfile>] \
[-n | --nodelist <nodelist>] \
<domain> <lockname> \
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
	parser.add_option('-d', 
		'--dlmfs', 
		dest='dlmfs',
		type='string',
		help='dlmfs pathname (dlmfs mountpoint - defaults to /dlm).')
#
	parser.add_option('-H', 
		'--hbdev', 
		dest='hbdev',
		type='string',
		help='OCFS2 heartbeat device name.')
#
	parser.add_option('-i', 
		'--iteractions', 
		dest='iteractions',
		type='int',
		help='Number of iteractions. Defaults to %s' % iteractions)
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
	(options, args) = parser.parse_args()
	if len(args) != 2:
		o2tf.printlog('args left %s' % len(args), 
			  logfile, 
		0, 
		'')
		parser.error('incorrect number of arguments')
	else:
		sys.argv[:] = args
		domain = sys.argv[0]
		lockname = sys.argv[1]
#
	if options.dlmfs:
		dlmfs = options.dlmfs
#
	if options.iteractions:
		iteractions = options.iteractions
#
	if options.hbdev:
		hbdev = ' -h '+options.hbdev
	else:
		hbdev = ''
#
	if options.logfile:
		logfile = options.logfile
#
	if options.nodelist:
		nodelist = options.nodelist
		nodelen = len(options.nodelist)
		if nodelen == 1:
			nodelist = nodelist.add(options.nodelist)
			procs=1
		else:
			nodelist = options.nodelist.split(',')
			procs=len(nodelist)
	else:
		parser.error('Please specify list of nodes to run the test.')

#
from os import access, W_OK
	
if DEBUGON:
	o2tf.printlog('%s: main - dlmfs = %s' % (pgm, dlmfs),
		logfile, 
		0, 
		'')
	o2tf.printlog('%s: main - iterations = %s' % (pgm, iteractions),
		logfile, 
		0, 
		'')
	o2tf.printlog('%s: main - hbdev = %s' % (pgm, hbdev),
		logfile, 
		0, 
		'')
	o2tf.printlog('%s: main - logfile = %s' % (pgm, logfile),
		logfile, 
		0, 
		'')
	o2tf.printlog('%s: main - nodelist = %s' % (pgm, nodelist),
		logfile, 
		0, 
		'')
#
o2tf.OpenMPIInit(DEBUGON, options.nodelist, logfile, 'ssh')
#
o2tf.openmpi_run(DEBUGON, procs, 
	str('%s -d %s %s -i %s %s %s 2>&1 | tee -a %s' % (cmd, 
	dlmfs, 
	hbdev, 
	iteractions, 
	domain,
	lockname,
	options.logfile)), 
	options.nodelist, 
	'ssh',
	options.logfile,
	'NOWAIT')
