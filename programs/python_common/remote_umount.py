#!/usr/bin/env python
#
#
# Copyright (C) 2006 Oracle.    All rights reserved.
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
# Program	:	remote_umount.py
# Description	:	Perform remote umount of partitions.
#			It will take nodes and mountpoints as
#			arguments.
#
# Author	:	Marcos E. Matsunaga 

#
import os, pwd, sys, optparse, socket, time, o2tf, pdb, config
#
DEBUGON = os.getenv('DEBUG',0)
#
userid = pwd.getpwuid(os.getuid())[0]
logfile = config.LOGFILE
#
Usage = 'Usage: %prog [-m|--mountpoint mountpoint] \
[-n|--nodes nodelist]'
#
if userid == 'root':
	o2tf.printlog('This program uses Openmpi. Should not run as root',
		logfile, 0, '')
	sys.exit(1)
if __name__=='__main__':
	parser = optparse.OptionParser(Usage)
#
	parser.add_option('-m', 
		'--mountpoint', 
		dest='mountpoint',
		type='string',
		help='Directory where the partition will be mount.')
#
	parser.add_option('-n', 
		'--nodes', 
		dest='nodelist',
		type='string',
		help='List of nodes where the test will be executed.')
#
	(options, args) = parser.parse_args()
	if len(args) != 0:
		parser.error('incorrect number of arguments')
	if not options.nodelist:
		parser.error('Please specify node(s) list.')
	if not options.mountpoint:
		parser.error('Please specify mountpoint.')
#
	nodelist = options.nodelist.split(',')
	nodelen = len(nodelist)
	if nodelen == 1:
		nodelist = nodelist.append(options.nodelist)
	else:
		nodelist = options.nodelist.split(',')

	nproc = nodelen
#
if DEBUGON:
	buildcmd=config.BINDIR+'/command.py --Debug --umount'
else:
	buildcmd=config.BINDIR+'/command.py --umount'
#
command = str('%s -m %s' % (buildcmd,
	options.mountpoint))
#
o2tf.OpenMPIInit(DEBUGON, options.nodelist, logfile, 'ssh')
#
#
ret = o2tf.openmpi_run(DEBUGON,
		 nproc,
		 str('%s' % command),
		 options.nodelist,
		 'ssh', logfile,
		 'WAIT')

if ret:
	sys.exit(1)
else:
	sys.exit(0)
