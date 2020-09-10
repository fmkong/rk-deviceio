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

#include "DeviceIo/DeviceIo.h"

#include <linux/rtc.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <cmath>
#include <pthread.h>
#include "alsa/asoundlib.h"
#include "led.h"
#include "key.h"
#include "wifi.h"
#include "Logger.h"
#include "rtc.h"
#include "shell.h"
#include "power.h"
#include "../../bluetooth/bluetooth.h"
#include "DeviceIo/NetLinkWrapper.h"
#include "DeviceIo/Rk_system.h"
#include "DeviceIo/RkBtBase.h"

using namespace std;


namespace DeviceIOFramework {

#define USER_VOL_MIN                    0   // keep some voice
#define USER_VOL_MAX                    100

// #define AUDIO_MIN_VOLUME                0
// #define AUDIO_MAX_VOLUME                100

#define SOFTVOL /*should play a music before ctl the softvol*/
#define SOFTVOL_CARD "default"
#define SOFTVOL_ELEM "name='Master Playback Volume'"

typedef struct {
    int     volume;
    bool    is_mute;
} user_volume_t;

// static snd_mixer_t*      mixer_fd   = nullptr;
// static snd_mixer_elem_t* mixer_elem = nullptr;
static user_volume_t    user_volume = {0, false};
static pthread_mutex_t  user_volume_mutex;

static int cset(char *value_string, int roflag)
{
    int err;
    int ret = 0;
    static snd_ctl_t *handle = NULL;
    snd_ctl_elem_info_t *info;
    snd_ctl_elem_id_t *id;
    snd_ctl_elem_value_t *control;
    snd_ctl_elem_info_alloca(&info);
    snd_ctl_elem_id_alloca(&id);
    snd_ctl_elem_value_alloca(&control);
    char card[64] = SOFTVOL_CARD;
    int keep_handle = 0;

    if (snd_ctl_ascii_elem_id_parse(id, SOFTVOL_ELEM)) {
        fprintf(stderr, "Wrong control identifier: %s\n", SOFTVOL_ELEM);
        return -EINVAL;
    }

    if (handle == NULL &&
        (err = snd_ctl_open(&handle, card, 0)) < 0) {
        APP_ERROR("Control %s open error: %d\n", card, err);
        return err;
    }
    snd_ctl_elem_info_set_id(info, id);
    if ((err = snd_ctl_elem_info(handle, info)) < 0) {
        APP_ERROR("Cannot find the given element from control %s\n", card);
        if (! keep_handle) {
            snd_ctl_close(handle);
            handle = NULL;
        }
        return err;
    }
    snd_ctl_elem_info_get_id(info, id);     /* FIXME: Remove it when hctl find works ok !!! */
    if (!roflag) {
        snd_ctl_elem_value_set_id(control, id);
        if ((err = snd_ctl_elem_read(handle, control)) < 0) {
            APP_ERROR("Cannot read the given element from control %s\n", card);
            if (! keep_handle) {
                snd_ctl_close(handle);
                handle = NULL;
            }
            return err;
        }
        err = snd_ctl_ascii_value_parse(handle, control, info, value_string);
        if (err < 0) {
            APP_ERROR("Control %s parse error: %d\n", card, err);
            if (!keep_handle) {
                snd_ctl_close(handle);
                handle = NULL;
            }
            return  err;
        }
        if ((err = snd_ctl_elem_write(handle, control)) < 0) {
            APP_ERROR("Control %s element write error: %d; errno: %d\n", card,
                      err, errno);
            if (!keep_handle) {
                snd_ctl_close(handle);
                handle = NULL;
            }
            return err;
        } else {
            APP_INFO("Control %s element write volume %s successfully\n", card,
                     value_string);
        }
        system("alsactl store --file=/data/cfg/asound.state");
    } else {
        int vol_l, vol_r;
        snd_ctl_elem_value_set_id(control, id);
        if ((err = snd_ctl_elem_read(handle, control)) < 0) {
            APP_ERROR("Cannot read the given element from control %s\n", card);
            if (! keep_handle) {
                snd_ctl_close(handle);
                handle = NULL;
            }
            return err;
        }
        vol_l = snd_ctl_elem_value_get_integer(control, 0);
        vol_r = snd_ctl_elem_value_get_integer(control, 1);
        APP_ERROR("%s:  cget %d, %d!\n", __func__, vol_l, vol_r);
        ret = (vol_l + vol_r) >> 1;
    }

    if (! keep_handle) {
        snd_ctl_close(handle);
        handle = NULL;
    }
    return ret;
}

static void softvol_set(int vol) {
    char value[128] = {0};

    sprintf(value, "%d%%", vol);
    int ret = cset(value, 0);
    int trytimes = 100;
    // try again, often fail first time
    while (ret && trytimes-- > 0) {
       usleep(100 * 1000);
       ret = cset(value, 0);
    }
}

static int softvol_get() {
    return cset(NULL, 1);
}

#ifndef SOFTVOL
static void mixer_exit() {
    if (mixer_fd != nullptr) {
        snd_mixer_close(mixer_fd);
        mixer_fd = nullptr;
    }

    mixer_elem = nullptr;
}

static int mixer_init(const char* card, const char* elem) {
#ifdef SOFTVOL
    APP_ERROR("mixer init\n");
    return 0;
#endif

    const char* _card = card ? card : "default";
#ifdef MTK8516
    const char* _elem = elem ? elem : "TAS5760";
#else
    const char* _elem = elem ? elem : "Playback";
#endif

    // open mixer
    if (snd_mixer_open(&mixer_fd, 0) < 0) {
        APP_ERROR("%s: snd_mixer_open error!\n", __func__);
        goto failed;
    }

    // Attach an HCTL to an opened mixer
    if (snd_mixer_attach(mixer_fd, _card) < 0) {
        APP_ERROR("%s: snd_mixer_attach error!\n", __func__);
        goto failed;
    }

    // register mixer
    if (snd_mixer_selem_register(mixer_fd, nullptr, nullptr) < 0) {
        APP_ERROR("%s: snd_mixer_selem_register error!\n", __func__);
        goto failed;
    }

    // load mixer
    if (snd_mixer_load(mixer_fd) < 0) {
        APP_ERROR("%s: snd_mixer_load error!\n", __func__);
        goto failed;
    }

    // each for
    for (mixer_elem = snd_mixer_first_elem(mixer_fd); mixer_elem;
            mixer_elem = snd_mixer_elem_next(mixer_elem)) {
        if (snd_mixer_elem_get_type(mixer_elem) == SND_MIXER_ELEM_SIMPLE
                && snd_mixer_selem_is_active(mixer_elem)) {
            if (strcmp(snd_mixer_selem_get_name(mixer_elem), _elem) == 0) {
                return 0;
            }
        }
    }

    APP_ERROR("%s: Cannot find master mixer elem!\n", __func__);
failed:
    mixer_exit();
    return -1;
}

static int mixer_set_volume(unsigned int user_vol) {
    long mix_vol;

    mix_vol = (user_vol > AUDIO_MAX_VOLUME) ? AUDIO_MAX_VOLUME : user_vol;
    mix_vol = (mix_vol < AUDIO_MIN_VOLUME) ? AUDIO_MIN_VOLUME : user_vol;

    if (mixer_elem == nullptr) {
        APP_INFO("%s: mixer_elem is NULL! mixer_init() will be called.\n", __func__);
        mixer_init(nullptr, nullptr);
    } else if (mixer_elem != nullptr) {
        snd_mixer_selem_set_playback_volume_range(mixer_elem, AUDIO_MIN_VOLUME, AUDIO_MAX_VOLUME);
        snd_mixer_selem_set_playback_volume_all(mixer_elem, mix_vol);
    }

    return 0;
}

static unsigned int mixer_get_volume() {
    long int alsa_left = AUDIO_MIN_VOLUME, alsa_right = AUDIO_MIN_VOLUME;
    int mix_vol = 0;

    if (mixer_elem == nullptr) {
        APP_INFO("%s: mixer_elem is NULL! mixer_init() will be called.\n", __func__);
        mixer_init(nullptr, nullptr);
    } else if (mixer_elem != nullptr) {
        snd_mixer_selem_set_playback_volume_range(mixer_elem, AUDIO_MIN_VOLUME, AUDIO_MAX_VOLUME);
        snd_mixer_selem_get_playback_volume(mixer_elem, SND_MIXER_SCHN_FRONT_LEFT,  &alsa_left);
        snd_mixer_selem_get_playback_volume(mixer_elem, SND_MIXER_SCHN_FRONT_RIGHT, &alsa_right);

        mix_vol = (alsa_left + alsa_right) >> 1;
    }

    return mix_vol;
}
#endif

static void user_set_volume(int user_vol) {
    APP_ERROR("%s, %d\n", __func__, user_vol);
    /* set volume will unmute */
    if (user_volume.is_mute) {
        if (user_vol == 0)
            user_vol = user_volume.volume;
        user_volume.is_mute = false;
    }
    user_vol = std::min(user_vol, USER_VOL_MAX);
    user_vol = std::max(user_vol, USER_VOL_MIN);

#ifdef SOFTVOL
    softvol_set(user_vol);
    user_volume.volume = user_vol;
#else
    double k, audio_vol;
    k = (double)AUDIO_MAX_VOLUME / USER_VOL_MAX;

    audio_vol = k * user_vol;

    mixer_set_volume(audio_vol);
#endif
}

static int user_get_volume() {
    int user_vol = 0;

#ifdef SOFTVOL
    user_vol = softvol_get();
    APP_ERROR("%s: %d\n", __func__, user_vol);
    return user_vol;
#else
    double k, offset, audio_vol;
    audio_vol = mixer_get_volume();

    k = (double)(USER_VOL_MAX - USER_VOL_MIN) / (AUDIO_MAX_VOLUME - AUDIO_MIN_VOLUME);
    offset = USER_VOL_MAX - k * AUDIO_MAX_VOLUME;

    user_vol = ceil(k * audio_vol + offset);

    user_vol = (user_vol > USER_VOL_MAX) ? USER_VOL_MAX : user_vol;
    user_vol = (user_vol < USER_VOL_MIN) ? USER_VOL_MIN : user_vol;

    APP_DEBUG("[%s] audio_vol:%f  user_vol:%d\n", __FUNCTION__, audio_vol, user_vol);
#endif

    return user_vol;
}

DeviceIo* DeviceIo::m_instance = nullptr;
DeviceInNotify* DeviceIo::m_notify = nullptr;
pthread_once_t DeviceIo::m_initOnce = PTHREAD_ONCE_INIT;
pthread_once_t DeviceIo::m_destroyOnce = PTHREAD_ONCE_INIT;

DeviceIo::DeviceIo() {
    int ret = 0;

    m_notify = nullptr;

	APP_DEBUG("[%s] DeviceIo Version: %s\n", __FUNCTION__, getVersion().c_str());

    ret = rk_led_init();
    if (ret) {
        APP_ERROR("[%s] error: rk_led_init fail, err is:%d\n",  __FUNCTION__, ret);
    }
    ret = rk_key_init();
    if (ret) {
        APP_ERROR("[%s] error: rk_key_init fail, err is:%d\n",  __FUNCTION__, ret);
    }
    ret = power_init();
    if (ret) {
        APP_ERROR("[%s] error: power_init fail, err is:%d\n",  __FUNCTION__, ret);
    }

#ifdef RTC_INIT
    ret = rtc_init();
    if (ret) {
        APP_ERROR("[%s] error: rtc_init fail, err is:%d\n",  __FUNCTION__, ret);
    }
#endif

#ifndef SOFTVOL
    ret = mixer_init(nullptr, nullptr);

    if (ret) {
        APP_ERROR("[%s] error: mixer_init fail, err is:%d\n",  __FUNCTION__, ret);

        return;
    }
#endif

    ret = pthread_mutex_init(&user_volume_mutex, nullptr);

    if (ret) {
        APP_ERROR("[%s] error: pthread_mutex_init fail, err is:%d\n",  __FUNCTION__, ret);

        return;
    }

    user_volume.volume = user_get_volume();

    m_destroyOnce = PTHREAD_ONCE_INIT;
}

DeviceIo::~DeviceIo() {
    pthread_mutex_destroy(&user_volume_mutex);
    rtc_deinit();
    power_deinit();
#ifndef SOFTVOL
    mixer_exit();
#endif
    rk_led_exit();
    m_notify = nullptr;
    m_initOnce = PTHREAD_ONCE_INIT;
}

DeviceIo* DeviceIo::getInstance() {
    pthread_once(&m_initOnce, DeviceIo::init);

    return m_instance;
}

void DeviceIo::releaseInstance() {
    pthread_once(&m_destroyOnce, DeviceIo::destroy);
}

void DeviceIo::init() {
    if (m_instance == nullptr) {
        m_instance = new DeviceIo;
    }
}

void DeviceIo::destroy() {
    if (m_instance != nullptr) {
	delete m_instance;
    	m_instance = nullptr;
    }
}

void DeviceIo::setNotify(DeviceInNotify* notify) {
    if (notify) {
        m_notify = notify;
        NetLinkWrapper::getInstance()->setCallback(notify);
    }
}

DeviceInNotify* DeviceIo::getNotify() {
    return m_notify;
}

int DeviceIo::controlLed(LedState cmd, void *data, int len) {
    APP_INFO("controlLed:%d\n", cmd);
    // just for ota check, i do not like it
    static bool isOtaMode = inOtaMode();

    if (isOtaMode) {
        if (!inOtaMode()) {
            isOtaMode = false;
        }
    }

    if (isOtaMode && cmd != LedState::LED_SLEEP_MODE) {
        return 0;
    } else {
        return rk_led_control(cmd, data, len);
    }
}

int DeviceIo::controlRtc(DeviceRTC cmd, void *data, int len) {
    return rtc_control(cmd, data, len);
}

int DeviceIo::controlPower(DevicePowerSupply cmd, void *data, int len) {
    return power_supply_control(cmd, data, len);
}

int DeviceIo::controlBt(BtControl cmd, void *data, int len) {
    return rk_bt_control(cmd, data, len);
}

int DeviceIo::controlWifi(WifiControl cmd, void *data, int len) {
    return rk_wifi_control(cmd, data, len);
}

int DeviceIo::transmitInfrared(std::string& infraredCode) {
    return 0;
}

int DeviceIo::openMicrophone() {
    return 0;
}

int DeviceIo::closeMicrophone() {
    return 0;
}

bool DeviceIo::isMicrophoneOpened() {
    return true;
}

typedef enum {
	ADC_VOLUME_LEVEL_30 = 0,// voluem < 30 时，设置adc级别
	ADC_VOLUME_LEVEL_50,
	ADC_VOLUME_LEVEL_60,
	ADC_VOLUME_LEVEL_70,
	ADC_VOLUME_LEVEL_99
} ADC_VOLUME_LEVEL_T;

static ADC_VOLUME_LEVEL_T getADCVolumeLevel(int volume) {
	if (volume >= 70)
		return ADC_VOLUME_LEVEL_99;
	else if (volume >= 60)
		return ADC_VOLUME_LEVEL_70;
	else if (volume >= 50)
		return ADC_VOLUME_LEVEL_60;
	else if (volume >= 30)
		return ADC_VOLUME_LEVEL_50;
	else
		return ADC_VOLUME_LEVEL_30;
}

static void setADCVolumeLevel(ADC_VOLUME_LEVEL_T level) {
	switch(level) {
		case ADC_VOLUME_LEVEL_99: {
			system("sh /oem/adc-gain/hisense-acodec-gain--99voicelevel.sh");
			break;
		}
		case ADC_VOLUME_LEVEL_70: {
			system("sh /oem/adc-gain/hisense-acodec-gain--70voicelevel.sh");
			break;
		}
		case ADC_VOLUME_LEVEL_60: {
			system("sh /oem/adc-gain/hisense-acodec-gain--60voicelevel.sh");
			break;
		}
		case ADC_VOLUME_LEVEL_50: {
			system("sh /oem/adc-gain/hisense-acodec-gain--50voicelevel.sh");
			break;
		}
		case ADC_VOLUME_LEVEL_30: {
			system("sh /oem/adc-gain/hisense-acodec-gain--30voicelevel.sh");
			break;
		}
	}
}

void DeviceIo::setVolume(int vol, int track_id) {
    pthread_mutex_lock(&user_volume_mutex);
	ADC_VOLUME_LEVEL_T old_level = getADCVolumeLevel(user_volume.volume);
	ADC_VOLUME_LEVEL_T new_level = getADCVolumeLevel(vol);
	// 增大音量，先调adc，再调音量
	if (old_level < new_level) {
		setADCVolumeLevel(new_level);
	}
    user_set_volume(vol);
	// 减小音量，先调音量，再调adc
	if (old_level > new_level) {
		setADCVolumeLevel(new_level);
	}
    pthread_mutex_unlock(&user_volume_mutex);
}

int DeviceIo::getVolume(int track_id) {
    int user_vol;

    pthread_mutex_lock(&user_volume_mutex);

    if (user_volume.is_mute) {
        user_vol = 0;
    } else {
        user_vol = user_volume.volume;
    }

    pthread_mutex_unlock(&user_volume_mutex);

    return user_vol;
}

int DeviceIo::setMute(bool mute) {
    int ret = -1;

    pthread_mutex_lock(&user_volume_mutex);

    if (mute && !user_volume.is_mute) {
        user_volume.is_mute = true;
#ifdef SOFTVOL
        softvol_set(0);
#else
        mixer_set_volume(0);
#endif
        ret = 0;
    } else if (!mute && user_volume.is_mute) {
        /* set volume will unmute */
        user_set_volume(0);
        ret = 0;
    }

    pthread_mutex_unlock(&user_volume_mutex);

    return ret;
}

bool DeviceIo::isMute() {
    bool ret;

    pthread_mutex_lock(&user_volume_mutex);
    ret = user_volume.is_mute;
    pthread_mutex_unlock(&user_volume_mutex);

    return ret;
}

int DeviceIo::getAngle() {
    return 0;
}

bool DeviceIo::getSn(char *sn)
{
#define VENDOR_REQ_TAG		0x56524551
#define VENDOR_READ_IO		_IOW('v', 0x01, unsigned int)
#define VENDOR_SN_ID		1

typedef     unsigned short      uint16;
typedef     unsigned int        uint32;
typedef     unsigned char       uint8;

    struct rk_vendor_req {
        uint32 tag;
        uint16 id;
        uint16 len;
        uint8 data[1024];
    };

    int ret ;
    uint8 p_buf[100]; /* malloc req buffer or used extern buffer */
    struct rk_vendor_req *req;

    req = (struct rk_vendor_req *)p_buf;
    memset(p_buf, 0, 100);
    int sys_fd = open("/dev/vendor_storage", O_RDWR, 0);
    if(sys_fd < 0){
        printf("vendor_storage open fail\n");
        return false;
    }

    req->tag = VENDOR_REQ_TAG;
    req->id = VENDOR_SN_ID;
    req->len = 32;

    ret = ioctl(sys_fd, VENDOR_READ_IO, req);

    close(sys_fd);

    if(ret){
        printf("vendor read error %d\n", ret);
        return false;
    }

    memcpy(sn, req->data, req->len);

    return true;
}

bool DeviceIo::setSn(char * sn) {
    return false;
}
bool DeviceIo::getPCB(char *sn){
    return false;
}

bool DeviceIo::setPCB(char *sn){
    return false;
}

std::string DeviceIo::getChipID() {
    char ret[1024] = {0};
    std::string chipid;

    Shell::exec("cat /proc/cpuinfo | grep Serial | awk -F ': ' '{printf $2}'", ret, 1024);
    chipid = ret;
    return chipid;
}

std::string DeviceIo::getVersion() {
	char version[64] = {0};

	RK_read_version(version, 64);
	return version;
}

int DeviceIo::setHostName(const char *name, size_t len) {
	printf("%s:%d",name, len);
    sethostname(name, len);
}

int DeviceIo::getHostName(char *name, size_t len) {
    gethostname(name, len);
}

bool DeviceIo::inOtaMode() {
    return false;
}

void DeviceIo::rmOtaFile() {

}

bool DeviceIo::isHeadPhoneInserted() {
    char ret_buff[1024] = {0};
    bool ret;

    ret = Shell::exec("cat /sys/devices/platform/ff560000.acodec/rk3308-acodec-dev/dac_output",ret_buff, 1024);
    if(!ret){
        APP_ERROR("cat dac_output failed.\n");
        return false;
    }
    if (strstr(ret_buff, "hp out"));
        return true;
    return false;
}

bool DeviceIo::startNetworkConfig(int timeout) {
    NetLinkWrapper::getInstance()->startNetworkConfig(timeout);
}

bool DeviceIo::stopNetworkConfig() {
    NetLinkWrapper::getInstance()->stopNetworkConfig();
}

bool DeviceIo::startNetworkRecovery() {
    NetLinkWrapper::getInstance()->startNetworkRecovery();
}

bool DeviceIo::stopNetworkRecovery() {
    NetLinkWrapper::getInstance()->stopNetworkRecovery();
}

void DeviceIo::initBTForHis() {
    NetLinkWrapper::getInstance()->initBTForHis();
}

NetLinkNetworkStatus DeviceIo::getNetworkStatus() const{
    return NetLinkWrapper::getInstance()->getNetworkStatus();
}

void DeviceIo::poweroff() {
    Shell::system("poweroff");
}

void DeviceIo::factoryReset() {
    Shell::system("recoverySystem reset &");
}

bool DeviceIo::OTAUpdate(std::string path) {
    string cmd = "recoverySystem ota ";

    cmd += path;
    cmd += " &";
    Shell::system(cmd.c_str());
    return true;
}

void DeviceIo::suspend() {
    Shell::system("echo mem > /sys/power/state");
}

bool DeviceIo::setEQParameter(std::string EQBinDir) {
    if (access(EQBinDir.c_str(), F_OK))
        return false;

    std::string cmd = "cp -rf " + EQBinDir + "/* /data/cfg/eq_bin/";
    Shell::system(cmd.c_str());
    Shell::system("sync");
    return true;
}

} // namespace framework


