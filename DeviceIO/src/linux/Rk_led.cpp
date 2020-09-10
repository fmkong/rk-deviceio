#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/prctl.h>
#include "DeviceIo/Rk_led.h"
#include "DeviceIo/RK_log.h"


#define LED_PWD_R			"/sys/devices/platform/pwmleds/leds/PWM-R/brightness"
#define LED_PWD_G			"/sys/devices/platform/pwmleds/leds/PWM-G/brightness"
#define LED_PWD_B			"/sys/devices/platform/pwmleds/leds/PWM-B/brightness"

#define TIMER_PERIOD (20)

typedef struct RK_LedFd {
	int fd_r;
	int fd_g;
	int fd_b;
	int status;  // 0 is not open, 1 is inited, -1 is init failed
} RK_LedFd_t;

typedef struct RK_Led_Effect_ins {
	RK_Led_Effect_t *effect;    // Parameters seted througth interface
	int time;                   // time has been display
	int colors;                 // current colors led displayed
	int count;                  // time of this effect displayed
	struct RK_Led_Effect_ins *next;
} RK_Led_Effect_ins_t;

typedef struct RK_Led_Manager {
	RK_Led_Effect_ins_t *temp;
	RK_Led_Effect_ins_t *stable;
	RK_Led_Effect_ins_t *realtime;
	int new_command_reached;     // -1 wait come; 0 stable layer; 1 realtime layer; 2 temp layer
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	pthread_t tid;
} RK_Led_Manager_t;

static RK_Led_Manager_t m_led_manager;
static RK_LedFd_t m_LedFd = {0,0,0,0};

// 呼吸灯效
static void led_effect_breath(RK_Led_Effect_ins_t *effect) {

	int timer_period_count = effect->effect->period / TIMER_PERIOD;
	int now_count = effect->count % timer_period_count;
	int half_timer_period_count = timer_period_count / 2;

	// 更新count，避免累加越界
	effect->count = now_count;
	if (now_count > half_timer_period_count) {
		now_count = timer_period_count - now_count;
	}

	int color = ((((effect->effect->colors & 0xFF0000) >> 16) * now_count / half_timer_period_count) << 16)
				| ((((effect->effect->colors & 0xFF00) >> 8) * now_count / half_timer_period_count) << 8)
				| ((effect->effect->colors & 0xFF) * now_count / half_timer_period_count);

	effect->colors = color;
}

// 闪烁灯效
static void led_effect_blink(RK_Led_Effect_ins_t *effect)
{
	if (effect->colors == effect->effect->colors) {
		effect->colors = effect->effect->colors_blink;
	} else {
		effect->colors = effect->effect->colors;
	}
	// 没有用到该参数，置为0，避免累加越界
	effect->count = 0;
}

static int led_write(const int color)
{
	int r, g, b;
	static int color_last = -1;

	if (color == color_last) {
		//RK_LOGD("ignored because The color %d the same as last", color);
		return 0;
	}

	color_last = color;
	r = (color >> 16) & 0xFF;
	g = (color >> 8) & 0xFF;
	b = color & 0xFF;

	return RK_set_all_led_status(r, g, b);
}

static int need_wait_forever(void)
{
	if (m_led_manager.temp != NULL && m_led_manager.temp->effect->period > 0) {
		return 0;
	}

	if (m_led_manager.realtime != NULL && m_led_manager.realtime->effect->period > 0) {
		return 0;
	}

	if (m_led_manager.stable != NULL) {
		RK_Led_Effect_ins_t *stable = m_led_manager.stable;
		while (stable) {
			if (stable->effect->period > 0) {
				return 0;
			}
			stable = stable->next;
		}
	}

	return 1;
}

