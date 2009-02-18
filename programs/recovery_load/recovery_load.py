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
# Program	: recovery_load.py
# Description	: This test script works in two phases. The first one
#		  it will extract a tar file in one of the directories. The
#		  second will run a find touching each file on other nodes
#		  extraction directories. That will force lock migration.
#
# Author	: Marcos E. Matsunaga 
#
#
import os, sys, time, optparse, socket, string, o2tf, config, pdb, timing
import time, config
#
MAXDIRLEVEL = 64
DEBUGON = os.getenv('DEBUG',0)
#
#
#
logfile = config.LOGFILE
nodelen = 0
uname = os.uname()
Client = False
lhostname = str(socket.gethostname())
stagedir = ''
#
# FUNCTIONS
#
def Populate():
	from os import access, remove, F_OK
	localdir=os.path.join(options.directory,str('%s_recovery' % lhostname))
	if DEBUGON:
		o2tf.printlog('recovery_load: localdir [%s]' % localdir,
			logfile, 0, '')
	if access(localdir, F_OK) == 1:
		os.system('rm -fr '+localdir)
	o2tf.CreateDir(DEBUGON, localdir, logfile)
	o2tf.untar(DEBUGON, localdir, tarfile, logfile)
#
def Find():
	finddir=os.path.join(options.directory, str('%s_recovery' % nodelist[NodeIndex]))
	if DEBUGON:
		o2tf.printlog('recovery_load: finddir [%s]' % finddir,
			logfile, 0, '')
	os.system(str('find %s -type f -exec touch {} \;' % finddir))
	if DEBUGON:
		o2tf.printlog('recovery_load: directory [%s]' % \
			options.directory, logfile, 0, '')
	os.system(str('find %s -type f -exec touch {} \;' % options.directory))
#
def Cleanup(ret):
	sys.exit(ret)
#
# MAIN
#
Usage = 'usage: %prog [-D|--Debug] \
[-d|--directory] \
[-e|--extract] \
[-f|--find] \
[-l|-logfile logfilename] \
[-n|nodes nodelist] \
[-t|--tarfile fullpath tar filename] \
[-h|--help]'
if __name__=='__main__':
	parser = optparse.OptionParser(Usage)
#
	parser.add_option('-D',
		'--Debug',
		action="store_true",
		dest='debug',
		default=False,
		help='Turn the debug option on. Default=False.')

#
	parser.add_option('-d',
		'--directory',
		dest='directory',
		type='string',
		help='Directory where the files will be extracted.')
#
	parser.add_option('-e',
		'--extract',
		action="store_true",
		dest='extract',
		default=False,
		help='In client mode, will extract file. Default=False.')
#
	parser.add_option('-f',
		'--find',
		action="store_true",
		dest='find',
		default=False,
		help='In client mode, will run find. Default=False.')
#
	parser.add_option('-l',
		'--logfile',
		dest='logfile',
		type='string',
		help='Logfile used by the process.')
#
	parser.add_option('-n',
		'--nodes',
		dest='nodes',
		type='string',
		help='List of nodes to be used in the test. Must \
			have at least 2 nodes, separated by comma \
			without space.')
#
	parser.add_option('-t',
		'--tarfile',
		dest='tarfile',
		type='string',
		help='Fullpath filename of the tar file containing \
			the kernel that will be used.')
#
	(options, args) = parser.parse_args()
	if len(args) != 0:
		parser.error('incorrect number of arguments')
#
	if options.debug:
		DEBUGON = True
#
	if not options.tarfile and not options.find:
		parser.error('Must provide a gzipped kernel tarball to '+
			'run the test.')
#
	if options.extract and options.find:
		parser.error('Extract and find are exclusive options.')
	if options.extract or options.find:
		Client = True
	tarfile = options.tarfile
	nodelist = options.nodes.split(',')
	nodelen = len(nodelist)
	logfile = options.logfile
	if nodelen < 2:
		o2tf.printlog('recovery_load: nodelist must have at least 2 '+
			'nodes' % options.directory, logfile, 0, '')
		sys.exit(1)
#
# Make nproc at least the number of nodes
#
	if nodelen > config.NPROC:
		nproc = nodelen 
	else:
		nproc = config.NPROC
# 
# End or arguments parsing
#
if DEBUGON:
	o2tf.printlog('recovery_load: Client = (%s)' % Client,
		logfile, 0, '')
	o2tf.printlog('recovery_load: directory = (%s)' % options.directory,
		logfile, 0, '')
	o2tf.printlog('recovery_load: nodelist = (%s)' % nodelist,
		logfile, 0, '')
	o2tf.printlog('recovery_load: nodelen = (%s)' % nodelen,
		logfile, 0, '')
	o2tf.printlog('recovery_load: logfile = (%s)' % logfile,
		logfile, 0, '')
	o2tf.printlog('recovery_load: tarfile = (%s)' % tarfile,
		logfile, 0, '')
#
NodeIndex = nodelist.index(lhostname)
if DEBUGON:
	o2tf.printlog('recovery_load: NodeIndex = (%s)' % NodeIndex,
		logfile, 0, '')
if NodeIndex == nodelen-1:
	NodeIndex = 0
	if DEBUGON:
		o2tf.printlog('recovery_load: Last entry, making it zero',
			logfile, 0, '')
#
# first. Start MPI
if not Client:
	o2tf.OpenMPIInit(DEBUGON, ','.join(nodelist), logfile, 'ssh')

	if DEBUGON:
		cmdline = os.path.join(config.BINDIR, 'recovery_load.py -D')
	else:
		cmdline = os.path.join(config.BINDIR, 'recovery_load.py')
# Extract the tar file
	ret = o2tf.openmpi_run( DEBUGON, nproc, 
		str('%s -e -d %s -l %s -n %s -t %s' % \
		(cmdline, options.directory,
		options.logfile,
		options.nodes, tarfile) ),
		','.join(nodelist),
		'ssh',
		logfile,
		'WAIT')
	if not ret:
		o2tf.printlog('recovery_load: extraction successful.',
			logfile, 0, '')
	else:
		o2tf.printlog('recovery_load: extraction failed.',
			logfile, 0, '')
		Cleanup(1)
# run the find command
	raw_input('Extraction completed. Press ENTER to continue.')
	ret = o2tf.openmpi_run( DEBUGON, nproc, 
		str('%s -f -d %s -l %s -n %s' % \
		(cmdline, options.directory,
		options.logfile,
		options.nodes) ),
		','.join(nodelist),
		'ssh',
		logfile,
		'WAIT')
	if not ret:
		o2tf.printlog('recovery_load: find successful.',
			logfile, 0, '')
	else:
		o2tf.printlog('recovery_load: find failed.',
			logfile, 0, '')
		Cleanup(1)
else:
	if options.extract:
		Populate()
	else:
		if options.find:
			Find()
		else:
			o2tf.printlog('recovery_load: SHOULD NOT BE HERE.',
			logfile, 0, '')
			sys.exit(1)
