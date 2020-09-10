#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <linux/input.h>
#include <sys/select.h>
#include "DeviceIo/Rk_key.h"
#include "DeviceIo/RK_log.h"
#include "DeviceIo/RK_timer.h"
#include <sys/prctl.h>

#define TIME_MULTIPLE        (500)

typedef int BOOL;

typedef struct dirents {
	int event;
	struct dirent file;
	struct dirents *next;
} Dirents;

typedef struct RK_input_long_press_key {
	int key_code;
	uint32_t time;
	RK_input_long_press_callback cb;
	RK_input_long_press_hb_callback hb;
	struct RK_input_long_press_key *next;
} RK_input_long_press_key_t;

typedef struct RK_input_long_press {
	int key_code;
	RK_input_long_press_key *event;
	struct RK_input_long_press *next;
} RK_input_long_press_t;

typedef struct RK_input_compose_press {
	char *keys;
	uint32_t time;
	RK_input_compose_press_callback cb;
	struct RK_input_compose_press *next;
} RK_input_compose_press_t;

typedef struct RK_input_compose_long_press {
	RK_input_compose_press_t *event;
	uint64_t time;                       // time of compose press event completed
	struct RK_input_compose_long_press *next;
} RK_input_compose_long_press_t;
static pthread_mutex_t m_mutex_compose_long_press;
static pthread_cond_t m_cond_compose_long_press;
static pthread_t m_tid_compose_long_press;
static int m_complete_compose_long_press = 0;
static int m_compose_long_press_key_up = 0;

typedef struct RK_input_transaction_press {
	char *keys;
	uint32_t time;
	RK_input_transaction_press_callback cb;
	struct RK_input_transaction_press *next;
} RK_input_transaction_press_t;

typedef struct RK_input_transaction_event {
	char *keys;
	char *remain;
	uint32_t time;
	uint64_t last_time;
	RK_input_transaction_press_callback cb;
	struct RK_input_transaction_event *next;
} RK_input_transaction_event_t;

typedef struct RK_input_multiple_press {
	int code;
	int times;
	RK_input_multiple_press_callback cb;
	struct RK_input_multiple_press *next;
} RK_input_multiple_press_t;

typedef struct RK_input_multiple_event {
	RK_input_multiple_press_t *event;
	RK_input_multiple_press_t *max;
	int times;
	int clicked;
	int code;
	uint64_t last_time;
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	pthread_t tid;
	int responded;
} RK_input_multiple_event_t;
static RK_input_multiple_event_t m_input_multiple_event;

typedef struct RK_input_pressed {
	int key_code;
	struct RK_input_pressed *next;
} RK_input_pressed_t;

typedef struct RK_input_timer {
	RK_Timer_t *timer;
	RK_input_long_press_callback cb_long_press;
	RK_input_long_press_callback hb_long_press;
	RK_input_compose_press_callback cb_compose_press;
	int key_code;
	char *keys;
} RK_input_timer_t;

static RK_input_long_press_t *m_long_press_head = NULL;
static RK_input_multiple_press_t *m_multiple_press_head = NULL;
static RK_input_compose_press_t *m_compose_press_head = NULL;
static RK_input_compose_long_press_t *m_compose_long_press_head = NULL;
static RK_input_transaction_press_t *m_transaction_press_head = NULL;
static RK_input_transaction_event_t *m_transaction_event_head = NULL;
static RK_input_pressed_t *m_key_pressed_head = NULL;
static RK_input_compose_long_press_t *m_compose_long_press_earliest = NULL;
static RK_input_compose_press_t *m_compose_press_ready = NULL;

static RK_input_callback m_cb;
static RK_input_press_callback m_input_press_cb;
static RK_input_timer_t m_input_timer;
static pthread_mutex_t m_mutex_input;

static pthread_t m_th;
static int m_maxfd = 0;
static Dirents *dirents = NULL;

static int is_bluealsa_event(char *node)
{
	char sys_path[100] = {0};
	char node_info[100] = {0};
	int fd = 0;

	if (strncmp(node, "event", 5))
		return 0;

	sprintf(sys_path, "sys/class/input/%s/device/name", node);
	fd = open(sys_path, O_RDONLY);
	if (fd < 0)
		return 0;

	if (read(fd, node_info, sizeof(node_info)) < 0) {
		close(fd);
		return 0;
	}

	/* BlueAlsa addr like XX:XX:XX:XX:XX:XX */
	if ((strlen(node_info) == 18) &&
		(node_info[2] == ':') && (node_info[5] == ':') &&
		(node_info[8] == ':') && (node_info[11] == ':') &&
		(node_info[14] == ':')) {
		close(fd);
		return 1;
	}

	close(fd);
	return 0;
}