static int led_wait_new_command(void)
{
	int ret = 0;
	pthread_mutex_lock(&m_led_manager.mutex);

	if (m_led_manager.new_command_reached < 0) {
		if (need_wait_forever()) { // wait forever
			pthread_cond_wait(&m_led_manager.cond, &m_led_manager.mutex);
		} else {
			struct timespec tout;
			RK_Led_Effect_ins *effect = NULL;
			clock_gettime(CLOCK_REALTIME, &tout);
			if (m_led_manager.temp) {
				effect = m_led_manager.temp;
			} else if (m_led_manager.realtime) {
				effect = m_led_manager.realtime;
			} else if (m_led_manager.stable) {
				effect = m_led_manager.stable;
			}

			if (effect) {
				if (effect->effect->type == Led_Effect_type_NONE) {
					if (effect->effect->timeout <= 0) {
						pthread_cond_wait(&m_led_manager.cond, &m_led_manager.mutex);
					} else {
						tout.tv_nsec += 1000000 * effect->effect->period;
						while (tout.tv_nsec > 1000000000) {
							tout.tv_sec += 1;
							tout.tv_nsec -= 1000000000;
						}
						pthread_cond_timedwait(&m_led_manager.cond, &m_led_manager.mutex, &tout);
					}
				} else {
					if (effect->effect->type == Led_Effect_type_BREATH) {
						tout.tv_nsec += 1000000 * TIMER_PERIOD;
					} else if (effect->effect->type == Led_Effect_type_BLINK) {
						tout.tv_nsec += 1000000 * effect->effect->period;
					}

					while (tout.tv_nsec > 1000000000) {
						tout.tv_sec += 1;
						tout.tv_nsec -= 1000000000;
					}
					pthread_cond_timedwait(&m_led_manager.cond, &m_led_manager.mutex, &tout);
				}
			} else {
				pthread_cond_wait(&m_led_manager.cond, &m_led_manager.mutex);
			}
		}
	}

	if (m_led_manager.new_command_reached >= 0) {
		ret = 1;
	}

	pthread_mutex_unlock(&m_led_manager.mutex);
	return ret;
}

static int led_handle_timeout(RK_Led_Effect_ins_t *effect)
{
	if (effect->effect->timeout > 0) {
		if (effect->time >= effect->effect->timeout) {
			return 1;
		}
	}

	return 0;
}

static void led_effect_task(void)
{
	pthread_mutex_lock(&m_led_manager.mutex);

	if (m_led_manager.temp) {
		if (led_handle_timeout(m_led_manager.temp)) {
			free(m_led_manager.temp->effect);
			free(m_led_manager.temp);
			m_led_manager.temp = NULL;
		} else {
			if (m_led_manager.temp->effect->type == Led_Effect_type_BLINK) {
				led_effect_blink(m_led_manager.temp);
				m_led_manager.temp->time += m_led_manager.temp->effect->period;
				m_led_manager.temp->count++;
			} else if (m_led_manager.temp->effect->type == Led_Effect_type_BREATH) {
				led_effect_breath(m_led_manager.temp);
				m_led_manager.temp->time += TIMER_PERIOD;
				m_led_manager.temp->count++;
			} else {
				m_led_manager.temp->time += m_led_manager.temp->effect->period;
			}

			led_write(m_led_manager.temp->colors);
			pthread_mutex_unlock(&m_led_manager.mutex);
			return;
		}
	}

	if (m_led_manager.realtime) {
		if (led_handle_timeout(m_led_manager.realtime)) {
			printf("led_effect_task layer of realtime timeout!\n");
			free(m_led_manager.realtime->effect);
			free(m_led_manager.realtime);
			m_led_manager.realtime = NULL;
		} else {
			if (m_led_manager.realtime->effect->type == Led_Effect_type_BLINK) {
				led_effect_blink(m_led_manager.realtime);
				m_led_manager.realtime->time += m_led_manager.realtime->effect->period;
				m_led_manager.realtime->count++;
			} else if (m_led_manager.realtime->effect->type == Led_Effect_type_BREATH) {
				led_effect_breath(m_led_manager.realtime);
				m_led_manager.realtime->time += TIMER_PERIOD;
				m_led_manager.realtime->count++;
			} else {
				m_led_manager.realtime->time += m_led_manager.realtime->effect->period;
			}

			led_write(m_led_manager.realtime->colors);
			pthread_mutex_unlock(&m_led_manager.mutex);
			return;
		}
	}

	if (m_led_manager.stable) {
		if (led_handle_timeout(m_led_manager.stable)) {
			printf("led_effect_task layer of stable timeout!\n");
			RK_Led_Effect_ins_t *stable = m_led_manager.stable;
			m_led_manager.stable = m_led_manager.stable->next;
			free(stable->effect);
			free(stable);

			if (!m_led_manager.stable) {
				RK_set_all_led_off();
				pthread_mutex_unlock(&m_led_manager.mutex);
				return;
			}
		}

		if (m_led_manager.stable->effect->type == Led_Effect_type_BLINK) {
			led_effect_blink(m_led_manager.stable);
			m_led_manager.stable->time += m_led_manager.stable->effect->period;
			m_led_manager.stable->count++;
		} else if (m_led_manager.stable->effect->type == Led_Effect_type_BREATH) {
			led_effect_breath(m_led_manager.stable);
			m_led_manager.stable->time += TIMER_PERIOD;
			m_led_manager.stable->count++;
		} else {
			m_led_manager.stable->time += m_led_manager.stable->effect->period;
		}

		led_write(m_led_manager.stable->colors);
	} else {
		RK_set_all_led_off();
	}

	pthread_mutex_unlock(&m_led_manager.mutex);
}

