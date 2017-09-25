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
# Program	  : crdel_gen_files.py
# Description : Run a build of the kernel on each one of the ocfs2
#					 partitions mounted, in parallel.
#					 i.e: The system has 7 partitions, it will run 7
#							builds in parallel, one for each partition.
# Author		  : Marcos E. Matsunaga 
# E-mail		  : Marcos.Matsunaga@oracle.com

#
import os, stat, sys, time, optparse, socket, string, o2tf, pdb
import config
#
#pdb.set_trace()
#
#args = sys.argv[1:]
#
DEBUGON = os.getenv('DEBUG',0)
#
uname = os.uname()
lhostname = str(socket.gethostname())
#
# FUNCTIONS
#
#
# MAIN
#
Usage = 'usage: %prog  [-D|--debug] \
	[-d|--dirlist dirlist] \
	[-l|-logfile logfilename] \
	[-s | --stagedir stagedir] \
	[-t|--tarfile fullpath tar filename] \
	[-h|--help]'
if __name__=='__main__':
	parser = optparse.OptionParser(Usage)
#
	parser.add_option('-D',
		'--debug',
		action="store_true",
		dest='debug',
		default=False,
		help='Turn the debug option on. Default=False.')
#
	parser.add_option('-d', 
		'--dirlist', 
		dest='dirlist',
		default='',
		type='string',
		help='Directory where the files will be extracted. ')
#
	parser.add_option('-l', 
		'--logfile', 
		dest='logfile',
		type='string', 
		help='Logfile used by the process.')
#
	parser.add_option('-s', 
		'--stage', 
		dest='stagedir',
		type='string', 
		help='Directory that will have the workfiles used \
			by the test.')
#
	parser.add_option('-t', 
		'--tarfile', 
		dest='tarfile', 
		type='string', 
		help='Fullpath filename of the tar file that will be \
			used to poulated the directory.')
#
	(options, args) = parser.parse_args()
	if len(args) != 0:
		parser.error('incorrect number of arguments')
#
	if options.debug:
		DEBUGON=1
	if not options.tarfile:
		parser.error('Must provide a gzipped kernel tarball to run the test.')
	tarfile = options.tarfile
#
	if options.dirlist != '':
		dirlist = options.dirlist.split(',')
		dirlen = len(dirlist)
	else:
		dirlen = 0
#
	logfile = options.logfile
	stagedir = options.stagedir
#
# First thing. Check if the dirlist is actually a directory or a file 
# containing the directory list.
#
if dirlen == 0:
	fd = open(os.path.join(stagedir, socket.gethostname()+'_C.dat'), 'r')
	dirlist = fd.read().split(',')
	fd.close()
	dirlen = len(dirlist)
if DEBUGON:
	o2tf.printlog('crdel_gen_files: dirlist = (%s)' % dirlist,
		logfile,
		0,
		'')
	o2tf.printlog('crdel_gen_files: stagedir = (%s)' % stagedir,
		logfile,
		0,
		'')
	o2tf.printlog('crdel_gen_files: dirlen = (%s)' % dirlen,
		logfile,
		0,
		'')
	o2tf.printlog('crdel_gen_files: logfile = (%s)' % logfile,
		logfile,
		0,
		'')
	o2tf.printlog('crdel_gen_files: tarfile = (%s)' % tarfile,
		logfile,
		0,
		'')
#
o2tf.CreateDir(DEBUGON, string.join(dirlist,','), logfile)
#
for i in range(dirlen):
	o2tf.untar(DEBUGON, dirlist[i], tarfile, logfile, '1')
#
# Remove the workfile after it is done.
#
sys.exit()