static Dirents* list_input_events(const char *path)
{
	Dirents* head = NULL;

	DIR *dir;
	struct dirent *ptr;
	char base[1000];

	if ((dir = opendir(path)) == NULL) {
		printf("Open dir \"%s\" error...", path);
		return NULL;
	}

	while ((ptr=readdir(dir)) != NULL) {
		if(strcmp(ptr->d_name, ".") == 0 || strcmp(ptr->d_name, "..") == 0) { ///current dir OR parrent dir
		} else if(ptr->d_type == 8 || ptr->d_type == 2) { ///file
			if (strncmp(ptr->d_name, "event", 5) != 0)
				continue;

			if (head == NULL) {
				head = (Dirents*) calloc(1, sizeof(Dirents));
				memcpy(&head->file, ptr, sizeof(struct dirent));
			} else {
				Dirents *tail = head;
				while (tail->next != NULL) {
					tail = tail->next;
				}

				Dirents *file = (Dirents*) calloc(1, sizeof(Dirents));
				memcpy(&file->file, ptr, sizeof(struct dirent));
				tail->next = file;
			}
		} else if(ptr->d_type == 10) { ///link file
		} else if(ptr->d_type == 4) {    ///dir
		}
	}
	closedir(dir);

	return head;
}

void dirents_free(Dirents* dirents)
{
	Dirents *cur, *prev;
	cur = dirents;

	while (cur) {
		prev = cur;
		cur = cur->next;

		free(prev);
	}
	dirents = NULL;
}

static uint64_t get_timestamp_ms(void)
{
	struct timeval ctime;
	gettimeofday(&ctime, NULL);

	return (1e+6 * (uint64_t)ctime.tv_sec + ctime.tv_usec) / 1000;
}

static BOOL is_compose_press_event_ready(RK_input_compose_press_t *comp)
{
	char *p;
	RK_input_pressed_t *key;
	if (comp == NULL || comp->keys == NULL)
		return 0;

	char str[strlen(comp->keys) + 1];

	memset(str, 0, sizeof(str));
	strcpy(str, comp->keys);
	p = strtok(str, " ");
	while (p) {
		key = m_key_pressed_head;
		while (key) {
			if (key->key_code == atoi(p)) {
				break;
			}
			key = key->next;
		}
		if (key == NULL)
			return 0;

		p = strtok(NULL, " ");
	}
	return 1;
}

static int check_compose_press_event(const int code)
{
	RK_input_compose_press_t *target;
	RK_input_compose_long_press_t *event;
	char str[8];
	int ret;

	ret = 0;
	memset(str, 0, sizeof(str));
	snprintf(str, sizeof(str), "%d ", code);
	target = m_compose_press_head;

	while (target) {
		if (is_compose_press_event_ready(target)) {
			if (ret == 0) ret = 1;
			if (strstr(target->keys, str)) {
				ret = 2;
				pthread_mutex_lock(&m_mutex_compose_long_press);
				event = m_compose_long_press_head;
				while (event) {
					if (strcmp(event->event->keys, target->keys) == 0) {
						break;
					}
					event = event->next;
				}
				if (!event) {
					RK_input_compose_long_press_t *comp = (RK_input_compose_long_press_t*) calloc(sizeof(RK_input_compose_long_press_t), 1);
					comp->event = target;
					comp->time = get_timestamp_ms();

					comp->next = m_compose_long_press_head;
					m_compose_long_press_head = comp;
					pthread_mutex_unlock(&m_mutex_compose_long_press);
				}
			}
		}
		target = target->next;
	}

	return ret;
}

static RK_input_long_press_key_t* get_max_input_long_press_key(const int code)
{
	RK_input_long_press_t *events;
	RK_input_long_press_key_t *event, *target;
	uint32_t max_time = 0;

	events = m_long_press_head;
	target = NULL;

	while (events) {
		if (events->key_code == code) {
			event = events->event;
			while (event) {
				if (max_time < event->time) {
					max_time = event->time;
					target = event;
				}
				event = event->next;
			}
			return target;
		}
		events = events->next;
	}

	return NULL;
}

static RK_input_long_press_key_t* get_input_long_press_key(const int key_code, const uint32_t time)
{
	RK_input_long_press_t *events;
	RK_input_long_press_key_t *event, *ret;

	events = m_long_press_head;
	ret = NULL;
	while (events) {
		if (events->key_code == key_code) {
			event = events->event;
			while (event) {
				if (time >= event->time) {
					ret = event;
				}
				event = event->next;
			}
		}
		events = events->next;
	}

	return ret;
}

static void input_timer_reset(RK_input_timer_t* input_timer)
{
	input_timer->timer = NULL;
	input_timer->key_code = 0;
	if (input_timer->keys) {
		free(input_timer->keys);
		input_timer->keys = NULL;
	}
	input_timer->cb_long_press = NULL;
	input_timer->cb_compose_press = NULL;
}

static void timer_cb(const int end)
{
	if (m_input_timer.cb_long_press) {
		RK_timer_stop(m_input_timer.timer);

		if (m_input_timer.key_code > 0) {
			m_input_timer.cb_long_press(m_input_timer.key_code, m_input_timer.timer->timer_time);
		}
		input_timer_reset(&m_input_timer);
	} else if (m_input_timer.hb_long_press) {
		if (m_input_timer.key_code > 0) {
			m_input_timer.hb_long_press(m_input_timer.key_code, 1);
		}
	}
}