static void led_handle_new_command(void)
{
	pthread_mutex_lock(&m_led_manager.mutex);
	if (m_led_manager.new_command_reached >= 0) {
	// No need to free TEMP or REALTIME while new commond comming
#if 0
		if (m_led_manager.new_command_reached == 0) {
			if (m_led_manager.temp) {
				free(m_led_manager.temp->effect);
				free(m_led_manager.temp);
				m_led_manager.temp = NULL;
			}

			if (m_led_manager.realtime) {
				free(m_led_manager.realtime->effect);
				free(m_led_manager.realtime);
				m_led_manager.realtime = NULL;
			}
		} else if (m_led_manager.new_command_reached == 1) {
			if (m_led_manager.temp) {
				free(m_led_manager.temp->effect);
				free(m_led_manager.temp);
				m_led_manager.temp = NULL;
			}
		}
#endif
		m_led_manager.new_command_reached = -1;
	}
	pthread_mutex_unlock(&m_led_manager.mutex);

	led_effect_task();
}

static void *led_thread(void *args)
{
	prctl(PR_SET_NAME,"led_thread");

	while (true) {
		if (led_wait_new_command()) {
			led_handle_new_command();
		} else {
			led_effect_task();
		}
	}
}

static int check_moudle_init(void)
{
	int ret;

	if (m_led_manager.tid <= 0) {
		m_led_manager.new_command_reached = -1;
		ret = pthread_mutex_init(&m_led_manager.mutex, NULL);
		if (ret != 0) {
			return -1;
		}

		ret = pthread_cond_init(&m_led_manager.cond, NULL);
		if (ret != 0) {
			pthread_mutex_destroy(&m_led_manager.mutex);
			return -2;
		}

		ret = pthread_create(&m_led_manager.tid, NULL, led_thread, NULL);
		if (ret != 0) {
			pthread_cond_destroy(&m_led_manager.cond);
			pthread_mutex_destroy(&m_led_manager.mutex);
			return -3;
		}

		pthread_detach(m_led_manager.tid);
	}

	return 0;
}

