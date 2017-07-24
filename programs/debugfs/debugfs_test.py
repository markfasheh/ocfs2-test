#! /usr/bin/env python2
# coding = utf-8

import os
import stat
import sys
import time
import argparse
import o2tf
import config
import subprocess
import re
from datetime import datetime


'''
debugfs_test.py --log-dir=${logdir} --block-size=${blocksize} --cluster-size=${clustersize} --cluster-stack=${cluster_stack} --cluster-name=${cluster_name} ${device}
'''


def get_cmd_output(cmd):
	#print(cmd)
	return subprocess.Popen(cmd, bufsize = 16<<10, shell = True, stdout=subprocess.PIPE).stdout.read().decode('utf-8')

def get_global_bitmap_inode(device):
	cmd = "debugfs.ocfs2 -R 'ls //' {device} | grep global_bitmap".format(device= device)
	cmd += " | awk '{print $1}'"
	inode = get_cmd_output(cmd)
	return inode.strip()

def sys_stat(device):
	cmd = "debugfs.ocfs2 -R 'stats' {device}".format(device=device)
	return get_cmd_output(cmd)

def global_bitmap_stat(device):
	global_bitmap_inode = get_global_bitmap_inode(device)
	cmd = "debugfs.ocfs2 -R 'stat <{inode}>' {device}".format(inode=global_bitmap_inode, device = device)
	ret = get_cmd_output(cmd)
	return ret

def check_grpextents(device):
	output = global_bitmap_stat(device)
	split_output = output.split("Group Chain")

	summary = split_output[0]
	chains_stat = summary[1:]
	
	free_before = re.findall(r'^\s+\d+\s+\d+\s+\d+\s+(?P<free>\d+)\s+(?P<block>\d+)$', chains_stat, re.M)
	cmd = "debugfs.ocfs2 -n -R 'grpextents <{block}>' {device}"
	for i in free_before:
		group_desc_block = i[1]
		output = get_cmd_output(cmd.format(block=group_desc_block, device=device))
		if not output.find("###   Start    Length"):
			return False
	return True
	
		

def parse_arg():
	parser = argparse.ArgumentParser(description='Process some integers.')
	parser.add_argument(name="--log-dir")



if __name__ == '__main__':

	print("hello this is debugfs test")
	parser = argparse.ArgumentParser(description="some information here")
	'''
	debugfs_test.py --log-dir=${logdir} --block-size=${blocksize} --cluster-size=${clustersize} --cluster-stack=${cluster_stack} --cluster-name=${cluster_name} ${device}
	debugfs_test.py --log-dir=/root/ocfs2.log/2017-07-28_11:27 --block-size=4096 --cluster-size=32768 --cluster-stack=pcmk --cluster-name=hacluster /dev/sdb
	'''

	parser.add_argument("--log-dir", 		dest = "log_dir", )
	parser.add_argument("--block-size", 	dest = "block_size", type=int)
	parser.add_argument("--cluster-size", 	dest ="cluster_size", type=int)
	parser.add_argument("--cluster-stack", 	dest ="cluster_stack")
	parser.add_argument("--mount-point", 	dest ="mount_point")
	parser.add_argument("--cluster-name", 	dest = 'cluster_name')
	parser.add_argument("device")

	args = parser.parse_args()
	
	logfile = "debugfs_test_%s.log" % datetime.now().strftime("%Y%m%d-%H.%M.%S")

	if not os.path.exists(args.log_dir):
		os.makedirs(args.log_dir)
	
	logfile = os.path.join(args.log_dir, logfile)

	sys.stdout = open(logfile, "w")

	#o2tf.printlog(args, logfile, 0, '')

	#print("global bitmap =", get_global_bitmap_inode(args.device))
	if not check_grpextents(args.device):
		o2tf.printlog("grpextents test failed!", logfile, 0, '')
		exit(1)
	o2tf.printlog("grpextents test succeeded!", logfile, 0, '')
	exit(0)


	

