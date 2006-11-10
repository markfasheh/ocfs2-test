#
# o2tf - OCFS2 Tests Functions.
#
import os, sys, signal, string, popen2, socket, time, pdb, shutil, platform
import config, stat
#from log import log
# Set time executable to use.
uname = os.uname()
#
class RunAlarm(Exception):
    pass
#
# printlog is used by :
#   - o2tf.py
#   - run_buildkernel.py
#   - buildkernel.py
def printlog(message, logfile, prflag=0, prsep=''):
   'print message to the logfile.'
   'takes 4 parameters: message, logfilename, prflag and prsep'
   'prflag can be:'
   '  0 : Do nothing. Just print the message'
   '  1 : Print prflag content before printing message'
   '  2 : Print prflag content after printing message'
   '  3 : Print prflag content before and after printing message'
   'prflag can be any single character used to make a separator'
   'The separator will be printed 10 times with no space between'
   hostname = str(socket.gethostname())
   datetime = time.asctime(time.localtime())
   from os import access,F_OK
   if os.access(logfile,F_OK) == 0:
      print 'logfile %s does not exist.' % logfile
      os.system('touch ' + logfile)
   
#
   fd = open(logfile, 'a+', 0)
#
   if prflag == 1 or prflag == 3:
      fd.write(hostname + ': ' + datetime + ' : '+ (prsep * 10) + '\n')
#
   fd.write(hostname + ': ' + datetime + ' : '+ message + '\n')
   print '%s : %s : %s ' % (hostname, datetime, message)
#
   if prflag == 2 or prflag == 3:
      fd.write(hostname + ': ' + datetime + ' : '+ (prsep * 10) + '\n')
   fd.close
#
# extract_tar is used by :
#   - o2tf.py
def extract_tar(DEBUGON, logfile, nodes, dirl, tarfile):
    'Extract the kernel source tarfile into each one of the directories, if \
	they do not exist'
    'And extract the proper config in the new directory'
    nodelist = []
    dirlist=dirl.split(',')
    dirlen=len(dirlist)
    if DEBUGON:
       printlog('o2tf.extract_tar: logfile = (%s)' % \
			logfile, 
			logfile, 
			0, 
			'')
       printlog('o2tf.extract_tar: nodes = (%s)' % \
			nodes, 
			logfile, 
			0, 
			'')
       printlog('o2tf.extract_tar: dirlist = (%s)' % \
			dirlist, 
			logfile, 
			0, 
			'')
       printlog('o2tf.extract_tar: tarfile = (%s)' % \
			tarfile, 
			logfile, 
			0, 
			'')
    srcdir=os.path.dirname(tarfile)
    if len(nodes) == 1:
       nodelist = nodelist.add(nodes)
    else:
       nodelist = nodes.split(',')
#
    if len(logfile) == 0:
       logfile=str(os.getcwd()  + '/extract_tar.log')
    CreateDir(DEBUGON, dirl, logfile)
    from os import access,F_OK
    for i in range(dirlen):
        nodelen=len(nodelist)
        for x in range(nodelen):
           wdir=dirlist[i] + '/' + nodelist[x]
           if DEBUGON:
              printlog('o2tf.extract_tar: wdir = %s' % 
			wdir, 
			logfile, 
			0, 
			'')
              printlog('o2tf.extract_tar: srcdir = %s' % 
			srcdir, 
			logfile, 
			0, 
			'')
           if os.access(wdir, F_OK) == 1:
              printlog('o2tf.extract_tar: Directory %s already exists. \
			Skipping...' % \
			wdir, 
			logfile, 
			0, 
			'')
              continue
           printlog('o2tf.extract_tar: Creating directory %s.' % 
			(dirlist[i] + '/' + nodelist[x]), 
			logfile, 
			0, 
			'')
           os.mkdir(dirlist[i] + '/' + nodelist[x]) 
           os.chdir(dirlist[i] + '/' + nodelist[x])
           untar(DEBUGON, (dirlist[i] + '/' + nodelist[x]), tarfile, logfile)
           if DEBUGON:
              printlog('o2tf.extract_tar: Src Config file = %s' % \
			str(srcdir + '/' + platform.machine() + \
			'-linux-2.6.config'), 
			logfile, 
			0, 
			'')
              printlog('o2tf.extract_tar: Dest Config file = %s' % \
			str(dirlist[i] + '/' + nodelist[x] + \
			'/linux-2.6/.config'), 
			logfile, 
			0, 
			'')
           shutil.copy2(srcdir+'/'+platform.machine()+'-linux-2.6.config', 
			dirlist[i]+'/'+nodelist[x]+'/linux-2.6/.config')
#
# CreateDir is used by :
#   - o2tf.py
#   - crdel_gen_files
def CreateDir(DEBUGON, dirl, logfile):
    'Create directories from a passed dirlist'
    if DEBUGON:
       printlog('o2tf.CreateDir: Started.', logfile, 0, '')
    dirlist = string.split(dirl,',')
    ndir = len(dirlist)
    if DEBUGON:
       printlog('o2tf.CreateDir: dirlist = (%s)' % dirlist, 
			logfile, 
			0, 
			'')
       printlog('o2tf.CreateDir: dirlen = (%s)' % ndir, 
			logfile, 
			0, 
			'')
    from os import access,F_OK
    for i in range(ndir):
#
       if os.access(dirlist[i],F_OK) == 0:
          printlog('o2tf.CreateDir: Directory %s does not exist. \
			Creating it.' % \
			dirlist[i], 
			logfile, 
			0, 
			'')
          os.makedirs(dirlist[i],0755)
    if DEBUGON:
       printlog('o2tf.CreateDir: Ended.', 
			logfile, 
			0, 
			'')

