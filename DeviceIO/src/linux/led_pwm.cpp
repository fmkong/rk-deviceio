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
#include "led.h"
#include "Timer.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <vector>
#include "Logger.h"
#include <sys/prctl.h>

#define TIMER_PERIOD (20)
#define LED_NUM (1)

static const int WHITE = 0xFFFFFF;
static const int YELLOW = 0xFFFF00;
static const int GREEN = 0x00FF00;
static const int RED = 0xFF0000;
static const int BLUE = 0x0000FF;
static const int OFF = 0x000000;

#define dbg(fmt, ...) APP_DEBUG("[rk leds debug] " fmt, ##__VA_ARGS__)
#define err(fmt, ...) APP_ERROR("[rk leds error] " fmt, ##__VA_ARGS__)
#define container_of(ptr, type, member) \
    (type *)((char *)(ptr) - (char *) &((type *)0)->member)
#define ARRAY_SIZE(x) sizeof(x)/sizeof((x)[0])

using std::vector;

struct led_effect {
    int period;               // 灯效周期，例如呼吸一次为3000ms.-1表示周期无限大
    int stop_time;
    int timeout;              // 超时时间，-1表示无限大
    int back_color;
    int fore_color;
    int led_num;              // 亮起几个灯带
    // internal data for calculate
    int count;
    int time;
    int leds_color;
};

struct led_command {
    LedState cmd;
    int data;
};

struct led_layer {
    int name;
    LedState cmd;
    struct led_effect effect;
};

struct led_manager {
    int leds_r_fd;
    int leds_g_fd;
    int leds_b_fd;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    pthread_t tid;
    vector<led_layer> led_layers;
    bool new_command_reached;
    vector<led_command> led_cmd;
};

static struct led_manager s_led;

static int led_open(void)
{
    s_led.leds_r_fd = open("/sys/devices/platform/pwmleds/leds/PWM-R/brightness", O_WRONLY);

    if (s_led.leds_r_fd < 0) {
        err("open /sys/devices/platform/pwmleds/leds/PWM-R/brightness failed\n");
        return -1;
    }

    s_led.leds_g_fd = open("/sys/devices/platform/pwmleds/leds/PWM-G/brightness", O_WRONLY);

    if (s_led.leds_g_fd < 0) {
        err("open /sys/devices/platform/pwmleds/leds/PWM-G/brightness failed\n");
        return -1;
    }

    s_led.leds_b_fd = open("/sys/devices/platform/pwmleds/leds/PWM-B/brightness", O_WRONLY);

    if (s_led.leds_b_fd < 0) {
        err("open /sys/devices/platform/pwmleds/leds/PWM-B/brightness failed\n");
        return -1;
    }

    return 0;
}

static int led_close(void)
{
    if (s_led.leds_r_fd > 0) {
        close(s_led.leds_r_fd);
    }

    if (s_led.leds_g_fd > 0) {
        close(s_led.leds_g_fd);
    }

    if (s_led.leds_b_fd > 0) {
        close(s_led.leds_b_fd);
    }

    return 0;
}

static int led_write(int num, int *color)
{
    int ret = 0;
    ret = dprintf(s_led.leds_r_fd, "%d", (*color >> 16) & 0xFF);

    if (ret < 0) {
        err("write led_r_fd error, error no:%d\n", ret);
        return ret;
    }

    ret = dprintf(s_led.leds_g_fd, "%d", (*color >> 8) & 0xFF);

    if (ret < 0) {
        err("write led_g_fd error, error no:%d\n", ret);
        return ret;
    }

    ret = dprintf(s_led.leds_b_fd, "%d", *color & 0xFF);

    if (ret < 0) {
        err("write led_b_fd error, error no:%d\n", ret);
        return ret;
    }

    return  ret;
}

static int led_write_all(int *color)
{
    int ret = 0;
    ret = dprintf(s_led.leds_r_fd, "%d", (*color >> 16) & 0xFF);

    if (ret < 0) {
        err("write led_r_fd error, error no:%d\n", ret);
        return ret;
    }

    ret = dprintf(s_led.leds_g_fd, "%d", (*color >> 8) & 0xFF);

    if (ret < 0) {
        err("write led_g_fd error, error no:%d\n", ret);
        return ret;
    }

    ret = dprintf(s_led.leds_b_fd, "%d", *color & 0xFF);

    if (ret < 0) {
        err("write led_b_fd error, error no:%d\n", ret);
        return ret;
    }

    return  ret;
}

