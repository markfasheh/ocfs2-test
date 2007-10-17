#!/usr/bin/perl 
#
# exorcist3.pl - A handy tool for testing disk and I/O-subsystem reliability
#
# Original concept by Michael Glad <glad@daimi.aau.dk>
# Version 2.0 by Martin Kasper Petersen <mkp@SunSITE.auc.dk>
# Version 2.1 by Marcos Matsunaga <marcos.matsunaga@oracle.com>
#
# Usage: exorcist3.pl <processes> <dirs> <files> <dirpref> <filepref> <filename>
#
# Example: exorcist3.pl 16 32 64 node1_dir node1_file /boot/vmlinux.gz
#
# Creates 32 dirs with 64 copies of /boot/vmlinux.gz in each and forks 
# 16 processes to kill/recreate random files. The files and directories are
# prefixed with the <filepref> and <dirpref> to allow cluster execution.
#

sub holy_water { 
  srand($$); 
  while(1) { 
    $victimdir = int(rand($dirs)); 
    $victimfile = int(rand($files));
    $file=sprintf("EXORCIST/%s_dir-%02d/%s_file-%02d", $dirpref, $victimdir, $filepref, $victimfile); 
    print "Priest \#$$: Exorcising victim: $file\n";
    system("cp $file /dev/null"); 
    system("rm -f $file"); 
    system("cp $name $file"); 
  } 
}; 

$procs=$ARGV[0]; $dirs=$ARGV[1]; $files=$ARGV[2]; $dirpref=$ARGV[3]; $filepref=$ARGV[4]; $name=$ARGV[5];

mkdir("EXORCIST", 0755);

for ($i=0 ; $i < $dirs ; $i++) {
  mkdir(sprintf("EXORCIST/%s_dir-%02d", $dirpref, $i), 0755);
  
  for ($j=0 ; $j < $files ; $j++) {
    $file=sprintf("EXORCIST/%s_dir-%02d/%s_file-%02d", $dirpref, $i, $filepref, $j);
    print "High priest preparing: $file\n";
    system("cp $name $file");
  };
};

print "High priest resigning. Let the exorcism begin...\n"; 

for ($k=0; $k < $procs ; $k++) { 
  $pid = fork; 
  if($pid == 0) { 
    &holy_water(); 
    exit(0); 
  } 
} 

sleep(10); 
print "Exorcist3.pl closing.\n"

# EOF
