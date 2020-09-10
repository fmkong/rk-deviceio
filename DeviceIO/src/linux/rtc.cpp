/*
 * Copyright (c) 2018 Rockchip, Inc. All Rights Reserved.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */
#include <fcntl.h>
#include <linux/input.h>
#include <string.h>
#include <sys/reboot.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <linux/rtc.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <pthread.h>
#include <unistd.h>
#include "Logger.h"
#include "DeviceIo/DeviceIo.h"
#include "rtc.h"
#include "shell.h"

#define rtc_dbg(fmt, ...)	APP_DEBUG("RTC debug: " fmt, ##__VA_ARGS__)
#define rtc_info(fmt, ...)	APP_INFO("RTC info: " fmt, ##__VA_ARGS__)
#define rtc_err(fmt, ...)	APP_ERROR("RTC error: " fmt, ##__VA_ARGS__)

#define	CMD_SIZE	2048
#define	RTC_DEVICE	"/dev/rtc0"

/* The frequencies 128Hz, 256Hz, ... 8192Hz are only allowed for root. */
#define	RTC_MAX_IRQ_PERIODIC 	64

using DeviceIOFramework::DeviceRTC;

struct rtc_manager {
	int rtc_fd;
	struct rtc_time rtc_tm;
};

static struct rtc_manager rtc_g;

/* Read the RTC time/date */
int rtc_read_time(struct rtc_time *rtc_tm)
{
	int retval = -1;
	retval = ioctl(rtc_g.rtc_fd, RTC_RD_TIME, rtc_tm);
	if (retval == -1) {
		rtc_err("RTC_RD_TIME ioctl");
		return -1;
	}

	fprintf(stderr, "RTC info: Current date/time is %d-%d-%d, %02d:%02d:%02d.\n",
		rtc_tm->tm_mday, rtc_tm->tm_mon + 1, rtc_tm->tm_year + 1900,
		rtc_tm->tm_hour, rtc_tm->tm_min, rtc_tm->tm_sec);
	return 0;
}

/* Set the alarm to x sec in the future, and check for rollover */
int rtc_set_time(unsigned int sec)
{
	struct rtc_time curr_rtc_tm;
	int retval = -1;

	retval = rtc_read_time(&curr_rtc_tm);
	if (retval == -1) {
		rtc_err("read rtc time error.");
		return -1;
	}

	curr_rtc_tm.tm_sec += sec;
	if (curr_rtc_tm.tm_sec >= 60) {
		curr_rtc_tm.tm_sec %= 60;
		curr_rtc_tm.tm_min++;
	}
	if (curr_rtc_tm.tm_min == 60) {
		curr_rtc_tm.tm_min = 0;
		curr_rtc_tm.tm_hour++;
	}
	if (curr_rtc_tm.tm_hour == 24)
		curr_rtc_tm.tm_hour = 0;

	retval = ioctl(rtc_g.rtc_fd, RTC_ALM_SET, &curr_rtc_tm);
	if (retval == -1) {
		rtc_err("RTC_ALM_SET ioctl");
		return -1;
	}

	return 0;
}

/* Read the current alarm settings */
int rtc_read_alarm(struct rtc_time *rtc_tm)
{
	int retval = -1;

	retval = ioctl(rtc_g.rtc_fd, RTC_ALM_READ, rtc_tm);
	if (retval == -1) {
		rtc_err("RTC_ALM_READ ioctl");
		return -1;
	}

	fprintf(stderr, "\nRTC info: Alarm time now set to %02d:%02d:%02d.\n",
		rtc_tm->tm_hour, rtc_tm->tm_min, rtc_tm->tm_sec);
	return 0;
}

/* This blocks until the alarm ring causes an interrupt */
int rtc_wait_alarm_ring(void)
{
	unsigned long data;
	int retval = -1;

	retval = read(rtc_g.rtc_fd, &data, sizeof(unsigned long));
	if (retval == -1) {
		rtc_err("wait alarm ring error.");
		return -1;
	}
	fprintf(stderr, "RTC info: okay. Alarm rang.\n");
	return 0;
}

/* Enable alarm interrupts */
int rtc_enable_alarm_interrupt(void)
{
	int retval = -1;

	retval = ioctl(rtc_g.rtc_fd, RTC_AIE_ON, 0);
	if (retval == -1) {
		rtc_err("RTC_AIE_ON ioctl");
		return -1;
	}
	return 0;
}

/* Disable alarm interrupts */
int rtc_disable_alarm_interrupt(void)
{
	int retval = -1;

	retval = ioctl(rtc_g.rtc_fd, RTC_AIE_OFF, 0);
	if (retval == -1) {
		perror("RTC_AIE_OFF ioctl");
		return -1;
	}
	return 0;
}

