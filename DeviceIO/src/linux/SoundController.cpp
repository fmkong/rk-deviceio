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

#include "SoundController.h"
#include "shell.h"

namespace DeviceIOFramework {

SoundController *SoundController::mSoundController = NULL;
pthread_once_t SoundController::m_initOnce = PTHREAD_ONCE_INIT;
pthread_once_t SoundController::m_destroyOnce = PTHREAD_ONCE_INIT;

SoundController::SoundController() {
    pthread_mutex_init(&m_mutex, NULL);
    m_destroyOnce = PTHREAD_ONCE_INIT;
}

SoundController::~SoundController() {
    pthread_mutex_destroy(&m_mutex);
    m_initOnce = PTHREAD_ONCE_INIT;
}

SoundController *SoundController::getInstance() {
    pthread_once(&m_initOnce, &SoundController::init);
    return mSoundController;
}

void SoundController::init() {
    if (mSoundController == NULL) {
        mSoundController = new SoundController();
    }
}

void SoundController::destory() {
    if (mSoundController) {
        delete mSoundController;
        mSoundController = NULL;
    }
}

void SoundController::release() {
    pthread_once(&m_destroyOnce, SoundController::destory);
}

void SoundController::audioPlay(const std::string &source,
                                bool needFocus,
                                void (*start_callback)(void *arg),
                                void *start_cb_arg,
                                void (*finish_callback)()) {
    /*char cmd_line[512];

    sprintf(cmd_line, "aplay -t raw -c 1 -f S16_LE -r 24000 %s&", source.c_str());
    Shell::system(cmd_line);*/
}

void SoundController::wakeUp() {
    audioPlay("./appresources/du.pcm", false, NULL, NULL, NULL);
}

void SoundController::linkStartFirst() {
    audioPlay("./appresources/link_start_first.pcm", true, NULL, NULL, NULL);
}

void SoundController::linkStart() {
    audioPlay("./appresources/link_start.pcm", true, NULL, NULL, NULL);
}

void SoundController::linkConnecting() {
    audioPlay("./appresources/link_connecting.pcm", true, NULL, NULL, NULL);
}

void SoundController::linkSuccess(void(*callback)()) {
    audioPlay("./appresources/link_success.pcm", true, NULL, NULL, callback);
}

void SoundController::linkFailedPing(void(*callback)()) {
    audioPlay("./appresources/link_failed_ping.pcm", true, NULL, NULL, NULL);
}

void SoundController::linkFailedIp(void(*callback)()) {
    audioPlay("./appresources/link_failed_ip.pcm", true, NULL, NULL, callback);
}

void SoundController::linkExit(void(*callback)()) {
    audioPlay("./appresources/link_exit.pcm", true, NULL, NULL, callback);
}

void SoundController::reLink() {
    audioPlay("./appresources/re_link.pcm", true, NULL, NULL, NULL);
}

void SoundController::reLinkSuccess(void(*callback)()) {
    audioPlay("./appresources/re_link_success.pcm", true, NULL, NULL, callback);
}

void SoundController::reLinkFailed() {
    audioPlay("./appresources/re_link_failed.pcm", true, NULL, NULL, NULL);
}

void SoundController::btUnpaired() {
    audioPlay("./appresources/bt_unpaired.pcm", true, NULL, NULL, NULL);
}

void SoundController::btPairSuccess(void(*callback)()) {
    audioPlay("./appresources/bt_pair_success.pcm", true, NULL, NULL, callback);
}

void SoundController::btPairFailedPaired() {
    audioPlay("./appresources/bt_pair_failed_paired.pcm", true, NULL, NULL, NULL);
}

void SoundController::btPairFailedOther() {
    audioPlay("./appresources/bt_pair_failed_other.pcm", true, NULL, NULL, NULL);
}

void SoundController::btDisconnect(void(*callback)()) {
    audioPlay("./appresources/bt_disconnect.pcm", true, NULL, NULL, callback);
}

void SoundController::networkConnectFailed() {
    audioPlay("./appresources/network_connect_failed.pcm", true, NULL, NULL, NULL);
}

void SoundController::networkSlow() {
    audioPlay("./appresources/network_slow.pcm", true, NULL, NULL, NULL);
}

void SoundController::openBluetooth(void(*callback)(void *arg), void *arg) {
    audioPlay("./appresources/open_bluetooth.pcm", true, callback, arg, NULL);
}

void SoundController::closeBluetooth(void(*callback)(void *arg), void *arg) {
    audioPlay("./appresources/close_bluetooth.pcm", true, callback, arg, NULL);
}

void SoundController::volume() {
}

void SoundController::serverConnecting() {
    audioPlay("./appresources/server_connecting.pcm", false, NULL, NULL, NULL);
}

void SoundController::serverConnectFailed() {
    audioPlay("./appresources/server_connect_failed.pcm", false, NULL, NULL, NULL);
}

void SoundController::bleNetworkConfig() {
    audioPlay("./appresources/ble_network_config.pcm", false, NULL, NULL, NULL);
}

void SoundController::accountUnbound(void(*callback)()) {
    audioPlay("./appresources/unbound.pcm", true, NULL, NULL, callback);
}

void SoundController::hotConnected() {
    audioPlay("./appresources/hot_connected.pcm", false, NULL, NULL, NULL);
}

void SoundController::waitLogin() {
    audioPlay("./appresources/wait_login.pcm", false, NULL, NULL, NULL);
}

}  // namespace application
