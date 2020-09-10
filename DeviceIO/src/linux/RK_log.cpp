#include <dirent.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include "DeviceIo/RK_log.h"
#include "slog.h"

#define MAX_BUFFER    (2048)

static pthread_mutex_t m_mutex = PTHREAD_MUTEX_INITIALIZER;
static FILE *m_fp = NULL;
static int m_logType = RK_LOG_TYPE_CONSOLE;
static char m_saveLevel = ' ';
static char m_saveDir[128] = "/tmp";
static char m_saveFile[128] = "/tmp/logcat.0001";
static int m_saveFileSize = 1024 * 1024 * 2;
static int m_saveFileNum = 20;

static void mkdirs(char *muldir)
{
	int i, len;
	char str[512];

	strncpy(str, muldir, 512);
	len = strlen(str);

	for (i=0; i<len; i++) {
		if (str[i] == '/') {
			str[i] = '\0';
			if (access(str, 0) != 0) {
				mkdir(str, 0777);
			}
			str[i] = '/';
		}
	}

	if (len > 0 && access(str,0) != 0) {
		mkdir(str, 0777);
	}
}

static char *get_time_info(void)
{
	static char str[20];
	char ftime[16];
	struct timespec tout;
	struct tm* ltime;

	memset(str, 0, sizeof(str));
	memset(ftime, 0, sizeof(ftime));
	clock_gettime(CLOCK_REALTIME, &tout);
	ltime = localtime(&tout.tv_sec);

	strftime(ftime, sizeof(ftime), "%m-%d %H:%M:%S", ltime);
	snprintf(str, sizeof(str), "%s.%03ld", ftime, tout.tv_nsec / 1000000);

	return str;
}

static char *get_thread_info(void)
{
	static char str[20];
	char pid[7], tid[7];

	memset(str, 0, sizeof(str));
	memset(pid, 0, sizeof(pid));
	memset(tid, 0, sizeof(tid));

	snprintf(pid, sizeof(pid), "%6d", getpid());
	snprintf(tid, sizeof(tid), "%6d", (pid_t)syscall(__NR_gettid));
	snprintf(str, sizeof(str), "%s%s", pid, tid);

	return str;
}

static char *log_prefix(const char level)
{
	static char str[40];
	char *time_info, *thread_info;

	memset(str, 0, sizeof(str));
	time_info = get_time_info();
	thread_info = get_thread_info();

	snprintf(str, sizeof(str), "%s%s %c ", time_info, thread_info, level);

	return str;
}

static int get_file_size(const char* file)
{
	struct stat statbuf;
	stat(file, &statbuf);

	return statbuf.st_size;
}

static int get_log_file_num(const char *dir)
{
	char maxFile[128];
	struct dirent* ent = NULL;
	DIR *pDir;

	pDir = opendir(dir);
	memset(maxFile, 0, sizeof(maxFile));

	while (NULL != (ent=readdir(pDir))) {
		if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
			continue;

		if (ent->d_type == 8) {
			if (strncmp(ent->d_name, "logcat.", 7) == 0) {
				if (maxFile == NULL || strcmp(maxFile, ent->d_name) < 0) {
					strcpy(maxFile, ent->d_name);
				}
			}
		}
	}
	closedir(pDir);

	if (strlen(maxFile) == 0)
		return 0;

	return atoi(strchr(maxFile, '.') + 1);
}

static int log_save(const char* buf)
{
	if (strlen(m_saveDir) == 0 || strlen(m_saveFile) == 0)
		return -1;

	if (get_file_size(m_saveFile) >= m_saveFileSize) { // The log file is full.
		char oldFile[128], newFile[128], mv[256];
		int i, n;
		if (m_fp) {
			fclose(m_fp);
			m_fp = NULL;
		}

		if (strlen(m_saveDir) > 0) {
			n = get_log_file_num(m_saveDir);
			if (n >= m_saveFileNum) {
				memset(oldFile, 0, sizeof(oldFile));
				memset(mv, 0, sizeof(mv));

				snprintf(oldFile, sizeof(oldFile), "%s/logcat.%.4d", m_saveDir, m_saveFileNum);
				snprintf(mv, sizeof(mv), "rm -rf %s", oldFile);
				system(mv);

				n = m_saveFileNum - 1;
			}

			if (n > 0) {
				for (i = n; i > 0; i--) {
					memset(oldFile, 0, sizeof(oldFile));
					memset(newFile, 0, sizeof(newFile));
					memset(mv, 0, sizeof(mv));

					snprintf(oldFile, sizeof(oldFile), "%s/logcat.%.4d", m_saveDir, i);
					snprintf(newFile, sizeof(newFile), "%s/logcat.%.4d", m_saveDir, i + 1);
					snprintf(mv, sizeof(mv), "mv %s %s", oldFile, newFile);
					system(mv);
				}
			}
		}
	}

	if (!m_fp) {
		m_fp = fopen(m_saveFile, "a+");
		if (!m_fp) {
			RK_LOGE("log_save open \"%s\" fail...\n", m_saveFile);
			return -1;
		}
	}
	fputs(buf, m_fp);
	fflush(m_fp);

	return 0;
}

