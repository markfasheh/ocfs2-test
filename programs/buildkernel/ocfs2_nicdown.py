#!/usr/bin/env python3
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
# Program      :  ocfs2_nicdown.py
# Description  :  This script will only disable the NIC used by OCFS2
#
# Author       :  Marcos E. Matsunaga
import o2tf, os, config, sys, pwd, optparse

DEBUGON = os.getenv('DEBUG',0)
userid = pwd.getpwuid(os.getuid())[0]
Usage = 'Usage: %prog [-D|--Debug] \
[-l|--logfile logfile]'
#
if userid != 'root':
	o2tf.printlog('ocfs2_nicdown: Not enough privileges to run. Need to be root.',
	logfile,
	0,
	'')
	sys.exit(1)
if __name__=='__main__':
	parser = optparse.OptionParser(Usage)
#
	parser.add_option('-l',
		'--logfile',
		dest='logfile',
		default='%s/log/ocfs2_nicdown.log' % config.O2TDIR,
		type='string',
		help='Logfile to be used by the script.')
#
	parser.add_option('-D',
		'--debug',
		action="store_true",
		dest='debug',
		default=False,
		help='Turn the debug option on. Default=False.')
#
	(options, args) = parser.parse_args()
	if len(args) != 0:
		parser.error('incorrect number of arguments')
	if options.debug:
		DEBUGON=1
	logfile=options.logfile
#
cluster = o2tf.GetOcfs2Cluster()
NIC = o2tf.GetOcfs2NIC(DEBUGON, cluster)
o2tf.printlog('ocfs2_nicdown: Bringing interface %s down' % NIC,
	logfile,
	0,
	'')
os.system('/sbin/ifconfig %s down' % NIC)
