/*
 * Copyright (c) 2016 Zibin Zheng <znbin@qq.com>
 * All rights reserved
 */
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include "DeviceIo/RK_timer.h"
#include <sys/prctl.h>

static RK_Timer_t *head_handle = NULL;
static pthread_t timer_thread;
static int timer_running = 0;

static uint64_t get_timestamp_ms(void)
{
	struct timeval ctime;
	gettimeofday(&ctime, NULL);

	return (1e+6 * (uint64_t)ctime.tv_sec + ctime.tv_usec) / 1000;
}

/**
  * @brief  Initializes the timer struct handle.
  * @param  handle: the timer handle strcut.
  * @param  cb: timer trigged callback.
  * @param  time: time of the timer
  * @param  repeat: repeat interval time.
  * @retval None
  */
int RK_timer_create(RK_Timer_t *handle, RK_timer_callback cb, const uint32_t time, const uint32_t repeat)
{
	handle->timer_cb = cb;
	handle->timer_time = time;
	handle->timer_repeat = repeat;

	return 0;
}

/**
  * @brief  Start the timer work, add the handle into work list.
  * @param  handle: target handle strcut.
  * @retval 0: succeed. -1: already exist.
  */
int RK_timer_start(RK_Timer_t *handle)
{
	if (!timer_running) {
		if (RK_timer_init() != 0) {
			return -1;
		}
	}

	RK_Timer_t *target = head_handle;
	while (target) {
		if (target == handle) {//already exist.
			return -1;
		}
		target = target->next;
	}
	handle->timer_start = handle->timer_last = get_timestamp_ms();

	handle->next = head_handle;
	head_handle = handle;

	return 0;
}

/**
  * @brief  Stop the timer work, remove the handle off work list.
  * @param  handle: target handle strcut.
  * @retval None
  */
int RK_timer_stop(RK_Timer_t *handle)
{
	RK_Timer_t *curr, *prev;
	curr = head_handle;
	prev = NULL;
	while (curr) {
		if (curr == handle) {
			if (prev == NULL) {
				head_handle = NULL;
			} else {
				prev->next = curr->next;
			}
			break;
		}
		curr = curr->next;
	}

	return 0;
}

/**
  * @brief  main loop.
  * @param  None.
  * @retval None
  */
static void* rk_thread_timer(void *arg)
{
	RK_Timer_t *target;
	uint64_t time;

	prctl(PR_SET_NAME,"rk_thread_timer");

	while (timer_running) {
		usleep(1000);
		for (target = head_handle; target; target = target->next) {
			time = get_timestamp_ms();
			if (target->timer_repeat <= 0 && time - target->timer_start >= target->timer_time) {
				target->timer_cb(1);
				RK_timer_stop(target);
				continue;
			}

			if (target->timer_repeat > 0 && time - target->timer_last >= target->timer_repeat) {
				target->timer_last = time;
				target->timer_cb(0);
			}
		}
	}

	return NULL;
}

int RK_timer_init(void)
{
	int ret;

	if (timer_running && timer_thread > 0)
		return 0;

	timer_running = 1;
	ret = pthread_create(&timer_thread, NULL, rk_thread_timer, NULL);

	if (ret != 0) {
		ret = -1;
		timer_running = 0;
	}

	return ret;
}

int RK_timer_exit(void)
{
	timer_running = 0;

	return 0;
}