static void check_transaction_event(const int code, const int value)
{
	RK_input_transaction_press_t *transaction_press = m_transaction_press_head;
	char strcode[10];

	memset(strcode, 0, sizeof(strcode));
	snprintf(strcode, sizeof(strcode), " %d ", code);

	if (m_transaction_event_head) {
		if (0 == strncmp(m_transaction_event_head->remain, strcode, strlen(strcode))) {
			if (strlen(m_transaction_event_head->remain) == strlen(strcode)) {
				if (m_transaction_event_head->cb) {
					m_transaction_event_head->cb(m_transaction_event_head->keys, m_transaction_event_head->time);
				}
				free(m_transaction_event_head->keys);
				free(m_transaction_event_head->remain);
				free(m_transaction_event_head);
				m_transaction_event_head = NULL;
			} else {
				if (get_timestamp_ms() - m_transaction_event_head->last_time > m_transaction_event_head->time) {
					printf("transaction invalid. now:%llu, last:%llu, time:%lu\n", get_timestamp_ms(),
						m_transaction_event_head->last_time, m_transaction_event_head->time);
					free(m_transaction_event_head->keys);
					free(m_transaction_event_head->remain);
					free(m_transaction_event_head);
					m_transaction_event_head = NULL;
					goto recheck;
				}

				int len = strlen(m_transaction_event_head->remain) - strlen(strcode) + 1;
				char *remain = (char *)calloc(sizeof(char), len + 1);
				strncpy(remain, m_transaction_event_head->remain + strlen(strcode) - 1, len);
				free(m_transaction_event_head->remain);
				m_transaction_event_head->remain = remain;
				m_transaction_event_head->last_time = get_timestamp_ms();
			}
		} else {
			free(m_transaction_event_head->keys);
			free(m_transaction_event_head->remain);
			free(m_transaction_event_head);
			m_transaction_event_head = NULL;

			goto recheck;
		}
		return;
	}

recheck:
	while (transaction_press) {
		if (0 == strncmp(transaction_press->keys, strcode, strlen(strcode))) {
			break;
		}
		transaction_press = transaction_press->next;
	}
	if (transaction_press) {
		RK_input_transaction_event_t *event = (RK_input_transaction_event_t*) calloc(sizeof(RK_input_transaction_event_t), 1);
		int len = strlen(transaction_press->keys) - strlen(strcode) + 1;
		char *keys = (char*) calloc(sizeof(char), strlen(transaction_press->keys) + 1);
		char *remain = (char*) calloc(sizeof(char), len + 1);

		strncpy(keys, transaction_press->keys, strlen(transaction_press->keys));
		strncpy(remain, transaction_press->keys + strlen(strcode) - 1, len);
		event->keys = keys;
		event->remain = remain;
		event->time = transaction_press->time;
		event->last_time = get_timestamp_ms();
		event->cb = transaction_press->cb;

		m_transaction_event_head = event;
	}
}

