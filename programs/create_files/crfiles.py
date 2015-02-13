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
# You should have received a copy of the GNU General Public
# License along with this program; if not, write to the
# Free Software Foundation, Inc., 59 Temple Place - Suite 330,
# Boston, MA 021110-1307, USA.
#
# XXX: Future improvements:
#   
# Program     : crfiles.py
# Description : Using extendo, create a N number of files according to
#               parameters provided.
#               This program is to be run individually, if needs to be
#               executed in cluster, it can be called by a control
#               program integrated with LAM/MPI.
# Author      : Marcos E. Matsunaga 
# E-mail      : Marcos.Matsunaga@oracle.com

#
import os, sys, time, optparse, socket, string, o2tf, pdb, config
#
DEBUGON = os.getenv('DEBUG',0)
#
uname = os.uname()
lhostname = str(socket.gethostname())
#
# MAIN
#
Usage = 'usage: %prog [-b|--blocksize size] \
[-d|--directory directory] \
[-e|--extendo] \
[--extint <interval to create extend in milliseconds>] \
[--extloop <loop count to create extends>] \
[-f|--fileprefix <filename prefix>] \
[-h|--help] \
[-i|--interval interval] \
[-l|--logfile logfilename] \
[-n|--nfiles <# of files to create>] \
[-s|--size <file size in Kbytes>]'

if __name__=='__main__':
    parser = optparse.OptionParser(Usage)
#
    parser.add_option('-b', 
			'--blocksize', 
			dest='blocksize', 
			default='256k', 
			type='string', 
			help='Blocksize used to create the file. \
				Not used with -e.')
#
    parser.add_option('-d',
			'--directory',
			dest='directory',
			type='string',
			help='Directory where the files will be created.')
#
    parser.add_option('-e',
			'--extendo',
			action='store_true',
			dest='pgm',
			help='Use extendo to create files. This doesn\'t \
			  make use of blocksize option. If not specified, \
			  it will use dd by default.')
#
    parser.add_option('--extint', 
			dest='extint',
			type='int', 
			default=0, 
			help='Interval between extents created by extendo.\
			  Only works with -e option')
#
    parser.add_option('--extloop', 
			dest='extloop',
			type='int', 
			default=1, 
			help='Number of extents to be create by extendo\
			  or number of blocks if using dd.')
#
    parser.add_option('-f', 
			'--fileprefix', 
			dest='fileprefix',
			type='string', 
			default='crfile_', 
			help='File prefix to be used to create the files.')
#
    parser.add_option('-i', 
			'--interval', 
			dest='interval',
			type='int', 
			default=2, 
			help='Interval in seconds between file creation.')
#
    parser.add_option('-l', 
			'--logfile', 
			dest='logfile',
			type='string', 
			default='/tmp/crfiles.log', 
			help='Logfile where the output will be saved. \
			      Defaults to /tmp/crfiles.log.')
#
    parser.add_option('-n',
			'--nfiles',
			dest='nfiles',
			default='10',
			type='int',
			help='Number of files that will be \
				created in the run.')
#
    parser.add_option('-s',
			'--size',
			dest='size',
			default='1024',
			type='int',
			help='File size in Kbytes')
#
    (options, args) = parser.parse_args()
    if len(args) != 0:
        parser.error('incorrect number of arguments')
    if not options.directory:
        parser.error('Must provide a directory to run the test.')
#
    blocksize = options.blocksize
    logfile = options.logfile
    directory = options.directory
    if options.pgm:
       EXEC = config.BINDIR + '/extendo'
    else:
       EXEC = '/bin/dd'
    extint = options.extint
    extloop = options.extloop
    fileprefix = options.fileprefix
    interval = options.interval
    nfiles = options.nfiles
    size = options.size
#
if DEBUGON:
   o2tf.printlog('crfiles: blocksize = (%s)' % blocksize, 
			logfile, 
			0, 
			'')
   o2tf.printlog('crfiles: directory = (%s)' % directory, 
			logfile, 
			0, 
			'')
   o2tf.printlog('crfiles: EXEC = (%s)' % EXEC, 
			logfile, 
			0, 
			'')
   o2tf.printlog('crfiles: extint = (%s)' % extint, 
			logfile, 
			0, 
			'')
   o2tf.printlog('crfiles: extloop = (%s)' % extloop, 
			logfile, 
			0, 
			'')
   o2tf.printlog('crfiles: fileprefix = (%s)' % fileprefix, 
			logfile, 
			0, 
			'')
   o2tf.printlog('crfiles: interval = (%s)' % interval, 
			logfile, 
			0, 
			'')
   o2tf.printlog('crfiles: logfile = (%s)' % logfile, 
			logfile, 
			0, 
			'')
   o2tf.printlog('crfiles: nfiles = (%s)' % nfiles, 
			logfile, 
			0, 
			'')
#
# First, if directory does exist.
#
from os import access,F_OK
if not os.access(directory,os.F_OK|os.W_OK) :
    if DEBUGON:
        o2tf.printlog('crfiles: Directory %s does not exist' % directory,
                        logfile, 
                        0,
                        '')
    os.makedirs(directory,0755)
else:
    if DEBUGON:
        o2tf.printlog('crfiles: Directory %s does exist' % directory,
                        logfile, 
                        0,
                        '')
#
for i in range(nfiles):
   if options.pgm:
      os.system('%s %s/%s_%s %s %s %s' % \
			(EXEC, \
			directory, \
			fileprefix + '_' + lhostname, \
			i, \
			size / extloop, \
			extint, \
			extloop))
      time.sleep(interval)
   else:
      os.system('%s if=/dev/zero of=%s/%s_%s bs=%s count=%s' % \
			(EXEC, \
			directory, \
			fileprefix + '_' + lhostname, \
			i, \
			size / extloop, \
			extloop))