int RK_led_init(void)
{
	if (m_LedFd.status == -1) {
		return -1;
	} else if (m_LedFd.status == 0) {
		m_LedFd.fd_r = open(LED_PWD_R, O_WRONLY);
		if (m_LedFd.fd_r < 0) {
			m_LedFd.status = -1;
			return -2;
		}

		m_LedFd.fd_g = open(LED_PWD_G, O_WRONLY);
		if (m_LedFd.fd_g < 0) {
			m_LedFd.status = -1;
			return -3;
		}

		m_LedFd.fd_b = open(LED_PWD_B, O_WRONLY);
		if (m_LedFd.fd_b < 0) {
			m_LedFd.status = -1;
			return -4;
		}

		m_LedFd.status = 1;
	}

	return 0;
}

int RK_set_led_effect(RK_Led_Effect_t *effect)
{
	int ret;

	if (0 != (ret = check_moudle_init())) {
		printf("RK_set_led_effect moudle init failed. ret:%d\n", ret);
		return -1;
	}

	pthread_mutex_lock(&m_led_manager.mutex);
	RK_Led_Effect_ins_t *ins, *stable, *prev;
	RK_Led_Effect_t *led;
	ins = (RK_Led_Effect_ins_t*) calloc(sizeof(RK_Led_Effect_ins_t), 1);
	led = (RK_Led_Effect_t*) calloc(sizeof(RK_Led_Effect_t), 1);
	memcpy(led, effect, sizeof(RK_Led_Effect_t));

	ins->time = 0;
	ins->count = 0;
	ins->colors = effect->colors;
	ins->effect = led;
	stable = m_led_manager.stable;
	prev = NULL;
	if (led->layer == Led_Effect_layer_STABLE) {
		// if has same name, remove old first
		while (stable) {
			if (strcmp(stable->effect->name, effect->name) == 0) {
				if (!prev) { // head
					m_led_manager.stable = m_led_manager.stable->next;
				} else {
					prev->next = stable->next;
				}
				free(stable->effect);
				free(stable);

				break;
			}
			prev = stable;
			stable = stable->next;
		}

		// Find a suitable insert position
		stable = m_led_manager.stable;
		prev = NULL;
		while (stable) {
			if (stable->effect->priority <= effect->priority) {
				break;
			}
			prev = stable;
			stable = stable->next;
		}

		if (!prev) { // empty stck or highest priority, insert into head
			ins->next = m_led_manager.stable;
			m_led_manager.stable = ins;
		} else {
			prev->next = ins;
			ins->next = stable;
		}
		m_led_manager.new_command_reached = 0;
	} else if (led->layer == Led_Effect_layer_REALTIME) {
		// if already exit, release first
		if (m_led_manager.realtime) {
			free(m_led_manager.realtime->effect);
			free(m_led_manager.realtime);
			m_led_manager.realtime = NULL;
		}

		m_led_manager.realtime = ins;
		m_led_manager.new_command_reached = 1;
	} else if (led->layer == Led_Effect_layer_TEMP) {
		// if already exit, release first
		if (m_led_manager.temp) {
			free(m_led_manager.temp->effect);
			free(m_led_manager.temp);
			m_led_manager.temp = NULL;
		}

		m_led_manager.temp = ins;
		m_led_manager.new_command_reached = 2;
	}
	pthread_cond_signal(&m_led_manager.cond);
	pthread_mutex_unlock(&m_led_manager.mutex);
	return 0;
}

int RK_set_led_effect_off(const RK_Led_Effect_layer_e layer, const char *name)
{
	pthread_mutex_lock(&m_led_manager.mutex);

	if (Led_Effect_layer_STABLE == layer) {
		RK_Led_Effect_ins_t *stable, *prev;
		stable = m_led_manager.stable;
		prev = NULL;
		while (stable) {
			if (strcmp(stable->effect->name, name) == 0) {
				if (!prev) { // head
					m_led_manager.stable = m_led_manager.stable->next;
				} else {
					prev->next = stable->next;
				}

				free(stable->effect);
				free(stable);

				break;
			}
			prev = stable;
			stable = stable->next;
		}
	} else if (Led_Effect_layer_REALTIME == layer) {
        if(m_led_manager.realtime != NULL){
            if(m_led_manager.realtime->effect != NULL)
                free(m_led_manager.realtime->effect);
            free(m_led_manager.realtime);
            m_led_manager.realtime = NULL;
        }
	} else if (Led_Effect_layer_TEMP == layer) {
		if (m_led_manager.temp != NULL) {
			if (m_led_manager.temp->effect != NULL)
				free(m_led_manager.temp->effect);
			free(m_led_manager.temp);
			m_led_manager.temp = NULL;
		}
	}
	if (!m_led_manager.stable && !m_led_manager.realtime && !m_led_manager.temp) {
		RK_set_all_led_off();
	}

	pthread_cond_signal(&m_led_manager.cond);
	pthread_mutex_unlock(&m_led_manager.mutex);
	return 0;
}

