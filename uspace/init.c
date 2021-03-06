#include <types.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <syscall.h>
#include <errno.h>
#include "matrix/matrix.h"

static void announce();
static void load_devfs();
static void load_procfs();
static void load_modules();
static void make_nodes();
static void set_hostname();
static void get_uid_gid();
static void start_crond();
static void start_unit_test();

int main(int argc, char **argv)
{
	int rc = 0;

	/* Load device file system */
	load_devfs();

	/* Load process file system */
	load_procfs();

	/* Load modules */
	load_modules();

	/* Make nodes */
	make_nodes();

	/* Set the host name */
	set_hostname();
	
	/* Get the UID and GID */
	get_uid_gid();

	/* Print our banner */
	announce();

	/* Start the cron daemon */
	start_crond();
	
	/* Start the unit_test program */
	start_unit_test();

	while (TRUE) {
		;
	}

	return rc;
}

void load_devfs()
{
	int rc = -1;

	rc = mkdir("/dev", S_IRWXU | S_IRWXG | S_IROTH);
	if (rc == -1) {
		printf("init: mkdir(/dev) for device fs failed.\n");
		goto out;
	}

	/* Mount devfs to /dev */
	rc = mount(NULL, "/dev", "devfs", 0, NULL);
	if (rc == -1) {
		printf("init: mount devfs to /dev failed.\n");
		goto out;
	}

	printf("init: mount devfs successfully.\n");

 out:
	return;
}

void load_procfs()
{
	int rc = -1;

	rc = mkdir("/proc", S_IRWXU | S_IRWXG | S_IROTH);
	if (rc == -1) {
		printf("init: mkdir(/proc) for process fs failed.\n");
		goto out;
	}

	/* Mount procfs to /proc */
	rc = mount(NULL, "/proc", "procfs", 0, NULL);
	if (rc == -1) {
		printf("init: mount procfs to /proc failed.\n");
		goto out;
	}

	printf("init: mount procfs successfully.\n");

 out:
	return;
}

void load_modules()
{
	int rc = -1;
	int handle = -1;
	
	/* FixMe: We should open the driver binary file first and then pass
	 * the file handle down to the kernel. As we don't really support
	 * loading driver modules yet so just pass the pseudo handle down.
	 */

	handle = 4;
	rc = create_module(handle);
	if (rc != 0) {
		printf("init: load pci module failed.\n");
	} else {
		printf("init: pci module loaded successfully.\n");
	}

	handle = 5;
	rc = create_module(handle);
	if (rc != 0) {
		printf("init: load keyboard module failed.\n");
	} else {
		printf("init: keyboard module loaded successfully.\n");
	}

	handle = 6;
	rc = create_module(handle);
	if (rc != 0) {
		printf("init: load floppy module failed.\n");
	} else {
		printf("init: floppy module loaded successfully.\n");
	}

	handle = 7;
	rc = create_module(handle);
	if (rc != 0) {
		printf("init: load null module failed.\n");
	} else {
		printf("init: null module loaded successfully.\n");
	}

	handle = 8;
	rc = create_module(handle);
	if (rc != 0) {
		printf("init: load zero module failed.\n");
	} else {
		printf("init: zero module loaded successfully.\n");
	}
}

void make_nodes()
{
	int rc = -1;

	rc = mknod("/dev/test1", 0755, 0);
	if (rc != 0) {
		printf("init: mknod(/dev/test1) failed.\n");
		goto out;
	} else {
		printf("init: /dev/test1 created.\n");
	}

	rc = mknod("/dev/test2", 0755, 0);
	if (rc != 0) {
		printf("init: mknod(/dev/test2) failed.\n");
		goto out;
	} else {
		printf("init: /dev/test2 created.\n");
	}

 out:
	return;
}

void set_hostname()
{
	int rc = -1;
	char buf[256];
	
	memset(buf, 0, 256);
	rc = gethostname(buf, 255);
	if (rc) {
		printf("init: gethostname failed.\n");
	} else {
		printf("init: hostname: %s\n", buf);
	}
}

void get_uid_gid()
{
	uid_t uid;
	gid_t gid;

	uid = getuid();
	gid = getgid();
	
	printf("init: uid(%d), gid(%d)\n", uid, gid);
}

void start_crond()
{
	int rc, status;
	char *crond[] = {
		"/crond",
		"-d",
		NULL
	};

	rc = create_process(crond[0], crond, 0, 16);
	if (rc == -1) {
		printf("create_process(%s) failed, err(%d).\n", crond[0], rc);
		goto out;
	}
	printf("crond started, process id(%d).\n", rc);

	rc = waitpid(rc, &status, 0);
	if (rc == -1) {
		printf("waiting %s failed, err(%d).\n", crond[0], rc);
		goto out;
	}
	printf("crond process terminated.\n");

 out:
	return;
}

void start_unit_test()
{
	int rc;
	char *unit_test[] = {
		"/unit_test",
		"-n",
		"1000",
		NULL
	};

	rc = create_process(unit_test[0], unit_test, 0, 16);
	if (rc == -1) {
		printf("create_process failed, err(%d).\n", rc);
		goto out;
	}
	printf("unit_test started, process id(%d).\n", rc);

 out:
	return;
}

void announce()
{
	/* Display the Matrix startup banner */
	printf("\nMatrix %d.%d "
	       "Copyright(c) 2012, Ted Zhang, Beijing, China.\n"
	       "Build date and time: %s, %s\n\n",
	       MATRIX_VERSION, MATRIX_RELEASE, __TIME__, __DATE__);
}

