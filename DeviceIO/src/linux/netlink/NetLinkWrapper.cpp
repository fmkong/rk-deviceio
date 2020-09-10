/*
 * Copyright (c) 2017 Rockchip, Inc. All Rights Reserved.
 *
 *	Licensed under the Apache License, Version 2.0 (the "License");
 *	you may not use this file except in compliance with the License.
 *	You may obtain a copy of the License at
 *
 *	   http://www.apache.org/licenses/LICENSE-2.0
 *
 *	Unless required by applicable law or agreed to in writing, software
 *	distributed under the License is distributed on an "AS IS" BASIS,
 *	WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *	See the License for the specific language governing permissions and
 *	limitations under the License.
 */

#include <string>
#include <fstream>
#include <unistd.h>
#include <cstring>
#include <algorithm>
#include <netdb.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <csignal>
#include <errno.h>
#include <paths.h>
#include <sys/wait.h>
#include <sys/prctl.h>

#include "TcpServer.h"
#include "UdpServer.h"
#include "../Logger.h"
#include "DeviceIo/DeviceIo.h"
#include "DeviceIo/WifiManager.h"
#include "DeviceIo/NetLinkWrapper.h"
#include "../SoundController.h"
#include "../shell.h"
#include <DeviceIo/RkBle.h>

extern "C" {
	extern int m_ping_interval;
}