static void led_effect_default(struct led_effect *effect)
{
    memset(effect, 0x00, sizeof(*effect));
    effect->stop_time = -1;
    effect->period = -1;
    effect->timeout = -1;
    effect->led_num = LED_NUM;
    effect->count = -1;
}

static void led_delete_a_layer(struct led_layer *layer);

static bool led_handle_timeout(struct led_effect *effect)
{
    if (effect->timeout != -1) {
        if (effect->time >= effect->timeout) {
            led_delete_a_layer(container_of(effect, struct led_layer, effect));
            return true;
        }
    }

    return false;
}

// 呼吸灯效
static void led_effect_breath(struct led_effect* effect) {

    int timer_period_count = effect->period / TIMER_PERIOD;
    int now_count = effect->count % timer_period_count;
    int half_timer_period_count = timer_period_count / 2;

    if (now_count > half_timer_period_count) {
        now_count = timer_period_count - now_count;
    }

    int color = ((((effect->fore_color & 0xFF0000) >> 16) * now_count / half_timer_period_count) << 16)
                | ((((effect->fore_color & 0xFF00) >> 8) * now_count / half_timer_period_count) << 8)
                | ((effect->fore_color & 0xFF) * now_count / half_timer_period_count);

    effect->leds_color = color;
}

// 闪烁灯效
static void led_effect_blink(struct led_effect *effect)
{
    if (effect->leds_color == effect->back_color) {
        effect->leds_color = effect->fore_color;

    } else {
        effect->leds_color = effect->back_color;
    }
}

static void led_net_recovery(struct led_effect *effect, int arg)
{
    led_effect_default(effect);
    effect->fore_color = YELLOW;
    effect->leds_color = effect->fore_color;
}

static void led_net_wait_connect(struct led_effect *effect, int arg)
{
    led_effect_default(effect);
    effect->count = 0;
    effect->fore_color = YELLOW;
    effect->period = 2000;
    effect->leds_color = effect->back_color;
}

static void led_net_do_connect(struct led_effect *effect, int arg)
{
    led_effect_default(effect);
    effect->fore_color = YELLOW;
    effect->leds_color = effect->fore_color;
}

static void led_net_connect_failed(struct led_effect *effect, int arg)
{
    led_effect_default(effect);
    effect->fore_color = RED;
    effect->period = 200;
    effect->stop_time = 1000;
    effect->leds_color = effect->back_color;
}

static void led_net_connect_success(struct led_effect *effect, int arg)
{
    led_effect_default(effect);
    effect->fore_color = YELLOW;
    effect->period = 200;
    effect->stop_time = 800;
    effect->leds_color = effect->back_color;
}

static void led_wakeup_doa(struct led_effect *effect, int angle)
{
    led_effect_default(effect);
    effect->fore_color = BLUE;
    effect->period = 140;
    effect->stop_time = 560;
    effect->leds_color = effect->back_color;
}
static void led_wakeup(struct led_effect *effect, int arg)
{
    led_effect_default(effect);
    effect->fore_color = BLUE;
    effect->period = 140;
    effect->stop_time = 560;
    effect->leds_color = effect->back_color;
}

static void led_speech_parse(struct led_effect *effect, int arg)
{
    led_effect_default(effect);
    effect->fore_color = BLUE;
    effect->period = 500;
    effect->leds_color = effect->back_color;
}

static void led_play_tts(struct led_effect* effect, int arg) {

    led_effect_default(effect);
    effect->count = 0;
    effect->fore_color = BLUE;
    effect->period = 1000;
    effect->leds_color = effect->back_color;
}

static void led_bt_wait_pair(struct led_effect *effect, int arg)
{
    led_effect_default(effect);
    effect->count = 0;
    effect->fore_color = GREEN;
    effect->period = 2000;
    effect->leds_color = effect->back_color;
}

