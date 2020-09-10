#include <ctype.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "DeviceIo/RK_property.h"
#include "DeviceIo/RK_log.h"

#define LEN_MAX_KEY		32+1
#define LEN_MAX_VALUE	128+1

typedef int BOOL;

typedef struct RK_property_map {
	char key[LEN_MAX_KEY];
	char value[LEN_MAX_VALUE];
	struct RK_property_map *next;
} RK_property_map;

static const char* LOCAL_PATH = "/data/local.prop";
static RK_property_map *m_property_map_head;
static pthread_mutex_t m_property_mutex = PTHREAD_MUTEX_INITIALIZER;

static char *ltrim(char *str) {
	if (str == NULL || *str == '\0') {
		return str;
	}

	int len = 0;
	char *p = str;
	while (*p != '\0' && isspace(*p)) {
		++p;
		++len;
	}

	memmove(str, p, strlen(str) - len + 1);

	return str;
}

static char *rtrim(char *str) {
	if (str == NULL || *str == '\0') {
		return str;
	}

	int len = strlen(str);
	char *p = str + len - 1;
	while (p >= str  && isspace(*p)) {
		*p = '\0';
		--p;
	}

	return str;
}

char *RK_property_trim(char *str)
{
	str = rtrim(str);
	str = ltrim(str);

	return str;
}

static BOOL is_property_line(const char *str)
{
	if (str == NULL || *str == '\0')
		return 0;

	char *chr;
	char key[LEN_MAX_KEY];

	chr = strchr(str, '=');

	if (chr) {
		memset(key, 0, sizeof(key));
		strncpy(key, str, chr - str);
		RK_property_trim(key);

		return strlen(key) > 0 ? 1 : 0;
	}

	return 0;
}

static BOOL is_empty_line(const char *str)
{
	if (str == NULL || *str == '\0')
		return 1;

	char tmp[strlen(str) + 1];

	memset(tmp, 0, sizeof(tmp));
	strcpy(tmp, str);

	RK_property_trim(tmp);

	return strlen(tmp) > 0 ? 0 : 1;
}

static BOOL is_comment_line(const char *str)
{
	if (str == NULL)
		return 0;

	return (*str == '#' ? 1 : 0);
}

static RK_property_map* property_parse(const char *str)
{
	RK_property_map* prop;
	char *chr;
	char key[LEN_MAX_KEY];
	char value[LEN_MAX_VALUE];

	if (is_empty_line(str) || is_comment_line(str)) {
		return NULL;
	} else if (is_property_line(str)) {
		prop = (RK_property_map*)malloc(sizeof(RK_property_map));
		chr = strchr(str, '=');
		if (chr) {
			memset(key, 0, sizeof(key));
			memset(prop->key, 0, sizeof(prop->key));
			strncpy(key, str, chr - str);
			RK_property_trim(key);
			strcpy(prop->key, key);

			memset(value, 0, sizeof(value));
			memset(prop->value, 0, sizeof(prop->value));
			strncpy(value, chr + 1, strlen(str) - (chr - str) - 1);
			RK_property_trim(value);
			strcpy(prop->value, value);
		}
	}

	return prop;
}

int RK_property_init(void)
{
	char buff[1024];
	FILE *fp;

	fp = fopen(LOCAL_PATH, "r");
	if (!fp)
		return 0;

	memset(buff, 0, sizeof(buff));
	while (fgets(buff, sizeof(buff) - 1, fp)) {
		RK_property_map* prop = property_parse(buff);
		if (prop) {
			prop->next = m_property_map_head;
			m_property_map_head = prop;
		}
	}
	fclose(fp);
	return 0;
}

int RK_property_get(const char *key, char *value, const char *def)
{
	RK_property_map* prop;
	int len = 0;

	pthread_mutex_lock(&m_property_mutex);
	prop = m_property_map_head;
	while (prop) {
		if (0 == strcmp(prop->key, key)) {
			strncpy(value, prop->value, strlen(prop->value) + 1);
			value[strlen(prop->value)] = '\0';
			pthread_mutex_unlock(&m_property_mutex);
			return strlen(prop->value);
		}
		prop = prop->next;
	}

	if (def) {
		len = strnlen(def, LEN_MAX_VALUE - 1);
		memcpy(value, def, len);
		value[len] = '\0';
	}
	pthread_mutex_unlock(&m_property_mutex);

	return len;
}

int RK_property_set(const char *key, const char *value)
{
	RK_property_map* prop;
	int len;

	pthread_mutex_lock(&m_property_mutex);
	prop = m_property_map_head;
	while (prop) {
		if (0 == strcmp(prop->key, key)) {
			break;
		}
		prop = prop->next;
	}

	if (prop) {
		len = strnlen(value, LEN_MAX_VALUE - 1);
		memcpy(prop->value, value, len);
		prop->value[len] = '\0';

		// update file
		FILE *fp;
		char *str;
		char line[1024];
		char str_prop[LEN_MAX_KEY + LEN_MAX_VALUE + 5];
		const int LEN = 1024;
		int size = LEN;

		str = (char*) calloc(size, sizeof(char));
		fp = fopen(LOCAL_PATH, "r");
		if (fp) {
			while (fgets(line, sizeof(line), fp)) {
				RK_property_trim(line);
				if (strstr(line, prop->key) && 0 == strncmp(line, prop->key, strlen(prop->key))) {
					memset(str_prop, 0, sizeof(str_prop));
					snprintf(str_prop, sizeof(str_prop), "%s = %s\n", prop->key, prop->value);

					if (size <= (strlen(str) + strlen(str_prop) + 1)) {
						size += LEN;
						str = (char*) realloc(str, sizeof(char) * size);
					}
					strncat(str, str_prop, strlen(str_prop));
				} else {
					if (size <= (strlen(str) + strlen(line) + 2)) {
						size += LEN;
						str = (char*) realloc(str, sizeof(char) * size);
					}
					strncat(str, line, strlen(line));
					strncat(str, "\n", 1);
				}
			}
			fclose(fp);

			fp = fopen(LOCAL_PATH, "w");
			if (fp) {
				fputs(str, fp);
				fclose(fp);
			}
		}
		free(str);
	} else {
		prop = (RK_property_map*) malloc(sizeof(RK_property_map));
		memset(prop->key, 0, sizeof(prop->key));
		strcpy(prop->key, key);

		memset(prop->value, 0, sizeof(prop->value));
		strcpy(prop->value, value);

		prop->next = m_property_map_head;
		m_property_map_head = prop;

		// append to file
		FILE * fp;
		char str_prop[LEN_MAX_KEY + LEN_MAX_VALUE + 5];

		memset(str_prop, 0, sizeof(str_prop));
		snprintf(str_prop, sizeof(str_prop), "%s = %s\n", prop->key, prop->value);

		fp = fopen(LOCAL_PATH, "a+");
		if (fp) {
			fputs(str_prop, fp);
			fclose(fp);
		}
	}
	system("sync");
	pthread_mutex_unlock(&m_property_mutex);

	return 0;
}

void RK_property_print(void)
{
	RK_property_map *prop;
	prop = m_property_map_head;

	pthread_mutex_lock(&m_property_mutex);
	while (prop) {
		RK_LOGD("%s = %s\n", prop->key, prop->value);

		prop = prop->next;
	}
	pthread_mutex_unlock(&m_property_mutex);
}
