#!/usr/bin/env python3
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
# Program	: run_flock_unit_test.py
# Description 	: Python launcher to run flock_unit_test. Will validate parameters and
#	        properly configure openmpi and start it, before starting
#		the test.
#		This progran will run on each node.
# Author	: Tristan.Ye	<tristan.ye@oracle.com>

#
import os, stat, sys, time, optparse, socket, string, o2tf, pdb, random, config
from os import access, F_OK
#
#
DEBUGON = os.getenv('DEBUG',0)
#
uname = os.uname()
lhostname = str(socket.gethostname())
logfile = config.LOGFILE
interface = 'eth0'
procs = 2
cmd = config.BINDIR+'/flock_unit_test'
#
Usage = """
%prog 
[-l | --logfile <logfile>] 
[-i | --interface <interface>] 
[-n | --nodelist <nodelist>] 
[-t | --type]  			lock type:should be flock or fcntl.
[-e | --file1 <filename1>]
[-f | --file2 <filename2>]
[-h | --help]
"""
#
# MAIN
#
if __name__=='__main__':
	parser = optparse.OptionParser(usage=Usage)
#
	parser.add_option('-t',
		'--type',
		dest='type',
		type='string',
		help='Type should be "flock" or "fcntl"')
#
	parser.add_option('-l', 
		'--logfile', 
		dest='logfile',
		type='string', 
		help='Logfile used by the process.')
#
	parser.add_option('-i', 
		'--interface', 
		dest='interface',
		type='string', 
		help='NIC used for MPI messaging.')
#
	parser.add_option('-n', 
		'--nodelist', 
		dest='nodelist',
		type='string', 
		help='List of nodes where test will run.')
#
	parser.add_option('-e', 
		'--file1', 
		dest='file1',
		type='string',
		help='file1 used by the test.')

#
        parser.add_option('-f',
                '--file2',
                dest='file2',
                type='string',
                help='file2 used by the test.')

#
	(options, args) = parser.parse_args()
	if len(args) != 0:
		o2tf.printlog('args left %s' % len(args), 
			logfile, 0, '')
		parser.error('incorrect number of arguments')
#
#verify args
#
	if not options.file1 or not options.file2:
		parser.error('file1 and file2 are mandatory for test')

	if options.file1 and options.file2:
		if access(options.file1,F_OK) != 1 or access(options.file2,F_OK) != 1:
			parser.error('file1 and file2 should exist')

	if options.type:
		if not options.type in ('flock','fcntl'):
			parser.error('use right lock type:flock,fcntl')
		else:
			type_arg = '-t ' + options.type
	else:
		type_arg = ''

	file1_arg = options.file1

	file2_arg = options.file2

	if options.logfile:
		logfile = options.logfile

	if options.nodelist:
		tmplist = options.nodelist.split(',')
		nodelen = len(tmplist)
		if nodelen == 1:
			options.nodelist = tmplist[0] + "," + tmplist[0]
		else:
			options.nodelist = tmplist[0] + "," + tmplist[1]
	else:
		if not options.cleanup:
			parser.error('Invalid node list.')

	if options.interface:
		interface = options.interface

if DEBUGON:
	o2tf.printlog('flock_unit_test: main - current directory %s' % 
		os.getcwd(), logfile, 0, '')
	o2tf.printlog('flock_unit_test: main - cmd = %s' % cmd,
		logfile, 0, '')
#
o2tf.OpenMPIInit(DEBUGON, options.nodelist, logfile, 'ssh')
#
ret = o2tf.openmpi_run(DEBUGON, procs, 
	str('%s %s %s %s 2>&1 >> %s' % (cmd,
	type_arg,
	file1_arg,
	file2_arg,
	logfile)), 
	options.nodelist, 
	'ssh',
	interface,
	logfile,
	'WAIT')
#
if not ret:
	o2tf.printlog('flock_unit_test: main - execution successful.',
		logfile, 0, '')

sys.exit(ret)