static void led_bt_do_pair(struct led_effect *effect, int arg)
{
    led_effect_default(effect);
    effect->fore_color = GREEN;
    effect->leds_color = effect->fore_color;
}

static void led_bt_pair_failed(struct led_effect *effect, int arg)
{
    led_effect_default(effect);
    effect->fore_color = RED;
    effect->period = 200;
    effect->timeout = 1000;
    effect->leds_color = effect->back_color;
}

static void led_bt_pair_success(struct led_effect *effect, int arg)
{
    led_effect_default(effect);
    effect->fore_color = GREEN;
    effect->period = 200;
    effect->timeout = 1000;
    effect->leds_color = effect->back_color;
}

static void led_volume(struct led_effect *effect, int arg)
{
    int volume = arg;

    led_effect_default(effect);
    effect->fore_color = WHITE;
    effect->period = 1000;
    effect->timeout = 1000;
    if (volume > 0) {
        effect->led_num = (volume - 1) / 25 + 1;
    } else {
        effect->led_num = 0;
    }

    effect->leds_color = effect->fore_color;
}

static void led_mute(struct led_effect *effect, int arg)
{
    led_effect_default(effect);
    effect->fore_color = RED;
    effect->leds_color = effect->fore_color;
}

static void led_alarm(struct led_effect *effect, int arg)
{
    led_effect_default(effect);
    effect->count = 0;
    effect->fore_color = BLUE;
    effect->period = 1500;
    effect->leds_color = effect->back_color;
}

static void led_sleep_mode(struct led_effect *effect, int arg)
{
    led_effect_default(effect);
    effect->fore_color = RED;
    effect->leds_color = effect->fore_color;
}

static void led_ota_doing(struct led_effect *effect, int arg)
{
    led_effect_default(effect);
    effect->fore_color = YELLOW;
    effect->leds_color = effect->fore_color;
}

struct led_operation {
    const int layer;
    const char *name;
    void (*effect_init)(struct led_effect *effect, int arg);
    void (*effect_task)(struct led_effect *effect);
};

static struct led_operation s_led_ops[] = {
    {0, "led_net_recovery",        led_net_recovery,         NULL},
    {0, "led_net_wait_connect",    led_net_wait_connect,     led_effect_breath},
    {0, "led_net_do_connect",      led_net_do_connect,       NULL},
    {0, "led_net_connect_failed",  led_net_connect_failed,   led_effect_blink},
    {0, "led_net_connect_success", led_net_connect_success,  led_effect_blink},
    {0, "led_net_wait_login",      NULL,                     NULL},
    {0, "led_net_do_login",        NULL,                     NULL},
    {0, "led_net_login_failed",    NULL,                     NULL},
    {0, "led_net_login_success",   NULL,                     NULL},

    {1, "led_wake_up_doa",         led_wakeup_doa,             led_effect_blink},
    {1, "led_wake_up",             led_wakeup,                 led_effect_blink},
    {1, "led_speech_parse",        led_speech_parse,           led_effect_blink},
    {1, "led_play_tts",            led_play_tts,               led_effect_breath},
    {1, "led_play_resource",       NULL,                       NULL},

    {2, "led_bt_wait_pair",        led_bt_wait_pair,           led_effect_breath},
    {2, "led_bt_do_pair",          led_bt_do_pair,             NULL},
    {2, "led_bt_pair_failed",      led_bt_pair_failed,         led_effect_blink},
    {2, "led_bt_pair_success",     led_bt_pair_success,        led_effect_blink},
    {2, "led_bt_play",             NULL,                       NULL},
    {2, "led_bt_close",            NULL,                       NULL},

    {3, "led_volume",              led_volume,                 NULL},
    {3, "led_mute",                led_mute,                   led_effect_blink},

    {4, "led_disable_mic",         NULL,                       NULL},

    {5, "led_alarm",               led_alarm,                  led_effect_breath},

    {6, "led_sleep_mode",          led_sleep_mode,             NULL},

    {7, "led_ota_doing",           led_ota_doing,              NULL},
    {7, "led_ota_success",         NULL,                       NULL},

