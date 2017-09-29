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
# Program	: test_atime_quantum.py
# Description	: Will test the implementation of atime_quantum.
# Author	: Marcos E. Matsunaga 

#
import os, stat, sys, time, optparse, socket, string, o2tf, pdb, config
import random, os.path
#
# FUNCTIONS START
#
def ChangeAtime(fname):
	from os import open, read, close, O_RDONLY
	fd = open(fname, O_RDONLY)
	line = read(fd, 10)
	close(fd)
#
def CheckAtime0(fname, logfile):
	from time import strftime, localtime
	from os.path import getatime
	from o2tf import printlog
	Atime1 = strftime("%m %d %Y %H %M", localtime(getatime(fname)))
	os.system('cat %s >> /dev/null' % fname)
	Atime2 = strftime("%m %d %Y %H %M", localtime(getatime(fname)))
	if Atime2 == strftime("%m %d %Y %H %M", localtime()):
		if DEBUGON:
			printlog('file %s - atime update successful - ' \
				'previous (%s), current(%s)' % (fname, Atime1, 
				Atime2),
				logfile,
				0,
				'')
	else:
		printlog('file %s - atime update failed - previous (%s),' \
			' current(%s), expected (%s)' % \
			(fname, Atime1, Atime2, strftime("%m %d %Y %H %M", \
			localtime())),
			logfile,
			0,
			'')
#
def CheckAtime(fname, logfile):
	from time import strftime, localtime
	from os.path import getatime
	from o2tf import printlog
	Atime1 = strftime("%m %d %Y %H %M", localtime(getatime(fname)))
	os.system('cat %s >> /dev/null' % fname)
	Atime2 = strftime("%m %d %Y %H %M", localtime(getatime(fname)))
	if Atime1 != Atime2:
		printlog('file %s - unexpected atime update - previous (%s),' \
			' current(%s)' % (fname, Atime1, Atime2),
			logfile,
			0,
			'')
	time.sleep(atime+1)
	Atime3 = strftime("%m %d %Y %H %M", localtime(getatime(fname)))
	if Atime3 == strftime("%m %d %Y %H %M", localtime()):
		if DEBUGON:
			printlog('file %s - atime update successful - ' \
				'previous (%s), current(%s)' % (fname, Atime1, 
				Atime2),
				logfile,
				0,
				'')
	else:
		printlog('file %s - atime update failed - previous (%s),' \
			' current(%s), expected (%s)' % \
			(fname, Atime1, Atime2, strftime("%m %d %Y %H %M", \
			localtime())),
			logfile,
			0,
			'')
#
# FUNCTIONS END
#
#pdb.set_trace()
#
#args = sys.argv[1:]
#
DEBUGON = os.getenv('DEBUG',0)
#
pgm = 'test_atime_quantum.py'
uname = os.uname()
lhostname = str(socket.gethostname())
logfile = config.LOGFILE
atime = 0
#
Usage = '\n%prog [-a|--atime_quantum <time in seconds>] \
[-d | --directory <directory>] \
[-l | --logfile logfilename] \
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
	parser.add_option('-a', 
		'--atime_quantum', 
		dest='atime',
		type='int',
		help='atime_quantum in use by the mounted filesystem')
#
	parser.add_option('-d',
		'--directory',
		dest='directory',
		type='string',
		help='directory where the files to be used in the test are' \
			'created.')
#
	parser.add_option('-l',
		'--logfile',
		dest='logfile',
		type='string',
		help='Logfile used by the process.')
#
	(options, args) = parser.parse_args()
	if len(args) != 0:
		o2tf.printlog('args left %s' % len(args), logfile, 0, '')
		parser.error('Incorrect number of arguments')
#
	if options.atime:
		atime = options.atime
#
	if options.directory:
		if not os.path.isdir(options.directory):
			parser.error('Invalid directory %s' % options.directory)
		directory = options.directory
	else:
		parser.error('Directory must be specified.')
#
	if options.logfile:
		logfile = options.logfile
#
if DEBUGON:
	o2tf.printlog('%s: main - atime_quantum %s' % (pgm, atime),
		logfile,
		0,
		'')
	o2tf.printlog('%s: main - directory = %s' % (pgm, directory),
		logfile,
		0,
		'')
	o2tf.printlog('%s: main - logfile = %s' %
		(pgm, logfile),
		logfile,
		0, 
		'')
		
#
for root, dirs, files in os.walk(directory):
	for names in files:
		if atime == 0:
			CheckAtime0(os.path.join(root, names), logfile)
		else:
			CheckAtime(os.path.join(root, names), logfile)
#
sys.exit()
