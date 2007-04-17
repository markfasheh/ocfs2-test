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
# You should have received a c.of the GNU General Public
# License along with this program; if not, write to the
# Free Software Foundation, Inc., 59 Temple Place - Suite 330,
# Boston, MA 021110-1307, USA.
#
# XXX: Future improvements:
#   
# Program     : cross_delete.py
# Description : Run a build of the kernel on each one of the ocfs2
#               partitions mounted, in parallel.
#               i.e: The system has 7 partitions, it will run 7
#                    builds in parallel, one for each partition.
# Author      : Marcos E. Matsunaga 
# E-mail      : Marcos.Matsunaga@oracle.com

#
import os, sys, time, optparse, socket, string, o2tf, config, pdb, timing
import time, config
#
MAXDIRLEVEL = 12
DEBUGON = os.getenv('DEBUG',0)
#
uname = os.uname()
lhostname = str(socket.gethostname())
#
# FUNCTIONS
#
def tbuild(thrid, command):
   'tbuild spawn process and build a process list'
   'tbuild takes 2 arguments:'
   '   thrid - Thread number, which will index pidlist'
   '   command - command to be executed'
   if DEBUGON:
      o2tf.printlog('cross_delete.tbuild - current directory is %s' % \
			os.getcwd(), 
			logfile, 
			0, 
			'')
      o2tf.printlog('cross_delete.command  %s ' % \
			command, 
			logfile, 
			0, 
			'')
   pidlist[thrid] = os.spawnv(os.P_NOWAIT, '/bin/bash', ['bash', 
							'-c', 
							command])
#   
def check_thread(runnumber, wdir, logfile):
   'check_thread checks the pidlist for running tasks until the last one \
	completes.'
   'check_thread takes 3 arguments:'
   '   runnumer - Sequence number of a specific batch.'
   '   wdir - working directory.'
   '   logfile - logfile name'
   o2tf.printlog('cross_delete.check_thread: Waiting for processes to finish \
			on thread %s, directory %s' % \
			(runnumber, wdir), 
			logfile, 
			0, 
			'')
   while len(pidlist) > 0:
      o2tf.printlog('cross_delete.check_thread: Checking processes on thread \
			%s, directory %s' % \
			(runnumber, wdir), 
			logfile, 
			0, 
			'')
      o2tf.printlog('cross_delete.check_thread: pid list %s' % \
			(pidlist), 
			logfile, 
			0, 
			'')
      for z in range(len(pidlist)):
           out = os.waitpid(pidlist[z],os.WNOHANG)
           o2tf.printlog('cross_delete.check_thread: z=%s, out=%s' % \
			(z, out[1]), 
			logfile, 
			0, 
			'')
           if out[0] > 0:
              o2tf.printlog('cross_delete.check_thread: Removing pid %s \
			from the list' %  \
			pidlist[z], 
			logfile, 
			0, 
			'')
              t2 = time.clock()
              o2tf.printlog('cross_delete.check_thread: %s [%s]: Build time \
			is %f seconds' % \
			(str(socket.gethostname()), pidlist[z], t2 - t1), 
			logfile, 
			0, 
			'')
              pidlist.remove(out[0])
              break
      time.sleep(10)
#
# MAIN
#
Usage = 'usage: %prog [-c|count count] \
[-d|--dirlist dirlist] \
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
			help='Number of times the test will be executed.')
#
   parser.add_option('-d', 
			'--dirlist', 
			dest='dirlist',
			type='string',
			help='Directory where the files will be extracted.')
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
   nodelist = options.nodes.split(',')
   nodelen = len(nodelist)
   logfile = options.logfile
if nodelen < 2:
   o2tf.printlog('cross_delete: nodelist must have at least 2 nodes' % \
			dirlist, 
			logfile, 
			0, 
			'')
   sys.exit(1)
if divmod(nodelen,2)[1] == 1:
   if DEBUGON:
      o2tf.printlog('cross_delete: nodelist = (%s)' % \
			nodelist, 
			logfile, 
			0, 
			'')
      o2tf.printlog('cross_delete: Deleting node %s from the list' % \
			nodelist[nodelen - 1], 
			logfile, 
			0, 
			'')
   nodelist.pop()
   nodelen = nodelen - 1
   if DEBUGON:
      o2tf.printlog('cross_delete: New nodelist = (%s)' % \
			nodelist, 
			logfile, 
			0, 
			'')