    {8, "led_close_a_layer",       NULL,                       NULL},
    {8, "led_all_off",             NULL,                       NULL},
    {8, "led_pwmr_set",            NULL,                       NULL},
    {8, "led_pwmg_set",            NULL,                       NULL},
    {8, "led_pwmb_set",            NULL,                       NULL},
};

static void led_final_write()
{
    if (s_led.led_layers.empty()) {
        int color = OFF;
        led_write_all(&color);

    } else {
        led_write(s_led.led_layers.back().effect.led_num, &s_led.led_layers.back().effect.leds_color);
    }
}

static void led_print_layers()
{
    vector<struct led_layer>::iterator it;

    for (it = s_led.led_layers.begin(); it < s_led.led_layers.end(); it++) {
        dbg("print layers: %s\n", s_led_ops[(int)(*it).cmd].name);
    }
}

static void led_add_a_layer(struct led_layer *layer)
{
    vector<struct led_layer>::iterator it;

    for (it = s_led.led_layers.begin(); it < s_led.led_layers.end(); it++) {
        if ((*it).name == layer->name) {
            s_led.led_layers.erase(it);
            break;
        }
    }

    s_led.led_layers.push_back(*layer);
}

static void led_delete_a_layer(struct led_layer *layer)
{
    vector<struct led_layer>::iterator it;

    for (it = s_led.led_layers.begin(); it < s_led.led_layers.end(); it++) {
        if ((*it).name == layer->name) {
            s_led.led_layers.erase(it);
            break;
        }
    }
}

static void led_handle_a_command(struct led_command *command)
{
    if (command->cmd == LedState::LED_ALL_OFF) {
        s_led.led_layers.clear();

    } else if (command->cmd == LedState::LED_CLOSE_A_LAYER) {
        int cmd = command->data;

        if (cmd >= 0 && cmd < (int)(ARRAY_SIZE(s_led_ops))) {
            struct led_layer layer;
            layer.name = s_led_ops[cmd].layer;
            dbg("close layer: %s\n", s_led_ops[cmd].name);
            led_delete_a_layer(&layer);
        }

    } else {
        struct led_layer layer;
        layer.name = s_led_ops[(int)command->cmd].layer;
        layer.cmd = command->cmd;

        if (s_led_ops[(int)command->cmd].effect_init) {
            s_led_ops[(int)command->cmd].effect_init(&layer.effect, command->data);
            dbg("add layer: %s\n", s_led_ops[(int)layer.cmd].name);
            led_add_a_layer(&layer);
        }
    }

    led_final_write();
    led_print_layers();
}

static void led_handle_command_queue(void)
{
    pthread_mutex_lock(&s_led.mutex);

    while (!s_led.led_cmd.empty()) {
        struct led_command led_cmd;
        led_cmd = s_led.led_cmd.front();
        s_led.led_cmd.erase(s_led.led_cmd.begin());
        led_handle_a_command(&led_cmd);
    }

    pthread_mutex_unlock(&s_led.mutex);
}

static void led_effect_task(void)
{
    vector<struct led_layer>::reverse_iterator rit = s_led.led_layers.rbegin();
    struct led_effect *effect = &(*rit).effect;
    effect->time += TIMER_PERIOD;

    if (led_handle_timeout(effect)) {
        led_final_write();
        return;
    }

    if (effect->period == -1) {
        return;
    }

    if (effect->stop_time != -1 && effect->time >= effect->stop_time) {
        return;
    }

    if (effect->count == -1) {
        if (effect->time % effect->period) {
            return;
        }
    } else {
        effect->count++;
    }

    if (s_led_ops[(int)(*rit).cmd].effect_task) {
        s_led_ops[(int)(*rit).cmd].effect_task(effect);
        led_final_write();
    }
}

static bool need_wait_forever()
{
    if (s_led.led_layers.empty()) {
        return true;
    }

    vector<struct led_layer>::iterator it;

    for (it = s_led.led_layers.begin(); it < s_led.led_layers.end(); it++) {
        if ((*it).effect.period != -1) {
            return false;
        }
    }

    return true;
}

