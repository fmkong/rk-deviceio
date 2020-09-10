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
#ifndef DEVICEIO_LINUX_POWER_H
#define DEVICEIO_LINUX_POWER_H

#define	DEVICEIO_POWER_DIRECTORY_PATH	"/sys/class/power_supply"
#define	DEVICEIO_POWER_TYPE_AC			"ac"
#define	DEVICEIO_POWER_TYPE_BATTERY		"battery"
#define	DEVICEIO_POWER_TYPE_USB			"usb"

#define DEFAULT_LOW_POWER_THRESHOLD		(0) /* 5 percent */
#define DEFAULT_CAPACITY_DETECT_PERIOD	(60) /* 60 seconds */

#define DEFAULT_TEMPERTURE_LOW_THRESHOLD	(0 * 10) /* 0 degree */
#define DEFAULT_TEMPERTURE_HIGH_THRESHOLD	(60 * 10) /* 60.0 degree */
#define DEFAULT_TEMPERTURE_DETECT_PERIOD	(60) /* 60 seconds */

#include <DeviceIo/DeviceIo.h>

using DeviceIOFramework::DevicePowerSupply;

#define BATTERY_PROP_TYPE_UNKNOWN              "Unknown"
#define BATTERY_PROP_TYPE_BATTERY              "Battery"
#define BATTERY_PROP_TYPE_UPS                  "UPS"
#define BATTERY_PROP_TYPE_MAINS                "Mains"
#define BATTERY_PROP_TYPE_USB                  "USB"
#define BATTERY_PROP_TYPE_USB_DCP              "USB_DCP"
#define BATTERY_PROP_TYPE_USB_CDP              "USB_CDP"
#define BATTERY_PROP_TYPE_USB_ACA              "USB_ACA"

#define BATTERY_PROP_STATUS_UNKNOWN            "Unknown"
#define BATTERY_PROP_STATUS_CHARGING           "Charging"
#define BATTERY_PROP_STATUS_DISCHARGING        "Discharging"
#define BATTERY_PROP_STATUS_NOT_CHARGING       "Not charging"
#define BATTERY_PROP_STATUS_FULL               "Full"

#define BATTERY_PROP_CHARGE_UNKNOWN            "Unknown"
#define BATTERY_PROP_CHARGE_NA                 "N/A"
#define BATTERY_PROP_CHARGE_TRICKLE            "Trickle"
#define BATTERY_PROP_CHARGE_FAST               "Fast"

#define BATTERY_PROP_HEALTH_UNKNOWN            "Unknown"
#define BATTERY_PROP_HEALTH_COLD               "Cold"
#define BATTERY_PROP_HEALTH_GOOD               "Good"
#define BATTERY_PROP_HEALTH_DEAD               "Dead"
#define BATTERY_PROP_HEALTH_OVER               "Over voltage"
#define BATTERY_PROP_HEALTH_OVERHEAT           "Overheat"
#define BATTERY_PROP_HEALTH_SAFETY             "Safety timer expire"
#define BATTERY_PROP_HEALTH_UNSPECIFIED        "Unspecified failure"
#define BATTERY_PROP_HEALTH_WATCHDOG           "Watchdog timer expire"

#define BATTERY_PROP_TECHNOLOGY_UNKNOWN        "Unknown"
#define BATTERY_PROP_TECHNOLOGY_NIMH           "NiMH"
#define BATTERY_PROP_TECHNOLOGY_LI_ION         "Li-ion"
#define BATTERY_PROP_TECHNOLOGY_LI_POLY        "Li-poly"
#define BATTERY_PROP_TECHNOLOGY_LIFE           "LiFe"
#define BATTERY_PROP_TECHNOLOGY_NICD           "NiCd"
#define BATTERY_PROP_TECHNOLOGY_LIMN           "LiMn"

#define BATTERY_PROP_CAPACITY_LEVEL_UNKNOWN    "Unknown"
#define BATTERY_PROP_CAPACITY_LEVEL_CRITICAL   "Critical"
#define BATTERY_PROP_CAPACITY_LEVEL_LOW        "Low"
#define BATTERY_PROP_CAPACITY_LEVEL_NORMAL     "Normal"
#define BATTERY_PROP_CAPACITY_LEVEL_HIGH       "High"
#define BATTERY_PROP_CAPACITY_LEVEL_FULL       "Full"

#define BATTERY_PROP_SCOPE_UNKNOWN             "Unknown"
#define BATTERY_PROP_SCOPE_SYSTEM              "System"
#define BATTERY_PROP_SCOPE_DEVICE              "Device"

int power_supply_control(DevicePowerSupply cmd, void *data, int len);
int power_init(void);
int power_deinit(void);

#endif //DEVICEIO_LINUX_POWER_H