/* Read periodic IRQ rate */
int rtc_read_irq_rate(unsigned int *rate)
{
	int retval = -1;
	retval = ioctl(rtc_g.rtc_fd, RTC_IRQP_READ, rate);
	if (retval == -1) {
		/* not all RTCs support periodic IRQs */
		if (errno == ENOTTY) {
			rtc_err("\nNo periodic IRQ support\n");
			return -1;
		}
		rtc_err("RTC_IRQP_READ ioctl");
		return -1;
	}
	return 0;
}

int rtc_set_irq_periodic(unsigned int *rate)
{
	int retval = -1;
	int i = 2;

	for (i=2; i<=RTC_MAX_IRQ_PERIODIC; i*=2) {
		if (*rate == i) {
			break;	
		}
	}

	if (i > RTC_MAX_IRQ_PERIODIC) {
		rtc_err("Periodic IRQ rate is invalid.\n");
		return -1;
	}

	retval = ioctl(rtc_g.rtc_fd, RTC_IRQP_SET, *rate);
	if (retval == -1) {
		/* not all RTCs can change their periodic IRQ rate */
		if (errno == ENOTTY) {
			rtc_err("Periodic IRQ rate is fixed\n");
			return -1;
		}
		rtc_err("RTC_IRQP_SET ioctl");
		return -1;
	}
	return 0;
}

/* Enable periodic interrupts */
int rtc_enable_periodic_interrupt(void)
{
	int retval = -1;

	retval = ioctl(rtc_g.rtc_fd, RTC_PIE_ON, 0);
	if (retval == -1) {
		rtc_err("RTC_PIE_ON ioctl");
		return -1;
	}
	return 0;
}

/* Disable periodic interrupts */
int rtc_disable_periodic_interrupt(void)
{
	int retval = -1;

	retval = ioctl(rtc_g.rtc_fd, RTC_PIE_OFF, 0);
	if (retval == -1) {
		rtc_err("RTC_PIE_OFF ioctl");
		return -1;
	}
	return 0;
}

int rtc_control(DeviceRTC cmd, void *data, int len)
{
    int ret = -1;
    
	switch (cmd) {
	case DeviceRTC::DEVICE_RTC_READ_TIME:
		ret = rtc_read_time((struct rtc_time *)data);
        break;

    case DeviceRTC::DEVICE_RTC_SET_TIME:
		ret = rtc_set_time(*((unsigned int *)data));
        break;

    case DeviceRTC::DEVICE_RTC_READ_ALARM:
		ret = rtc_read_alarm((struct rtc_time *)data);
        break;

    case DeviceRTC::DEVICE_RTC_ENABLE_ALARM_INTERRUPT:
		ret = rtc_enable_alarm_interrupt();
        break;

    case DeviceRTC::DEVICE_RTC_DISABLE_ALARM_INTERRUPT:
		ret = rtc_disable_alarm_interrupt();
        break;

    case DeviceRTC::DEVICE_RTC_READ_IRQ_RATE:
		ret = rtc_read_irq_rate((unsigned int *)data);
        break;

    case DeviceRTC::DEVICE_RTC_SET_IRQ_PERIODIC:
		ret = rtc_set_irq_periodic((unsigned int *)data);
        break;

    case DeviceRTC::DEVICE_RTC_ENABLE_PERIODIC_INTERRUPT:
		ret = rtc_enable_periodic_interrupt();
        break;

    case DeviceRTC::DEVICE_RTC_DISABLE_PERIODIC_INTERRUPT:
		ret = rtc_disable_periodic_interrupt();
        break;

    case DeviceRTC::DEVICE_RTC_WAIT_ALARM_RING:
		ret = rtc_wait_alarm_ring();
        break;


	default:
		rtc_info("%s:%d cmd [%d] Not Found.\n", __func__, __LINE__, cmd);
		break;
	}

	return ret;
}

int rtc_init(void)
{
    int ret = 0;
	
    memset(&rtc_g, 0x00, sizeof(rtc_g));

	rtc_g.rtc_fd = open(RTC_DEVICE, O_RDONLY);
	if (rtc_g.rtc_fd ==  -1) {
		rtc_err("%s:%d open %s fail.\n", __func__, __LINE__, RTC_DEVICE);
		return -1;
	}

    return 0;
}

int rtc_deinit(void)
{
	close(rtc_g.rtc_fd);
	return 0;
}		/* -----  end of function rtc_deinit  ----- */
