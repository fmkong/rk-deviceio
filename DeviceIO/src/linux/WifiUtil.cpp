#include "WifiUtil.h"
#include "shell.h"
#include "Logger.h"

#include <stdlib.h>
#include <thread>
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
#include <fcntl.h>
#include <pthread.h>
#include<signal.h>
#include <sys/prctl.h>

#include <signal.h>//SIGQUIT /usr/include/bits/signum.h
#include <errno.h>// ESRCH  /usr/include/asm-/error-bash.h


#include "DeviceIo/DeviceIo.h"
#include "Timer.h"

using DeviceIOFramework::Timer;
using DeviceIOFramework::TimerManager;
using DeviceIOFramework::TimerNotify;
using DeviceIOFramework::DeviceIo;
using DeviceIOFramework::DeviceInput;
using DeviceIOFramework::wifi_config;
using DeviceIOFramework::NetLinkNetworkStatus;

using std::string;
using std::vector;
using std::ifstream;

typedef std::list<std::string> LIST_STRING;
typedef std::list<WifiInfo*> LIST_WIFIINFO;
static int network_id;
static struct wifi_config *gwifi_cfg;
int get_pid(const char Name[]);

static const char *WIFI_CONFIG_FORMAT = "ctrl_interface=/var/run/wpa_supplicant\n"
                                "ap_scan=1\n\nnetwork={\nssid=\"%s\"\n"
                                "psk=\"%s\"\npriority=1\n}\n";

WifiUtil* WifiUtil::m_instance = nullptr;
pthread_once_t WifiUtil::m_initOnce = PTHREAD_ONCE_INIT;
pthread_once_t WifiUtil::m_destroyOnce = PTHREAD_ONCE_INIT;

WifiUtil::WifiUtil() {
    m_destroyOnce = PTHREAD_ONCE_INIT;
}

WifiUtil::~WifiUtil() {
    m_initOnce = PTHREAD_ONCE_INIT;
}

WifiUtil* WifiUtil::getInstance() {
    pthread_once(&m_initOnce, WifiUtil::init);
    return m_instance;
}

void WifiUtil::releaseInstance() {
    pthread_once(&m_destroyOnce, WifiUtil::destroy);
}

void WifiUtil::init() {
    if (m_instance == nullptr) {
        m_instance = new WifiUtil;
    }
}

void WifiUtil::destroy() {
    if (m_instance != nullptr) {
        delete m_instance;
        m_instance = nullptr;
    }
}

bool check_ap_interface_status(string ap) {
    int sockfd;
    bool ret = false;
    struct ifreq ifr_mac;

    if ((sockfd = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) <= 0) {
        APP_ERROR("socket create failed.\n");
        return false;
    }

    memset(&ifr_mac,0,sizeof(ifr_mac));
    strncpy(ifr_mac.ifr_name, ap.c_str(), sizeof(ifr_mac.ifr_name)-1);

    if ((ioctl(sockfd, SIOCGIFHWADDR, &ifr_mac)) < 0) {
        APP_ERROR("Mac ioctl failed.\n");
    } else {
        APP_DEBUG("Mac ioctl suceess.\n");
        ret = true;
    }
    close(sockfd);

    return ret;
}

bool WifiUtil::start_wpa_supplicant() {
	static pthread_t start_wifi_monitor_threadId = 0;
	int count = 10;

	printf("start_wpa_supplicant wpa_pid: %d, monitor_id: %d\n",
			Shell::pidof("wpa_supplicant"), start_wifi_monitor_threadId);

	if (Shell::pidof("wpa_supplicant") && (start_wifi_monitor_threadId != 0)) {
		int kill_ret = pthread_kill(start_wifi_monitor_threadId, 0);
		if (kill_ret == ESRCH) {
			printf("start_wifi_monitor_threadId not found\n");
		} else if (kill_ret == EINVAL)
			printf("start_wifi_monitor_threadId no vaild\n");
		else if (kill_ret == 0) {
			printf("start_wifi_monitor_threadId is found\n");
			return 1;
		}
	}

    Shell::system("ifconfig wlan0 down");
    Shell::system("ifconfig wlan0 up");
    Shell::system("ifconfig wlan0 0.0.0.0");
    Shell::system("killall dhcpcd");
    Shell::system("killall wpa_supplicant");
    usleep(100000);

retry:
    Shell::system("wpa_supplicant -B -i wlan0 -c /data/cfg/wpa_supplicant.conf");
	if ((!Shell::pidof("wpa_supplicant")) && (count--))
		goto retry;

    usleep(600000);
    Shell::system("dhcpcd -L -f /etc/dhcpcd.conf");
    Shell::system("dhcpcd wlan0 -t 0 &");
	if (start_wifi_monitor_threadId > 0)
		pthread_cancel(start_wifi_monitor_threadId);
    pthread_create(&start_wifi_monitor_threadId, nullptr, start_wifi_monitor, nullptr);
    pthread_detach(start_wifi_monitor_threadId);

    return true;
}

bool WifiUtil::stop_wpa_supplicant() {
    Shell::system("ifconfig wlan0 down");
    return Shell::system("killall wpa_supplicant &");
}

bool WifiUtil::stop_ap_mode() {
    APP_INFO("stop_ap_mode\n");

    Shell::system("softapDemo stop");
    Shell::system("killall softapServer &");
    int time = 100;
    while (time-- > 0 && !access("/var/run/hostapd", F_OK)) {
        usleep(10 * 1000);
    }
    APP_INFO("End stop_ap_mode\n");
}