static void handle_input_event(const int code, const int value, RK_Timer_t *timer)
{
	static RK_input_pressed_t *event, *prev;
	static RK_input_compose_press_t *comp;
	static RK_input_multiple_press_t *multiple, *max_multiple;
	static RK_input_compose_long_press_t *comp_long, *comp_long_prev;
	static RK_input_long_press_key_t *max_long;
	static int compose_state;
	static int is_multiple;
	static char strcode[8];

	if (value) {
		// insert key into input pressed list
		event = (RK_input_pressed_t*) calloc(sizeof(RK_input_pressed_t), 1);
		event->key_code = code;
		event->next = m_key_pressed_head;
		m_key_pressed_head = event;

		check_transaction_event(code, value);
		compose_state = 0;
		if (compose_state = check_compose_press_event(code)) {
			RK_timer_stop(m_input_timer.timer);
			input_timer_reset(&m_input_timer);
			pthread_mutex_lock(&m_mutex_compose_long_press);
			if (compose_state == 2) {
				m_complete_compose_long_press = 1;
				pthread_cond_signal(&m_cond_compose_long_press);
			}
			pthread_mutex_unlock(&m_mutex_compose_long_press);
			return;
		}

		if (max_long = get_max_input_long_press_key(code)) {
			if (max_long->cb) {
				RK_timer_create(timer, timer_cb, max_long->time, 0);
				m_input_timer.cb_long_press = max_long->cb;
				m_input_timer.hb_long_press = NULL;
			} else if (max_long->hb) {
				RK_timer_create(timer, timer_cb, 0, max_long->time);
				m_input_timer.cb_long_press = NULL;
				m_input_timer.hb_long_press = max_long->hb;
			}
			m_input_timer.timer = timer;
			m_input_timer.key_code = code;
			RK_timer_start(timer);

			// check has multiple key registed
			max_multiple = NULL;
			multiple = m_multiple_press_head;
			while (multiple) {
				if (multiple->code == code) {
					if (max_multiple == NULL || max_multiple->times < multiple->times) {
						max_multiple = multiple;
					}
				}
				multiple = multiple->next;
			}
			if (max_multiple) {
				pthread_mutex_lock(&m_input_multiple_event.mutex);
				m_input_multiple_event.clicked = 1;
				m_input_multiple_event.code = code;
				m_input_multiple_event.max = max_multiple;
				pthread_cond_signal(&m_input_multiple_event.cond);
				pthread_mutex_unlock(&m_input_multiple_event.mutex);
			}

			return;
		}

		// check has multiple key registed
		max_multiple = NULL;
		multiple = m_multiple_press_head;
		while (multiple) {
			if (multiple->code == code) {
				if (max_multiple == NULL || max_multiple->times < multiple->times) {
					max_multiple = multiple;
				}
			}
			multiple = multiple->next;
		}
		if (max_multiple) {
			pthread_mutex_lock(&m_input_multiple_event.mutex);
			m_input_multiple_event.clicked = 1;
			m_input_multiple_event.code = code;
			m_input_multiple_event.max = max_multiple;
			pthread_cond_signal(&m_input_multiple_event.cond);
			pthread_mutex_unlock(&m_input_multiple_event.mutex);
		} else {
			if (m_input_press_cb) {
				m_input_press_cb(code);
			}
		}
	} else {
		// remove key from input pressed list
		event = prev = m_key_pressed_head;
		while (event) {
			if (event->key_code == code) {
				if (event == prev) {// head
					m_key_pressed_head = event->next;
				} else {// not head
					prev->next = event->next;
				}
				free(event);
				break;
			}
			prev = event;
			event = event->next;
		}

		// check key up whether affected compose key list, if so remove from list and notify
		pthread_mutex_lock(&m_mutex_compose_long_press);
		m_compose_long_press_key_up = 0;
		comp_long = comp_long_prev = m_compose_long_press_head;
		memset(strcode, 0, sizeof(strcode));
		snprintf(strcode, sizeof(strcode), "%d ", code);
		while (comp_long) {
			if (strstr(comp_long->event->keys, strcode)) {
				if (comp_long == comp_long_prev) {
					m_compose_long_press_head = comp_long->next;
					free(comp_long);

					comp_long = comp_long_prev = m_compose_long_press_head;
				} else {
					comp_long_prev->next = comp_long->next;
					free(comp_long);

					comp_long = comp_long_prev->next;
				}
				m_compose_long_press_key_up = code;
				continue;
			}
			comp_long_prev = comp_long;
			comp_long = comp_long->next;
		}
		if (m_compose_long_press_key_up > 0) {
			pthread_cond_signal(&m_cond_compose_long_press);
		}
		pthread_mutex_unlock(&m_mutex_compose_long_press);

		// if compose event was ready front, ignore until all key up
		if (compose_state == 2) {
			if (m_key_pressed_head) {
				return;
			} else {
				compose_state = 0;
			}
		}

		// check whether has long key
		if (max_long = get_max_input_long_press_key(code)) {
			if (m_input_timer.timer && m_input_timer.key_code > 0 && !m_input_timer.keys) {
				uint32_t time = get_timestamp_ms() - m_input_timer.timer->timer_start;
				RK_timer_stop(m_input_timer.timer);
				input_timer_reset(&m_input_timer);

				RK_input_long_press_key_t *long_press = get_input_long_press_key(code, time);
				if (long_press) {
					if (long_press->cb)
						long_press->cb(long_press->key_code, long_press->time);
				} else {
					pthread_mutex_lock(&m_input_multiple_event.mutex);
					is_multiple = (m_input_multiple_event.event || m_input_multiple_event.responded) ? 1 : 0;
					pthread_mutex_unlock(&m_input_multiple_event.mutex);
					if (!is_multiple && m_input_press_cb) {
						m_input_press_cb(code);
					}
				}
			}
		}
		m_input_multiple_event.responded = 0;
	}
}

static RK_input_compose_long_press_t *get_earliest_compose_long_event(void)
{
//	uint64_t time;
	RK_input_compose_long_press_t *event, *target;
	event = target = m_compose_long_press_head;

	while (event) {
//		time = get_timestamp_ms();
//		if (event->event->time - (time - event->time) <
//				target->event->time - (time - target->time))
		if (event->event->time + event->time < target->event->time + target->time) {
			target = event;
		}
		event = event->next;
	}

	return target;
}

static int compose_wait_new_event(void)
{
	int ret = 0;
	pthread_mutex_lock(&m_mutex_compose_long_press);

	ret = m_complete_compose_long_press;

	pthread_mutex_unlock(&m_mutex_compose_long_press);
	return ret;
}

static void compose_check_has_event(void);

static void compose_handle_new_event(void)
{
	RK_input_compose_long_press_t *event;

	pthread_mutex_lock(&m_mutex_compose_long_press);
	m_complete_compose_long_press = 0;
	pthread_mutex_unlock(&m_mutex_compose_long_press);

	compose_check_has_event();
}

static void compose_long_press_event_dequeue(RK_input_compose_long_press_t *event)
{
	RK_input_compose_long_press_t *target, *prev;
	target = m_compose_long_press_head;
	prev = NULL;
	while (target) {
		if (strcmp(target->event->keys, event->event->keys) == 0) {
			if (!prev) {
				m_compose_long_press_head = target->next;
			} else {
				prev->next = target->next;
			}
			free(target);
			event = NULL;

			return;
		}

		prev = target;
		target = target->next;
	}
}

