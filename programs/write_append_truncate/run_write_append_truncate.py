#!/usr/bin/env python
#
#
# Copyright (C) 2008 Oracle.	All rights reserved.
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
# Program	: run_write_append_truncate.py
# Description 	: Python launcher to run write_append_truncate test.
#		Will validate parameters and properly configure openmpi and start it.
#		This progran will run on each node.
# Author	: Tristan.Ye	<tristan.ye@oracle.com>

#
import os, stat, sys, time, optparse, socket, string, o2tf, pdb, random, config
#
#
DEBUGON = os.getenv('DEBUG',0)
#
uname = os.uname()
lhostname = str(socket.gethostname())
logfile = config.LOGFILE
interface = 'eth0'
procs = 1
cmd = config.BINDIR+'/write_append_truncate'
#
Usage = """
%prog 
[-i | --iterations <iterations>] 
[-I | --interface <interface>] 
[-C | --cleanup] 
[-l | --logfile <logfile>] 
[-n | --nodelist <nodelist>] 
[-f | --filename <filename>] 
[-h | --help]
"""
#
# FUNCTIONS
#
def Cleanup(ret):
	from os import access, F_OK
	filename = options.filename
	if access(filename,F_OK) == 1:
		if DEBUGON:
			o2tf.printlog('write_append_truncate: Removing filename (%s)' 
				      % filename, logfile, 0, '')
			os.system('rm -f %s' % filename)
	sys.exit(ret)
#
# MAIN
#
if __name__=='__main__':
	parser = optparse.OptionParser(usage=Usage)
#
	parser.add_option('-C',
		'--cleanup',
		action="store_true",
		dest='cleanup',
		default=False,
		help='Perform directory cleanup.')
#
	parser.add_option('-i', 
		'--iterations', 
		dest='iterations',
		type='int',
		help='Number of iterations.')
#
	parser.add_option('-l', 
		'--logfile', 
		dest='logfile',
		type='string', 
		help='Logfile used by the process.')
#
	parser.add_option('-I', 
		'--interface', 
		dest='interface',
		type='string', 
		help='NIC used by MPI messaging.')
#
	parser.add_option('-n', 
		'--nodelist', 
		dest='nodelist',
		type='string', 
		help='List of nodes where test will run.')
#
	parser.add_option('-f', 
		'--filename', 
		dest='filename',
		type='string',
		help='filename used by the test.')
#
	(options, args) = parser.parse_args()
	if len(args) != 0:
		o2tf.printlog('args left %s' % len(args), 
			logfile, 0, '')
		parser.error('incorrect number of arguments')
#
#verify args
#
	if not options.filename:
		parser.error('filename is mandatory for test')


	if options.iterations:
		nloops = str(options.iterations)
	else:
		nloops = ''
	
	if options.filename:
		filename =  options.filename

	if options.logfile:
		logfile = options.logfile

	if options.interface:
		interface = options.interface

	if options.nodelist:
		nodelist = options.nodelist.split(',')
		nodelen = len(nodelist)
		procs = nodelen
		if nodelen == 1:
			nodelist = nodelist.append(options.nodelist)
		else:
			nodelist = options.nodelist.split(',')
	else:
		if not options.cleanup:
			parser.error('Invalid node list.')

if DEBUGON:
	o2tf.printlog('write_append_truncate: main - current directory %s' % 
		os.getcwd(), logfile, 0, '')
	o2tf.printlog('write_append_truncate: main - cmd = %s' % cmd,
		logfile, 0, '')
#
if options.cleanup:
	Cleanup(0)
o2tf.OpenMPIInit(DEBUGON, options.nodelist, logfile, 'ssh')
#
ret = o2tf.openmpi_run(DEBUGON, procs, 
	str('%s %s %s 2>&1 >> %s' % (cmd,
	filename,
	nloops,
	logfile)), 
	options.nodelist, 
	'ssh',
	interface,
	logfile,
	'WAIT')
#
if not ret:
	o2tf.printlog('write_append_truncate: main - execution successful.',
		logfile, 0, '')
Cleanup(ret)
