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
# Program	  : run_write_torture.py
# Description : Interface to run run_write_torture. Will validate parameters and
#					 properly configure LAM/MPI and start it, before starting
#					 the write_torture.py.
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
Usage = '\n	 %prog [-b|--blocksize] \
[-c | --count count] \
[-d | --directory directory] \
[-f | --filename <filename>] \
[-i | --if <Network Interface>] \
[-l | --logfile logfile] \
[-n | --nodelist nodelist] \
[-p | --procs procs] \
[-s | --seconds seconds] \
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
	parser.add_option('-b', 
		'--blocksize', 
		dest='blocksize',
		type='string',
		help='Blocksize interval that will be during test. \
				Range from 512 to 8192 bytes (Format:xxx,yyy).')
#
	parser.add_option('-c', 
		'--count', 
		dest='count',
		type='int',
		help='Number of times that test will execute.')
#
	parser.add_option('-d', 
		'--directory', 
		dest='directory',
		type='string',
		help='Directory that will be used by the test.')
#
	parser.add_option('-f', 
		'--filename', 
		dest='filename',
		type='string',
		help='Filename that will be used by the test. \
				If specified, a single file will be used by \
				instances of the test.')
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
		'--seconds', 
		dest='seconds',
		type='int', 
		help='Number of seconds the test will run (def. 60).')
#
	(options, args) = parser.parse_args()
	if len(args) != 0:
		o2tf.printlog('args left %s' % len(args), 
			  logfile, 0, '')
		parser.error('incorrect number of arguments')
#
	if options.blocksize:
		blocksize = options.blocksize
		blockvalues = blocksize.split(',')
		if len(blockvalues) != 2:
			o2tf.printlog('Blocksize must be specified in format xxx,yyy\n\n', 
			logfile, 0, '')
			parser.error('Invalid format.')
	else:
		parser.error('Blocksize parameter needs to be specified.')
		 
	if int(blockvalues[0]) < MINBLOCKSIZE or int(blockvalues[1]) > MAXBLOCKSIZE:
		o2tf.printlog('Blocksize must be between %s and %s\n\n' % 
		(MINBLOCKSIZE, MAXBLOCKSIZE), 
		logfile, 0, '')
		parser.error('Invalid range.')
	if DEBUGON:
		o2tf.printlog('Blocksize range from %s to %s\n\n' % 
	  (str(blockvalues[0]), str(blockvalues[1])), 
		logfile, 0, '')
#
	if options.count:
		count = options.count
#
	if options.directory:
		directory = options.directory
	else:
		parser.error('Directory parameter needs to be specified.')
#
	if options.filename:
		filename = options.filename
		UNIQUEFILE = 1
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
#
	interface = options.interface
#
	if options.procs:
		procs = options.procs
#
	if options.seconds:
		seconds = options.seconds
#
if UNIQUEFILE:
	cmd=os.path.join(config.BINDIR, 'write_torture.py -u')
else:
	cmd=os.path.join(config.BINDIR, 'write_torture.py')
#
if DEBUGON:
	o2tf.printlog('run_write_torture: main - current directory %s' % os.getcwd(),
		logfile, 0, '')
	o2tf.printlog('run_write_torture: main - cmd = %s' % cmd,
		logfile, 0, '')
	o2tf.printlog('run_write_torture: main - blocksize = %s' % options.blocksize,
		logfile, 0, '')
#
for z in range(options.count):
	o2tf.printlog('run_write_torture: Running test# %s' % z, 
	logfile, 0, '')
#
	o2tf.OpenMPIInit(DEBUGON, options.nodelist, logfile, 'ssh')
	ret = o2tf.openmpi_run(DEBUGON, options.procs,
		str('%s -b %s -l %s -s %s -f %s' % (cmd, 
		options.blocksize, 
		options.logfile, 
		options.seconds, 
		os.path.join(options.directory, filename) ) ), 
		options.nodelist, 
		'ssh',
		options.interface,
		options.logfile,
		'WAIT')
	if not ret:
		o2tf.printlog('run_write_torture: main - execution successful.',
			logfile, 0, '')
	else:
		o2tf.printlog('run_write_torture: main - execution failed.',
			logfile, 0, '')
		sys.exit(1)