static void compose_check_has_event(void)
{
	pthread_mutex_lock(&m_mutex_compose_long_press);
	RK_input_compose_long_press_t *event;

	event = get_earliest_compose_long_event();

	if (event) {
		uint32_t time = get_timestamp_ms() - event->time;
		if (time >= event->event->time) {
			if (event->event->cb) {
				event->event->cb(event->event->keys, event->event->time);
				compose_long_press_event_dequeue(event);
			}
		} else {
			struct timespec tout;
			clock_gettime(CLOCK_REALTIME, &tout);

			time = event->event->time - time;
			tout.tv_sec += time / 1000;
			tout.tv_nsec += 1000000 * (time % 1000);
			while (tout.tv_nsec > 1000000000) {
				tout.tv_sec += 1;
				tout.tv_nsec -= 1000000000;
			}

			pthread_cond_timedwait(&m_cond_compose_long_press, &m_mutex_compose_long_press, &tout);

			if (m_compose_long_press_key_up <= 0 && !m_complete_compose_long_press && event->event->cb) {
				event->event->cb(event->event->keys, event->event->time);
				compose_long_press_event_dequeue(event);
			}
		}
	} else {
		pthread_cond_wait(&m_cond_compose_long_press, &m_mutex_compose_long_press);
	}

	pthread_mutex_unlock(&m_mutex_compose_long_press);
}

static void* thread_compose_long_key(void *arg)
{
	prctl(PR_SET_NAME,"thread_compose_long_key");

	while (1) {
		if (compose_wait_new_event()) {
			compose_handle_new_event();
		} else {
			compose_check_has_event();
		}
	}

	return NULL;
}

static void multiple_press_event_reset(RK_input_multiple_event_t *event)
{
	if (event == NULL) {
		return;
	}

	event->event = NULL;
	event->times = 0;
	event->max = NULL;
	event->last_time = 0;
	event->code = 0;
}

static RK_input_multiple_press_t *get_multiple_press(const int code, const int times)
{
	RK_input_multiple_press_t *event, *target;
	event = m_multiple_press_head;

	target = NULL;
	while (event) {
		if (event->code == code && times >= event->times) {
			if (target == NULL || target->times < event->times) {
				target = event;
			}
		}
		event = event->next;
	}
	return target;
}

static int multiple_wait_new_event(void)
{
	static int ret;
	static RK_input_multiple_press_t *event;
	pthread_mutex_lock(&m_input_multiple_event.mutex);

	if (!m_input_multiple_event.clicked) {
		if (m_input_multiple_event.event) {
			uint32_t time = TIME_MULTIPLE - (get_timestamp_ms() - m_input_multiple_event.last_time);
			if (time <= 0) {
				event = get_multiple_press(m_input_multiple_event.code, m_input_multiple_event.times);
				if (event) {
					if (event->cb) {
						event->cb(event->code, event->times);
					}
					m_input_multiple_event.responded = 1;
				} else {
					if (m_input_press_cb) {
						if (m_input_timer.key_code <= 0) {
							m_input_press_cb(m_input_multiple_event.code);
						}
					}
				}

				multiple_press_event_reset(&m_input_multiple_event);
			} else {
				struct timespec tout;
				clock_gettime(CLOCK_REALTIME, &tout);

				tout.tv_sec += time / 1000;
				tout.tv_nsec += 1000000 * (time % 1000);
				while (tout.tv_nsec > 1000000000) {
					tout.tv_sec += 1;
					tout.tv_nsec -= 1000000000;
				}

				pthread_cond_timedwait(&m_input_multiple_event.cond, &m_input_multiple_event.mutex, &tout);
				if (!m_input_multiple_event.clicked) {
					event = get_multiple_press(m_input_multiple_event.code, m_input_multiple_event.times);
					if (event) {
						if (event->cb) {
							event->cb(event->code, event->times);
						}
						m_input_multiple_event.responded = 1;
					} else {
						if (m_input_press_cb) {
							if (m_input_timer.key_code <= 0) {
								m_input_press_cb(m_input_multiple_event.code);
							}
						}
					}
					multiple_press_event_reset(&m_input_multiple_event);
				}
			}
		} else {
			pthread_cond_wait(&m_input_multiple_event.cond, &m_input_multiple_event.mutex);
		}
	}
	ret = m_input_multiple_event.clicked;

	pthread_mutex_unlock(&m_input_multiple_event.mutex);
	return ret;
}

static int multiple_handle_new_event(void)
{
	static RK_input_multiple_press_t *event;
	pthread_mutex_lock(&m_input_multiple_event.mutex);
	event = NULL;
	m_input_multiple_event.clicked = 0;
	m_input_multiple_event.last_time = get_timestamp_ms();
	if (m_input_multiple_event.event) {
		if (m_input_multiple_event.event->code != m_input_multiple_event.code) {
			event = get_multiple_press(m_input_multiple_event.event->code, m_input_multiple_event.times);
			if (event) {
				if (event->cb) {
					event->cb(event->code, event->times);
				}
				m_input_multiple_event.responded = 1;
			} else {
				if (m_input_press_cb) {
					m_input_press_cb(event->code);
				}
			}
			m_input_multiple_event.times = 1;
			m_input_multiple_event.event = m_input_multiple_event.max;
		} else {
			m_input_multiple_event.times++;
			if (m_input_multiple_event.times >= m_input_multiple_event.max->times) {
				if (m_input_multiple_event.max->cb) {
					m_input_multiple_event.max->cb(m_input_multiple_event.max->code, m_input_multiple_event.max->times);
				}
				multiple_press_event_reset(&m_input_multiple_event);
				m_input_multiple_event.responded = 1;
			}
		}
	} else {
		m_input_multiple_event.times = 1;
		m_input_multiple_event.event = m_input_multiple_event.max;
	}

	pthread_mutex_unlock(&m_input_multiple_event.mutex);

	return 0;
}

