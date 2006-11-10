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
# Program     : buildkernel.py
# Description : Run a build of the kernel on each one of the ocfs2
#               partitions mounted, in parallel.
#               i.e: The system has 7 partitions, it will run 7
#                    builds in parallel, one for each partition.
# Author      : Marcos E. Matsunaga 
# E-mail      : Marcos.Matsunaga@oracle.com

#
import os, sys, time, optparse, socket, string, o2tf, pdb, timing, time, config
#
deftarfile = config.WORKFILESDIR + '/' + config.TARFILE
#
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
      o2tf.printlog('tbuild - current directory %s' % \
			os.getcwd(), 
			logfile, 
			0, 
			'')
      o2tf.printlog('command  %s ' % \
			command, 
			logfile, 
			0, 
			'')
   pidlist[thrid] = os.spawnv(os.P_NOWAIT, 
			'/bin/bash', 
			['bash', 
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
   o2tf.printlog('buildkernel.check_thread: Waiting for processes to finish \
	on thread %s, directory %s' % \
			(runnumber, wdir), 
			logfile, 
			0, 
			'')
   while len(pidlist) > 0:
      o2tf.printlog('buildkernel.check_thread: Checking processes on thread \
		%s, directory %s' % \
			(runnumber, wdir), 
			logfile, 
			0, 
			'')
      o2tf.printlog('buildkernel.check_thread: pid list %s' % 
			(pidlist), 
			logfile, 
			0, 
			'')
      for z in range(len(pidlist)):
           out = os.waitpid(pidlist[z],os.WNOHANG)
           o2tf.printlog('buildkernel.check_thread: z=%s, out=%s' % \
			(z, out[1]), 
			logfile, 
			0, 
			'')
           if out[0] > 0:
              o2tf.printlog('buildkernel.check_thread: Removing pid \
		%s from the list' %  \
			pidlist[z], 
			logfile, 
			0, 
			'')
              t2 = time.clock()
              o2tf.printlog('buildkernel.check_thread: %s [%s]: Build \
		time is %f seconds' % 
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
if __name__=='__main__':
    parser = optparse.OptionParser('usage: %prog [-c|--count count] \
			[-d|--dirlist dirlist] \
			[-l|-logfile logfilename] \
			[-n | --nodelist nodename] \
			[-t|--tarfile fullpath tar filename] \
			[-h|--help]')

    parser.add_option('-c', 
			'--count', 
			dest='count', 
			default='1', 
			type='int', 
			help='Number of times it will build \
				the kernel. Default = 1.')
#
    parser.add_option('-d', 
			'--directorylist', 
			dest='dirlist',
			type='string',
			help='List of directories that will be \
				used by the test. ')
#
    parser.add_option('-l', 
			'--logfile', 
			dest='logfile',
			type='string', 
			help='If logfile is specified, a single logfile will \
				be used by all processes, otherwise, \
				individual logfiles will be created per \
				process. Default will be to create a logfile \
				per process.')
#
    parser.add_option('-n', 
			'--nodelist', 
			dest='nodelist',
			type='string', 
			help='Nodename owner of the directory it will run the \
				find command. Default to local hostname.')
    parser.add_option('-t', 
			'--tarfile', 
			dest='tarfile', 
			default=deftarfile,
			type='string', 
			help='Fullpath filename of the tar file containing the\
				 kernel that will be used. Defaults to \'%s\'' \
				% deftarfile)
#
    (options, args) = parser.parse_args()
    dirlist = options.dirlist.split(',')
    dirlen = len(dirlist)
    count = options.count
    logfile = options.logfile
    nodelen = len(options.nodelist)
    if nodelen == 1:
       nodelist = nodelist.add(options.nodelist)
    else:
       nodelist = options.nodelist.split(',')
    nodelen = len(nodelist)
    tarfile = options.tarfile
#
if DEBUGON:
   o2tf.printlog('buildkernel: dirlist = (%s)' % dirlist, logfile, 0, '')
   o2tf.printlog('buildkernel: dirlen = (%s)' % dirlen, logfile, 0, '')
   o2tf.printlog('buildkernel: logfile = (%s)' % logfile, logfile, 0, '')
   o2tf.printlog('buildkernel: nodelist = (%s)' % nodelist, logfile, 0, '')
   o2tf.printlog('buildkernel: count = (%s)' % count, logfile, 0, '')
   o2tf.printlog('buildkernel: tarfile = (%s)' % tarfile, logfile, 0, '')
#
o2tf.extract_tar(DEBUGON, logfile, lhostname, options.dirlist, tarfile)
#
# For each entry in dirlist, fork a process to run the build and a find on the
# directory owned by another node.
#
# First find which node to run the find.
#
for y in range(nodelen):
    if lhostname == nodelist[y]:
       if y == 0:
          nodefind = nodelist[ -1 ]
       else:
          nodefind = nodelist[ y - 1 ]
       if DEBUGON:
          o2tf.printlog('buildkernel: Main - Node to use on find %s' % \
			nodefind, 
			logfile, 
			0, 
			'')

#
for i in range(int(count)):
    pidlist = [0] * dirlen * 2
    for x in range(dirlen):
       wdir = dirlist[x] + '/' + str(socket.gethostname()) +'/'+ config.KERNELDIR
       cmd = 'cd ' + wdir + ';  /usr/bin/make -j2 V=1 2>&1 >> %s' % logfile

       if DEBUGON:
          o2tf.printlog('buildkernel: main - current directory %s' % \
			os.getcwd(), \
			logfile, \
			0, \
			'')
          o2tf.printlog('buildkernel: main - working directory %s' % \
			dirlist[x], \
			logfile, \
			0, \
			'')
          o2tf.printlog('buildkernel: main - wdir =  %s' % \
			wdir, \
			logfile, \
			0, \
			'')
          o2tf.printlog('buildkernel: main - cmd = %s' % \
			cmd, \
			logfile, \
			0, \
			'')
       t1 = time.clock()
       tbuild(x, cmd)
       cmd = 'cd ' + dirlist[x] + '/' + nodefind + '; find . -print \
			(2>&1 >> %s' % logfile
       tbuild(int(x + dirlen), cmd)
       if DEBUGON:
          o2tf.printlog('buildkernel: main - cmd = %s' % cmd, logfile, 0, '')
    check_thread(i, wdir, logfile)
                                                                                                                      
