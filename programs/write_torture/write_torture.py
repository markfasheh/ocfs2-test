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
# Program	  : write_torture.py
# Description : Interface to run write_torture. Will validate parameters and
#					 properly configure LAM/MPI and start it before starting
#					 the write_torture program. This progran will run on each
#		node.
# Author		  : Marcos E. Matsunaga 

#
import os, stat, sys, time, optparse, socket, string, o2tf, pdb, config
import random
#
#pdb.set_trace()
#
#args = sys.argv[1:]
#
MINBLOCKSIZE = 512
MAXBLOCKSIZE = 8192
#
DEBUGON = os.getenv('DEBUG',0)
#
EXECPGM = os.path.join(config.BINDIR,'write_torture')
#
uname = os.uname()
lhostname = str(socket.gethostname())
numnodes = 0
logfile = config.LOGFILE
blocksize = '512,4096'
seconds = 60
#
Usage = '\n	 %prog [-b|--blocksize] \
[-f | --filename <fullpath filename>] \
[-l | --logfile logfilename] \
[-s | --seconds seconds] \
[-u | --uniquefile] \
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
	parser.add_option('-f', 
		'--filename', 
		dest='filename',
		type='string',
		help='Filename that will be used during test.')
#
	parser.add_option('-l', 
		'--logfile', 
		dest='logfile',
		type='string', 
		help='Logfile used by the process.')
#
	parser.add_option('-s', 
		'--seconds', 
		dest='seconds',
		type='int', 
		help='Number of seconds the test will run (def. 60).')
#
	parser.add_option('-u', 
		'--uniquefile', 
		action="store_true",
		dest='uniquefile',
		default=False)
#
	(options, args) = parser.parse_args()
	if len(args) != 0:
		o2tf.printlog('args left %s' % len(args), logfile, 0, '')
		parser.error('incorrect number of arguments')
#
	if options.blocksize:
		blocksize = options.blocksize
		blockvalues = blocksize.split(',')
		if len(blockvalues) != 2:
			o2tf.printlog('Blocksize must be specified in format xxx,yyy\n\n',
				logfile,
				0,
				'')
			parser.error('Invalid format.')
	else:
		parser.error('Blocksize parameter needs to be specified.')
		 
	if int(blockvalues[0]) < MINBLOCKSIZE or int(blockvalues[1]) > MAXBLOCKSIZE:
		o2tf.printlog('Blocksize must be between %s and %s\n\n' % \
			(MINBLOCKSIZE, MAXBLOCKSIZE),
			logfile,
			0,
			'')
		parser.error('Invalid range.')
	if DEBUGON:
		o2tf.printlog('Blocksize range from %s to %s\n\n' % \
		  (str(blockvalues[0]), str(blockvalues[1])),
			logfile,
			0,
			'')
#
	if options.filename:
		filename = options.filename
	else:
		parser.error('filename parameter needs to be specified.')
#
	if options.logfile:
		logfile = options.logfile
#
	if options.seconds:
		seconds = options.seconds
#
	print options.uniquefile
	if not options.uniquefile:
		filename = options.filename + '_' + lhostname + '_' + str(os.getpid())
#
BLKSZ = random.randint(int(blockvalues[0]), int(blockvalues[1]))
cmd = (EXECPGM + ' -s %s -b %s %s 2>&1 | tee -a %s' % 
		(seconds, BLKSZ, filename, logfile))

if DEBUGON:
	o2tf.printlog('write_torture: main - current directory %s' % os.getcwd(), 
		logfile, 
		0, 
		'')
	o2tf.printlog('write_torture: main - filename = %s' % filename, 
		logfile, 
		0, 
		'')
	o2tf.printlog('write_torture: main - BLKSZ = %s' % 
		BLKSZ, 
		logfile, 
		0, 
		'')
t1 = time.time()
if DEBUGON:
	o2tf.printlog('write_torture: main - cmd = %s' % cmd, 
	logfile, 
	0, 
	'')
RC = os.system(cmd)
t2 = time.time()
if DEBUGON:
	o2tf.printlog('write_torture: elapsed time = %s - RC = %s' % 
		((t2 - t1), RC),
		logfile, 
		0, 
		'')
		
#
sys.exit(RC)
