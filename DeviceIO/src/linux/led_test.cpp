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
#include <stdio.h>
#include <unistd.h>

using namespace DeviceIOFramework;

int led_test(int argc, char* argv[]) {
    while (true) {

        printf("check led LED_NET_WAIT_CONNECT\n\n");
        DeviceIo::getInstance()->controlLed(LedState::LED_NET_WAIT_CONNECT);
        sleep(5);

        printf("check led LED_NET_DO_CONNECT\n\n");
        DeviceIo::getInstance()->controlLed(LedState::LED_NET_DO_CONNECT);
        sleep(5);

        printf("check led LED_NET_CONNECT_FAILED\n\n");
        DeviceIo::getInstance()->controlLed(LedState::LED_NET_CONNECT_FAILED);
        sleep(5);

        printf("check led LED_NET_CONNECT_SUCCESS\n\n");
        DeviceIo::getInstance()->controlLed(LedState::LED_NET_CONNECT_SUCCESS);
        sleep(5);

        printf("check led LED_WAKE_UP_DOA\n\n");
        int angle = 90;
        DeviceIo::getInstance()->controlLed(LedState::LED_WAKE_UP_DOA, &angle, sizeof(angle));
        sleep(5);

        printf("check led LED_WAKE_UP\n\n");
        DeviceIo::getInstance()->controlLed(LedState::LED_WAKE_UP);
        sleep(5);

        printf("check led LED_SPEECH_PARSE\n\n");
        DeviceIo::getInstance()->controlLed(LedState::LED_SPEECH_PARSE);
        sleep(5);

        printf("check led LED_PLAY_TTS\n\n");
        DeviceIo::getInstance()->controlLed(LedState::LED_PLAY_TTS);
        sleep(5);

        printf("check led LED_PLAY_RESOURCE\n\n");
        DeviceIo::getInstance()->controlLed(LedState::LED_PLAY_RESOURCE);
        sleep(5);

        printf("check led LED_BT_WAIT_PAIR\n\n");
        DeviceIo::getInstance()->controlLed(LedState::LED_BT_WAIT_PAIR);
        sleep(5);

        printf("check led LED_BT_DO_PAIR\n\n");
        DeviceIo::getInstance()->controlLed(LedState::LED_BT_DO_PAIR);
        sleep(5);

        printf("check led LED_BT_PAIR_FAILED\n\n");
        DeviceIo::getInstance()->controlLed(LedState::LED_BT_PAIR_FAILED);
        sleep(5);

        printf("check led LED_BT_PAIR_SUCCESS\n\n");
        DeviceIo::getInstance()->controlLed(LedState::LED_BT_PAIR_SUCCESS);
        sleep(5);

        printf("check led LED_BT_PLAY\n\n");
        DeviceIo::getInstance()->controlLed(LedState::LED_BT_PLAY);
        sleep(5);

        printf("check led LED_VOLUME 50\n\n");
        int volume = 50;
        DeviceIo::getInstance()->controlLed(LedState::LED_VOLUME, &volume, sizeof(volume));
        sleep(5);

        printf("check led LED_MICMUTE\n\n");
        DeviceIo::getInstance()->controlLed(LedState::LED_MICMUTE);
        sleep(5);

        printf("check led LED_DISABLE_MIC\n\n");
        DeviceIo::getInstance()->controlLed(LedState::LED_DISABLE_MIC);
        sleep(5);

        /*remove begin*/
        LedState layer;

        printf("[remove]check led LED_DISABLE_MIC\n\n");
        layer = LedState::LED_DISABLE_MIC;
        DeviceIo::getInstance()->controlLed(LedState::LED_CLOSE_A_LAYER, &layer, sizeof(int));
        sleep(5);

        printf("[remove]check led LED_MICMUTE\n\n");
        layer = LedState::LED_MICMUTE;
        DeviceIo::getInstance()->controlLed(LedState::LED_CLOSE_A_LAYER, &layer, sizeof(int));
        sleep(5);

        printf("[remove]check led LED_VOLUME\n\n");
        layer = LedState::LED_VOLUME;
        DeviceIo::getInstance()->controlLed(LedState::LED_CLOSE_A_LAYER, &layer, sizeof(int));
        sleep(5);

        printf("[remove]check led LED_BT_PLAY\n\n");
        layer = LedState::LED_BT_PLAY;
        DeviceIo::getInstance()->controlLed(LedState::LED_CLOSE_A_LAYER, &layer, sizeof(int));
        sleep(5);

        printf("[remove]check led LED_BT_PAIR_SUCCESS\n\n");
        layer = LedState::LED_BT_PAIR_SUCCESS;
        DeviceIo::getInstance()->controlLed(LedState::LED_CLOSE_A_LAYER, &layer, sizeof(int));
        sleep(5);

        printf("[remove]check led LED_BT_PAIR_FAILED\n\n");
        layer = LedState::LED_BT_PAIR_FAILED;
        DeviceIo::getInstance()->controlLed(LedState::LED_CLOSE_A_LAYER, &layer, sizeof(int));
        sleep(5);

        printf("[remove]check led LED_WAKE_UP_DOA\n\n");
        layer = LedState::LED_WAKE_UP_DOA;
        DeviceIo::getInstance()->controlLed(LedState::LED_CLOSE_A_LAYER, &layer, sizeof(int));
        sleep(5);

        printf("[remove]check led LED_NET_DO_CONNECT\n\n");
        layer = LedState::LED_NET_DO_CONNECT;
        DeviceIo::getInstance()->controlLed(LedState::LED_CLOSE_A_LAYER, &layer, sizeof(int));
        sleep(5);

        printf("check led LED_ALL_OFF\n\n");
        DeviceIo::getInstance()->controlLed(LedState::LED_ALL_OFF);
        sleep(10);
    }

    return 0;
}



