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
# Program	  : cross_delete.py
# Description : Run a build of the kernel on each one of the ocfs2
#					 partitions mounted, in parallel.
#					 i.e: The system has 7 partitions, it will run 7
#							builds in parallel, one for each partition.
# Author		  : Marcos E. Matsunaga 
# E-mail		  : Marcos.Matsunaga@oracle.com

#
import os, sys, time, optparse, socket, string, o2tf, config, pdb, timing
import time, config
#
MAXDIRLEVEL = 64
DEBUGON = os.getenv('DEBUG',0)
#
#
# create a list with 3 lists
# first list is nodenames
# second list is directories to be created
# third list is directories to be removed.
#
node_dirs = {}
#
logfile = config.LOGFILE
nodelen = 0
dirlen = 0
uname = os.uname()
lhostname = str(socket.gethostname())
stagedir = ''
#
# FUNCTIONS
#
def BuildCrList(nodelist, nodelen, dirlist, dirlen):
	global node_dirs, stagedir
	from os import access, remove, F_OK
	for n in range(nodelen):
		dirl = []
		DelDir = []
		for i in range(dirlen):
			dirlvl = o2tf.lrand(1,MAXDIRLEVEL)
			directory=dirlist[i]
			DelDir.append(directory+'/'+nodelist[n]+'_0')
			for x in range(dirlvl):
				directory = directory+'/'+nodelist[n]+'_'+str(x)
			dirl.append(directory)
			if DEBUGON:
				o2tf.printlog('cross_delete:BuildList: Node = (%s)' % nodelist[n],
					logfile,
					0,
					'')
				o2tf.printlog('cross_delete:BuildList: dirlvl = (%s)' %  dirlvl,
					logfile,
					0,
					'')
				o2tf.printlog('cross_delete:BuildList: directory = (%s)' % 
					directory,
					logfile,
					0,
					'')
				o2tf.printlog('cross_delete:BuildList: DelDir = (%s)' % 
					DelDir,
					logfile,
					0,
					'')
				o2tf.printlog('cross_delete: dirl (%s)' % dirl,
					logfile,
					0,
					'')
#
		node_dirs[nodelist[n]+'_C'] = (dirl)
		node_dirs[nodelist[n]+'_D'] = (DelDir)
		if DEBUGON:
			o2tf.printlog('cross_delete: nodes_dirs = %s' % node_dirs,
				logfile, 
				0, 
				'')
#
# Since the directory list is ready for the node, write it to the workfile
#
		if access(os.path.join(stagedir, nodelist[n]+'_C.dat'), F_OK) == 1:
			remove(os.path.join(stagedir, nodelist[n]+'_C.dat'))
		fd = open(os.path.join(stagedir, nodelist[n]+'_C.dat'), 'w',0)
		fd.write(string.join(node_dirs[nodelist[n]+'_C'],','))
		fd.close
		if DEBUGON:
			o2tf.printlog('cross_delete:BuildDelList: (%s) was written to \
				the file %s' % (string.join(node_dirs[nodelist[n]+'_C'],','),
			os.path.join(stagedir, nodelist[n]+'_C.dat')), 
			logfile, 
			0, 
			'')
#
# Build Delete list
#
def BuildDelList(nodelist, dirlist):
#
# Now, invert the pair directories (A = Bdir and B = Adir).
#
	from os import access, remove, F_OK
	for n in range(nodelen):
		if divmod(n,2)[1] == 1:
			if access(os.path.join(stagedir,nodelist[n]+'_D.dat'), F_OK) == 1:
				remove(os.path.join(stagedir,nodelist[n]+'_D.dat'))
			fd = open(os.path.join(stagedir,nodelist[n]+'_D.dat'), 'w',0)
			fd.write(string.join(node_dirs[nodelist[n-1]+'_D'],','))
			fd.close
			if DEBUGON:
				o2tf.printlog('cross_delete:BuildDelList: (%s) was written to \
					the file %s' % (string.join(node_dirs[nodelist[n-1]+'_D'],','),
				os.path.join(stagedir, nodelist[n]+'_D.dat')), 
				logfile, 
				0, 
				'')
			fd = open(os.path.join(stagedir, nodelist[n-1]+'_D.dat'), 'w',0)
			fd.write(string.join(node_dirs[nodelist[n]+'_D'],','))
			fd.close
			if DEBUGON:
				o2tf.printlog('cross_delete:BuildDelList: (%s) was written to \
					the file %s' % (string.join(node_dirs[nodelist[n]+'_D'],','),
				os.path.join(stagedir, nodelist[n-1]+'_D.dat')),
				logfile,
				0,
				'')
#
def Cleanup(ret):
#
	os.system('rm -f '+stagedir+'/*.dat')
	sys.exit(ret)
