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
# Program	: run_multi_mmap.py
# Description 	: Python launcher to run multi_mmap. Will validate parameters and
#		properly configure openmpi and start it, before starting
#		the test.
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
cmd = config.BINDIR+'/multi_mmap'
#
Usage = """
%prog 
[-i | --iterations <iterations>] 
[-C | --cleanup] 
[-l | --logfile <logfile>] 
[-I | --interface <interface>] 
[-n | --nodelist <nodelist>] 
[-t | --truncate]  		Don\'t create or trunc the file - will fail if it doesn\'t exist.
[-c | --cache]	   		Populate the local cache by reading the full file first.
[-r | --reader <how>] 		Readers use "mmap", "regular" or "random" I/O: default "mmap".
[-w | --writer <how>] 		Writers use "mmap", "regular" or "random" I/O: default "mmap".
[-b | --blocksize <blocksize>]  Blocksize to use, defaults to 508. Must be > 10.
[-H | --hole] 			Creating a hole where it will be writing.
[-e | --error <which>] 		Inject an error by truncating the entire file length on iteration <which>.
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
			o2tf.printlog('multi_mmap: Removing filename (%s)' 
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
	parser.add_option('-c',
		'--cache',
		action="store_true",
		dest='cache',
		default=False,
		help='Populate the local cache.')
#
	parser.add_option('-t',
		'--truncate',
		action="store_true",
		dest='truncate',
		default=False,
		help='Do not create or trunc file,will fail if does not exist.')
#
	parser.add_option('-H',
		'--hole',
		action="store_true",
		dest='hole',
		default=False,
		help='Create a hole.')
#
	parser.add_option('-i', 
		'--iterations', 
		dest='iterations',
		type='int',
		help='Number of iterations.')
#
	parser.add_option('-b',
		'--blocksize',
		dest='blocksize',
		type='int',
		help='Blocksize to use,defaults to 508,must be > 10.')
#
	parser.add_option('-e',
		'--error',
		dest='error',
		type='int',
		help='Inject an error by truncating the entire file length on iteration <which>.')
#
	parser.add_option('-r',
		'--reader',
		dest='reader',
		type='string',
		help='Readers use "mmap", "regular" or "random" I/O: default "mmap".')
#
	parser.add_option('-w',
		'--writer',
		dest='writer',
		type='string',
		help='Writers use "mmap", "regular" or "random" I/O: default "mmap".')

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

	if options.truncate and options.hole:
		parser.error('-t and -H flags can not be mixed')
	
	if options.error > options.iterations:
		parser.error('error pos must be < itertions')

	if options.reader:
		if not options.reader in ('mmap','regular','random'):
			parser.error('use right reader type:mmap,regular,random')

	if options.writer:
		if not options.writer in ('mmap','regular','random'):
			parser.error('use right writer type:mmap,regular,random')

	if options.truncate:
		truncate_arg = '-t '
	else:
		truncate_arg = ''

	if options.cache:
		cache_arg = '-c '
	else:
		cache_arg = ''

	if options.hole:
		hole_arg = '-h '
	else:
		hole_arg = ''

	if options.reader:
		reader_arg = '-r ' + options.reader
	else:
		reader_arg = ''

	if options.writer:
		writer_arg = '-w ' + options.writer
	else:
		writer_arg = ''

	if options.iterations:
		iterations_arg = '-i ' + str(options.iterations)
	else:
		iterations_arg = ''

	if options.blocksize:
		blocksize_arg = '-b ' + str(options.blocksize)
	else:
		blocksize_arg = ''

	if options.error:
		error_arg = '-e ' + str(options.error)
	else:
		error_arg = ''
	
	if options.filename:
		filename_arg =  options.filename

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
	o2tf.printlog('run_multi_mmap: main - current directory %s' % 
		os.getcwd(), logfile, 0, '')
	o2tf.printlog('run_multi_mmap: main - cmd = %s' % cmd,
		logfile, 0, '')
#
if options.cleanup:
	Cleanup(0)
o2tf.OpenMPIInit(DEBUGON, options.nodelist, logfile, 'ssh')
#
ret = o2tf.openmpi_run(DEBUGON, procs, 
	str('%s %s %s %s %s %s %s %s %s %s 2>&1 >> %s' % (cmd,
	truncate_arg,
	cache_arg, 
	reader_arg,
	writer_arg,
	blocksize_arg,
	hole_arg,
	iterations_arg,
	error_arg,
	filename_arg,
	logfile)), 
	options.nodelist, 
	'ssh',
	interface,
	logfile,
	'WAIT')
#
if not ret:
	o2tf.printlog('run_multi_mmap: main - execution successful.',
		logfile, 0, '')
Cleanup(ret)
