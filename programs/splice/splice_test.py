#!/usr/bin/env python
#
# * vim: noexpandtab sw=8 ts=8 sts=0:
# *
# * splice_test.py
# *
# * Test splice on ocfs2.
# *
# * Copyright (C) 2004, 2008 Oracle.  All rights reserved.
# *
# * This program is free software; you can redistribute it and/or
# * modify it under the terms of the GNU General Public
# * License version 2 as published by the Free Software Foundation.
# *
# * This program is distributed in the hope that it will be useful,
# * but WITHOUT ANY WARRANTY; without even the implied warranty of
# * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# * General Public License for more details.
#
#
#	 
# Program	: splice_test.py
# Description 	: This program will just test the splice feature on ocfs2.
# Author	: Marcos E. Matsunaga 

#
import os, sys, time, optparse, socket, string, o2tf, config, pdb
import time, config, os.path
#
MD5SUM = '/usr/bin/md5sum'
#
DEBUGON = os.getenv('DEBUG',0)
PID = os.getpid()
node_dirs = {}
#
logfile = config.LOGFILE
filename = 'splice_test.dat'
SPLICEWRITE_BIN = config.BINDIR + '/splice_write'
SPLICEREAD_BIN = config.BINDIR + '/splice_read'
count = 5
uname = os.uname()
lhostname = str(socket.gethostname())
BASEMD5SUM=''
TEMPMD5SUM=''
SCRIPT_STATUS=0
#
# FUNCTIONS
#
def Cleanup():
	from os import access, F_OK
	if access(os.path.join(directory, filename), F_OK) ==1:
		os.system('rm -fr ' + os.path.join(directory, filename))
	if access(os.path.join('/tmp', filename), F_OK) ==1:
		os.system('rm -fr ' + os.path.join('/tmp', filename))
def CreateBaseFile():
	from commands import getoutput
	global BASEMD5SUM 
	os.system('ps -efl > %s' % os.path.join('/tmp', filename))
	BASEMD5SUM=getoutput('%s %s|cut -f1 -d" "' % \
		(MD5SUM, os.path.join('/tmp', filename)))
	if DEBUGON:
		o2tf.printlog('splice_test: Running CreateBaseFile',
			logfile, 0, '')
		o2tf.printlog('splice_test: BASEMD5SUM %s' % BASEMD5SUM,
			logfile, 0, '')
def SpliceWrite():
	global SCRIPT_STATUS
	from commands import getoutput
	print('Testing splice_write ........')
	from os import access, F_OK
	if access(os.path.join(directory, filename), F_OK) ==1:
		os.system('rm -fr ' + os.path.join(directory, filename))
	os.system('cat %s | %s %s %i' % (os.path.join('/tmp', filename), \
		SPLICEWRITE_BIN, os.path.join(directory, filename), \
		os.stat(os.path.join('/tmp', filename)).st_size))
	TEMPMD5SUM=getoutput('%s %s|cut -f1 -d" "' % \
		(MD5SUM, os.path.join(directory, filename)))
	if BASEMD5SUM == TEMPMD5SUM:
		print('PASSED')
	else:
		SCRIPT_STATUS=-1
		print('FAILED')
		o2tf.printlog('splice_test: BASEMD5SUM %s' % BASEMD5SUM,
			logfile, 0, '')
		o2tf.printlog('splice_test: TEMPMD5SUM %s' % TEMPMD5SUM,
			logfile, 0, '')
def SpliceRead():
	global SCRIPT_STATUS
	from commands import getoutput
	from os import access, F_OK
	print('Testing splice_read ........')
	if access(os.path.join(directory, filename), F_OK) ==1:
		os.system('rm -fr ' + os.path.join(directory, filename))
	os.system('%s %s | cat > %s' % (SPLICEREAD_BIN, \
		os.path.join('/tmp', filename), \
		os.path.join(directory, filename)))
	TEMPMD5SUM=getoutput('%s %s|cut -f1 -d" "' % \
		(MD5SUM, os.path.join(directory, filename)))
	if BASEMD5SUM == TEMPMD5SUM:
		print('PASSED')
	else:
		SCRIPT_STATUS=-1
		print('FAILED')
		o2tf.printlog('splice_test: BASEMD5SUM %s' % BASEMD5SUM,
			logfile, 0, '')
		o2tf.printlog('splice_test: TEMPMD5SUM %s' % TEMPMD5SUM,
			logfile, 0, '')
#
# MAIN
#
Usage = 'usage: %prog [-d|--directory directory] \
[-c| --count] \
[-D| --debug] \
[-f|--filename filename] \
[-l|-logfile logfilename] \
[-h|--help]'
if __name__=='__main__':
	parser = optparse.OptionParser(Usage)
#
	parser.add_option('-d',
		'--directory',
		dest='directory',
		type='string',
		help='Directory where the file will be created.')
#
	parser.add_option('-c',
		'--count',
		dest='count',
		type='int',
		help='Number of times the test will be repeated.')
#
	parser.add_option('-D',
			'--debug',
			action="store_true",
			dest='debug',
			default=False,
			help='Turn the debug option on. Default=False.')
#
	parser.add_option('-l',
		'--logfile',
		dest='logfile',
		type='string',
		help='Logfile used by the process.')
#
	parser.add_option('-f',
		'--filename',
		dest='filename',
		type='string',
		help='Test filename.')
#
	(options, args) = parser.parse_args()
	if len(args) != 0:
		parser.error('incorrect number of arguments')
#
	if options.debug:
		DEBUGON=1
#
	if options.filename:
		filename = options.filename
#
	if not options.directory:
		parser.error('Please specify working directory.')
	else:
		if not os.path.isdir(options.directory):
			os.makedirs(options.directory, 0o755)
	directory = options.directory
	if options.logfile:
		logfile = options.logfile+'_'+lhostname
	if options.count:
		count = options.count
#
# Make nproc at least the number of nodes
#
# End or arguments parsing
#
if DEBUGON:
	o2tf.printlog('splice_test: directory = (%s)' % directory,
		logfile, 0, '')
	o2tf.printlog('splice_test: filename = (%s)' % filename,
		logfile, 0, '')
	o2tf.printlog('splice_test: logfile = (%s)' % logfile,
		logfile, 0, '')
#
for i in range(count):
	CreateBaseFile()
	SpliceWrite()
	SpliceRead()
	if SCRIPT_STATUS:
		break
#
if SCRIPT_STATUS:
	o2tf.printlog('splice_test: Job Failed',
		logfile, 3, '=')
	sys.exit(1)
else:
	o2tf.printlog('splice_test: Job completed successfully',
		logfile, 3, '=')
	Cleanup();
	sys.exit(0)