bool WifiUtil::start_ap_mode(char *ap_name) {
    bool ret_value = true;
    string cmd;

    if (ap_name == NULL)
        ap_name = "RockchipEcho-123";

    APP_INFO("start_ap_mode: %s\n", ap_name);

    cmd.append("softapServer ");
    cmd += ap_name;
    cmd += " &";

    if (Shell::pidof("hostapd") || Shell::pidof("softapServer"))
        stop_ap_mode();

    Shell::system(cmd.c_str());
    int time = 100;
    while (time-- > 0 && access("/var/run/hostapd", F_OK)) {
        usleep(100 * 1000);
    }
    usleep(100 * 1000);
    APP_INFO("End start_ap_mode");

    return ret_value;
}

bool starup_ap_interface() {
    if (check_ap_interface_status(NETWORK_DEVICE_FOR_AP)) {
        APP_DEBUG("%s is up.\n", NETWORK_DEVICE_FOR_AP);

        return true;
    }

    return Shell::system("ifconfig wlan1 up &");
}

bool down_ap_interface() {
    if (!check_ap_interface_status(NETWORK_DEVICE_FOR_AP)) {
        APP_DEBUG("%s is down.\n", NETWORK_DEVICE_FOR_AP);
        return true;
    }

    return Shell::system("ifconfig wlan1 down &");
}

bool starup_wlan0_interface() {

    return Shell::system("ifconfig wlan0 up &");
}

bool down_wlan0_interface() {
    if (!check_ap_interface_status(NETWORK_DEVICE_FOR_WORK)) {
        APP_DEBUG("%s is down.\n", NETWORK_DEVICE_FOR_WORK);
        return true;
    }

    Shell::system("ifconfig wlan0 0.0.0.0");

    return Shell::system("ifconfig wlan0 down &");
}

bool stop_dhcp_server() {
    return Shell::system("killall dnsmasq &");
}

bool start_dhcp_server() {
    if (stop_dhcp_server()) {
        APP_DEBUG("[Start_dhcp_server] dnsmasq is killed.\n");
    }
    sleep(1);

    return Shell::system("dnsmasq &");
}

bool get_device_interface_ip()
{
    int sock_ip;
    struct ifreq ifr;
        struct sockaddr_in  sin;

    sock_ip = socket( AF_INET, SOCK_DGRAM, 0 );
    if (sock_ip == -1) {
        APP_ERROR("create ip socket failed.\n");
        return false;
    }

    memset(&ifr,0,sizeof(ifr));
    strncpy(ifr.ifr_name, NETWORK_DEVICE_FOR_WORK, sizeof(ifr.ifr_name)-1);

    if ((ioctl( sock_ip, SIOCGIFADDR, &ifr)) < 0) {
        APP_ERROR("ip socket ioctl failed.\n");
        close(sock_ip);
        return false;
    }

        memcpy(&sin,&ifr.ifr_addr,sizeof(sin));
        APP_DEBUG("eth0 ip: %s\n",inet_ntoa(sin.sin_addr));
    return true;

}

bool get_device_interface_mac(string &mac_address) {
    int sock_mac;
    struct ifreq ifr_mac;
    char mac_addr[30] = {0};

    sock_mac = socket( AF_INET, SOCK_STREAM, 0 );
    if (sock_mac == -1) {
        APP_ERROR("create mac socket failed.\n");
        return false;
    }

    memset(&ifr_mac,0,sizeof(ifr_mac));
    strncpy(ifr_mac.ifr_name, NETWORK_DEVICE_FOR_WORK, sizeof(ifr_mac.ifr_name)-1);

    if ((ioctl( sock_mac, SIOCGIFHWADDR, &ifr_mac)) < 0) {
        APP_ERROR("Mac socket ioctl failed.\n");
        close(sock_mac);
        return false;
    }

    sprintf(mac_addr,"%02X%02X",
            (unsigned char)ifr_mac.ifr_hwaddr.sa_data[4],
            (unsigned char)ifr_mac.ifr_hwaddr.sa_data[5]);

    APP_DEBUG("local mac:%s\n",mac_addr);

    close(sock_mac);

    mac_address = mac_addr;

    std::transform(mac_address.begin(), mac_address.end(), mac_address.begin(), toupper);

    return true;
}

void get_device_wifi_chip_type(string &wifi_chip_type)
{
	char wifi_chip[30] = {0};
    int fd = open("/sys/class/rkwifi/chip", O_RDONLY);
    if (fd < 0) {
		APP_ERROR("open /sys/class/rkwifi/chip err!\n");
		bzero(wifi_chip, sizeof(wifi_chip));
		strcpy(wifi_chip, "RTL8723DS");
	}
    else {
        memset(wifi_chip, '\0', sizeof(wifi_chip));
        read(fd, wifi_chip, sizeof(wifi_chip));
        close(fd);
    } 
	wifi_chip_type = wifi_chip;
	APP_INFO("get wifi chip: %s\n", wifi_chip);
}

/**
 * split buff array by '\n' into string list.
 * @parm buff[]
 */
LIST_STRING charArrayToList(char buff[]){
    LIST_STRING stringList;
    std::string item;
    for(int i=0;i<strlen(buff);i++){
        if(buff[i] != '\n'){
            item += buff[i];
        } else {
            stringList.push_back(item);
            item.clear();
        }
    }
    return stringList;
}

/**
 * format string list into wifiInfo list by specific rules
 * @parm string_list
 * @return LIST_WIFIINFO
 */
