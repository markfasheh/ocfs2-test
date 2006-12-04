#!/usr/bin/env python
#
import os, sys, optparse, time, o2tf, pdb, config
#
deftarfile = os.path.join(config.WORKFILESDIR, config.TARFILE)
#
#
DEBUGON = os.getenv('DEBUG',0)
#
if __name__=='__main__':
    parser = optparse.OptionParser('usage: %prog [-c|--count count] \
				[-n|--nodes nodelist] \
				[-t|--tarfile fullpath tar filename] \
				[-d|--directorylist dirlist] \
				[-l|-logfile logfilename] \
				[-u|--user username] \
				[-h|--help]')
#
    parser.add_option('-c', 
			'--count', 
			dest='count', 
			default='1',
			type='int', 
			help='Number of times it will build the kernel. \
				Default = 1.')
#
    parser.add_option('-u', 
			'--user', 
			dest='userid',
			type='string', 
			help='Userid used to open ssh connections.')
#
    parser.add_option('-d', 
			'--directorylist', 
			dest='dirlist',
			type='string',
			help='List of directories that will be used by the \
				test.')
#
    parser.add_option('-l', 
			'--logfile', 
			dest='logfile',
			default='%s/log/run_buildkernel.log' % config.O2TDIR,
			type='string', 
			help='If logfile is specified, a single logfile will \
				be used by all processes, otherwise, \
				individual logfiles will be created per \
				process. Default will be to create a logfile \
				per process.')
#
    parser.add_option('-n', 
			'--nodes', 
			dest='nodelist',
			type='string',
			help='List of nodes where the test will be executed.')
#
    parser.add_option('-t', 
			'--tarfile', 
			dest='tarfile', 
			default=deftarfile,
			type='string', 
			help='Fullpath filename of the tar file containing \
				the kernel that will be used. Defaults \
				to \'%s\'' % deftarfile)

    (options, args) = parser.parse_args()
    if len(args) != 0:
        parser.error('incorrect number of arguments')
    dirlist = options.dirlist.split(',')
    dirlen = len(dirlist)
    nodelen = len(options.nodelist)
    if nodelen == 1:
       nodelist = nodelist.add(options.nodelist)
    else:
       nodelist = options.nodelist.split(',')
    logfile = options.logfile
    tarfile = options.tarfile
buildcmd= config.BINDIR+'/buildkernel.py'
if DEBUGON:
   o2tf.printlog('dirlist = (%s)' % dirlist, logfile, 0, '')
   o2tf.printlog('dirlen = (%s)' % dirlen, logfile, 0, '')
   o2tf.printlog('nodelist = (%s)' % nodelist, logfile, 0, '')
   o2tf.printlog('nodelen = (%s)' % nodelen, logfile, 0, '')
   o2tf.printlog('logfile = (%s)' % logfile, logfile, 0, '')
   o2tf.printlog('tarfile = (%s)' % tarfile, logfile, 0, '')
   o2tf.printlog('buildcmd = (%s)' % buildcmd, logfile, 0, '')
o2tf.StartMPI(DEBUGON, options.nodelist, logfile)
o2tf.mpirun( DEBUGON, config.NPROC, str('%s -d %s -c %s -l %s -n %s -t %s' % \
			(buildcmd, 
			options.dirlist, 
			options.count, 
			options.logfile, 
			options.nodelist, 
			tarfile) ), 
			options.nodelist, 
			options.userid )