#
# untar is used by :
#   - o2tf.py
def untar(DEBUGON, destdir, tarfile, logfile):
    'Untar into a destdir, the tarfile, logging the output into a logfile'
    'For the moment, it only works with compressed tar files.'
    if DEBUGON:
       printlog('o2tf.untar: Started.', logfile, 0, '')
    import zipfile
    options = 'xvf'
#    if zipfile.is_zipfile(tarfile): #for now, it is always compressed
    compress = 'z'
#    else:
#       compress = ''
    st = os.stat(destdir)
    if stat.S_ISREG(st.st_mode):
       o2tf.printlog('o2tf.untar: (%s) is a regular file. Can\'t \
			extract tarfile into a regular file.' % \
			destdir, 
			logfile, 
			0, 
			'')
    else:
       printlog('o2tf.untar: Extracting tar file %s into %s directory.' % \
			(tarfile, destdir), 
			logfile, 
			1, 
			'+-')
       os.system('cd %s; tar %s %s 2>&1 >> %s; du -sh * >> %s' % \
			(destdir, 
			options + compress, 
			tarfile, 
			logfile, 
			logfile) )
    printlog('o2tf.untar: Extraction ended.', logfile, 2, '+-')
#
# StartMPI is used by :
#   - o2tf.py
def StartMPI(DEBUGON, nodes, logfile):
    'Start LAM/MPI on all nodes, doing a sanity check before.'
    from os import access,F_OK
    if os.access(config.LAMBOOT, F_OK) == 0:
       printlog('o2tf.StartMPI: Lamboot not found', 
			logfile, 
			0, 
			'')
       sys.exit(1)
    if os.access(config.LAMHOSTS, F_OK) == 1:
       os.system('rm -f ' + config.LAMHOSTS)
    nodelist = string.split(nodes,',')
    nodelen = len(nodelist)
    fd = open(config.LAMHOSTS,'w',0)
    for i in range(nodelen):
       fd.write(nodelist[i] + '\n')
    fd.close()
# Check if all hosts are accessible
    try:
       if DEBUGON:
          printlog('o2tf.StartMPI: Trying to run %s with %s file.' % \
			(config.RECON, config.LAMHOSTS), 
			logfile, 
			0, 
			'')
       os.system('%s -v %s' % (config.RECON, config.LAMHOSTS))
    except os.error:
       pass
# Looks like everything is ok. So, run lamboot.
    try:
       if DEBUGON:
          printlog('o2tf.StartMPI: Trying to run %s with %s file.' % \
			(config.LAMBOOT, config.LAMHOSTS), 
			logfile, 
			0, 
			'')
       os.system('%s -v %s' % (config.LAMBOOT, config.LAMHOSTS))
    except os.error:
       pass
#
# mpirun is used by :
#   - o2tf.py
#   - run_buildkernel.py
def mpirun(DEBUGON, nproc, cmd, nodes, logfile):
    'Execute commands in parallel using LAM/MPI.'
    from os import access,F_OK
    found = 0
    uname = os.uname()
    nodelen = len(string.split(nodes,','))
    StartMPI(DEBUGON, nodes, logfile)
    try:
       if DEBUGON:
          printlog('o2tf.mpirun: MPIRUN = %s' % config.MPIRUN, 
			logfile, 
			0, 
			'')
          printlog('o2tf.mpirun: nproc = %s' % nproc, 
			logfile, 
			0, 
			'')
          printlog('o2tf.mpirun: nodelen = %d' % nodelen, 
			logfile, 
			0, 
			'')
          printlog('o2tf.mpirun: MPIRUNPARTS = %s' % config.MPIRUNPARTS, 
			logfile, 
			0, 
			'')
          printlog('o2tf.mpirun: cmd = %s' % cmd, 
			logfile, 
			0, 
			'')
       pid = os.spawnv(os.P_NOWAIT, 
			'/bin/bash', 
			['bash', 
			'-xc', 
			config.MPIRUN + ' -w n0-%d %s %s' % \
			( nodelen - 1, config.MPIRUNPARTS, cmd)])
       os.waitpid(pid,0)
    except os.error:
       pass
#
# lamexec is used by :
#   - 
def lamexec(DEBUGON, nproc, cmd, nodes, logfile):
    'Execute commands in parallel using LAM/MPI.'
    from os import access,F_OK
    found = 0
    uname = os.uname()
    nodelen = len(string.split(nodes,','))
    StartMPI(DEBUGON, nodes, logfile)
    try:
       if DEBUGON:
          printlog('o2tf.mpirun: MPIRUN = %s' % config.MPIRUN, 
			logfile, 
			0, 
			'')
          printlog('o2tf.mpirun: nproc = %s' % nproc, 
			logfile, 
			0, 
			'')
          printlog('o2tf.mpirun: nodelen = %d' % nodelen, 
			logfile, 
			0, 
			'')
          printlog('o2tf.mpirun: MPIRUNPARTS = %s' % config.MPIRUNPARTS, 
			logfile, 
			0, 
			'')
          printlog('o2tf.mpirun: cmd = %s' % cmd, 
			logfile, 
			0, 
			'')
       pid = os.spawnv(os.P_NOWAIT, 
			'/bin/bash', 
			['bash', 
			'-xc', 
			config.LAMEXEC + ' -w n0-%d %s' % \
			( nodelen - 1, cmd)])
       os.waitpid(pid,0)
    except os.error:
       pass
#
# lrand is used by :
#   - 
def lrand(DEBUGON, max):
#
    'Generate and return an integer random number. Takes the max number \
	as parameter.'
    import time, random
    random.seed(time.time())
    return(random.randint(1,max))