LIST_WIFIINFO wifiStringFormat(LIST_STRING string_list){
    LIST_WIFIINFO wifiInfo_list;

    LIST_STRING::iterator stringIte;

    /* delete first useless item */
    string_list.pop_front();

    for(stringIte=string_list.begin();stringIte!=string_list.end();stringIte++){
        WifiInfo *wifiInfoItem = new WifiInfo();
        std::string wifiStringItem = *stringIte;

        /* use for set wifiInfo item:bssid ssid etc*/
        std::string tempString;
        int index = 0; /* temp index,flag '\t' appear count*/

        for(int i=0;i<wifiStringItem.size();i++){
            if(wifiStringItem.at(i)!='\t' && i != (wifiStringItem.size()-1)){
                tempString += wifiStringItem.at(i);
            } else {
                switch(index){
                case 0: //bssid
                    wifiInfoItem->setBssid(tempString);
                    break;
                case 1: //frequency
                    wifiInfoItem->setFrequency(tempString);
                    break;
                case 2: //signalLevel
                    wifiInfoItem->setSignalLevel(tempString);
                    break;
                case 3: //flags
                    wifiInfoItem->setFlags(tempString);
                    break;
                case 4: //ssid
                    tempString += wifiStringItem.at(i);
                    wifiInfoItem->setSsid(tempString);
                    break;
                default:
                    break;
                }
                index ++;
                tempString.clear();
            }
        }
        wifiInfo_list.push_back(wifiInfoItem);
    }
    return wifiInfo_list;
}

/**
 * parse wifi info list into json string.
 * @parm wifiInfoList
 * @return json string
 */
std::string parseIntoJson(LIST_WIFIINFO wifiInfoList){
    LIST_WIFIINFO::iterator iterator;

    rapidjson::Document document;
    document.SetObject();
    rapidjson::Document::AllocatorType& allocator = document.GetAllocator();

    /* 1. add return type */
    document.AddMember("type","WifiList",allocator);
    /* 2. add reutn content */
    rapidjson::Value wifiArrayValue(rapidjson::kArrayType);
    for(iterator = wifiInfoList.begin(); iterator != wifiInfoList.end(); ++iterator){
        (*iterator)->addJsonToRoot(document,wifiArrayValue);
    }
    document.AddMember("content",wifiArrayValue,allocator);

    /* parse into string */
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    document.Accept(writer);

    return buffer.GetString();
}

/**
 * get json substr from http respose head.
 * split by '{' and "}"
 * @parm message http.
 */
std::string getJsonFromMessage(char message[]){
    std::string str(message);
    return str.substr(str.find('{'));
}

static char wifi_ssid[256];
static char wifi_ssid_bk[256];
static char wifi_password[256];
static char wifi_password_bk[256];
static char wifi_ssid_bk_s[1024];
static char wifi_security[256];
static char wifi_hide[256];
static char check_data[256];
static int priority = 0;
static int select_id = -1;

static volatile bool g_results = false;
bool checkWifiIsConnected();
static volatile bool wifi_wrong_key = false;
static int connect_retry_count;
#define WIFI_CONNECT_RETRY 50

static bool save_wifi_config(int mode, bool wrong_key)
{
	int fail_reason = 0;

	//7. save config
	if (mode == 1) {
		Shell::system("wpa_cli enable_network all");
		Shell::system("wpa_cli save_config");
		Shell::system("sync");
		if (gwifi_cfg->wifi_status_callback)
			gwifi_cfg->wifi_status_callback(NetLinkNetworkStatus::NETLINK_NETWORK_CONFIG_SUCCEEDED, fail_reason);
	} else {
		if ((wifi_wrong_key) || wrong_key)
			fail_reason = 1;
		Shell::system("wpa_cli flush");
		if (gwifi_cfg->wifi_status_callback)
			gwifi_cfg->wifi_status_callback(NetLinkNetworkStatus::NETLINK_NETWORK_CONFIG_FAILED, fail_reason);
		usleep(500000);
		Shell::system("wpa_cli reconfigure");
		usleep(500000);
		Shell::system("wpa_cli -iwlan0 disable_network all");
		usleep(500000);
		Shell::system("wpa_cli -iwlan0 disable_network all");
	}

	memset(wifi_ssid, 0, 256);
	memset(wifi_ssid_bk, 0, 256);
	memset(wifi_password, 0, 256);
	memset(wifi_password_bk, 0, 256);
	memset(wifi_security, 0, 256);
	memset(wifi_ssid_bk_s, 0, 1024);

	return 0;
}

