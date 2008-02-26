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
# Program	:	command.py
# Description	:	This script just a client to be called from LAM/MPI
#			to locally execute commands like mount, umount, etc.
#
# Author	:	Marcos E. Matsunaga 

#
import os, pwd, sys, optparse, socket, time, o2tf, pdb, config
#
logfile = config.LOGFILE
#
Usage = 'Usage: %prog [--Debug] \
[-l|--label label] \
[-m|--mountpoint mountpoint] \
[--mount] \
[--umount]'
#
if __name__=='__main__':
	parser = optparse.OptionParser(Usage)
#
	parser.add_option('--Debug',
		action="store_true",
		dest='DEBUGON',
		default=False)
#
	parser.add_option('-l', 
		'--label', 
		dest='label', 
		type='string', 
		help='Label of the partition to be mounted.')
#
	parser.add_option('--mount',
		action="store_true",
		dest='domount',
		default=False)
#
	parser.add_option('-m', 
		'--mountpoint', 
		dest='mountpoint',
		type='string',
		help='Directory where the partition will be mount.')
#
	parser.add_option('--umount',
		action="store_true",
		dest='doumount',
		default=False)
#
	(options, args) = parser.parse_args()
	if len(args) != 0:
		parser.error('incorrect number of arguments')
	mounted = o2tf.CheckMounted(options.DEBUGON, logfile, 
		options.mountpoint)
	if options.domount:
		if not options.mountpoint:
			parser.error('Please specify mountpoint.')
		if not options.label:
			parser.error('Please specify Label.')
		if not mounted:
			o2tf.SudoMount(options.DEBUGON, logfile, 
				options.mountpoint, options.label)
		else:
			o2tf.printlog('Partition already mounted.',
				logfile, 0, '')
	if options.doumount:
		if not options.mountpoint:
			parser.error('Please specify mountpoint.')
		if mounted:
			o2tf.SudoUmount(options.DEBUGON, logfile, 
				options.mountpoint)
		else:
			o2tf.printlog('Partition not mounted.',
				logfile, 0, '')
#