#=============================================================================
#
# MAIN
#
Usage = 'usage: %prog [-c|count count] \
[-d|--dirlist dirlist] \
[-i|--if <Network Interface>] \
[-l|-logfile logfilename] \
[-n|nodes nodelist] \
[-t|--tarfile fullpath tar filename] \
[-h|--help]'
if __name__=='__main__':
	parser = optparse.OptionParser(Usage)
#
	parser.add_option('-c',
		'--count',
		dest='count',
		type='int',
		default=1,
		help='Number of times the test will be executed.')
#
	parser.add_option('-d',
		'--dirlist',
		dest='dirlist',
		type='string',
		help='Directory where the files will be extracted.')
#
        parser.add_option('-i',
                '--if',
                dest='interface',
                type='string',
                help='Network Interface name to be used for MPI messaging.')
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
	if not options.tarfile:
		parser.error('Must provide a gzipped kernel tarball to run the test.')
#
	tarfile = options.tarfile
	count = options.count
	dirlist = options.dirlist.split(',')
	dirlen = len(dirlist)
	interface = options.interface
	nodelist = options.nodes.split(',')
	nodelen = len(nodelist)
	if options.logfile:
		logfile = options.logfile
	if nodelen < 2:
		o2tf.printlog('cross_delete: nodelist must have at least 2 nodes' %
			dirlist,
			logfile,
			0,
			'')
		sys.exit(1)
#
# Find if the #of nodes is odd. If so, remove the last one to make even.
#
	if divmod(nodelen,2)[1] == 1:
		if DEBUGON:
			o2tf.printlog('cross_delete: nodelist = (%s)' % nodelist,
				logfile,
				0,
				'')
			o2tf.printlog('cross_delete: Deleting node %s from the list' %
				nodelist[nodelen - 1],
				logfile,
				0,
				'')
		nodelist.pop()
		nodelen = nodelen - 1
		if DEBUGON:
			o2tf.printlog('cross_delete: New nodelist = (%s)' % nodelist,
				logfile,
				0,
				'')
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
	o2tf.printlog('cross_delete: count = (%s)' % count,
		logfile,
		0,
		'')
	o2tf.printlog('cross_delete: dirlist = (%s)' % dirlist,
		logfile,
		0,
		'')
	o2tf.printlog('cross_delete: dirlen = (%s)' % dirlen,
		logfile,
		0,
		'')
	o2tf.printlog('cross_delete: nodelist = (%s)' % nodelist,
		logfile,
		0,
		'')
	o2tf.printlog('cross_delete: nodelen = (%s)' % nodelen,
		logfile,
		0,
		'')
	o2tf.printlog('cross_delete: logfile = (%s)' % logfile,
		logfile,
		0,
		'')
	o2tf.printlog('cross_delete: tarfile = (%s)' % tarfile,
		logfile,
		0,
		'')
#
# Will use the first test directory to place the workfiles.
#
stagedir = dirlist[0]
#
o2tf.OpenMPIInit(DEBUGON, ','.join(nodelist), logfile, 'ssh')
for y in range(count):
	o2tf.printlog('cross-delete: RUN# %s of %s' % (y+1, count),
		logfile,
		3,
		'=')
	BuildCrList(nodelist, nodelen, dirlist, dirlen)
	BuildDelList(nodelist, dirlist)
#
	if DEBUGON:
		cmdline = os.path.join(config.BINDIR, 'crdel_gen_files.py -D')
	else:
		cmdline = os.path.join(config.BINDIR, 'crdel_gen_files.py')
	ret = o2tf.openmpi_run( DEBUGON, nproc, str('%s -s %s -l %s -t %s' % \
		(cmdline, stagedir,
		options.logfile,
		tarfile) ),
		','.join(nodelist),
		'ssh',
		options.interface,
		logfile,
		'WAIT')
	if not ret:
		o2tf.printlog('cross_delete: RUN# %s extraction successful.'\
			% (y+1), logfile, 0, '')
	else:
		o2tf.printlog('cross_delete: RUN# %s extraction failed.' \
			% (y+1), logfile, 0, '')
		Cleanup(1)
	if DEBUGON:
		cmdline = os.path.join(config.BINDIR, 'crdel_del_files.py -D')
	else:
		cmdline = os.path.join(config.BINDIR, 'crdel_del_files.py')
	ret = o2tf.openmpi_run( DEBUGON, nproc, str('%s -s %s -l %s ' % \
		(cmdline, stagedir, options.logfile) ),
		','.join(nodelist),
		'ssh',
		options.interface,
		logfile,
		'WAIT')
	if not ret:
		o2tf.printlog(
			'cross_delete: RUN# %s delete successful.' % (y+1),
			logfile, 0, '')
	else:
		o2tf.printlog('cross_delete: RUN# %s delete failed.' % (y+1),
			logfile, 0, '')
		Cleanup(1)
Cleanup(0)