namespace DeviceIOFramework {

using std::string;
using std::vector;
using std::ifstream;

NetLinkWrapper *NetLinkWrapper::s_netLinkWrapper = nullptr;
pthread_once_t	NetLinkWrapper::s_initOnce = PTHREAD_ONCE_INIT;
pthread_once_t	NetLinkWrapper::s_destroyOnce = PTHREAD_ONCE_INIT;

static string m_target_ssid;
static string m_target_pwd;
static string m_target_ssid_prefix;

static int m_network_status = 0;
static bool m_pinging = false;

NetLinkWrapper::NetLinkWrapper() : m_networkStatus{NETLINK_NETWORK_SUCCEEDED},
		m_isLoopNetworkConfig{false},
		m_isNetworkOnline{false},
		m_isFirstNetworkReady{true},
		m_isFromConfigNetwork{false},
		m_callback{NULL},
		wifi_link_state{false},
		net_link_state{false}{
	s_destroyOnce = PTHREAD_ONCE_INIT;
	m_maxPacketSize = MAX_PACKETS_COUNT;
	m_datalen = 56;
	m_nsend = 0;
	m_nreceived = 0;
	m_icmp_seq = 0;
	m_stop_network_recovery = false;
	m_operation_type = operation_type::EOperationStart;

	pthread_mutex_init(&m_ping_lock, NULL);
	init_network_config_timeout_alarm();
}

NetLinkWrapper::~NetLinkWrapper() {
	s_initOnce = PTHREAD_ONCE_INIT;
	pthread_mutex_destroy(&m_ping_lock);
}

NetLinkWrapper* NetLinkWrapper::getInstance() {
	pthread_once(&s_initOnce, &NetLinkWrapper::init);
	return s_netLinkWrapper;
}

void NetLinkWrapper::init() {
	if (s_netLinkWrapper == nullptr) {
		s_netLinkWrapper = new NetLinkWrapper();
	}
}

void NetLinkWrapper::destroy() {
	delete s_netLinkWrapper;
	s_netLinkWrapper = nullptr;
}

void NetLinkWrapper::release() {
	pthread_once(&s_destroyOnce, NetLinkWrapper::destroy);
}

void NetLinkWrapper::setCallback(INetLinkWrapperCallback *callback) {
	m_callback = callback;
}

void NetLinkWrapper::logFunction(const char* msg, ...) {
}

void NetLinkWrapper::sigalrm_fn(int sig) {
	APP_INFO("alarm is run.");

	getInstance()->m_operation_type = operation_type::EAutoEnd;

	getInstance()->stop_network_config_timeout_alarm();
	getInstance()->stop_network_config();
	getInstance()->notify_network_config_status(ENetworkConfigRouteFailed);
}

void NetLinkWrapper::init_network_config_timeout_alarm() {
	APP_INFO("set alarm.");

	signal(SIGALRM, sigalrm_fn);
}

void NetLinkWrapper::start_network_config_timeout_alarm(int timeout) {
	APP_INFO("start alarm.");

	alarm(timeout);
}

void NetLinkWrapper::stop_network_config_timeout_alarm() {
	APP_INFO("stop alarm.");

	alarm(0);
}

static string generate_ssid(void) {
	string ssid;
	char mac_address[18];

	ssid.append(NETLINK_SSID_PREFIX_ROCKCHIP);
	DeviceIo::getInstance()->controlWifi(WifiControl::GET_WIFI_MAC, mac_address, 18);
	ssid += mac_address;
	ssid.erase(std::remove(ssid.begin(), ssid.end(), ':'), ssid.end());
	return ssid;
}

/* Immediate wifi Service UUID */
#define WIFI_SERVICES_UUID		 "1B7E8251-2877-41C3-B46E-CF057C562023"
#define SECURITY_CHAR_UUID		 "CAC2ABA4-EDBB-4C4A-BBAF-0A84A5CD93A1"
#define HIDE_CHAR_UUID			 "CAC2ABA4-EDBB-4C4A-BBAF-0A84A5CD26C7"
#define SSID_CHAR_UUID			 "ACA0EF7C-EEAA-48AD-9508-19A6CEF6B356"
#define PASSWORD_CHAR_UUID		 "40B7DE33-93E4-4C8B-A876-D833B415A6CE"
#define CHECKDATA_CHAR_UUID		 "40B7DE33-93E4-4C8B-A876-D833B415C759"
#define NOTIFY_CHAR_UUID		 "8AC32D3f-5CB9-4D44-BEC2-EE689169F626"
#define NOTIFY_DESC_UUID		 "00002902-0000-1000-8000-00805f9b34fb"
#define WIFILIST_CHAR_UUID		 "8AC32D3f-5CB9-4D44-BEC2-EE689169F627"
#define DEVICECONTEXT_CHAR_UUID	 "8AC32D3f-5CB9-4D44-BEC2-EE689169F628"

#define BLE_UUID_SERVICE	"0000180A-0000-1000-8000-00805F9B34FB"
#define BLE_UUID_WIFI_CHAR	"00009999-0000-1000-8000-00805F9B34FB"

#define BT_CONFIG_FAILED 2
#define BT_CONFIG_OK 1

static pthread_t wificonfig_tid = 0;
static char wifi_ssid[256];
static char wifi_ssid_bk[256];
static char wifi_password[256];
static char wifi_password_bk[256];
static char wifi_security[256];
static char wifi_hide[256];
static char check_data[256];
static int priority = 0;
static RkBleConfig ble_cfg;
static struct wifi_config wifi_cfg;

#define HOSTNAME_MAX_LEN	250	/* 255 - 3 (FQDN) - 2 (DNS enc) */
#define BLE_CONFIG_WIFI_SUCCESS 1
#define BLE_CONFIG_WIFI_FAILED 2
#define BLE_CONFIG_WIFI_TIMEOUT 3
#define BLE_CONFIG_WIFI_WRONG_KEY_FAILED 3

#define UUID_MAX_LEN			36
#define WIFI_MSG_BUFF_LEN (20 * 1024) //max size for wifi list

char wifi_list_buf[WIFI_MSG_BUFF_LEN] = {0};
char devcontext_list_buf[WIFI_MSG_BUFF_LEN] = {0};
#define BLE_SEND_MAX_LEN (134) //(20) //(512)

static int scanr_len = 0, scanr_len_use = 0;
static int devcontext_len = 0, devcontext_len_use = 0;

extern "C" void ble_wifi_clean(void)
{
	printf("-ble_wifi_clean-\n");
	memset(&wifi_cfg, 0, sizeof(struct wifi_config));
	memset(&ble_cfg, 0, sizeof(ble_cfg));
	memset(wifi_list_buf, 0, WIFI_MSG_BUFF_LEN);
	memset(devcontext_list_buf, 0, WIFI_MSG_BUFF_LEN);
	memset(wifi_ssid, 0, sizeof(wifi_ssid));
	memset(wifi_ssid_bk, 0, sizeof(wifi_ssid_bk));
	memset(wifi_password, 0, sizeof(wifi_password));
	memset(wifi_password_bk, 0, sizeof(wifi_password_bk));
	memset(wifi_security, 0, sizeof(wifi_security));
	memset(wifi_hide, 0, sizeof(wifi_hide));
	memset(check_data, 0, sizeof(check_data));
	scanr_len = 0;
	scanr_len_use = 0;
	devcontext_len = 0;
	devcontext_len_use = 0;
}

void ble_request_data(char *uuid)
{
	int scan_retry;
	if (!strcmp(WIFILIST_CHAR_UUID, uuid)) {
		if (!scanr_len) {
			scan_retry = 3;
retry:
			memset(wifi_list_buf, 0, WIFI_MSG_BUFF_LEN);
			scanr_len = DeviceIo::getInstance()->controlWifi(WifiControl::WIFI_SCAN, wifi_list_buf, WIFI_MSG_BUFF_LEN);
			scanr_len_use = 0;
			//scanr_len = strlen(wifi_list_buf);
			if (!scanr_len) {
				printf("%s: wifi total list is null\n", __func__);
				if (scan_retry-- > 0)
					goto retry;
			} else {
				printf("%s: wifi total len: %d, context: %s.\n", __func__, scanr_len, wifi_list_buf);
			}
		}

		printf("%s: wifi use: %d, len: %d\n", __func__, scanr_len_use, scanr_len);

		//chr_write(chr, slist, (len > BLE_SEND_MAX_LEN) ? BLE_SEND_MAX_LEN : len);
		ble_cfg.len = (scanr_len > BLE_SEND_MAX_LEN) ? BLE_SEND_MAX_LEN : scanr_len;
		memset(ble_cfg.data, 0, BLE_SEND_MAX_LEN);
		memcpy(ble_cfg.data, wifi_list_buf + scanr_len_use, ble_cfg.len);
		memcpy(ble_cfg.uuid, WIFILIST_CHAR_UUID, UUID_MAX_LEN);
		DeviceIo::getInstance()->controlBt(BtControl::BT_BLE_WRITE, &ble_cfg);
		scanr_len -= ble_cfg.len;
		scanr_len_use += ble_cfg.len;
	}

	if (!strcmp(DEVICECONTEXT_CHAR_UUID, uuid)) {
		if (!devcontext_len) {
			DeviceIo::getInstance()->controlWifi(WifiControl::WIFI_GET_DEVICE_CONTEXT, devcontext_list_buf, devcontext_len);
			devcontext_len = strlen(devcontext_list_buf);
			devcontext_len_use = 0;
			printf("%s: WIFI_GET_DEVICE_CONTEXT is	%s, len = %d\n",
				__func__, devcontext_list_buf, devcontext_len);
		}
		printf("%s: devcontext use: %d, len: %d\n", __func__, devcontext_len_use, devcontext_len);
		//chr_write(chr, devicesn, (len > BLE_SEND_MAX_LEN) ? BLE_SEND_MAX_LEN : len);
		ble_cfg.len = (devcontext_len > BLE_SEND_MAX_LEN) ? BLE_SEND_MAX_LEN : devcontext_len;
		memset(ble_cfg.data, 0, BLE_SEND_MAX_LEN);
		memcpy(ble_cfg.data, devcontext_list_buf + devcontext_len_use, ble_cfg.len);
		memcpy(ble_cfg.uuid, DEVICECONTEXT_CHAR_UUID, UUID_MAX_LEN);
		DeviceIo::getInstance()->controlBt(BtControl::BT_BLE_WRITE, &ble_cfg);
		devcontext_len -= ble_cfg.len;
		devcontext_len_use += ble_cfg.len;
	}
}

void *config_wifi_thread(void)
{
	prctl(PR_SET_NAME,"config_wifi_thread");

	printf("config_wifi_thread\n");
	printf("=== wifi info ssid: %s, psk: %s, hide: %d ===\n", wifi_cfg.ssid, wifi_cfg.psk, wifi_cfg.hide);
	NetLinkWrapper::getInstance()->notify_network_config_status(ENetworkConfigIng);
	DeviceIo::getInstance()->controlWifi(WifiControl::WIFI_CONNECT, &wifi_cfg);
}

void wifi_status_callback(int status, int reason)
{
	printf("%s: status: %d.\n", __func__, status);

	if (status == NetLinkNetworkStatus::NETLINK_NETWORK_CONFIG_FAILED) {
		if (reason == 1)
			NetLinkWrapper::getInstance()->notify_network_config_status(ENetworkWifiWrongKeyFailed);
		else
			NetLinkWrapper::getInstance()->notify_network_config_status(ENetworkWifiFailed);
	} else if (status == NetLinkNetworkStatus::NETLINK_NETWORK_CONFIG_SUCCEEDED) {
		NetLinkWrapper::getInstance()->notify_network_config_status(ENetworkWifiSucceed);
	}
}

void ble_callback(char *uuid, void *data, int len)
{
	char str[120];
	memset(str, 0, 120);

	memcpy(str, data, len);
	str[len] = '\0';
	printf("chr_write_value	 %p, %d\n", data, len);

	if (!strcmp(BLE_UUID_WIFI_CHAR, uuid)) {
		strcpy(wifi_ssid, str + 20);
		strcpy(wifi_password, str + 52);
		printf("wifi ssid is %s\n", wifi_ssid);
		printf("wifi psk is %s\n", wifi_password);
		printf("wifi start: %d, end: %d %d\n", str[0], str[99], str[100]);

		for (int i = 0; i < len; i++) {
			if (!( i % 8))
				printf("\n");
			printf("0x%02x ", str[i]);
		}
		printf("\n");
		char value = 6;
		printf("start to back 6\n");
		//chr_write(chr, &value, 1);
	}

	if (!strcmp(SSID_CHAR_UUID, uuid)) {
		strcpy(wifi_ssid, str);
		//saveCheckdata(2, wifi_ssid_bk);
		strcpy(wifi_cfg.ssid, wifi_ssid);
		wifi_cfg.ssid_len = strlen(wifi_ssid);
		printf("wifi ssid is %s, len: %d\n", wifi_cfg.ssid, wifi_cfg.ssid_len);
	}

	if (!strcmp(PASSWORD_CHAR_UUID, uuid)) {
		strcpy(wifi_password, str);
		strcpy(wifi_cfg.psk, wifi_password);
		wifi_cfg.psk_len = strlen(wifi_password);
		printf("wifi pwd is %s, len: %d\n", wifi_cfg.psk, wifi_cfg.psk_len);
	}

	if (!strcmp(HIDE_CHAR_UUID, uuid)) {
		strcpy(wifi_hide, str);
		printf("wifi hide is %s\n", wifi_hide);
		wifi_cfg.hide = atoi(wifi_hide);
		printf("wifi_cfg hide is %d\n", wifi_cfg.hide);
	}

	if (!strcmp(SECURITY_CHAR_UUID, uuid)) {
		strcpy(wifi_security, str);
		if (strstr(wifi_security, "WPA"))
			strcpy(wifi_cfg.key_mgmt, "WPA-PSK");
		else if (strstr(wifi_security, "WEP"))
			strcpy(wifi_cfg.key_mgmt, "WEP");
		else if (strstr(wifi_security, "ESS") != NULL
				 && strstr(wifi_security, "WPA") == NULL)
			strcpy(wifi_cfg.key_mgmt, "NONE");

		wifi_cfg.key_len = strlen(wifi_security);
		printf("wifi sec is %s, len: %d\n", wifi_cfg.key_mgmt, wifi_cfg.key_len);
	}

	if (!strcmp(CHECKDATA_CHAR_UUID, uuid)) {
		strncpy(check_data, str, len);
		printf("check_data is  %s\n", check_data);
		printf("=== wifi info ssid: %s, psk: %s, hide: %d ===\n", wifi_cfg.ssid, wifi_cfg.psk, wifi_cfg.hide);
		wifi_cfg.wifi_status_callback = wifi_status_callback;
		pthread_create(&wificonfig_tid, NULL, config_wifi_thread, NULL);
	}
}

static RkBtContent bt_content;

//void bt_adv_set(RkBtContent *p_bt_content)
static bt_init_for_hisense(void)
{
	RkBtContent *p_bt_content;

	p_bt_content = &bt_content;
	p_bt_content->bt_name = NULL;//"HISENSE_AUDIO";

	p_bt_content->ble_content.ble_name = NULL; //"小聚音箱MINI-6666";
	p_bt_content->ble_content.server_uuid.uuid = WIFI_SERVICES_UUID;
	p_bt_content->ble_content.server_uuid.len = UUID_128;
	p_bt_content->ble_content.chr_uuid[0].uuid = SECURITY_CHAR_UUID;
	p_bt_content->ble_content.chr_uuid[0].len = UUID_128;
	p_bt_content->ble_content.chr_uuid[1].uuid = HIDE_CHAR_UUID;
	p_bt_content->ble_content.chr_uuid[1].len = UUID_128;
	p_bt_content->ble_content.chr_uuid[2].uuid = SSID_CHAR_UUID;
	p_bt_content->ble_content.chr_uuid[2].len = UUID_128;
	p_bt_content->ble_content.chr_uuid[3].uuid = PASSWORD_CHAR_UUID;
	p_bt_content->ble_content.chr_uuid[3].len = UUID_128;
	p_bt_content->ble_content.chr_uuid[4].uuid = CHECKDATA_CHAR_UUID;
	p_bt_content->ble_content.chr_uuid[4].len = UUID_128;
	p_bt_content->ble_content.chr_uuid[5].uuid = NOTIFY_CHAR_UUID;
	p_bt_content->ble_content.chr_uuid[5].len = UUID_128;
	p_bt_content->ble_content.chr_uuid[6].uuid = NOTIFY_DESC_UUID;
	p_bt_content->ble_content.chr_uuid[6].len = UUID_128;
	p_bt_content->ble_content.chr_uuid[7].uuid = WIFILIST_CHAR_UUID;
	p_bt_content->ble_content.chr_uuid[7].len = UUID_128;
	p_bt_content->ble_content.chr_uuid[8].uuid = DEVICECONTEXT_CHAR_UUID;
	p_bt_content->ble_content.chr_uuid[8].len = UUID_128;

	p_bt_content->ble_content.chr_cnt = 9;
	p_bt_content->ble_content.cb_ble_recv_fun = ble_callback;
	p_bt_content->ble_content.cb_ble_request_data = ble_request_data;
}

bool NetLinkWrapper::start_network_config() {
	printf("==start start_network_config ===\n");
	initBTForHis();

	string ssid = generate_ssid();
	//disconnect wifi if wifi network is ready;
	DeviceIo::getInstance()->controlWifi(WifiControl::WIFI_DISCONNECT);
	DeviceIo::getInstance()->controlBt(BtControl::BT_BLE_COLSE);
#ifdef ENABLE_SOFTAP
	printf("-------- NetLinkWrapper::start_network_config --------\n");
	DeviceIOFramework::WifiManager* wifiManager = DeviceIOFramework::WifiManager::getInstance();
	wifiManager->enableWifiAp(ssid);

	DeviceIOFramework::TcpServer* tcpServer = DeviceIOFramework::TcpServer::getInstance();
	tcpServer->startTcpServer();

	DeviceIOFramework::UdpServer* udpServer = DeviceIOFramework::UdpServer::getInstance();
	udpServer->startUdpServer();
#endif

	DeviceIo::getInstance()->controlBt(BtControl::BT_BLE_OPEN);
	printf("==start notify_network_config_status ===\n");
	getInstance()->notify_network_config_status(ENetworkConfigStarted);
	m_ping_interval = 1;
	wifi_link_state = false;
}

void NetLinkWrapper::stop_network_config() {
#ifdef ENABLE_SOFTAP
	DeviceIo::getInstance()->controlWifi(WifiControl::WIFI_CLOSE_AP_MODE);
#endif
	DeviceIo::getInstance()->controlBt(BtControl::BT_BLE_COLSE);
	DeviceIo::getInstance()->controlBt(BtControl::BT_BLE_DISCONNECT);
}

void *NetLinkWrapper::monitor_work_routine(void *arg) {
	auto thread = static_cast<NetLinkWrapper*>(arg);
	int time_interval = 1;
	int time_count = 1;
	while(1) {
		thread->ping_network(false);
		time_count = time_interval = m_ping_interval;
		APP_DEBUG("monitor_work_routine m_ping_interval:%d", m_ping_interval);

		while (time_count > 0) {
			struct timeval tv;
			tv.tv_sec = 1;
			tv.tv_usec = 0;
			::select(0, NULL, NULL, NULL, &tv);
			time_count--;
			if (time_interval != m_ping_interval) {
				APP_DEBUG("monitor_work_routine m_ping_interval:%d, time_interval:%d", m_ping_interval, time_interval);
				break;
			}
		}
	}

	return nullptr;
}

void NetLinkWrapper::start_network_monitor() {
	pthread_t network_config_threadId;

	pthread_create(&network_config_threadId, nullptr, monitor_work_routine, this);
	pthread_detach(network_config_threadId);
}

bool is_first_network_config(string path) {
	ifstream it_stream;
	int length = 0;
	string wpa_config_file = path;

#if 1 //for hisense board
	if (access("/data/property.txt", F_OK))
	return true;
#endif

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

void NetLinkWrapper::initBTForHis() {
	static bool inited = false;
	if (inited)
		return;
	inited = true;

	bt_init_for_hisense();
	DeviceIo::getInstance()->controlBt(BtControl::BT_OPEN, &bt_content);
	sleep(3);
}

void NetLinkWrapper::startNetworkRecovery() {
	DeviceIOFramework::WifiManager* wifiManager = DeviceIOFramework::WifiManager::getInstance();
	//wifiManager->disableWifiAp();
	//wifiManager->setWifiEnabled(true);
	DeviceIo::getInstance()->controlWifi(WifiControl::WIFI_OPEN);

	if (is_first_network_config(NETLINK_WPA_CONFIG_FILE)) {
		getInstance()->m_operation_type = operation_type::EAutoConfig;
		start_network_config_timeout_alarm(NETLINK_AUTO_CONFIG_TIMEOUT);
		start_network_config();
	} else {
		check_recovery_network_status();
	}

	printf("==start start_network_monitor ===\n");
	start_network_monitor();
}

void NetLinkWrapper::stopNetworkRecovery() {
	m_stop_network_recovery = true;
}

bool NetLinkWrapper::startNetworkConfig(int timeout) {
	APP_INFO("start configing %d.", timeout);

	m_operation_type = operation_type::EManualConfig;

	start_network_config_timeout_alarm(timeout);
	DeviceIo::getInstance()->controlWifi(WifiControl::WIFI_OPEN);
	start_network_config();
}

void NetLinkWrapper::stopNetworkConfig() {
	APP_INFO("stopping networkconfig.");

	m_operation_type = operation_type::EAutoEnd;

	stop_network_config_timeout_alarm();
	stop_network_config();
	getInstance()->notify_network_config_status(ENetworkConfigExited);
}

NetLinkNetworkStatus NetLinkWrapper::getNetworkStatus() const {
	return m_networkStatus;
}


static char *netstatus[] = {
	"NETLINK_NETWORK_CONFIG_STARTED",
	"NETLINK_NETWORK_CONFIGING",
	"NETLINK_NETWORK_CONFIG_SUCCEEDED",
	"NETLINK_NETWORK_CONFIG_FAILED",
	"NETLINK_NETWORK_SUCCEEDED",
	"NETLINK_NETWORK_FAILED",
	"NETLINK_NETWORK_RECOVERY_START",
	"NETLINK_NETWORK_RECOVERY_SUCCEEDED",
	"NETLINK_NETWORK_RECOVERY_FAILED",
	"NETLINK_WAIT_LOGIN",
	"NETLINK_NETWORK_CONFIG_WRONG_KEY_FAILED"
};

static char *notifyEvent[] = {
	"ENetworkNone",
	"ENetworkConfigExited",
	"ENetworkConfigStarted",
	"ENetworkDeviceConnected",
	"ENetworkConfigIng",
	"ENetworkConfigRouteFailed",
	"ENetworkLinkSucceed",
	"ENetworkLinkFailed",
	"ENetworkRecoveryStart",
	"ENetworkRecoverySucceed",
	"ENetworkRecoveryFailed",
	"ENetworkWifiSucceed",
	"ENetworkWifiFailed",
	"ENetworkWifiWrongKeyFailed"
};

void NetLinkWrapper::setNetworkStatus(NetLinkNetworkStatus networkStatus) {
	std::lock_guard<std::mutex> lock(m_mutex);
	APP_INFO("#### setNetworkStatus from %s to %s\n", netstatus[m_networkStatus - NETLINK_NETWORK_CONFIG_STARTED],
		netstatus[networkStatus - NETLINK_NETWORK_CONFIG_STARTED]);

	if (NetLinkWrapper::m_networkStatus == networkStatus)
		return ;

	NetLinkWrapper::m_networkStatus = networkStatus;
	if (m_callback)
		m_callback->netlinkNetworkStatusChanged(networkStatus);
}

void NetLinkWrapper::notify_network_config_status(notify_network_status_type notify_type) {

	APP_INFO("#### notify_event: ops %d, event: %s\n", get_operation_type(), notifyEvent[notify_type - ENetworkNone]);
	printf("### notify_event: ops %d, notify_type: %d, event: %s ###\n",
		get_operation_type(), notify_type, notifyEvent[notify_type - ENetworkNone]);

	switch (notify_type) {
		case ENetworkConfigStarted: {
			setNetworkStatus(NETLINK_NETWORK_CONFIG_STARTED);
			if (!m_isLoopNetworkConfig) {
				if (get_operation_type() == operation_type::EAutoConfig) {
					SoundController::getInstance()->linkStartFirst();
				} else if (get_operation_type() == operation_type::EManualConfig) {
					SoundController::getInstance()->linkStart();
				}
				m_isLoopNetworkConfig = true;
			}
			break;
		}
		case ENetworkConfigIng: {
			setNetworkStatus(NETLINK_NETWORK_CONFIGING);
			if (get_operation_type() != operation_type::EAutoEnd) {
				SoundController::getInstance()->linkConnecting();
			}
			break;
		}
		case ENetworkWifiFailed:
		case ENetworkWifiWrongKeyFailed:
			wifi_link_state = false;

			/* fall though */
		case ENetworkLinkFailed: {
			//Network config failed, reset wpa_supplicant.conf
			//set_wpa_conf(false);
			memset(ble_cfg.data, 0, BLE_SEND_MAX_LEN);
			if (notify_type == ENetworkWifiWrongKeyFailed)
				ble_cfg.data[0] = BLE_CONFIG_WIFI_FAILED;//BLE_CONFIG_WIFI_WRONG_KEY_FAILED;
			else
				ble_cfg.data[0] = BLE_CONFIG_WIFI_FAILED;
			ble_cfg.len = 1;
			memcpy(ble_cfg.uuid, NOTIFY_CHAR_UUID, UUID_MAX_LEN);
			DeviceIo::getInstance()->controlBt(BtControl::BT_BLE_WRITE, &ble_cfg);

			if (notify_type == ENetworkWifiWrongKeyFailed)
				setNetworkStatus(NETLINK_NETWORK_CONFIG_WRONG_KEY_FAILED);
			else
				setNetworkStatus(NETLINK_NETWORK_CONFIG_FAILED);
			SoundController::getInstance()->linkFailedPing(NetLinkWrapper::networkLinkFailed);
			break;
		}
		case ENetworkConfigRouteFailed: {
			//Network config failed, reset wpa_supplicant.conf
			//set_wpa_conf(false);
			//memset(ble_cfg.data, 0, BLE_SEND_MAX_LEN);
			//ble_cfg.data[0] = BLE_CONFIG_WIFI_TIMEOUT;
			//ble_cfg.len = 1;
			//memcpy(ble_cfg.uuid, NOTIFY_CHAR_UUID, UUID_MAX_LEN);
			//DeviceIo::getInstance()->controlBt(BtControl::BT_BLE_WRITE, &ble_cfg);

			setNetworkStatus(NETLINK_NETWORK_CONFIG_FAILED);
			SoundController::getInstance()->linkFailedIp(NetLinkWrapper::networkLinkFailed);
			break;
		}
		case ENetworkConfigExited: {
			setNetworkStatus(NETLINK_NETWORK_FAILED);
			m_isLoopNetworkConfig = false;
			SoundController::getInstance()->linkExit(nullptr);
			APP_INFO("notify_network_config_status: ENetworkConfigExited=====End");
			break;
		}

		case ENetworkWifiSucceed:
		case ENetworkLinkSucceed: {
			if (notify_type == ENetworkWifiSucceed)
				wifi_link_state = true;
			else
				net_link_state = true;

			if (!(net_link_state && wifi_link_state))
				break;

			setFromConfigNetwork(false);
			if (1) {//DeviceIoWrapper::getInstance()->isTouchStartNetworkConfig()) {
				/// from networkConfig
				setFromConfigNetwork(true);
			}
			//Network config succed, update wpa_supplicant.conf
			stop_network_config_timeout_alarm();
			//set_wpa_conf(true);
			DeviceIo::getInstance()->controlBt(BtControl::BT_BLE_COLSE);
			memset(ble_cfg.data, 0, BLE_SEND_MAX_LEN);
			ble_cfg.data[0] = BLE_CONFIG_WIFI_SUCCESS;
			ble_cfg.len = 1;
			memcpy(ble_cfg.uuid, NOTIFY_CHAR_UUID, UUID_MAX_LEN);
			DeviceIo::getInstance()->controlBt(BtControl::BT_BLE_WRITE, &ble_cfg);

			networkLinkOrRecoverySuccess();

			setNetworkStatus(NETLINK_NETWORK_CONFIG_SUCCEEDED);
			if (!isFromConfigNetwork()) {
			}

			m_isLoopNetworkConfig = false;
			OnNetworkReady();
			//DeviceIo::getInstance()->controlBt(BtControl::BT_BLE_COLSE);

			break;
		}
		case ENetworkRecoveryStart: {
			setNetworkStatus(NETLINK_NETWORK_RECOVERY_START);
			SoundController::getInstance()->reLink();
			break;
		}
		case ENetworkRecoverySucceed: {
			setNetworkStatus(NETLINK_NETWORK_RECOVERY_SUCCEEDED);

			networkLinkOrRecoverySuccess();

			setFromConfigNetwork(false);
			OnNetworkReady();
			break;
		}
		case ENetworkRecoveryFailed: {
			setNetworkStatus(NETLINK_NETWORK_RECOVERY_FAILED);
			SoundController::getInstance()->reLinkFailed();
			break;
		}
		case ENetworkDeviceConnected: {
			SoundController::getInstance()->hotConnected();
			break;
		}
		default:
			break;
	}
}

#define SYSTEM_AUTHOR_CODE_PATH		"/userdata/cfg/check_data"
#define SYSTEM_RM_AUTHOR_CODE	  "rm /userdata/cfg/check_data"

void NetLinkWrapper::network_status_changed(InternetConnectivity current_status, bool wakeupTrigger) {
	if (current_status == InternetConnectivity::AVAILABLE) {
		if (!isNetworkOnline()) {
			setNetworkOnline(true);
			if (m_callback) {
				m_callback->netlinkNetworkOnlineStatus(isNetworkOnline());
			}
			printf("%s: AVAILABLE getNetworkStatus: %d.\n", __func__, getNetworkStatus());
			switch (getNetworkStatus()) {
				case DeviceIOFramework::NETLINK_NETWORK_CONFIG_STARTED:
				case DeviceIOFramework::NETLINK_NETWORK_CONFIGING:
						notify_network_config_status(ENetworkLinkSucceed);
						break;
				case DeviceIOFramework::NETLINK_NETWORK_RECOVERY_FAILED:
						notify_network_config_status(ENetworkRecoverySucceed);
						break;
				case DeviceIOFramework::NETLINK_NETWORK_FAILED:
				case DeviceIOFramework::NETLINK_NETWORK_CONFIG_FAILED:
				case DeviceIOFramework::NETLINK_NETWORK_CONFIG_WRONG_KEY_FAILED:
						setNetworkStatus(NETLINK_NETWORK_SUCCEEDED);
						break;
				default:
						break;
				break;
			}
		}
		//setNetworkStatus(NETLINK_NETWORK_SUCCEEDED);
	} else {
		if (isNetworkOnline()) {
			setNetworkOnline(false);
			if (m_callback) {
				m_callback->netlinkNetworkOnlineStatus(isNetworkOnline());
			}
		}
		if (0) {//!DeviceIoWrapper::getInstance()->isTouchStartNetworkConfig()) {
			//setNetworkStatus(NETLINK_NETWORK_FAILED);
			if (wakeupTrigger) {
				wakeupNetLinkNetworkStatus();
			}
		}

		net_link_state = false;
		printf("%s: !NO_AVAILABLE getNetworkStatus: %d.\n", __func__, getNetworkStatus());
		switch (getNetworkStatus()) {
			case DeviceIOFramework::NETLINK_NETWORK_CONFIG_STARTED:
					break;
			case DeviceIOFramework::NETLINK_NETWORK_CONFIG_SUCCEEDED:
			case DeviceIOFramework::NETLINK_NETWORK_SUCCEEDED:
			case DeviceIOFramework::NETLINK_NETWORK_RECOVERY_SUCCEEDED:
					setNetworkStatus(NETLINK_NETWORK_FAILED);
					break;
			default:
					break;
			break;
		}
	}
}

void NetLinkWrapper::wakeupNetLinkNetworkStatus() {
	switch (m_networkStatus) {
		case NETLINK_NETWORK_CONFIG_STARTED: {
			APP_INFO("wakeup_net_link_network_status: NETLINK_NETWORK_CONFIG_STARTED=====");
			SoundController::getInstance()->linkStart();
			break;
		}
		case NETLINK_NETWORK_CONFIGING: {
			APP_INFO("wakeup_net_link_network_status: NETLINK_NETWORK_CONFIGING=====");
			SoundController::getInstance()->linkConnecting();
			break;
		}
		case NETLINK_NETWORK_RECOVERY_START: {
			APP_INFO("wakeup_net_link_network_status: NETLINK_NETWORK_RECOVERY_START=====");
			SoundController::getInstance()->reLink();
			break;
		}
		case NETLINK_NETWORK_FAILED: {
			APP_INFO("wakeup_net_link_network_status: NETLINK_NETWORK_FAILED=====");
			SoundController::getInstance()->networkConnectFailed();
			break;
		}
		case NETLINK_NETWORK_RECOVERY_FAILED: {
			APP_INFO("wakeup_net_link_network_status: NETLINK_NETWORK_RECOVERY_FAILED=====");
			SoundController::getInstance()->reLinkFailed();
			break;
		}
		default:
			break;
	}
}

void NetLinkWrapper::networkLinkSuccess() {

}

void NetLinkWrapper::networkLinkFailed() {

}

bool NetLinkWrapper::isNetworkOnline() const {
	return m_isNetworkOnline;
}

void NetLinkWrapper::setNetworkOnline(bool isNetworkOnline) {
	NetLinkWrapper::m_isNetworkOnline = isNetworkOnline;
}

void NetLinkWrapper::networkLinkOrRecoverySuccess() {
	if (isFromConfigNetwork()) {
		SoundController::getInstance()->linkSuccess(NetLinkWrapper::networkLinkSuccess);
	} else {
		SoundController::getInstance()->reLinkSuccess(NetLinkWrapper::networkLinkSuccess);
	}
}

bool NetLinkWrapper::isFirstNetworkReady() const {
	return m_isFirstNetworkReady;
}

void NetLinkWrapper::setFirstNetworkReady(bool isFirstNetworkReady) {
	NetLinkWrapper::m_isFirstNetworkReady = isFirstNetworkReady;
}

bool NetLinkWrapper::isFromConfigNetwork() const {
	return m_isFromConfigNetwork;
}

void NetLinkWrapper::setFromConfigNetwork(bool isFromConfigNetwork) {
	NetLinkWrapper::m_isFromConfigNetwork = isFromConfigNetwork;
}

void NetLinkWrapper::OnNetworkReady() {
	if (m_callback)
		m_callback->networkReady();
	Shell::system("/etc/init.d/S49ntp stop;"
				   "ntpdate cn.pool.ntp.org;"
				   "/etc/init.d/S49ntp start");
}

unsigned short NetLinkWrapper::getChksum(unsigned short *addr,int len) {
	int nleft = len;
	int sum = 0;
	unsigned short *w = addr;
	unsigned short answer = 0;

	while (nleft > 1) {
		sum += *w++;
		nleft-= 2;
	}

	if (nleft == 1) {
		*(unsigned char *)(&answer) = *(unsigned char *)w;
		sum += answer;
	}

	sum = ((sum>>16) + (sum&0xffff));
	sum += (sum>>16);
	answer = ~sum;

	return answer;
}

int NetLinkWrapper::packIcmp(int pack_no, struct icmp* icmp) {
	int packsize;
	struct icmp *picmp;
	struct timeval *tval;

	picmp = icmp;
	picmp->icmp_type = ICMP_ECHO;
	picmp->icmp_code = 0;
	picmp->icmp_cksum = 0;
	picmp->icmp_seq = pack_no;
	picmp->icmp_id = m_pid;
	packsize = (8 + m_datalen);
	tval= (struct timeval *)icmp->icmp_data;
	gettimeofday(tval, nullptr);
	picmp->icmp_cksum = getChksum((unsigned short *)icmp, packsize);

	return packsize;
}

bool NetLinkWrapper::unpackIcmp(char *buf, int len, struct IcmpEchoReply *icmpEchoReply) {
	int iphdrlen;
	struct ip *ip;
	struct icmp *icmp;
	struct timeval *tvsend, tvrecv, tvresult;
	double rtt;

	ip = (struct ip *)buf;
	iphdrlen = ip->ip_hl << 2;
	icmp = (struct icmp *)(buf + iphdrlen);
	len -= iphdrlen;

	if (len < 8) {
		APP_ERROR("ICMP packets's length is less than 8.");
		return false;
	}

	if( (icmp->icmp_type == ICMP_ECHOREPLY) && (icmp->icmp_id == m_pid) ) {
		tvsend = (struct timeval *)icmp->icmp_data;
		gettimeofday(&tvrecv, nullptr);
		tvresult = timevalSub(tvrecv, *tvsend);
		rtt = tvresult.tv_sec*1000 + tvresult.tv_usec/1000;	 //ms
		icmpEchoReply->rtt = rtt;
		icmpEchoReply->icmpSeq = icmp->icmp_seq;
		icmpEchoReply->ipTtl = ip->ip_ttl;
		icmpEchoReply->icmpLen = len;

		return true;
	} else {
		return false;
	}
}

struct timeval NetLinkWrapper::timevalSub(struct timeval timeval1, struct timeval timeval2) {
	struct timeval result;

	result = timeval1;

	if ((result.tv_usec < timeval2.tv_usec) && (timeval2.tv_usec < 0)) {
		-- result.tv_sec;
		result.tv_usec += 1000000;
	}

	result.tv_sec -= timeval2.tv_sec;

	return result;
}

bool NetLinkWrapper::sendPacket() {
	size_t packetsize;
	while( m_nsend < m_maxPacketSize) {
		m_nsend ++;
		m_icmp_seq ++;
		packetsize = packIcmp(m_icmp_seq, (struct icmp*)m_sendpacket);

		if (sendto(m_sockfd,m_sendpacket, packetsize, 0, (struct sockaddr *) &m_dest_addr, sizeof(m_dest_addr)) < 0) {
			APP_ERROR("Ping sendto failed:%s.", strerror(errno));
			continue;
		}
	}

	return true;
}

bool NetLinkWrapper::recvPacket(PingResult &pingResult) {
	int len = 0;
	struct IcmpEchoReply icmpEchoReply;
	int maxfds = m_sockfd + 1;
	int nfd	 = 0;
	fd_set rset;
	struct timeval timeout;
	socklen_t fromlen = sizeof(m_from_addr);

	timeout.tv_sec = MAX_WAIT_TIME;
	timeout.tv_usec = 0;

	FD_ZERO(&rset);

	for (int recvCount = 0; recvCount < m_maxPacketSize; recvCount ++) {
		FD_SET(m_sockfd, &rset);
		if ((nfd = select(maxfds, &rset, nullptr, nullptr, &timeout)) == -1) {
			APP_ERROR("Ping recv select failed:%s.", strerror(errno));
			continue;
		}

		if (nfd == 0) {
			icmpEchoReply.isReply = false;
			pingResult.icmpEchoReplys.push_back(icmpEchoReply);
			continue;
		}

		if (FD_ISSET(m_sockfd, &rset)) {
			if ((len = recvfrom(m_sockfd,
								m_recvpacket,
								sizeof(m_recvpacket),
								0,
								(struct sockaddr *)&m_from_addr,
								&fromlen)) <0) {
				if(errno == EINTR) {
					continue;
				}
				APP_ERROR("Ping recvfrom failed: %s.", strerror(errno));
				continue;
			}

			icmpEchoReply.fromAddr = inet_ntoa(m_from_addr.sin_addr) ;
			if (strncmp(icmpEchoReply.fromAddr.c_str(), pingResult.ip, strlen(pingResult.ip)) != 0) {
				recvCount--;
				continue;
			}
		}

		if (!unpackIcmp(m_recvpacket, len, &icmpEchoReply)) {
			recvCount--;
			continue;
		}

		icmpEchoReply.isReply = true;
		pingResult.icmpEchoReplys.push_back(icmpEchoReply);
		m_nreceived ++;
	}

	return true;

}

bool NetLinkWrapper::getsockaddr(const char * hostOrIp, struct sockaddr_in* sockaddr) {
	struct hostent *host;
	struct sockaddr_in dest_addr;
	unsigned long inaddr = 0l;

	bzero(&dest_addr,sizeof(dest_addr));
	dest_addr.sin_family = AF_INET;

	inaddr = inet_addr(hostOrIp);
	if (inaddr == INADDR_NONE) {
		host = gethostbyname(hostOrIp);
		if (host == nullptr) {
			return false;
		}
		memcpy( (char *)&dest_addr.sin_addr,host->h_addr, host->h_length);
	} else if (!inet_aton(hostOrIp, &dest_addr.sin_addr)) {
		return false;
	}

	*sockaddr = dest_addr;

	return true;
}

bool NetLinkWrapper::ping(string host, int count, PingResult& pingResult) {
	int size = 50 * 1024;
	IcmpEchoReply icmpEchoReply;

	m_nsend = 0;
	m_nreceived = 0;
	pingResult.icmpEchoReplys.clear();
	m_maxPacketSize = count;
	m_pid = getpid();

	pingResult.dataLen = m_datalen;

	if ((m_sockfd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP)) < 0) {
		APP_ERROR("Ping socket failed:%s.", strerror(errno));
		pingResult.error = strerror(errno);
		return false;
	}

	if (setsockopt(m_sockfd, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size)) != 0) {
		APP_ERROR("Setsockopt SO_RCVBUF failed:%s.", strerror(errno));
		close(m_sockfd);
		return false;
	}

