#include <sys/ioctl.h>
#include <inttypes.h>
#include <linux/types.h>

/*
 * Space reservation / allocation / free ioctls and argument structure
 * are designed to be compatible with XFS.
 */
struct ocfs2_space_resv {
	int16_t		l_type;
	int16_t		l_whence;
	int64_t		l_start;
	int64_t		l_len;		/* len == 0 means until end of file */
	int32_t		l_sysid;
	uint32_t		l_pid;
	int32_t		l_pad[4];	/* reserve area			    */
};

#define OCFS2_IOC_ALLOCSP		_IOW ('X', 10, struct ocfs2_space_resv)
#define OCFS2_IOC_FREESP		_IOW ('X', 11, struct ocfs2_space_resv)
#define OCFS2_IOC_RESVSP		_IOW ('X', 40, struct ocfs2_space_resv)
#define OCFS2_IOC_UNRESVSP	_IOW ('X', 41, struct ocfs2_space_resv)
#define OCFS2_IOC_ALLOCSP64	_IOW ('X', 36, struct ocfs2_space_resv)
#define OCFS2_IOC_FREESP64	_IOW ('X', 37, struct ocfs2_space_resv)
#define OCFS2_IOC_RESVSP64	_IOW ('X', 42, struct ocfs2_space_resv)
#define OCFS2_IOC_UNRESVSP64	_IOW ('X', 43, struct ocfs2_space_resv)
