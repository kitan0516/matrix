#include <types.h>
#include <stddef.h>
#include <stdlib.h>
#include "matrix/process.h"

void libc_main(struct process_args *args)
{
	int rc = -1;

	rc = main(0, NULL);
	exit(rc);
}
