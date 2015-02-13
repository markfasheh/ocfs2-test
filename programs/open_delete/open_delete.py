#!/usr/bin/env python
#
#
# Copyright (C) 2006 Oracle.  All rights reserved.
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
# Program	:	open_delete.py
# Description	:	Interface to run open_delete. Will validate parameters
#			and properly configure LAM/MPI and start it before
#			starting the open_delete program.
# Author	:	Marcos E. Matsunaga

#
import os, stat, sys, time, optparse, socket, string, o2tf, pdb, config
#
#pdb.set_trace()
#
#args = sys.argv[1:]
#
#
DEBUGON = os.getenv('DEBUG',0)
#
EXECPGM = 'open_delete'
#
uname = os.uname()
lhostname = str(socket.gethostname())
numnodes = 0
logfile = config.LOGFILE
interface = 'eth0'
#
Usage = '\n	 %prog [-l|-logfile logfilename] \
[-f | --file filename] \
[-i | --interactions count] \
[-I | --interface] \
[-n | --nodes nodelist] \
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
	parser.add_option('-f', 
			'--file', 
			dest='filename',
			type='string',
			help='Fullpath filename that will be used during \
				test.')
#
	parser.add_option('-i', 
			'--interactions', 
			dest='count',
			type='int', 
			default=1,
			help='Number of times the test will be executed.')
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
			help='Nic used by MPI messaging.')
#
	parser.add_option('-n', 
			'--nodes', 
			dest='nodelist',
			type='string', 
			help='List of nodes to be used by the test.')
#
	(options, args) = parser.parse_args()
	if len(args) != 0:
		o2tf.printlog('args left %s' % len(args), logfile, 0, '')
		parser.error('incorrect number of arguments')
	if options.logfile:
		logfile = options.logfile
	if options.interface:
		interface = options.interface
	count = options.count
	filename = options.filename
	if options.nodelist:
		nodelist = options.nodelist.split(',')
		numnodes = len(nodelist)

#
if numnodes < 2:
	o2tf.printlog('open_delete: Must provide at least 2 nodes to \
		run the test.', logfile, 0, '')
	parser.print_help()
	sys.exit(1)
#
o2tf.OpenMPIInit(DEBUGON, options.nodelist, logfile, 'ssh')
command=os.path.join(config.BINDIR, EXECPGM)
if DEBUGON:
	o2tf.printlog('command = %s' % command, 
	logfile, 0, '')
ret = o2tf.openmpi_run(DEBUGON, 
	'C', 
	str('%s -i %s %s' % 
	(command, 
	options.count, 
	filename) ),
	options.nodelist, 
	'ssh',
	interface,
	options.logfile,
	'WAIT')
if not ret:
	o2tf.printlog('open_delete: execution successful.',
		logfile, 0, '')
else:
	o2tf.printlog('open_delete: execution failed.',
		logfile, 0, '')
	sys.exit(1)