static void check_wifiinfo(int flag, char *info)
{
    char temp[1024];
    char buff[1024] = {0};
	char buff1[1024] = {0};
    char cmdline[1024] = {0};
    int j = 0;
    char *str;

    if (flag == 0) {
		for (int i = 0; i < strlen(info); i++) {
			sprintf(temp+2*i, "%02x", info[i]);
		}
		temp[strlen(info)*2] = '\0';
		strcpy(info, temp);
		printf("check_wifiinfo ssid: %s\n", info);

		j = 0;
		for (int i = 0; i < strlen(wifi_ssid_bk); i++) {
			if (!((wifi_ssid_bk[i] >= 48) && (wifi_ssid_bk[i] <= 57)))
				wifi_ssid_bk_s[j++] = '\\';

			wifi_ssid_bk_s[j++] = wifi_ssid_bk[i];
		}
		wifi_ssid_bk_s[j] = '\0';

		select_id = -1;
		memset(cmdline, 0, sizeof(cmdline));
		sprintf(cmdline,"wpa_cli -iwlan0 list_network | grep \"%s\" | awk -F \" \" '{print $1}'", wifi_ssid_bk_s);
		Shell::exec(cmdline, buff, 1024);

		memset(cmdline, 0, sizeof(cmdline));
		sprintf(cmdline,"cat /data/cfg/wpa_supplicant.conf | grep %s", info);
		Shell::exec(cmdline, buff1, 1024);

		if (strlen(buff) > 0) {
			select_id = atoi(buff);
		} else if (strlen(buff1) > 0) {
			Shell::exec("cat data/cfg/wpa_supplicant.conf | grep -v scan_ssid | grep ssid", buff, 1024);
			while ((str = strchr(buff, '\n')) != NULL) {
				select_id++;
				if (strstr(str, info) == NULL)
					break;
				strcpy(buff, str + 1);
			}
		}
		return select_id;
    } else if (flag == 1) {
		int j = 0;
		while (j < 6) {
			memset(cmdline, 0, sizeof(cmdline));
			sprintf(cmdline,"wpa_cli -iwlan0 scan_result | grep \"%s\"", wifi_ssid_bk_s);
			Shell::exec("wpa_cli -iwlan0 scan", buff, 1024);
			Shell::exec(cmdline, buff, 1024);
			if (strlen(buff) > 0) {
					printf("scan ssid: %s\n", buff);
					break;
			}
			j++;
			sleep(1);
		}

		if (strstr(buff, "WPA") != NULL)
	        strcpy(wifi_security, "WPA-PSK");
		else if (strstr(buff, "WEP") != NULL)
			strcpy(wifi_security, "WEP");
		else if (strstr(buff, "ESS") != NULL && strstr(buff, "WPA") == NULL)
			strcpy(wifi_security, "NONE");

		printf("check_wifiinfo security: %s\n", wifi_security);
    } else if (flag == 2) {
		for (int i = 0; i < strlen(info); i++) {
	        temp[j++] = '\\';
	        temp[j++] = info[i];
		}
		temp[j] = '\0';
		strcpy(info, temp);

		printf("check_wifiinfo password: %s\n", info);
    }                
}

/**
 * use wpa_cli tool to connnect wifi in alexa device
 * @parm ssid
 * @parm password
 */
