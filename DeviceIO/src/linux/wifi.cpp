/*
 * Copyright (c) 2018 Rockchip, Inc. All Rights Reserved.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may owifiain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include "wifi.h"
#include "Logger.h"
#include "WifiUtil.h"
#include "shell.h"

#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <unistd.h>
#include <signal.h>
#include <iostream>
#include <fstream>
#include <string>

using namespace std;
using DeviceIOFramework::DeviceIo;
using DeviceIOFramework::DeviceInput;
using DeviceIOFramework::wifi_config;

#define dbg(fmt, ...) APP_DEBUG("[deviceio wifi debug ]" fmt, ##__VA_ARGS__)
#define err(fmt, ...) APP_ERROR("[deviceio wifi error ]" fmt, ##__VA_ARGS__)

#define WIFI_STATUS_PATH "/tmp/wifi_status.txt"

static int get_wifi_mac(char *wifi_mac)
{
    int sock_mac;
    struct ifreq ifr_mac;
    char mac_addr[18] = {0};

    sock_mac = socket(AF_INET, SOCK_STREAM, 0);

    if (sock_mac == -1) {
        APP_ERROR("create mac socket failed.");
        return -1;
    }

    memset(&ifr_mac, 0, sizeof(ifr_mac));
    strncpy(ifr_mac.ifr_name, "wlan0", sizeof(ifr_mac.ifr_name) - 1);

    if ((ioctl(sock_mac, SIOCGIFHWADDR, &ifr_mac)) < 0) {
        APP_ERROR("Mac socket ioctl failed.");
        return -1;
    }

    sprintf(mac_addr, "%02X:%02X:%02X:%02X:%02X:%02X",
            (unsigned char)ifr_mac.ifr_hwaddr.sa_data[0],
            (unsigned char)ifr_mac.ifr_hwaddr.sa_data[1],
            (unsigned char)ifr_mac.ifr_hwaddr.sa_data[2],
            (unsigned char)ifr_mac.ifr_hwaddr.sa_data[3],
            (unsigned char)ifr_mac.ifr_hwaddr.sa_data[4],
            (unsigned char)ifr_mac.ifr_hwaddr.sa_data[5]);

    close(sock_mac);
    strncpy(wifi_mac, mac_addr, 18);
    APP_DEBUG("the wifi mac : %s\r\n", wifi_mac);

    return 0;
}

static int get_wifi_ip(char *wifi_ip)
{
    char system_cmd[512] = {0};
    char reply[128] = {"\0"};
    FILE *fp = NULL;
    char *p = NULL;

    snprintf(system_cmd, 512 - 1,
             "wpa_cli -iwlan0 -p/var/run/wpa_supplicant status > %s", WIFI_STATUS_PATH);
    system(system_cmd);

    fp = fopen(WIFI_STATUS_PATH, "r");

    if (!fp) {
        printf("open get wifi_status file error!");
        return -1;
    }

    memset(reply, 0, sizeof(reply));

    while (fgets(reply, sizeof(reply), fp)) {
        p = strstr(reply, "ip_address=");

        if (!p) {
            continue;
        }

        p = p + strlen("ip_address=");
        strncpy(wifi_ip, p, strlen(p) - 1);
        p = NULL;
        break;
    }

    fclose(fp);

    APP_DEBUG("the ip address : %s, len : %d \n", wifi_ip, strlen(wifi_ip));
    return 0;

}

static int get_wifi_ssid(char *wifi_ssid)
{
    char system_cmd[512] = {0};
    char reply[128] = {"\0"};
    FILE *fp = NULL;
    char *p = NULL;

    snprintf(system_cmd, 512 - 1,
             "wpa_cli -iwlan0 -p/var/run/wpa_supplicant status > %s", WIFI_STATUS_PATH);
    system(system_cmd);

    fp = fopen(WIFI_STATUS_PATH, "r");

    if (!fp) {
        printf("open get wifi_status file error!");
        return -1;
    }

    memset(reply, 0, sizeof(reply));

    while (fgets(reply, sizeof(reply), fp)) {
        if (strstr(reply, "bssid=")) {
            continue;
        }

        p = strstr(reply, "ssid=");

        if (!p) {
            continue;
        }

        p = p + strlen("ssid=");
        strncpy(wifi_ssid, p, strlen(p) - 1);
        p = NULL;
        break;
    }


    fclose(fp);

    APP_DEBUG("the wifi ssid : %s, len : %d \n", wifi_ssid, strlen(wifi_ssid));

    return 0;

}

static int get_wifi_bssid(char *wifi_bssid)
{
    char system_cmd[512] = {0};
    char reply[128] = {"\0"};
    FILE *fp = NULL;
    char *p = NULL;

    snprintf(system_cmd, 512 - 1,
             "wpa_cli -iwlan0 -p/var/run/wpa_supplicant status > %s", WIFI_STATUS_PATH);
    system(system_cmd);

    fp = fopen(WIFI_STATUS_PATH, "r");

    if (!fp) {
        printf("open get wifi_status file error!");
        return -1;
    }

    memset(reply, 0, sizeof(reply));

    while (fgets(reply, sizeof(reply), fp)) {
        p = strstr(reply, "bssid=");

        if (!p) {
            continue;
        }

        p = p + strlen("bssid=");
        strncpy(wifi_bssid, p, strlen(p) - 1);
        p = NULL;
        break;
    }


    fclose(fp);

    APP_DEBUG("the wifi bssid : %s\n", wifi_bssid);
    return 0;

}

static bool isWifiFirstConfig() {

    ifstream it_stream;
    int length = 0;
    string wpa_config_file = "/data/cfg/wpa_supplicant.conf";

    it_stream.open(wpa_config_file.c_str());
    if (!it_stream.is_open()) {
        APP_ERROR("wpa config file open error.");
        return false;
    }

    it_stream.seekg(0,std::ios::end);
    length = it_stream.tellg();
    it_stream.seekg(0,std::ios::beg);

    char *buffer = new char[length + 1];
    it_stream.read(buffer, length);
    it_stream.close();
    buffer[length] = 0;

    char * position = nullptr;
    position = strstr(buffer,"ssid");
    delete [] buffer;
    buffer = nullptr;

    if (nullptr == position) {
        APP_ERROR("First network config.");
        return true;
    }

    APP_INFO("Not first network config.");

    return false;
}

bool isWifiConnected() {
    char ret_buff[1024] = {0};
    bool ret;

    ret = Shell::exec("wpa_cli -iwlan0 status | grep wpa_state | awk -F '=' '{printf $2}'", ret_buff, 1024);
    if (!ret) {
        APP_ERROR("getWifiMac failed.\n");
        return false;
    }
    if (!strncmp(ret_buff, "COMPLETED", 9))
        return true;
    return false;
}

int rk_wifi_control(WifiControl cmd, void *data, int len)
{
    APP_DEBUG("controlWifi, cmd: %d\n", cmd);

    int ret = 0;

    switch (cmd) {

    case WifiControl::WIFI_OPEN:
        WifiUtil::getInstance()->start_wpa_supplicant();
        break;

    case WifiControl::WIFI_CLOSE:
        WifiUtil::getInstance()->stop_wpa_supplicant();
        break;

    case WifiControl::WIFI_CONNECT:
        WifiUtil::getInstance()->connect(data);
        break;

    case WifiControl::WIFI_DISCONNECT:
        WifiUtil::getInstance()->disconnect();
        break;

    case WifiControl::WIFI_IS_OPENED:
        break;

    case WifiControl::WIFI_IS_CONNECTED:
        return isWifiConnected();
        break;

    case WifiControl::WIFI_SCAN:
    {
		std::string list = WifiUtil::getInstance()->getWifiListJson();
		if (list.size()) {
			ret = list.size();
			strncpy((char *)data, list.c_str(), ret);
		}

		break;
    }
	case WifiControl::WIFI_GET_DEVICE_CONTEXT:
	{
		std::string Dlist = WifiUtil::getInstance()->getDeviceContextJson();
		strcpy((char *)data, Dlist.c_str());
		len = strlen(Dlist.c_str());
		break;
	}
    case WifiControl::WIFI_IS_FIRST_CONFIG:
        ret = isWifiFirstConfig();
        break;

    case WifiControl::WIFI_OPEN_AP_MODE:
        WifiUtil::getInstance()->start_ap_mode((char *)data);
        break;

    case WifiControl::WIFI_CLOSE_AP_MODE:
        WifiUtil::getInstance()->stop_ap_mode();
        break;

    case WifiControl::GET_WIFI_MAC:
        if (get_wifi_mac((char *)data) <= 0)
            ret = -1;

        break;

    case WifiControl::GET_WIFI_IP:
        if (get_wifi_ip((char *)data) <= 0)
            ret = -1;

        break;

    case WifiControl::GET_WIFI_SSID:
        if (get_wifi_ssid((char *)data) <= 0)
            ret = -1;

        break;

    case WifiControl::GET_WIFI_BSSID:
        if (get_wifi_bssid((char *)data) <= 0)
            ret = -1;

        break;
	case WifiControl::WIFI_RECOVERY:
		printf("+++++++ WIFI_RECOVERY  ++++++++++\n");
        WifiUtil::getInstance()->recovery();
		break;

    default:
        APP_DEBUG("%s, cmd <%d> is not implemented.\n", __func__, cmd);
    }

    return ret;
}
