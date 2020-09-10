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


#ifndef RK_FRAMEWORK_LED_H_
#define RK_FRAMEWORK_LED_H_

#include <DeviceIo/DeviceIo.h>

using DeviceIOFramework::LedState;

int rk_led_init(void);

int rk_led_exit(void);

int rk_led_control(LedState cmd, void* data, int len);

#endif //RK_FRAMEWORK_LED_H_
