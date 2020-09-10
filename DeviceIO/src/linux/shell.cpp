#include "shell.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <paths.h>
#include <sys/wait.h>

#include "Logger.h"

static char *spec_char_convers(const char *buf, char *dst)
{
	char buf_temp[strlen(buf) + 1];
	int i = 0;
	unsigned long con;

	memset(buf_temp, 0, sizeof(buf_temp));
	while(*buf != '\0') {
		if(*buf == '\\' && *(buf + 1) == 'x') {
			strcpy(buf_temp, buf);
			*buf_temp = '0';
			*(buf_temp + 4) = '\0';
			con = strtoul(buf_temp, NULL, 16);
			if (con == 0)
				con += 32;
			dst[i] = con;
			buf += 3;
		} else {
			dst[i] = *buf;
		}
		i++;
		buf++;
	}
	dst[i] = '\0';
	return dst;
}

static char conver[8 * 1024];
bool Shell::scan(const char *cmdline, char *recv_buff) {
	printf("scan: %s\n",cmdline);

	FILE *stream = NULL;
	char buff[1024];

	memset(recv_buff, 0, sizeof(recv_buff));
	memset(conver, 0, sizeof(conver));

	if((stream = popen(cmdline,"r"))!=NULL){
		while(fgets(buff,1024,stream)){
			strcat(conver, buff);
		}
		pclose(stream);
	}

	printf("exec conver[%d]: %s\n", strlen(conver), conver);
	spec_char_convers(conver, recv_buff);
	printf("exec recv_buff[%d]: %s\n", strlen(recv_buff), recv_buff);
	return true;
}

bool Shell::exec(const char *cmdline, char *recv_buff, int len) {
	printf("[SHELL] exec: %s\n", cmdline);

	FILE *stream = NULL;
	char *tmp_buff = recv_buff;

	memset(recv_buff, 0, len);

	if ((stream = popen(cmdline, "r")) != NULL) {
		while (fgets(tmp_buff, len, stream)) {
			//printf("tmp_buf[%d]: %s\n", strlen(tmp_buff), tmp_buff);
			tmp_buff += strlen(tmp_buff);
			len -= strlen(tmp_buff);
			if (len <= 1)
				break;
		}
		pclose(stream);
		printf("%s\n", recv_buff);
	} else {
		printf("%s failed\n", cmdline);
	}

	return true;
}

int system_fd_closexec(const char* command) {
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
            APP_ERROR("Warning, sc_open_max is unlimited!\n");
            sc_open_max = 20000; /* enough? */
        }
        /* close all descriptors in child sysconf(_SC_OPEN_MAX) */
        for (; i < sc_open_max; i++) {
            if (i == stdin_fd || i == stdout_fd || i == stderr_fd)
                continue;
            close(i);
        }

        execl(_PATH_BSHELL, "sh", "-c", command, (char*)0);
        APP_DEBUG("%s, %d\n", __func__, __LINE__);
        _exit(127);
    }

    while (waitpid(pid, &wait_val, 0) < 0) {
        APP_INFO("%s, errno: %d. This is fine.\n", __func__, errno);
        if (errno != EINTR) {
            wait_val = -1;
            break;
        }
    }

    return wait_val;
}

bool Shell::system(const char *cmd) {
    pid_t status = 0;
    bool ret_value = false;

    APP_INFO("System [%s]", cmd);

    status = system_fd_closexec(cmd);

    if (-1 == status) {
        APP_ERROR("system failed!");
    } else {
        if (WIFEXITED(status)) {
            if (0 == WEXITSTATUS(status)) {
                ret_value = true;
            } else {
                APP_ERROR("System shell script failed:[%d].", WEXITSTATUS(status));
            }
        } else {
            APP_INFO("System status = [%d]", WEXITSTATUS(status));
        }
    }

    return ret_value;

}

int Shell::pidof(const char *Name) {
    int len;
    char name[32] = {0};
    len = strlen(Name);
    strncpy(name,Name,len);
    name[31] ='\0';
    char cmdresult[256] = {0};
    char cmd[64] = {0};
    FILE *pFile = NULL;
    int  pid = 0;

    sprintf(cmd, "pidof %s", name);
    pFile = popen(cmd, "r");
    if (pFile != NULL)  {
        while (fgets(cmdresult, sizeof(cmdresult), pFile)) {
            pid = atoi(cmdresult);
            break;
        }
        pclose(pFile);
    }
    return pid;
}