static bool led_wait_new_command()
{
    bool ret = false;
    pthread_mutex_lock(&s_led.mutex);

    if (!s_led.new_command_reached) {
        if (need_wait_forever()) { // wait forever
            pthread_cond_wait(&s_led.cond, &s_led.mutex);

        } else {
            struct timespec tout;
            clock_gettime(CLOCK_REALTIME, &tout);
            tout.tv_nsec += 1000000 * TIMER_PERIOD;

            if (tout.tv_nsec > 1000000000) {
                tout.tv_sec += 1;
                tout.tv_nsec -= 1000000000;
            }

            pthread_cond_timedwait(&s_led.cond, &s_led.mutex, &tout);
        }
    }

    if (s_led.new_command_reached) {
        s_led.new_command_reached = false;
        ret = true;
    }

    pthread_mutex_unlock(&s_led.mutex);
    return ret;
}

static bool is_sleep_mode()
{
    for (auto &layer : s_led.led_layers) {
        if (LedState::LED_SLEEP_MODE == layer.cmd) {
            return true;
        }
    }

    return false;
}

static void *led_task(void *param)
{
    prctl(PR_SET_NAME,"led_task");

    while (true) {
        // check every layer color
        if (led_wait_new_command()) {
            led_handle_command_queue();
        } else {
            led_effect_task();
        }
    }
}

int rk_led_init(void)
{
    int ret = 0;
    memset(&s_led, 0x00, sizeof(s_led));
    ret = led_open();

    if (ret < 0) {
        goto quit;
    }

    ret = pthread_mutex_init(&s_led.mutex, NULL);

    if (ret) {
        err("error in [%s]:init mutex fail, err is:%d\n", __FUNCTION__, ret);
        goto quit;
    }

    ret = pthread_cond_init(&s_led.cond, NULL);

    if (ret) {
        err("error in [%s]:init condition fail, err is:%d\n", __FUNCTION__, ret);
        pthread_mutex_destroy(&s_led.mutex);
        goto quit;
    }

    ret = pthread_create(&s_led.tid, NULL, led_task, NULL);

    if (ret) {
        err("error in [%s]:create thread fail, err is:%d\n", __FUNCTION__, ret);
        pthread_cond_destroy(&s_led.cond);
        pthread_mutex_destroy(&s_led.mutex);
        goto quit;
    }

    return 0;
quit:
    led_close();
    return -1;
}

int rk_led_exit(void)
{
    pthread_cancel(s_led.tid);
    pthread_mutex_destroy(&s_led.mutex);
    pthread_cond_destroy(&s_led.cond);
    led_close();
    return 0;
}

int rk_led_control(LedState cmd, void *data, int len)
{
    if ((int)cmd < 0 || (int)cmd >= (int)(ARRAY_SIZE(s_led_ops))) {
        err("command is invalid: %d\n", (int)cmd);
        return -1;
    }

    dbg("received command: %s\n", s_led_ops[(int)cmd].name);
    pthread_mutex_lock(&s_led.mutex);

    if (is_sleep_mode() && cmd != LedState::LED_CLOSE_A_LAYER && cmd != LedState::LED_OTA_DOING
        && cmd != LedState::LED_ALL_OFF) {
        dbg("s_led in sleep mode, return\n");
        pthread_mutex_unlock(&s_led.mutex);
        return 0;
    }

    switch (cmd) {
        case LedState::LED_PWMR_SET:
           if (dprintf(s_led.leds_r_fd, "%d", *(int *)data) < 0)
                err("write led_r_fd error\n");
           goto exit;
        case LedState::LED_PWMG_SET:
            if (dprintf(s_led.leds_g_fd, "%d", *(int *)data) < 0)
                err("write led_g_fd error\n");
           goto exit;
        case LedState::LED_PWMB_SET:
            if (dprintf(s_led.leds_b_fd, "%d", *(int *)data) < 0)
                err("write led_b_fd error\n");
           goto exit;
        default:
            break;
    }

    struct led_command led_cmd;

    led_cmd.cmd = cmd;

    if (data) {
        led_cmd.data = *(int *)data;
    }

    s_led.led_cmd.push_back(led_cmd);
    s_led.new_command_reached = true;
    pthread_cond_signal(&s_led.cond);
exit:
    pthread_mutex_unlock(&s_led.mutex);
    return 0;
}
