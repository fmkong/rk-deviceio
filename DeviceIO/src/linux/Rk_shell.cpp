#include <errno.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include "DeviceIo/RK_log.h"
#include "DeviceIo/Rk_shell.h"

static char* exec(const char* cmd)
{
	if (NULL == cmd || 0 == strlen(cmd))
		return NULL;

	FILE *fp = NULL;
	char buf[128];
	char *ret;
	static int SIZE_UNITE = 512;
	size_t size = SIZE_UNITE;

	fp = popen((const char *) cmd, "r");
	if (NULL == fp)
		return NULL;

	memset(buf, 0, sizeof(buf));
	ret = (char*) calloc(sizeof(char), sizeof(char) * size);
	if(ret == NULL) {
		printf("exec, calloc %d failed\n", size);
		return NULL;
	}

	while (NULL != fgets(buf, sizeof(buf)-1, fp)) {
		if (size <= (strlen(ret) + strlen(buf))) {
			size += SIZE_UNITE;
			ret = (char*) realloc(ret, sizeof(char) * size);
			if(ret == NULL) {
				printf("exec, realloc %d failed\n", size);
				return NULL;
			}
		}
		strcat(ret, buf);
	}

	pclose(fp);
	ret = (char*) realloc(ret, sizeof(char) * (strlen(ret) + 1));

	return ret;
}

int RK_shell_exec(const char* cmd, char* result, size_t size)
{
	if (NULL == cmd || 0 == strlen(cmd))
		return -1;

	char *str = exec(cmd);
	int len = size - 1;

	if (NULL == str)
		return -1;

	if (strlen(str) < len) {
		len = strlen(str);
	}

	memset(result, 0, size);
	if (len > 0) {
		strncpy(result, str, len);
	}
	free(str);

	return 0;
}

static int system_fd_closexec(const char* command) {
	int wait_val = 0;
	pid_t pid = -1;

	if (!command)
		return 1;

	if ((pid = vfork()) < 0)
		return -1;

	if (pid == 0) {
		int i = 0;
		int stdin_fd = fileno(stdin);
		int stdout_fd = fileno(stdout);
		int stderr_fd = fileno(stderr);
		long sc_open_max = sysconf(_SC_OPEN_MAX);
		if (sc_open_max < 0) {
			sc_open_max = 20000; /* enough? */
		}
		/* close all descriptors in child sysconf(_SC_OPEN_MAX) */
		for (i = 0; i < sc_open_max; i++) {
			if (i == stdin_fd || i == stdout_fd || i == stderr_fd)
				continue;
			close(i);
		}

		execl(_PATH_BSHELL, "sh", "-c", command, (char*)0);
		_exit(127);
	}

	while (waitpid(pid, &wait_val, 0) < 0) {
		if (errno != EINTR) {
			wait_val = -1;
			break;
		}
	}

	return wait_val;
}

int RK_shell_system(const char *cmd)
{
	pid_t status;

	status = system_fd_closexec(cmd);

	if (-1 == status) {
		return -1;
	} else {
		if (WIFEXITED(status)) {
			if (0 == WEXITSTATUS(status)) {
				return 0;
			} else {
				return -2;
			}
		} else {
			return -3;
		}
	}

	return 0;
}