bool wifiConnect(std::string ssid,std::string password,std::string security, bool hide){
    char ret_buff[1024] = {0};
    char cmdline[1024] = {0};
    int id = -1;
    bool execute_result = 0;

	if (ssid.empty() || password.empty()) {
		printf("Input param is empty.");
	}

	wifi_wrong_key = false;

	printf("%s ssid: %s, password: %s, security: %s, hide: %d\n", __func__, ssid.c_str(), password.c_str(), security.c_str(), hide);

	strcpy(wifi_ssid, ssid.c_str());
	strcpy(wifi_ssid_bk, ssid.c_str());

	strcpy(wifi_password, password.c_str());
	strcpy(wifi_password_bk, password.c_str());

	strcpy(wifi_security, security.c_str());

	Shell::system("wpa_cli -iwlan0 disable_network all");
	sleep(1);

    // 1. add network
    //Shell::exec("wpa_cli -iwlan0 add_network", ret_buff, 1024);
    //id = atoi(ret_buff);
    //if (id < 0) {
	//	save_wifi_config(0);
    //    log_err("add_network failed.\n");
	//	goto falsed;
    //}
	if (wifi_ssid != NULL && strlen(wifi_ssid) != 0) {
		check_wifiinfo(0, wifi_ssid);
		if (select_id != -1)
			id = select_id;
	}

    if (select_id == -1) {
        // 1. add network
        Shell::exec("wpa_cli -iwlan0 add_network", ret_buff, 1024);
        id = atoi(ret_buff);
        if (id < 0) {
			//save_wifi_config(0);
			log_err("add_network failed.\n");
			goto falsed;
        }
    }

	// prioity
	priority = id + 1;

    // 2. setNetWorkSSID
	//check_wifiinfo(0, wifi_ssid);
    memset(cmdline, 0, sizeof(cmdline));
    sprintf(cmdline,"wpa_cli -iwlan0 set_network %d ssid %s", id, wifi_ssid);
    printf("%s\n", cmdline);
    Shell::exec(cmdline, ret_buff, 1024);
    execute_result = !strncmp(ret_buff, "OK", 2);
    if(!execute_result){
        log_err("setNetWorkSSID failed.\n");
		goto falsed;
    }

	// 2-1. setNetWork_priority
	memset(cmdline, 0, sizeof(cmdline));
	sprintf(cmdline,"wpa_cli -iwlan0 set_network %d priority %d", id, priority);
	printf("%s\n", cmdline);
	Shell::exec(cmdline, ret_buff, 1024);
	execute_result = !strncmp(ret_buff, "OK", 2);
	if(!execute_result){
		log_err("setNetWork_priority failed.\n");
		goto falsed;
	}

	// 5. setNetWorkHIDE
	memset(cmdline, 0, sizeof(cmdline));
	sprintf(cmdline,"wpa_cli -iwlan0 set_network %d scan_ssid %d", id, hide);
	printf("%s\n", cmdline);
	Shell::exec(cmdline, ret_buff, 1024);
	execute_result = !strncmp(ret_buff, "OK", 2);
	if (!execute_result) {
		log_err("setNetWorkHIDE failed.\n");
		goto falsed;
	}

#if 0
    // 3. setNetWorkSECURe
    check_wifiinfo(1, wifi_security);
    memset(cmdline, 0, sizeof(cmdline));
    sprintf(cmdline,"wpa_cli -iwlan0 set_network %d key_mgmt %s", id, wifi_security);
    printf("%s\n", cmdline);
    Shell::exec(cmdline, ret_buff);
    execute_result = !strncmp(ret_buff, "OK", 2);   
    if(!execute_result){
        perror("setNetWorkSECURe failed.\n");
		goto falsed;
    }

	if (strcmp(wifi_security, "NONE") == 0) {
        printf("wifi_security is NONE! ignore the password\n");
		goto enable_network;
    }
#endif

	if (hide) {
		if (wifi_password[0] == 0)
			strcpy(wifi_security, "NONE");
		else
			strcpy(wifi_security, "WPA-PSK");
	} else
		check_wifiinfo(1, wifi_security);

	if (strncmp(wifi_security, "WEP", 3) == 0) {
		memset(cmdline, 0, sizeof(cmdline));
		sprintf(cmdline, "wpa_cli -iwlan0 set_network %d key_mgmt NONE", id);
		Shell::exec(cmdline, ret_buff, 1024);
	} else if (strncmp(wifi_security, "WPA-PSK", 7) == 0) {
		memset(cmdline, 0, sizeof(cmdline));
		sprintf(cmdline, "wpa_cli -iwlan0 set_network %d key_mgmt WPA-PSK", id);
		Shell::exec(cmdline, ret_buff, 1024);
	} else if (strncmp(wifi_security, "NONE", 4) == 0) {
		memset(cmdline, 0, sizeof(cmdline));
		sprintf(cmdline, "wpa_cli -iwlan0 set_network %d key_mgmt NONE", id);
		Shell::exec(cmdline, ret_buff, 1024);
		printf("wifi_security is NONE! ignore the password\n");
		goto enable_network;
	}

    // 4. setNetWorkPWD
	check_wifiinfo(2, wifi_password);
    memset(cmdline, 0, sizeof(cmdline));
	if (strncmp(wifi_security, "WPA-PSK", 7) == 0) {
		sprintf(cmdline,"wpa_cli -iwlan0 set_network %d psk \\\"%s\\\"", id, wifi_password);
	} else if (strncmp(wifi_security, "WEP", 3) == 0) {
		memset(cmdline, 0, sizeof(cmdline));
		if (strlen(wifi_password_bk) == 10 || strlen(wifi_password_bk) == 26)
			 sprintf(cmdline,"wpa_cli -iwlan0 set_network %d wep_key0 %s", id, wifi_password);
		else
			 sprintf(cmdline,"wpa_cli -iwlan0 set_network %d wep_key0 \\\"%s\\\"", id, wifi_password);
	}
    printf("%s\n", cmdline);
    Shell::exec(cmdline,ret_buff, 1024);
    execute_result = !strncmp(ret_buff,"OK",2);
    if(!execute_result){
        log_err("setNetWorkPWD failed.\n");
		goto falsed;
    }

enable_network:
    // 5. selectNetWork
    memset(cmdline, 0, sizeof(cmdline));
    sprintf(cmdline,"wpa_cli -iwlan0 select_network %d", id);
    printf("%s\n", cmdline);
    Shell::exec(cmdline,ret_buff, 1024);
    execute_result = !strncmp(ret_buff,"OK",2);
    if(!execute_result){
        log_err("setNetWorkPWD failed.\n");
		goto falsed;
    }

	if (checkWifiIsConnected()) {
		save_wifi_config(1, false);
	} else {
		save_wifi_config(0, false);
	}

	return true;

falsed:
	save_wifi_config(0, true);
	return false;	
}

bool checkWifiIsConnected() {
    char ret_buff[1024] = {0};
	int dhcpcd_retry = 5;
	bool is_connected = false;
	bool is_vaild_ip_addr = false;

    LIST_STRING stateSList;
    LIST_STRING::iterator iterator;

    prctl(PR_SET_NAME,"checkWifiIsConnected");

    bool isWifiConnected = false;
	connect_retry_count = WIFI_CONNECT_RETRY;

	Shell::exec("dhcpcd -k wlan0", ret_buff, 1024);
	usleep(500000);
	Shell::exec("killall dhcpcd", ret_buff, 1024);
	usleep(500000);

    /* 15s to check wifi whether connected */
    for(int i=0;i<connect_retry_count;i++){
        sleep(1);
		is_connected = false;
		is_vaild_ip_addr = false;

        Shell::exec("wpa_cli -iwlan0 status",ret_buff, 1024);
        stateSList = charArrayToList(ret_buff);
        for(iterator=stateSList.begin();iterator!=stateSList.end();iterator++){
            std::string item = (*iterator);
			if (item.find("wpa_state") != std::string::npos) {
				if(item.substr(item.find('=')+1)=="COMPLETED")
					is_connected = true;
			}
			if (item.find("ip_address") != std::string::npos) {
				if(item.substr(item.find('=')+1)!="127.0.0.1")
					is_vaild_ip_addr = true;
			}
        }

		if ((is_connected == true) && (is_vaild_ip_addr == false)) {
			if (dhcpcd_retry) {
				dhcpcd_retry--;
				// udhcpc network
				Shell::exec("dhcpcd -k wlan0", ret_buff, 1024);
				usleep(500000);
				Shell::exec("killall dhcpcd", ret_buff, 1024);
				usleep(500000);
				Shell::exec("dhcpcd -L -f /etc/dhcpcd.conf", ret_buff, 1024);
				sleep(1);
				Shell::system("dhcpcd wlan0 -t 0 &");
				sleep(1);
			}
		}

		if ((is_connected == true) && (is_vaild_ip_addr == true)) {
            isWifiConnected = true;
            // TODO play audio: wifi connected
            log_err("Congratulation: wifi connected.\n");
            break;
        }
		if (wifi_wrong_key)
			break;
        log_err("Check wifi state with none state. try more %d/%d, \n",i+1, WIFI_CONNECT_RETRY);
    }
	return isWifiConnected;
}


