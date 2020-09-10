#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "DeviceIo/Rk_system.h"


static int exec(const char *cmd, char *buf, const size_t size)
{
	FILE *stream = NULL;
	char tmp[1024];

	if ((stream = popen(cmd,"r")) == NULL) {
		return -1;
	}

	if (buf == NULL) {
		pclose(stream);
		return -2;
	}

	buf[0] = '\0';
	while (fgets(tmp, sizeof(tmp) -1, stream)) {
		if (strlen(buf) + strlen(tmp) >= size) {
			pclose(stream);
			return -3;
		}
		strcat(buf, tmp);
	}
	pclose(stream);

	return 0;
}

int RK_read_chip_id(char *buffer, const int size)
{
	int ret;

	ret = exec("cat /proc/cpuinfo | grep Serial | awk -F ': ' '{printf $2}'", buffer, size);

	return ret;
}

int RK_read_version(char *buffer, const int size)
{
	if (!buffer)
		return -1;

	memset(buffer, 0, size);
	strncpy(buffer, DEVICEIO_VERSION, size);
	return 0;
}

int RK_system_factory_reset(const int reboot = 1)
{
	if (reboot) {
		system("rkboot_control wipe_userdata reboot");
	} else {
		system("rkboot_control wipe_userdata");
	}
	return 0;
}