int RK_set_all_led_effect_off(void)
{
	pthread_mutex_lock(&m_led_manager.mutex);

	if (m_led_manager.temp) {
		free(m_led_manager.temp->effect);
		free(m_led_manager.temp);
		m_led_manager.temp = NULL;
	}

	if (m_led_manager.realtime) {
		free(m_led_manager.realtime->effect);
		free(m_led_manager.realtime);
		m_led_manager.realtime = NULL;
	}

	RK_Led_Effect_ins_t *stable, *tmp;
	stable = tmp = m_led_manager.stable;
	while (stable) {
		tmp = stable;
		stable = stable->next;

		free(tmp->effect);
		free(tmp);
	}
	m_led_manager.stable = NULL;

	pthread_cond_signal(&m_led_manager.cond);
	pthread_mutex_unlock(&m_led_manager.mutex);
	return 0;
}

int RK_set_all_led_status(const int Rval, const int Gval, const int Bval)
{
	int ret;
	char cmd[64];

	if (m_LedFd.status == -1) {
		return -1;
	} else if (m_LedFd.status == 0) {
		m_LedFd.fd_r = open(LED_PWD_R, O_WRONLY);
		if (m_LedFd.fd_r < 0) {
			m_LedFd.status = -1;
			return -2;
		}

		m_LedFd.fd_g = open(LED_PWD_G, O_WRONLY);
		if (m_LedFd.fd_g < 0) {
			m_LedFd.status = -1;
			return -3;
		}

		m_LedFd.fd_b = open(LED_PWD_B, O_WRONLY);
		if (m_LedFd.fd_b < 0) {
			m_LedFd.status = -1;
			return -4;
		}

		m_LedFd.status = 1;
	}

	ret = dprintf(m_LedFd.fd_r, "%d", Rval & 0xFF);
	if (ret < 0) {
		RK_LOGE("RK_set_all_led_status(%d, %d, %d) fd_r failed", Rval, Gval, Bval);
		return ret;
	}

	ret = dprintf(m_LedFd.fd_g, "%d", Gval & 0xFF);
	if (ret < 0) {
		RK_LOGE("RK_set_all_led_status(%d, %d, %d) fd_g failed", Rval, Gval, Bval);
		return ret;
	}

	ret = dprintf(m_LedFd.fd_b, "%d", Bval & 0xFF);
	if (ret < 0) {
		RK_LOGE("RK_set_all_led_status(%d, %d, %d) fd_b failed", Rval, Gval, Bval);
		return ret;
	}

	return 0;
}

int RK_set_all_led_off()
{
	return RK_set_all_led_status(0x00, 0x00, 0x00);
}

int RK_led_exit(void)
{
	if (m_LedFd.fd_r > 0) {
		close(m_LedFd.fd_r);
		m_LedFd.fd_r = 0;
	}

	if (m_LedFd.fd_g > 0) {
		close(m_LedFd.fd_g);
		m_LedFd.fd_g = 0;
	}

	if (m_LedFd.fd_b > 0) {
		close(m_LedFd.fd_b);
		m_LedFd.fd_b = 0;
	}

	m_LedFd.status = 0;

	return 0;
}
