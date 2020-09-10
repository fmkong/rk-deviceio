/*
 * Copyright (c) 2017 Rockchip, Inc. All Rights Reserved.
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

#include "key.h"
#include "DeviceIo/DeviceIo.h"
#include "Timer.h"
#include <string.h>
#include <pthread.h>
#include <sys/select.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>
#include <math.h>
#include "Logger.h"
#include <sys/prctl.h>

#define dbg(fmt, ...) APP_DEBUG("[rk keys debug ]" fmt, ##__VA_ARGS__)
#define err(fmt, ...) APP_ERROR("[rk keys error ]" fmt, ##__VA_ARGS__)

using DeviceIOFramework::Timer;
using DeviceIOFramework::TimerManager;
using DeviceIOFramework::TimerNotify;
using DeviceIOFramework::DeviceIo;
using DeviceIOFramework::DeviceInput;

enum keys_bit {
    KEYS_BIT_PLAY_PAUSE  = 0x01 << 0,
    KEYS_BIT_VOLUME_DOWN = 0x01 << 1,
    KEYS_BIT_VOLUME_UP   = 0x01 << 2,
    KEYS_BIT_MIC_MUTE    = 0x01 << 3,
    KEYS_BIT_POWER	 = 0x01 << 4,
};

struct key_manager {
    int gpio_keys_fd;
    int rk816_keys_fd;
    int hpdet_fd;
    int keys_state;
    int keys_state_prev;
    int count;

    Timer *timer;
    TimerNotify *notify;
    pthread_t tid;
    pthread_mutex_t keys_state_mutex;
};

static struct key_manager key;

static int m_mute_long = 0;
static void report_key_event(DeviceInput event, void *data, int len) {
    if (DeviceIo::getInstance()->getNotify())
        DeviceIo::getInstance()->getNotify()->callback(event, data, len);
}

static void handle_keys_press(int state) {
    switch (state) {
    case KEYS_BIT_VOLUME_DOWN: {
        dbg("press KEYS_BIT_VOLUME_DOWN\n");
        int value = 10;
        report_key_event(DeviceInput::KEY_VOLUME_DOWN, &value, sizeof(value));
        break;
    }
    case KEYS_BIT_VOLUME_UP: {
        dbg("press KEYS_BIT_VOLUME_UP\n");
        int value = 10;
        report_key_event(DeviceInput::KEY_VOLUME_UP, &value, sizeof(value));
        break;
    }
    case KEYS_BIT_PLAY_PAUSE: {
        dbg("press KEYS_BIT_PLAY_PAUSE\n");
        report_key_event(DeviceInput::KEY_PLAY_PAUSE, NULL, 0);
        break;
    }
    case KEYS_BIT_MIC_MUTE: {
        dbg("press KEYS_BIT_MUTE\n");
        //report_key_event(DeviceInput::KEY_MIC_MUTE, NULL, 0);
        break;
    }
    case KEYS_BIT_POWER: {
        dbg("press KEYS_BIT_POWER\n");
        report_key_event(DeviceInput::KEY_RK816_POWER, NULL, 0);
        break;
    }
    default:
        break;
    }
}

static void handle_keys_on_2s(int state) {
    if (state == (KEYS_BIT_VOLUME_UP | KEYS_BIT_VOLUME_DOWN)) {
        dbg("on 2s KEYS_BIT_VOLUME_UP and KEYS_BIT_VOLUME_DOWN\n");
        report_key_event(DeviceInput::KEY_ENTER_AP, NULL, 0);
    }
}
static void handle_keys_on_3s(int state) {
    if (state == (KEYS_BIT_POWER)) {
        dbg("on 3s KEYS_BIT_POWER\n");
        report_key_event(DeviceInput::KEY_SHUT_DOWN, NULL, 0);
    }
    if (state == (KEYS_BIT_VOLUME_UP | KEYS_BIT_POWER)) {
        dbg("on 3s KEYS_BIT_VOLUME_UP | KEYS_BIT_POWER\n");
        report_key_event(DeviceInput::KEY_FACTORY_RESET, NULL, 0);
    }
    if (state == (KEYS_BIT_MIC_MUTE)) {
        dbg("on 3s KEYS_BIT_MIC_MUTE\n");
        report_key_event(DeviceInput::KEY_ENTER_AP, NULL, 0);
                m_mute_long = 1;
    }
}
static void handle_keys_on_5s(int state) {
    if (state == (KEYS_BIT_MIC_MUTE)) {
        dbg("on 5s KEYS_BIT_MIC_MUTE\n");
        report_key_event(DeviceInput::KEY_ENTER_AP, NULL, 0);
		m_mute_long = 1;
    }
}
static void handle_keys_after_1s(int state) {
    switch (state) {
    case KEYS_BIT_VOLUME_DOWN: {
        dbg("after 1s KEYS_BIT_VOLUME_DOWN\n");
        int value = 5;
        report_key_event(DeviceInput::KEY_VOLUME_DOWN, &value, sizeof(value));
        break;
    }
    case KEYS_BIT_VOLUME_UP: {
        dbg("after 1s KEYS_BIT_VOLUME_UP\n");
        int value = 5;
        report_key_event(DeviceInput::KEY_VOLUME_UP, &value, sizeof(value));
        break;
    }
    default:
        break;
    }
}

static void handle_keys(void) {
    pthread_mutex_lock(&key.keys_state_mutex);

    if (key.keys_state == 0) {
        TimerManager::getInstance()->timerStop(key.timer);
        key.keys_state_prev = key.keys_state;
        key.count = 0;
    } else {
        if (key.keys_state_prev != key.keys_state) {
            key.keys_state_prev = key.keys_state;
            key.count = 0;
            TimerManager::getInstance()->timerStart(key.timer);

        } else {
            key.count++;

            if (key.count == 1) {// 为了组合键不能同时按下100ms延时
                handle_keys_press(key.keys_state);
            } else if (key.count > 10) {
                if (!(key.count % 2))
                    handle_keys_after_1s(key.keys_state);
            }

            if (key.count == 20) {
                handle_keys_on_2s(key.keys_state);
            }
 
            if (key.count == 30) {
                handle_keys_on_3s(key.keys_state);
            }

            if (key.count == 50) {
                //handle_keys_on_5s(key.keys_state);
            }
        }
    }

    pthread_mutex_unlock(&key.keys_state_mutex);
}

class KeyNotify : public TimerNotify {
    void timeIsUp(Timer* timer) {
        handle_keys();
    }
};

static void check_keys(struct input_event* event) {
    pthread_mutex_lock(&key.keys_state_mutex);

    //callback raw input_event for app
    report_key_event(DeviceInput::KEY_RAW_INPUT_EVENT, event, sizeof(struct input_event));

    switch (event->code) {
    case KEY_PLAY:
    case KEY_PLAYPAUSE:{
        if (event->value)
            key.keys_state |= KEYS_BIT_PLAY_PAUSE;
        else
            key.keys_state &= ~KEYS_BIT_PLAY_PAUSE;
        break;
    }
    case KEY_VOLUMEDOWN: {
        if (event->value)
            key.keys_state |= KEYS_BIT_VOLUME_DOWN;
        else
            key.keys_state &= ~KEYS_BIT_VOLUME_DOWN;
        break;
    }
    case KEY_VOLUMEUP: {
        if (event->value)
            key.keys_state |= KEYS_BIT_VOLUME_UP;
        else
            key.keys_state &= ~KEYS_BIT_VOLUME_UP;
        break;
    }
    case KEY_MICMUTE: {
        if (event->value) {
            key.keys_state |= KEYS_BIT_MIC_MUTE;
        } else {
            if (!m_mute_long) {
                report_key_event(DeviceInput::KEY_MIC_MUTE, NULL, 0);
            }
            key.keys_state &= ~KEYS_BIT_MIC_MUTE;
            m_mute_long = 0;
        }
        break;
    }
    case KEY_POWER: {
        if (event->value)
            key.keys_state |= KEYS_BIT_POWER;
        else
            key.keys_state &= ~KEYS_BIT_POWER;
        break;
    }
    case SW_HEADPHONE_INSERT: {
        if (event->value) {
            int value = 1;
            report_key_event(DeviceInput::KEY_HEADPHONE_INSERT, &value, sizeof(value));
        } else {
            int value = 0;
            report_key_event(DeviceInput::KEY_HEADPHONE_INSERT, &value, sizeof(value));
        }
        break;
    }
    default: {
        pthread_mutex_unlock(&key.keys_state_mutex);
        return;
    }

    }

    pthread_mutex_unlock(&key.keys_state_mutex);
    //dbg("keys state:%d\n", key.keys_state);
    if (2 == event->value) {
        return;
    }

    handle_keys();
}

static void * key_task(void *param) {
    fd_set rfds;
    int nfds = 0;

    nfds = key.gpio_keys_fd > key.rk816_keys_fd ? key.gpio_keys_fd : key.rk816_keys_fd;
    nfds = nfds > key.hpdet_fd ? nfds : key.hpdet_fd;
    nfds = nfds + 1;

    prctl(PR_SET_NAME,"key_task");

    while (1) {
        FD_ZERO(&rfds);
        FD_SET(key.gpio_keys_fd, &rfds);
        if (key.rk816_keys_fd > 0)
            FD_SET(key.rk816_keys_fd, &rfds);
        if (key.hpdet_fd > 0)
            FD_SET(key.hpdet_fd, &rfds);

        select(nfds, &rfds, NULL, NULL, NULL);

        struct input_event ev_key;
        int ret;

        if (FD_ISSET(key.gpio_keys_fd, &rfds)) {
            ret = read(key.gpio_keys_fd, &ev_key, sizeof(ev_key));
            //dbg("ret=%d key=%d value=%d\n", ret, ev_key.code, ev_key.value);
            if (ret == sizeof(ev_key)) {
                check_keys(&ev_key);
            }
        } else if (FD_ISSET(key.rk816_keys_fd, &rfds)) {
            ret = read(key.rk816_keys_fd, &ev_key, sizeof(ev_key));
            dbg("816 ret=%d key=%d value=%d\n", ret, ev_key.code, ev_key.value);
            if (ret == sizeof(ev_key)) {
                check_keys(&ev_key);
            }
        } else if (FD_ISSET(key.hpdet_fd, &rfds)) {
            ret = read(key.hpdet_fd, &ev_key, sizeof(ev_key));
            dbg("head phone ret=%d key=%d value=%d\n", ret, ev_key.code, ev_key.value);
            if (ret == sizeof(ev_key)) {
                check_keys(&ev_key);
            }
        }
    }
}

int rk_key_init(void) {
    int ret = 0;

    memset(&key, 0x00, sizeof(key));

    key.gpio_keys_fd = open("/dev/input/event2", O_RDONLY);
    key.rk816_keys_fd = open("/dev/input/event0", O_RDONLY);
    key.hpdet_fd = open("/dev/input/event1", O_RDONLY);

    if (key.gpio_keys_fd < 0) {
        err("[%s]open gpio keys fd failed\n", __FUNCTION__);
        return -1;
    }
    if (key.rk816_keys_fd < 0) {
        err("[%s]open rk816 keys fd failed\n", __FUNCTION__);
    }

    if (key.hpdet_fd < 0) {
        err("[%s]open hpdet keys fd failed\n", __FUNCTION__);
    }
    key.notify = new KeyNotify();
    key.timer = TimerManager::getInstance()->timer_create(0.1, key.notify, true, false);
    if (key.timer == NULL) {
        err("[%s]create timer failed\n", __FUNCTION__);
        return -1;
    }

    ret = pthread_mutex_init(&key.keys_state_mutex, NULL);
    if (ret) {
        err("[%s]init mutex failed\n", __FUNCTION__);
        TimerManager::getInstance()->timerDelete(key.timer);
        delete key.notify;
        return -1;
    }

    ret = pthread_create(&key.tid, NULL, key_task, NULL);
    if (ret) {
        err("[%s]create thread failed\n", __FUNCTION__);
        pthread_mutex_destroy(&key.keys_state_mutex);
        TimerManager::getInstance()->timerDelete(key.timer);
        delete key.notify;
        return -1;
    }

    return  0;
}

int rk_key_exit(void) {
    if (key.tid) {
        pthread_cancel(key.tid);
        pthread_join(key.tid, NULL);
        key.tid = 0;
    }

    pthread_mutex_destroy(&key.keys_state_mutex);

    if (key.timer) {
        TimerManager::getInstance()->timerDelete(key.timer);
        key.timer = NULL;
    }

    if (key.notify) {
        delete key.notify;
        key.notify = NULL;
    }

    if (key.gpio_keys_fd) {
        close(key.gpio_keys_fd);
        key.gpio_keys_fd = 0;
    }

    if (key.rk816_keys_fd) {
        close(key.rk816_keys_fd);
        key.rk816_keys_fd = 0;
    }

    if (key.hpdet_fd) {
        close(key.hpdet_fd);
        key.hpdet_fd = 0;
    }
    return 0;
}
