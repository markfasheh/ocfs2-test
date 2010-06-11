#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <et/com_err.h>

#include <o2dlm/o2dlm.h>

#define DOMAINNAME	"o2dlm-polltest"
#define LOCKNAME	"/contendme"

static sig_atomic_t sig_exit;
static int bast_error;

static int setup_domain(struct o2dlm_ctxt **dlm)
{
	errcode_t ret;

	ret = o2dlm_initialize("/dlm/", DOMAINNAME, dlm);
	if (ret) {
		com_err(DOMAINNAME, ret, "while starting dlm domain");
		return 1;
	}
	return 0;
}

static errcode_t teardown_domain(struct o2dlm_ctxt *dlm)
{
	errcode_t ret;

	ret = o2dlm_destroy(dlm);
	if (ret) {
		com_err(DOMAINNAME, ret,
			"Trying to tear down domain");
		return 1;
	}

	return 0;
}

static void bast_func(void *arg)
{
	errcode_t ret;
	struct o2dlm_ctxt *dlm = arg;
	fprintf(stdout, "Bast unlock!\n");

	ret = o2dlm_unlock(dlm, LOCKNAME);
	if (ret) {
		com_err(DOMAINNAME, ret, "while unlocking");
		bast_error = 1;
	}
}

static int lock(struct o2dlm_ctxt *dlm, struct pollfd *event)
{
	static int first = 1;
	errcode_t ret;
	enum o2dlm_lock_level level;

	if (first) {
		srand(getpid());
		first = 0;
	}

	level = ((rand() & 0x01) ? O2DLM_LEVEL_EXMODE : O2DLM_LEVEL_PRMODE);
	fprintf(stdout, "Locking %s\n",
		(level == O2DLM_LEVEL_EXMODE) ? "EX" : "PR");
	ret = o2dlm_lock_with_bast(dlm, LOCKNAME, 0, level,
				   bast_func, dlm, &event->fd);
	if (ret) {
		com_err(DOMAINNAME, ret, "while trying to lock");
		return -1;
	}

	return 0;
}

static int unlock(struct o2dlm_ctxt *dlm)
{
	errcode_t ret;

	fprintf(stdout, "Unlocking\n");
	ret = o2dlm_unlock(dlm, LOCKNAME);
	if (ret) {
		com_err(DOMAINNAME, ret, "while unlocking");
		return -1;
	}

	return 0;
}

static int run_test(struct o2dlm_ctxt *dlm)
{
	int rc;
	struct pollfd event = {
		.fd = -1,
	};

	while (1) {
		if (event.fd < 0) {
			rc = lock(dlm, &event);
			if (rc)
				break;
		}

		event.events = POLLIN|POLLHUP;
		event.revents = 0;
		fprintf(stdout, "Waiting\n");
		rc = poll(&event, 1, 5000);
		if (!rc) {
			fprintf(stdout, "Timeout\n");
			rc = unlock(dlm);
			if (rc)
				break;
			event.fd = -1;
			continue;
		}
		if (rc < 0) {
			perror("Error checking for events");
			break;
		}

		if (event.revents & POLLIN) {
			o2dlm_process_bast(dlm, event.fd);
			if (bast_error) {
				rc = -1;
				break;
			}
			event.fd = -1;
		}
		if (event.revents & POLLHUP) {
			fprintf(stdout, "Hangup on lock\n");
			break;
		}
	}

	if (event.fd > -1) {
		if (unlock(dlm))
			rc = -1;
	}

	if (rc > 0)
		rc = 0;

	return rc;
}

static void handler(int signum)
{
	sig_exit = 1;
}

int main(int argc, char *argv[])
{
	int rc = -1;
	struct o2dlm_ctxt *dlm = NULL;

	initialize_o2dl_error_table();

	if (signal(SIGINT, handler) == SIG_ERR) {
		perror("SIGINT");
		goto out;
	}

	if (setup_domain(&dlm))
		goto out;

	rc = run_test(dlm);
	if (teardown_domain(dlm))
		rc = -1;

out:
	return rc;
}