static void* thread_key_multiple(void *arg)
{
	prctl(PR_SET_NAME,"thread_key_multiple");

	while (1) {
		if (multiple_wait_new_event()) {
			multiple_handle_new_event();
		}
	}

	return NULL;
}

static void* thread_key_monitor(void *arg)
{
	fd_set rfds;

	prctl(PR_SET_NAME,"thread_key_monitor");

	m_maxfd = m_maxfd + 1;

	RK_timer_init();
	RK_Timer_t timer;

	int ret;
	struct input_event ev_key;
	Dirents *event = NULL;
	while (1) {
		FD_ZERO(&rfds);
		event = dirents;
		while (event) {
			if (event->event > 0) {
				FD_SET(event->event, &rfds);
			}
			event = event->next;
		}
		select(m_maxfd, &rfds, NULL, NULL, NULL);

		event = dirents;
		while (event) {
			if (FD_ISSET(event->event, &rfds)) {
				ret = read(event->event, &ev_key, sizeof(ev_key));
				break;
			}
			event = event->next;
		}

		// ignore if error
		if (!event || ret == -1)
			continue;

		// ignore illegal key code
		if (ev_key.code == 0)
			continue;

		// ignore illegal key value
		if (ev_key.value != 0 && ev_key.value != 1)
			continue;

		if(m_cb != NULL) {
			m_cb(ev_key.code, ev_key.value);
		}

		pthread_mutex_lock(&m_mutex_input);
		handle_input_event(ev_key.code, ev_key.value, &timer);
		pthread_mutex_unlock(&m_mutex_input);
	}


	return NULL;
}

static int input_multiple_init(void)
{
	int ret;

	memset(&m_input_multiple_event, 0, sizeof(RK_input_multiple_event_t));
	ret = pthread_mutex_init(&m_input_multiple_event.mutex, NULL);
	if (ret != 0) {
		RK_LOGE("input_multiple_init pthread_mutex_init m_input_multiple_event.mutex failed... error:%d\n", ret);
		return -1;
	}

	ret = pthread_cond_init(&m_input_multiple_event.cond, NULL);
	if (ret != 0) {
		RK_LOGE("input_multiple_init pthread_cond_init m_input_multiple_event.cond failed... error:%d\n", ret);
		pthread_mutex_destroy(&m_input_multiple_event.mutex);
		return -2;
	}

	ret = pthread_create(&m_input_multiple_event.tid, NULL, thread_key_multiple, NULL);
	if (ret != 0) {
		RK_LOGE("input_multiple_init pthread_create m_input_multiple_event.tid failed... error:%d\n", ret);
		pthread_cond_destroy(&m_input_multiple_event.cond);
		pthread_mutex_destroy(&m_input_multiple_event.mutex);
		return -3;
	}
	pthread_detach(m_input_multiple_event.tid);

	return ret;
}

int RK_input_init(RK_input_callback input_callback_cb)
{
	int ret, ret1;
	char input_event[64];

	m_cb = input_callback_cb;

	dirents = list_input_events("/dev/input");
	if (dirents) {
		Dirents *event = dirents;
		while (event) {
			if (is_bluealsa_event(event->file.d_name)) {
				printf("INFO:%s %s is bluealsa node, pass over!", __func__, event->file.d_name);
				continue;
			}
			memset(input_event, 0, sizeof(input_event));
			snprintf(input_event, sizeof(input_event), "/dev/input/%s", event->file.d_name);
			event->event = open(input_event, O_RDONLY);
			if (event->event <= 0) {
				printf("open %s failed...\n", input_event);
			}
			m_maxfd = (m_maxfd > event->event ? m_maxfd : event->event);
			event = event->next;
		}
	}

	ret = pthread_mutex_init(&m_mutex_input, NULL);
	if (ret != 0) {
		printf("RK_input_init pthread_mutex_init m_mutex_input failed... error:%d\n", ret);
		return -1;
	}

	ret = pthread_mutex_init(&m_mutex_compose_long_press, NULL);
	if (ret != 0) {
		printf("RK_input_init pthread_mutex_init m_mutex_compose_long_press failed...error:%d\n", ret);
		pthread_mutex_destroy(&m_mutex_input);
		return -2;
	}

	ret = pthread_cond_init(&m_cond_compose_long_press, NULL);
	if (ret != 0) {
		printf("RK_input_init pthread_cond_init m_cond_compose_long_press failed...error:%d\n", ret);
		pthread_mutex_destroy(&m_mutex_input);
		pthread_mutex_destroy(&m_mutex_compose_long_press);
		return -3;
	}

	ret = pthread_create(&m_tid_compose_long_press, NULL, thread_compose_long_key, NULL);
	if (ret != 0) {
		printf("RK_input_init pthread_create thread_compose_long_key failed...error:%d\n", ret);
		pthread_mutex_destroy(&m_mutex_input);
		pthread_cond_destroy(&m_cond_compose_long_press);
		pthread_mutex_destroy(&m_mutex_compose_long_press);
		return -4;
	}
	pthread_detach(m_tid_compose_long_press);

	ret = pthread_create(&m_th, NULL, thread_key_monitor, NULL);
	if (ret != 0) {
		printf("RK_input_init pthread_create thread_key_monitor failed... error:%d\n", ret);
		pthread_mutex_destroy(&m_mutex_input);
		pthread_cond_destroy(&m_cond_compose_long_press);
		pthread_mutex_destroy(&m_mutex_compose_long_press);
		return -5;
	}
	pthread_detach(m_th);

	input_multiple_init();

	return ret;
}

