TOPDIR = ..

include $(TOPDIR)/Preamble.make


SUBDIRS =			\
	aio-stress		\
	aio_direct		\
	atime_quantum		\
	backup_super		\
	basic-dual-data		\
	buildkernel		\
	check_file_size_limits	\
	create_and_open		\
	create_files		\
	create_racer		\
	cross_delete		\
	db_resize		\
	dirop_fileop_racer	\
	dlmstress1		\
	exorcist		\
	extend_and_write	\
	extend_files		\
	extendo			\
	fill_verify_holes	\
	flock_tests		\
	fsck-tests		\
	forkwriter		\
	fsx			\
	inline-data		\
	kernel_build_parallel_find	\
	lock_grab		\
	logwriter		\
	lvb_torture		\
	mkfs-tests		\
	mmap_test		\
	mmap_truncate		\
	multi_node_mmap		\
	open_delete		\
	parallel_timing		\
	python_common		\
	quick_appending_writes	\
	recovery_load		\
	resize_test		\
	reserve_space		\
	sendfiletest		\
	splice			\
	truncate		\
	ttest			\
	tunefs-test		\
	untar_files		\
	write_append_truncate	\
	write_torture		\
	xattr_tests		\
	acl_tests		\
	quota_tests		\
	inode_alloc_perf_tests

ifdef OCFS2_TEST_DX_DIRS
	SUBDIRS += dx_dirs_tests
endif

ifdef OCFS2_TEST_ENABLE_THIRDPARTY
SUBDIRS += bonnie dbench iozone postmark
endif

include $(TOPDIR)/Postamble.make