static int log_level_convert(const char level)
{
	int nLev = 0;
	switch (level) {
	case 'V':
		nLev = 0;
		break;
	case 'D':
		nLev = 1;
		break;
	case 'I':
		nLev = 2;
		break;
	case 'W':
		nLev = 3;
		break;
	case 'E':
		nLev = 4;
		break;
	default:
		break;
	}

	return nLev;
}

static int log_need_save(const char level)
{
	if (m_saveLevel == ' ')
		return 0;

	if (log_level_convert(m_saveLevel) > log_level_convert(level))
		return 0;

	return 1;
}

static int RK_LOG(const char level, const char *format, va_list arg)
{
	static char buffer[MAX_BUFFER + 1];
	int done, len_prefix, len_buffer;
	char *prefix;

	prefix = log_prefix(level);
	len_prefix = strlen(prefix);

	strncpy(buffer, prefix, sizeof(buffer));
	done = vsnprintf(buffer + len_prefix, sizeof(buffer) - len_prefix, format, arg);
	len_buffer = strlen(buffer);
	if (buffer[len_buffer - 1] != '\n') {
		if (len_buffer == sizeof(buffer) - 1) {
			buffer[len_buffer - 1] = '\n';
		} else {
			buffer[len_buffer] = '\n';
		}
	}
	if (m_logType & RK_LOG_TYPE_CONSOLE) {
		pr_info("%s", buffer);
	}

	if (m_logType & RK_LOG_TYPE_FILE) {
		if (log_need_save(level)) {
			log_save(buffer);
		}
	}
	return done;
}

int RK_LOG_set_type(const int type)
{
	m_logType = type;
	return 0;
}

int RK_LOG_set_save_parameter(const char saveLevel, const char *dir, const int fileSize, const int fileNu)
{
	m_saveLevel = saveLevel;
	memset(m_saveDir, 0, sizeof(m_saveDir));
	memset(m_saveFile, 0, sizeof(m_saveFile));
	strncpy(m_saveDir, dir, strlen(dir));
	strcat(m_saveFile, m_saveDir);
	strcat(m_saveFile, "/logcat.0001");
	m_saveFileSize = fileSize;
	m_saveFileNum = fileNu;
	mkdirs(m_saveDir);
	return 0;
}

int RK_LOGV(const char *format, ...)
{
	va_list arg;
	int done;
	pthread_mutex_lock(&m_mutex);
	va_start (arg, format);
	done = RK_LOG('V', format, arg);
	va_end (arg);
	pthread_mutex_unlock(&m_mutex);
	return done;
}

int RK_LOGD(const char *format, ...)
{
	va_list arg;
	int done;
	pthread_mutex_lock(&m_mutex);
	va_start (arg, format);
	done = RK_LOG('D', format, arg);
	va_end (arg);
	pthread_mutex_unlock(&m_mutex);
	return done;
}

int RK_LOGI(const char *format, ...)
{
	va_list arg;
	int done;
	pthread_mutex_lock(&m_mutex);
	va_start (arg, format);
	done = RK_LOG('I', format, arg);
	va_end (arg);
	pthread_mutex_unlock(&m_mutex);
	return done;
}

int RK_LOGE(const char *format, ...)
{
	va_list arg;
	int done;
	pthread_mutex_lock(&m_mutex);
	va_start (arg, format);
	done = RK_LOG('E', format, arg);
	va_end (arg);
	pthread_mutex_unlock(&m_mutex);
	return done;
}