int RK_input_register_press_callback(RK_input_press_callback cb)
{
	m_input_press_cb = cb;
	return 0;
}

int RK_input_register_long_press_callback(RK_input_long_press_callback cb, const uint32_t time, const int key_code)
{
	RK_input_long_press_t *events, *events_prev;
	RK_input_long_press_key_t *event, *event_prev;
	event = event_prev = NULL;
	events = m_long_press_head;
	events_prev = NULL;

	while (events) {
		if (events->key_code == key_code) {
			if (events->event->hb) {
				event = events->event;
				event_prev = NULL;
				while (event) {
					event_prev = event;
					event = event->next;

					free(event_prev);
				}
				events->event = NULL;
				if (events_prev == NULL) {
					m_long_press_head = m_long_press_head->next;
				} else {
					events_prev->next = events->next;
				}
				free(events);
				events = NULL;
			}
			break;
		}
		events_prev = events;
		events = events->next;
	}

	if (events) {
		event = events->event;
		while (event) {
			if (event->time == time && event->key_code == key_code) {
				break;
			}
			event = event->next;
		}

		if (event) {// already registered, ignore

		} else {
			event = (RK_input_long_press_key_t*) calloc(sizeof(RK_input_long_press_key_t), 1);
			event->key_code = key_code;
			event->time = time;
			event->cb = cb;
			event->hb = NULL;

			event->next = events->event;
			events->event = event;
		}
	} else {
		events = (RK_input_long_press_t*) calloc (sizeof(RK_input_long_press_t), 1);
		event = (RK_input_long_press_key_t*) calloc(sizeof(RK_input_long_press_key_t), 1);
		event->key_code = key_code;
		event->time = time;
		event->cb = cb;
		event->hb = NULL;

		events->event = event;
		events->key_code = key_code;

		events->next = m_long_press_head;
		m_long_press_head = events;
	}

	return 0;
}

int RK_input_register_long_press_hb_callback(RK_input_long_press_hb_callback hb, const uint32_t time, const int key_code)
{
	RK_input_long_press_t *events, *events_prev;
	RK_input_long_press_key_t *event, *event_prev;
	events = m_long_press_head;
	events_prev = NULL;
	event = event_prev = NULL;

	while (events) {
		if (events->key_code == key_code) {
			if (events->event->cb) {
				event = events->event;
				event_prev = NULL;
				while (event) {
					event_prev = event;
					event = event->next;
					free(event_prev);
				}
				events->event = NULL;
				if (events_prev == NULL) {
					m_long_press_head = m_long_press_head->next;
				} else {
					events_prev->next = events->next;
				}
				free(events);
				events = NULL;
			}
			break;
		}
		events_prev = events;
		events = events->next;
	}

	if (events) {
		event = events->event;
		while (event) {
			if (event->time == time && event->key_code == key_code) {
				break;
			}
			event = event->next;
		}

		if (event) {// already registered, ignore

		} else {
			event = (RK_input_long_press_key_t*) calloc(sizeof(RK_input_long_press_key_t), 1);
			event->key_code = key_code;
			event->time = time;
			event->cb = NULL;
			event->hb = hb;

			event->next = events->event;
			events->event = event;
		}
	} else {
		events = (RK_input_long_press_t*) calloc (sizeof(RK_input_long_press_t), 1);
		event = (RK_input_long_press_key_t*) calloc(sizeof(RK_input_long_press_key_t), 1);
		event->key_code = key_code;
		event->time = time;
		event->cb = NULL;
		event->hb = hb;

		events->event = event;
		events->key_code = key_code;

		events->next = m_long_press_head;
		m_long_press_head = events;
	}

	return 0;
}

int RK_input_register_multiple_press_callback(RK_input_multiple_press_callback cb, const int key_code, const int times)
{
	RK_input_multiple_press_t *event;
	event = m_multiple_press_head;

	while (event) {
		if (event->code == key_code && event->times == times) {
			printf("RK_input_register_multiple_press_callback already exist. code:%d; times:%d\n", key_code, times);
			return -1;
		}
		event = event->next;
	}

	event = (RK_input_multiple_press_t*) calloc(sizeof(RK_input_multiple_press_t), 1);
	event->code = key_code;
	event->times = times;
	event->cb = cb;

	event->next = m_multiple_press_head;
	m_multiple_press_head = event;
	return 0;
}

int RK_input_register_compose_press_callback(RK_input_compose_press_callback cb, const uint32_t time, const int key_code, ...)
{
	char* keys;
	char strKey[64];
	char tmp[8];
	va_list keys_ptr;
	int i, count, key;
	RK_input_compose_press_t *comp;

	memset(strKey, 0, sizeof(strKey));
	count = key_code;
	va_start(keys_ptr, key_code);
	for (i = 0; i < count; i++) {
		key = va_arg(keys_ptr, int);
		memset(tmp, 0, sizeof(tmp));

		snprintf(tmp, sizeof(tmp), "%d ", key);
		strcat(strKey, tmp);
	}
	va_end(keys_ptr);
	keys = (char*) calloc(sizeof(char), (strlen(strKey) + 1));
	strcpy(keys, strKey);

	comp = (RK_input_compose_press_t*) calloc(sizeof(RK_input_compose_press_t), 1);
	comp->cb = cb;
	comp->keys = keys;
	comp->time = time;

	comp->next = m_compose_press_head;
	m_compose_press_head = comp;

	return 0;
}