	if (!getsockaddr(host.c_str(), &m_dest_addr)) {
		pingResult.error = "unknow host " + host;
		close(m_sockfd);
		return false;
	}

	strcpy(pingResult.ip, inet_ntoa(m_dest_addr.sin_addr));

	sendPacket();
	recvPacket(pingResult);

	pingResult.nsend = m_nsend;
	pingResult.nreceived = m_nreceived;

	close(m_sockfd);

	return true;
}

bool NetLinkWrapper::ping_network(bool wakeupTrigger) {
	string hostOrIp = PING_DEST_HOST1;
	int nsend = 0, nreceived = 0;
	bool ret;
	PingResult pingResult;
	InternetConnectivity networkResult = UNAVAILABLE;

	pthread_mutex_lock(&m_ping_lock);

	for (int count = 1; count <= MAX_PACKETS_COUNT; count ++) {
		memset(&pingResult.ip, 0x0, 32);
		ret = ping(hostOrIp, 1, pingResult);

		if (!ret) {
			APP_ERROR("Ping error:%s", pingResult.error.c_str());
		} else {
			nsend += pingResult.nsend;
			nreceived += pingResult.nreceived;
			if (nreceived > 0)
				break;
		}

		if (count == 2) {
			hostOrIp = PING_DEST_HOST2;
		}
	}

	if (nreceived > 0) {
		ret = true;
		networkResult = AVAILABLE;
		if (m_network_status == (int)UNAVAILABLE) {
			m_ping_interval = 1;
		} else {
			if (m_ping_interval < MAX_PING_INTERVAL) {
				m_ping_interval = m_ping_interval * 2;
				if (m_ping_interval > MAX_PING_INTERVAL) {
					m_ping_interval = MAX_PING_INTERVAL;
				}
			}
		}
		m_network_status = 1;
	} else {
		ret = false;
		networkResult = UNAVAILABLE;
		m_network_status = 0;
		m_ping_interval = 1;
	}

	network_status_changed(networkResult, wakeupTrigger);

	pthread_mutex_unlock(&m_ping_lock);

	return ret;
}

bool NetLinkWrapper::check_recovery_network_status() {
	notify_network_config_status(ENetworkRecoveryStart);

	int network_check_count = 0;
	bool recovery = false;

	while(!(recovery = ping_network(false))) {
		if (m_stop_network_recovery || network_check_count == NETLINK_NETWORK_CONFIGURE_PING_COUNT) {
			APP_ERROR("Network recovery ping failed.");

			break;
		}

		sleep(1);
		network_check_count++;
	}

	if (m_stop_network_recovery) {
		APP_INFO("Network recovery cancel.");
		startNetworkConfig(NETLINK_AUTO_CONFIG_TIMEOUT);
		return true;
	} else if (recovery) {
		APP_INFO("Network recovery succed.");
		notify_network_config_status(ENetworkRecoverySucceed);
		return true;
	} else {
		APP_ERROR("Network recovery failed.");
		notify_network_config_status(ENetworkRecoveryFailed);
		return false;
	}
}

}  // namespace application