std::string WifiUtil::getWifiListJson(){
    char ret_buff[MSG_BUFF_LEN] = {0};
    std::string ret;
    int retry_count = 10;

    LIST_STRING wifiStringList;
    LIST_WIFIINFO wifiInfoList;

retry:
    memset(ret_buff, 0, MSG_BUFF_LEN);
    Shell::exec("wpa_cli -i wlan0 -p /var/run/wpa_supplicant scan", ret_buff, MSG_BUFF_LEN);
    sleep(1);
    sync();

    memset(ret_buff, 0, MSG_BUFF_LEN);
    Shell::scan("wpa_cli -i wlan0 -p /var/run/wpa_supplicant scan_r", ret_buff);
    wifiStringList = charArrayToList(ret_buff);
    wifiInfoList = wifiStringFormat(wifiStringList);
    
    if ((wifiInfoList.size() == 0)  && (--retry_count > 0)) {
        usleep(500000);
        goto retry;
    }
    // parse wifiInfo list into json.
    ret = parseIntoJson(wifiInfoList);
    log_info("getWifiListJson list size: %d, ret.size: %d, ret.str: %s\n",wifiInfoList.size(), ret.size(), ret.c_str());
    return ret;
}

std::string WifiUtil::getDeviceContextJson() {
    std::string ret = " ";
    std::string sn1;
    std::string sn2;
    std::string sn3;
    std::string sn4;

#define SN_1 "ro.hisense.jhkdeviceid"
#define SN_2 "ro.hisense.jhldeviceid"
#define SN_3 "ro.hisense.wifiid"
#define SN_4 "ro.hisense.uuid"

    rapidjson::Document newDoc;
    FILE *myFile = fopen(DEVICE_CONFIG_FILE, "r");
    if (!myFile) {
        log_info("%s, %s not exist\n", __func__, DEVICE_CONFIG_FILE);
        return ret;
    }
    char readBuffer[65536];
    rapidjson::FileReadStream is(myFile, readBuffer, sizeof(readBuffer));
    newDoc.ParseStream<0>(is);
    fclose(myFile);

    if (newDoc.HasParseError()) {
        log_info("Json Parse error: %d\n", newDoc.GetParseError());
        return ret;
    }
    if (newDoc.HasMember(SN_1)) {
         sn1 = newDoc[SN_1].GetString();
    }
    if (newDoc.HasMember(SN_2)) {
         sn2 = newDoc[SN_2].GetString();
    }
    if (newDoc.HasMember(SN_3)) {
         sn3 = newDoc[SN_3].GetString();
    }
    if (newDoc.HasMember(SN_4)) {
         sn4 = newDoc[SN_4].GetString();
    }
    log_info("Json Parse : %s, %s, %s, %s\n", sn1.c_str(), sn2.c_str(), sn3.c_str(), sn4.c_str());

    rapidjson::Document document;
    document.SetObject();
    rapidjson::Document::AllocatorType& allocator = document.GetAllocator();

    /* 1. add return type */
    document.AddMember("type","DeviceContext",allocator);
    /* 2. add reutn content */
    rapidjson::Value snObj(rapidjson::kObjectType);
    snObj.AddMember(SN_1, rapidjson::StringRef(sn1.c_str()), allocator);
    snObj.AddMember(SN_2, rapidjson::StringRef(sn2.c_str()), allocator);
    snObj.AddMember(SN_3, rapidjson::StringRef(sn3.c_str()), allocator);
    snObj.AddMember(SN_4, rapidjson::StringRef(sn4.c_str()), allocator);
    document.AddMember("content",snObj,allocator);
    /* parse into string */
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    document.Accept(writer);

    return buffer.GetString();
}

static bool saveWifiConfig(const char* name, const char* pwd)
{
    FILE *fp;
    char body[WIFI_CONFIG_MAX];
    int fd;
    fp = fopen("/data/cfg/wpa_supplicant.conf", "w");

    if (fp == NULL)
    {
        return -1;
    }

    snprintf(body, sizeof(body), WIFI_CONFIG_FORMAT, name, pwd);
    fputs(body, fp);
    fflush(fp);
    fd = fileno(fp);
    if (fd >= 0) {
        fsync(fd);
        printf("save wpa_supplicant.conf sucecees.\n");
    }
    fclose(fp);

    return 0;
}

void WifiUtil::connect(void *data) {
	gwifi_cfg = data;
	wifiConnect(gwifi_cfg->ssid, gwifi_cfg->psk, gwifi_cfg->key_mgmt, gwifi_cfg->hide);
}

void WifiUtil::disconnect() {
    Shell::system("wpa_cli -iwlan0 disconnect");
	Shell::system("wpa_cli -iwlan0 disable_network all");
}

void WifiUtil::recovery() {
	printf("=== wifi recovery===\n");
    Shell::system("wpa_cli reconfigure");
	sleep(1);
    Shell::system("wpa_cli reconnect");
}