int RK_input_register_transaction_press_callback(RK_input_transaction_press_callback cb, const uint32_t time, const int key_code, ...)
{
	char *keys;
	char strKey[64];
	char tmp[8];
	va_list keys_ptr;
	int i, count, key;
	RK_input_transaction_press_t *trans;

	memset(strKey, 0, sizeof(strKey));
	count = key_code;
	va_start(keys_ptr, key_code);

	strcat(strKey, " ");
	for (i = 0; i < count; i++) {
		key = va_arg(keys_ptr, int);
		memset(tmp, 0, sizeof(tmp));

		snprintf(tmp, sizeof(tmp), "%d ", key);
		strcat(strKey, tmp);
	}
	va_end(keys_ptr);
	keys = (char*) calloc(sizeof(char), (strlen(strKey) + 1));
	strcpy(keys, strKey);

	trans = calloc(sizeof(RK_input_transaction_press_t), 1);
	trans->cb = cb;
	trans->keys = keys;
	trans->time = time;
	printf("RK_input_register_transaction_press_callback keys:\"%s\"\n", trans->keys);

	trans->next = m_transaction_press_head;
	m_transaction_press_head = trans;

	return 0;
}

int RK_input_events_print(void)
{
	RK_input_long_press_t *events = m_long_press_head;
	RK_input_long_press_key_t *event = NULL;
	RK_input_compose_press_t *comp = m_compose_press_head;
	RK_input_transaction_press_t *trans = m_transaction_press_head;
	char tmp[32];
	char *str;
	int size = 1024;

	str = (char*) calloc(sizeof(char), 1024);
	strcat(str, "long:{");

	while (events) {
		memset(tmp, 0, sizeof(tmp));
		snprintf(tmp, sizeof(tmp), "%d:[", events->key_code);
		strcat(str, tmp);
		event = events->event;
		while (event) {
			memset(tmp, 0, sizeof(tmp));
			snprintf(tmp, sizeof(tmp), "(%d %lu),", event->key_code, event->time);

			if (strlen(str) + strlen(tmp) + 1 + 4 >= size) {
				size += 1024;
				str = (char*) realloc(str, size);
			}
			strcat(str, tmp);

			event = event->next;
		}
		if (str[strlen(str) - 1] == ',') {
			str[strlen(str) - 1] = ']';
		} else {
			strcat(str, "]");
		}
		strcat(str, ",");

		events = events->next;
	}
	if (str[strlen(str) - 1] == ',') {
		str[strlen(str) - 1] = '}';
		strcat(str, "\n");
	} else {
		strcat(str, "}\n");
	}

	if (strlen(str) + 1 + 11 <= size) {
		size += 1024;
		str = (char*) realloc(str, size);
	}

	strcat(str, "compose:{");
	while (comp) {
		memset(tmp, 0, sizeof(tmp));
		snprintf(tmp, sizeof(tmp), "(%lu, %s),", comp->time, comp->keys);
		if (strlen(str) + strlen(tmp) + 1 + 2 <= size) {
			size += 1024;
			str = (char*) realloc(str, size);
		}
		strcat(str, tmp);
		comp = comp->next;
	}
	if (str[strlen(str) - 1] == ',') {
		str[strlen(str) - 1] = '}';
		strcat(str, "\n");
	} else {
		strcat(str, "}\n");
	}

	if (strlen(str) + 1 + 15 <= size) {
		size += 1024;
	}
	strcat(str, "transaction:{");
	while (trans) {
		memset(tmp, 0, sizeof(tmp));
		snprintf(tmp, sizeof(tmp), "(%lu, %s),", trans->time, trans->keys);
		if (strlen(str) + strlen(tmp) + 1 + 2 <= size) {
			size += 1024;
			str = (char*) realloc(str, size);
		}
		strcat(str, tmp);
		trans = trans->next;
	}
	if (str[strlen(str) - 1] == ',') {
		str[strlen(str) - 1] = '}';
		strcat(str, "\n");
	} else {
		strcat(str, "}\n");
	}

	printf("%s\n", str);
	free(str);
	return 0;
}

int RK_input_exit(void)
{
	int ret;

	if (m_th > 0) {
		ret  = pthread_cancel(m_th);
		if (ret == 0) {
			pthread_join(m_th, NULL);
		}
		m_th = -1;
	}

	if (m_tid_compose_long_press > 0) {
		ret = pthread_cancel(m_tid_compose_long_press);
		if (ret == 0) {
			pthread_join(m_tid_compose_long_press, NULL);
		}
		m_tid_compose_long_press = 1;
	}

	Dirents *event = dirents;
	while (event) {
		if (event->event > 0) {
			close(event->event);
		}
		event = event->next;
	}
	dirents_free(dirents);
	dirents = NULL;

	return 0;
}