#
if DEBUGON:
   o2tf.printlog('cross_delete: count = (%s)' % \
			count, 
			logfile, 
			0, 
			'')
   o2tf.printlog('cross_delete: dirlist = (%s)' % \
			dirlist, 
			logfile, 
			0, 
			'')
   o2tf.printlog('cross_delete: dirlen = (%s)' % \
			dirlen, 
			logfile, 
			0, 
			'')
   o2tf.printlog('cross_delete: nodelist = (%s)' % \
			nodelist, 
			logfile, 
			0, 
			'')
   o2tf.printlog('cross_delete: nodelen = (%s)' % \
			nodelen, 
			logfile, 
			0, 
			'')
   o2tf.printlog('cross_delete: logfile = (%s)' % \
			logfile, 
			logfile, 
			0, 
			'')
   o2tf.printlog('cross_delete: tarfile = (%s)' % \
			tarfile, 
			logfile, 
			0, 
			'')
#
# Build a dictionary with nodename and directory
#
nod_dir = {}
# Will use the first test directory to place the workfiles.
#
stagedir = dirlist[0]
#
for n in range(nodelen):
   dirl = []
   for i in range(dirlen):
      dirlvl = o2tf.lrand(1,MAXDIRLEVEL)
      directory=dirlist[i]
      if DEBUGON:
         o2tf.printlog('cross_delete: Node = (%s)' % \
			nodelist[n], 
			logfile, 
			0, 
			'')
         o2tf.printlog('cross_delete: dirlvl = (%s)' % \
			dirlvl, 
			logfile, 
			0, 
			'')
         o2tf.printlog('cross_delete: directory = (%s)' % \
			directory, 
			logfile, 
			0, 
			'')
      for x in range(dirlvl):
         directory = directory + '/' + nodelist[n] + '_' + str(x)
      if DEBUGON:
         o2tf.printlog('cross_delete: Creating directory (%s)' % \
			directory, 
			logfile, 
			0, 
			'')
      o2tf.CreateDir(DEBUGON, directory, logfile)
#
      dirl.append(directory)
   nod_dir[nodelist[n]] = (dirl)
# Since the directory list is ready for the node, write it to the workfile
   if os.access(os.path.join(stagedir, nodelist[n] + '.dat'), os.F_OK) == 1:
      os.remove(os.path.join(stagedir, nodelist[n] + '.dat'))
   fd = open(os.path.join(stagedir, nodelist[n] + '.dat'), 'w',0)
   fd.write(string.join(nod_dir[nodelist[n]],','))
   fd.close
   if DEBUGON:
      o2tf.printlog('cross_delete: nod_dir = %s' % nod_dir, 
			
			logfile, 0, '')
#
# Start Creating and populating the directories.
# Call crdel_gen_files.py to do that on each node using mpi-run-parts.
#
cmdline = os.path.join(config.BINDIR, 'crdel_gen_files.py')
o2tf.StartMPI(DEBUGON, ','.join(nodelist), logfile)
o2tf.mpi_runparts( DEBUGON, config.NPROC, str('%s -s %s -l %s -t %s' % \
			(cmdline, stagedir, 
			options.logfile, 
			tarfile) ), 
			','.join(nodelist), 
			logfile)
#
# Now, invert the pair directories (A = Bdir and B = Adir).
#
for n in range(nodelen):
   if divmod(n,2)[1] == 1:
      if os.access(os.path.join(stagedir, nodelist[n] + '.dat'), os.F_OK) == 1:
         os.remove(os.path.join(stagedir, nodelist[n] + '.dat'))
      fd = open(os.path.join(stagedir, nodelist[n] + '.dat'), 'w',0)
      fd.write(string.join(nod_dir[nodelist[n-1]],','))
      fd.close
      if DEBUGON:
         o2tf.printlog('cross_delete:  (%s) was written to the file %s' % \
			(string.join(nod_dir[nodelist[n-1]],','), 
			os.path.join(stagedir, nodelist[n])), 
			logfile, 
			0, 
			'')
      fd = open(os.path.join(stagedir, nodelist[n-1] + '.dat'), 'w',0)
      fd.write(string.join(nod_dir[nodelist[n]],','))
      fd.close
      if DEBUGON:
         o2tf.printlog('cross_delete:  (%s) was written to the file %s' % \
			(string.join(nod_dir[nodelist[n]],','), 
			os.path.join(stagedir, nodelist[n-1])), 
			logfile, 
			0, 
			'')
#
#
# Start deleting the files the other node in the pair created.
# Call crdel_del_files.py to do that on each node using mpi-run-parts.
#
cmdline = os.path.join(config.BINDIR, 'crdel_del_files.py')
o2tf.mpi_runparts( DEBUGON, config.NPROC, str('%s -s %s -l %s ' % \
			(cmdline, stagedir, options.logfile) ), 
			','.join(nodelist), 
			logfile)
