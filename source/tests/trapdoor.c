/* test for a trapdoor uid system */

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>

main()
{
	if (getuid() != 0) {
		fprintf(stderr,"ERROR: This test must be run as root - assuming non-trapdoor system\n");
		exit(0);
	}

	if (seteuid(1) != 0) exit(1);
	if (geteuid() != 1) exit(1);
	if (seteuid(0) != 0) exit(1);
	if (geteuid() != 0) exit(1);

	if (setegid(1) != 0) exit(1);
	if (getegid() != 1) exit(1);
	if (setegid(0) != 0) exit(1);
	if (getegid() != 0) exit(1);

	exit(0);
}