void WifiUtil::connectJson(char *recv_buff) {
    std::string jsonString = getJsonFromMessage(recv_buff);

    /* get setUp user name and password */
    rapidjson::Document document;
    if (document.Parse(jsonString.c_str()).HasParseError()) {
        log_err("parseJsonFailed \n");
        return;
    }

    std::string userName;
    std::string password;
    std::string sec;

    auto userNameIterator = document.FindMember("ssid");
    if (userNameIterator != document.MemberEnd() && userNameIterator->value.IsString()) {
       	userName = userNameIterator->value.GetString();
    }

    auto passwordIterator = document.FindMember("pwd");
    if (passwordIterator != document.MemberEnd() && passwordIterator->value.IsString()) {
        password = passwordIterator->value.GetString();
    }

    if(userName.empty()||password.empty()){
        log_err("userName or password empty. \n");
        return;
    }

    /* use wpa_cli to connect wifi by ssid and password */
    bool connectResult = wifiConnect(userName,password,sec,0);

    if(connectResult){
        std::thread thread(checkWifiIsConnected);
        thread.detach();

        saveWifiConfig(userName.c_str(), password.c_str());
    }else{
        log_info("wifi connect failed.please check enviroment. \n");
        // TODO play audio: wifi connect failed.
    }
    return;
}

#define EVENT_BUF_SIZE 1024
#define PROPERTY_VALUE_MAX 32
#define PROPERTY_KEY_MAX 32
#include <poll.h>
#include <wpa_ctrl.h>
void wifi_close_sockets();
static const char WPA_EVENT_IGNORE[]    = "CTRL-EVENT-IGNORE ";
static const char IFNAME[]              = "IFNAME=";
static const char IFACE_DIR[]           = "/var/run/wpa_supplicant";
#define WIFI_CHIP_TYPE_PATH				"/sys/class/rkwifi/chip"
#define WIFI_DRIVER_INF         		"/sys/class/rkwifi/driver"
#define IFNAMELEN                       (sizeof(IFNAME) - 1)
static struct wpa_ctrl *ctrl_conn;
static struct wpa_ctrl *monitor_conn;
#define DBG_NETWORK 1

static int exit_sockets[2];
static char primary_iface[PROPERTY_VALUE_MAX] = "wlan0";
char SSID[33];
char PASSWD[65];

#define HOSTAPD "hostapd"
#define WPA_SUPPLICANT "wpa_supplicant"
#define DNSMASQ "dnsmasq"
#define SIMPLE_CONFIG "simple_config"
#define SMART_CONFIG "smart_config"
#define UDHCPC "udhcpc"

int get_pid(const char Name[]) {
    int len;
    char name[32] = {0};
    len = strlen(Name);
    strncpy(name,Name,len);
    name[31] ='\0';
    char cmdresult[256] = {0};
    char cmd[64] = {0};
    FILE *pFile = NULL;
    int  pid = 0;

    sprintf(cmd, "pidof %s", name);
    pFile = popen(cmd, "r");
    if (pFile != NULL)  {
        while (fgets(cmdresult, sizeof(cmdresult), pFile)) {
            pid = atoi(cmdresult);
            break;
        }
        pclose(pFile);
    }
    printf("get_pid pidof %s: %d\n", name, pid);
    return pid;
}

void wifi_close_sockets() {
	if (ctrl_conn != NULL) {
		wpa_ctrl_close(ctrl_conn);
		ctrl_conn = NULL;
	}

	if (monitor_conn != NULL) {
		wpa_ctrl_close(monitor_conn);
		monitor_conn = NULL;
	}

	if (exit_sockets[0] >= 0) {
		close(exit_sockets[0]);
		exit_sockets[0] = -1;
	}

	if (exit_sockets[1] >= 0) {
		close(exit_sockets[1]);
		exit_sockets[1] = -1;
	}
}

int str_starts_with(char * str, char * search_str)
{
	if ((str == NULL) || (search_str == NULL))
		return 0;
	return (strstr(str, search_str) == str);
}

extern "C" {
	extern int m_ping_interval;
}
int dispatch_event(char* event)
{
	if (strstr(event, "CTRL-EVENT-BSS") || strstr(event, "CTRL-EVENT-TERMINATING"))
		return 0;

	if (1)
		printf("%s: %s\n", __func__, event);
	
	if (str_starts_with(event, (char *)WPA_EVENT_DISCONNECTED)) {
		printf("%s: wifi is disconnect\n", __FUNCTION__);
		system("ip addr flush dev wlan0");
		m_ping_interval = 1;
	} else if (str_starts_with(event, (char *)WPA_EVENT_CONNECTED)) {
		printf("%s: wifi is connected\n", __func__);
	} else if (str_starts_with(event, (char *)WPA_EVENT_SCAN_RESULTS)) {
		g_results = true;
		printf("%s: wifi event results g_results: %d \n", __func__, g_results);
	} else if (strstr(event, "reason=WRONG_KEY")) {
		wifi_wrong_key = true;
		printf("%s: wifi reason=WRONG_KEY \n", __func__);
	} else if (str_starts_with(event, (char *)WPA_EVENT_TERMINATING)) {
		printf("%s: wifi is WPA_EVENT_TERMINATING!\n", __func__);
		wifi_close_sockets();
		return -1;
	}

	return 0;
}

int check_wpa_supplicant_state() {
	int count = 5;
	int wpa_supplicant_pid = 0;
	wpa_supplicant_pid = get_pid(WPA_SUPPLICANT);
	printf("%s: wpa_supplicant_pid = %d\n",__FUNCTION__,wpa_supplicant_pid);
	if(wpa_supplicant_pid > 0) {
		return 1;
	}
	return 0;
}

