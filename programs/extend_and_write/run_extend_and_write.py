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
# Program	: run_extend_and_write.py
# Description 	: This is just a wrapper for extend_and_write and verify
#		programs. 
# Author	: Marcos E. Matsunaga 

#
import os, stat, sys, time, optparse, socket, string, o2tf, pdb, random, config
#
#pdb.set_trace()
#
#args = sys.argv[1:]
#
DEBUGON = os.getenv('DEBUG',0)
#
pgm='run_extend_and_write'
logfile = config.LOGFILE
filename = '/tmp/extend_and_write.dat'
size = 1024
numwrites = 10240
cmd = config.BINDIR+'/extend_and_write'
cmd_verify = config.BINDIR+'/verify'
param = 0
#
Usage = '\n	 %prog [-f | --filename <filename>] \
[-l | --logfile <logfile>] \
[-n | --numwrites <numwrites>] \
[-s | --size <extend size>] \
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
		'--filename', 
		dest='filename',
		type='string',
		help='Full path filename used by this test.')
#
	parser.add_option('-l', 
		'--logfile', 
		dest='logfile',
		type='string',
		help='Log file name.')
#
	parser.add_option('-n', 
		'--numwrites', 
		dest='numwrites',
		type='int',
		help='Number of writes performed by the test.')
#
	parser.add_option('-s', 
		'--size', 
		dest='size',
		type='int',
		help='Extend size')
#
	(options, args) = parser.parse_args()
	if len(args) != 0:
		o2tf.printlog('args left %s' % len(args), 
			logfile, 0, '')
		parser.error('incorrect number of arguments')
#
	if options.filename:
		param = 1
		filename = options.filename
#
	if options.size:
		param = 1
		size = options.size
#
	if options.numwrites:
		param = 1
		numwrites = options.numwrites
#
	if options.logfile:
		param = 1
		logfile = options.logfile
#
#
from os import access, W_OK
#
if not param:
	o2tf.printlog('%s: main - Test will use all default parameters' % 
		pgm, logfile, 0, '')

if DEBUGON:
	o2tf.printlog('%s: main - filename = %s' % (pgm, filename),
		logfile, 0, '')
	o2tf.printlog('%s: main - logfile = %s' % (pgm, logfile),
		logfile, 0, '')
	o2tf.printlog('%s: main - numwrites = %s' % (pgm, numwrites),
		logfile, 0, '')
	o2tf.printlog('%s: main - size = %s' % (pgm, size),
		logfile, 0, '')
	o2tf.printlog('%s: main - cmdline = %s -f %s -n %s -s %s 2>&1|tee -a '
		'%s' % (pgm, cmd, filename, numwrites, size, logfile),
		logfile, 0, '')
#
os.system(str('%s -f %s -n %s -s %s 2>&1|tee -a %s' % (cmd, 
	filename, 
	numwrites, 
	size, 
	options.logfile)))
#
os.system(str('%s -f %s -n %s -s %s 2>&1|tee -a %s' % (cmd_verify, 
	filename, 
	numwrites, 
	size, 
	options.logfile)))
