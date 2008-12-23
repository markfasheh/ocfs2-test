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
# Program	: run_quota_multi_tests.py
# Description 	: Python launcher to run quota_multi_tests. 
#		Will validate parameters and properly configure
#		openmpi and start it, before starting the test.
#		This progran will run on each node.
#
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
procs = 1
cmd = config.BINDIR+'/quota_multi_tests'
#
Usage = """
%prog 
[-i | --iterations <iterations>]
[-l | --logfile <logfile>] 
[-n | --nodelist <nodelist>] 
[-u | --users <user_nums>] 
[-g | --groups <group_nums>]
[-d | --device <device>]
[-m | --mountpoint <mountpoint>]
[-h | --help]
"""
#
# MAIN
#
if __name__=='__main__':
	parser = optparse.OptionParser(usage=Usage)
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
        parser.add_option('-n',
                '--nodelist',
                dest='nodelist',
                type='string',
                help='List of nodes where test will run.')
#
        parser.add_option('-u',
                '--users',
                dest='users',
                type='int',
                help='Number of user.')
#
        parser.add_option('-g',
                '--groups',
                dest='groups',
                type='int',
                help='Number of groups.')
#
        parser.add_option('-d',
                '--device',
                dest='device',
                type='string',
                help='Target volume.')
#
        parser.add_option('-m',
                '--mountpoint',
                dest='mountpoint',
                type='string',
                help='Mount point.')

	(options, args) = parser.parse_args()
	if len(args) != 0:
		o2tf.printlog('args left %s' % len(args), 
			logfile, 0, '')
		parser.error('incorrect number of arguments')
#
#verify args
#
	if not options.device:
		parser.error('device mandatory for test')
	
	if not options.mountpoint:
		parser.error('mount point is mandatory for test');

	if not options.users and not options.groups:
			parser.error('either Users or groups should be specified at least')

	if options.iterations:
		iter_arg = '-i ' + str(options.iterations)
	else:
		iter_arg = ''

	if options.users:
		user_arg = '-u ' + str(options.users)
	else:
		user_arg = ''

	if options.groups:
		group_arg = '-g ' + str(options.groups)
	else:
		group_arg = ''

	device_arg = '-d ' + options.device
	mountpoint_arg = options.mountpoint

	if options.logfile:
		logfile = options.logfile

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
	o2tf.printlog('quota_multi_test: main - current directory %s' % 
		os.getcwd(), logfile, 0, '')
	o2tf.printlog('quota_multi_test: main - cmd = %s' % cmd,
		logfile, 0, '')
#
o2tf.OpenMPIInit(DEBUGON, options.nodelist, logfile, 'rsh')
#
ret = o2tf.openmpi_run(DEBUGON, procs, 
	str('%s %s %s %s %s %s 2>&1 | tee -a %s' % (cmd, 
	iter_arg,
	user_arg,
	group_arg,
	device_arg,
	mountpoint_arg,
	logfile)), 
	options.nodelist, 
	'rsh',
	logfile,
	'WAIT')
#
if not ret:
	o2tf.printlog('quota_multi_test: main - execution successful.',
		logfile, 0, '')
