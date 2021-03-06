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

#ifndef DEVICEIO_FRAMEWORK_WIFI_H
#define DEVICEIO_FRAMEWORK_WIFI_H

#include <DeviceIo/DeviceIo.h>

using DeviceIOFramework::WifiControl;

int rk_wifi_control(WifiControl cmd, void *data, int len);

#endif //DEVICEIO_FRAMEWORK_WIFI_H