int wifi_ctrl_recv(char *reply, size_t *reply_len)
{
	int res;
	int ctrlfd = wpa_ctrl_get_fd(monitor_conn);
	struct pollfd rfds[2];

	memset(rfds, 0, 2 * sizeof(struct pollfd));
	rfds[0].fd = ctrlfd;
	rfds[0].events |= POLLIN;
	rfds[1].fd = exit_sockets[1];
	rfds[1].events |= POLLIN;
	do {
		res = TEMP_FAILURE_RETRY(poll(rfds, 2, 30000));
		if (res < 0) {
			printf("Error poll = %d\n", res);
			return res;
		} else if (res == 0) {
            /* timed out, check if supplicant is activeor not .. */
			res = check_wpa_supplicant_state();
			if (res < 0)
				return -2;
		}
	} while (res == 0);

	if (rfds[0].revents & POLLIN) {
		return wpa_ctrl_recv(monitor_conn, reply, reply_len);
	}
	return -2;
}

int wifi_wait_on_socket(char *buf, size_t buflen)
{
	size_t nread = buflen - 1;
	int result;
	char *match, *match2;

	if (monitor_conn == NULL) {
		return snprintf(buf, buflen, "IFNAME=%s %s - connection closed",
			primary_iface, WPA_EVENT_TERMINATING);
	}

	result = wifi_ctrl_recv(buf, &nread);

	/* Terminate reception on exit socket */
	if (result == -2) {
		return snprintf(buf, buflen, "IFNAME=%s %s - connection closed",
			primary_iface, WPA_EVENT_TERMINATING);
	}

	if (result < 0) {
		//printf("wifi_ctrl_recv failed: %s\n", strerror(errno));
		//return snprintf(buf, buflen, "IFNAME=%s %s - recv error",
		//	primary_iface, WPA_EVENT_TERMINATING);
	}

	buf[nread] = '\0';

	/* Check for EOF on the socket */
	if (result == 0 && nread == 0) {
        /* Fabricate an event to pass up */
		printf("Received EOF on supplicant socket\n");
		return snprintf(buf, buflen, "IFNAME=%s %s - signal 0 received",
			primary_iface, WPA_EVENT_TERMINATING);
	}

	if (strncmp(buf, IFNAME, IFNAMELEN) == 0) {
		match = strchr(buf, ' ');
        if (match != NULL) {
			if (match[1] == '<') {
				match2 = strchr(match + 2, '>');
					if (match2 != NULL) {
						nread -= (match2 - match);
						memmove(match + 1, match2 + 1, nread - (match - buf) + 1);
					}
			}
		} else {
			return snprintf(buf, buflen, "%s", WPA_EVENT_IGNORE);
		}
	} else if (buf[0] == '<') {
		match = strchr(buf, '>');
		if (match != NULL) {
			nread -= (match + 1 - buf);
			memmove(buf, match + 1, nread + 1);
			if (0)
				printf("supplicant generated event without interface - %s\n", buf);
		}
	} else {
		if (0)
			printf("supplicant generated event without interface and without message level - %s\n", buf);
	}

	return nread;
}

int wifi_connect_on_socket_path(const char *path)
{
	char supp_status[PROPERTY_VALUE_MAX] = {'\0'};

	if(!check_wpa_supplicant_state()) {
		printf("%s: wpa_supplicant is not ready\n",__FUNCTION__);
		return -1;
	}

	ctrl_conn = wpa_ctrl_open(path);
	if (ctrl_conn == NULL) {
		printf("Unable to open connection to supplicant on \"%s\": %s\n",
		path, strerror(errno));
		return -1;
	}
	monitor_conn = wpa_ctrl_open(path);
	if (monitor_conn == NULL) {
		wpa_ctrl_close(ctrl_conn);
		ctrl_conn = NULL;
		return -1;
	}
	if (wpa_ctrl_attach(monitor_conn) != 0) {
		wpa_ctrl_close(monitor_conn);
		wpa_ctrl_close(ctrl_conn);
		ctrl_conn = monitor_conn = NULL;
		return -1;
	}

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, exit_sockets) == -1) {
		wpa_ctrl_close(monitor_conn);
		wpa_ctrl_close(ctrl_conn);
		ctrl_conn = monitor_conn = NULL;
		return -1;
	}
	return 0;
}

/* Establishes the control and monitor socket connections on the interface */
int wifi_connect_to_supplicant()
{
	static char path[1024];
	int count = 10;

	printf("%s \n", __FUNCTION__);
	while(count-- > 0) {
		if (access(IFACE_DIR, F_OK) == 0)
			break;
		sleep(1);
	}

	snprintf(path, sizeof(path), "%s/%s", IFACE_DIR, primary_iface);

	return wifi_connect_on_socket_path(path);
}


void WifiUtil::start_wifi_monitor(void *arg)
{
	char eventStr[EVENT_BUF_SIZE];
	int ret;

	prctl(PR_SET_NAME,"start_wifi_monitor");

	if ((ret = wifi_connect_to_supplicant()) != 0) {
		printf("%s, connect to supplicant fail.\n", __FUNCTION__);
		return;
	}

	for (;;) {
		memset(eventStr, 0, EVENT_BUF_SIZE);
		if (!wifi_wait_on_socket(eventStr, EVENT_BUF_SIZE))
			continue;
		if (dispatch_event(eventStr)) {
			printf("disconnecting from the supplicant, no more events\n");
			break;
		}
	}
}

