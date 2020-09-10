#include <mutex>
#include <stdio.h>
#include <unistd.h>
#include "DeviceIo/WifiManager.h"
#include "Hostapd.h"

namespace DeviceIOFramework {

const std::string TRIM_DELIMITERS = " \f\n\r\t\v";
WifiManager* WifiManager::m_instance;
WifiManager* WifiManager::getInstance() {
	if (m_instance == NULL) {
		static std::mutex mt;
		mt.lock();
		if (m_instance == NULL)
			m_instance = new WifiManager();
		mt.unlock();
	}

	return m_instance;
}

int WifiManager::init(Properties* properties) {
	this->m_properties = properties;
	return 0;
}

static char *spec_char_convers(const char *buf, char *dst)
{
	char buf_temp[strlen(buf) + 1];
	int i = 0;
	unsigned long con;

	memset(buf_temp, 0, sizeof(buf_temp));
	while(*buf != '\0') {
		if(*buf == '\\' && *(buf + 1) == 'x') {
			strcpy(buf_temp, buf);
			*buf_temp = '0';
			*(buf_temp + 4) = '\0';
			con = strtoul(buf_temp, NULL, 16);
			dst[i] = con;
			buf += 4;
		} else {
			dst[i] = *buf;
			buf++;
		}
		i++;
	}
	dst[i] = '\0';
	return dst;
}

static std::string exec(const std::string& cmd) {
	if (cmd.empty())
		return "fail";

	FILE* fp = NULL;
	char buf[128];
	char* ret;
	static int SIZE_UNITE = 512;
	size_t size = SIZE_UNITE;

	fp = popen((const char *) cmd.c_str(), "r");
	if (NULL == fp)
		return "fail";

	memset(buf, 0, sizeof(buf));
	ret = (char*) malloc(sizeof(char) * size);
	memset(ret, 0, sizeof(char) * size);
	while (!feof(fp)) {
		if(fgets(buf, sizeof(buf)-1, fp)) {
			if (size <= (strlen(ret) + strlen(buf))) {
				size += SIZE_UNITE;
				ret = (char*) realloc(ret, sizeof(char) * size);
			}
			strcat(ret, buf);
		}
	}

	pclose(fp);
	ret = (char*) realloc(ret, sizeof(char) * (strlen(ret) + 1));

	char convers[strlen(ret) + 1];
	spec_char_convers(ret, convers);
	std::string str = convers;
	if (NULL != ret)
		free(ret);

	return str;
}

bool WifiManager::isWifiConnected() {
	std::string state;
	state = exec("wpa_cli -iwlan0 status | grep wpa_state | awk -F '=' '{printf $2}'");

	if (0 == state.compare("fail"))
		return false;

	if (0 == state.compare(0, 9, "COMPLETED", 0, 9)) {
		saveConfiguration();
		return true;
	}

	return false;
}

bool WifiManager::isWifiEnabled() {
	std::string pid;
	pid = exec("pidof wpa_supplicant");
	if (0 == pid.compare("fail") || pid.empty()) {
		return false;
	}
	return true;
}

int WifiManager::setWifiEnabled(const bool enable) {
	if (enable) {
		system("ifconfig wlan0 down");
		system("ifconfig wlan0 up");
		system("ifconfig wlan0 0.0.0.0");
		system("killall dhcpcd");
		system("killall wpa_supplicant");
		usleep(500000);
		system("wpa_supplicant -B -i wlan0 -c /data/cfg/wpa_supplicant.conf");
		usleep(500000);
		system("dhcpcd -L -f /etc/dhcpcd.conf");
		system("dhcpcd wlan0 -t 0 &");
	} else {
		system("ifconfig wlan0 down");
		system("killall wpa_supplicant");
	}

	return 0;
}

int WifiManager::enableWifiAp(const std::string& ssid, const std::string& psk, const std::string& ip) {
	return wifi_rtl_start_hostapd(ssid.c_str(), psk.c_str(), ip.c_str());
}

int WifiManager::disableWifiAp() {
	return wifi_rtl_stop_hostapd();
}

int WifiManager::startScan() {
	std::string scan;
	scan = exec("wpa_cli -iwlan0 scan");
	if (0 == scan.compare("fail")) {
		return -1;
	}

	if (scan.find_first_of("OK") == std::string::npos && scan.find_first_of("ok") == std::string::npos) {
		return -2;
	}

	return 0;
}

static std::list<std::string> strToList(const char* str) {
	std::list<std::string> strList;
	std::string item;
	size_t i;
	for (i = 0; i < strlen(str); i++){
		if(str[i] != '\n'){
			item += str[i];
		} else {
			strList.push_back(item);
			item.clear();
		}
	}
	return strList;
}

static std::list<ScanResult*> strToScanResult(std::list<std::string> strList) {
	std::list<ScanResult*> scanResult;
	std::list<std::string>::iterator strIte;

	/* delete first useless item */
	strList.pop_front();

	for (strIte = strList.begin(); strIte != strList.end(); strIte++) {
		std::string strItem = *strIte;
		ScanResult* item = new ScanResult();

		/* use for set item:bssid ssid etc*/
		int index;
		size_t i;
		std::string tmpStr;
		for (index = 0, i = 0; i < strItem.size(); i++) {
			if (strItem.at(i) != '\t')
				tmpStr += strItem.at(i);

			if (strItem.at(i) == '\t' || i == (strItem.size() - 1)){
				switch (index) {
				case 0: //bssid
					item->setBssid(tmpStr);
					break;
				case 1: //frequency
					item->setFrequency(std::stoi(tmpStr));
					break;
				case 2: //signalLevel
					item->setLevel(std::stoi(tmpStr));
					break;
				case 3: //flags
					item->setFlags(tmpStr);
					break;
				case 4: //ssid
					item->setSsid(tmpStr);
					break;
				default:
					break;
				}
				index ++;
				tmpStr.clear();
			}
		}
		scanResult.push_back(item);
	}
	return scanResult;
}

std::list<ScanResult*> WifiManager::getScanResults() {
	std::list<ScanResult*> result;
	std::list<std::string> strList;
	std::string scan_r;
	scan_r = exec("wpa_cli -iwlan0 scan_r");
	if (0 == scan_r.compare("fail"))
		return result;

	strList = strToList((const char*) scan_r.c_str());
	result = strToScanResult(strList);

	return result;
}

int WifiManager::addNetwork() {
	int id = -1;
	std::string cmdRet;

	cmdRet = exec("wpa_cli -iwlan0 add_network");
	if (0 == cmdRet.compare("fail"))
		return -1;

	id = atoi(cmdRet.c_str());

	return id;
}

int WifiManager::setNetwork(const int id, const std::string& ssid, const std::string& psk, const Encryp encryp) {
	char cmd[128];
	std::string cmdRet;

	// 1. set network ssid
	memset(cmd, 0, sizeof(cmd));
	snprintf(cmd, sizeof(cmd), "wpa_cli -iwlan0 set_network %d ssid \\\"%s\\\"", id, ssid.c_str());
	cmdRet = exec(cmd);
	if (0 == cmdRet.compare("fail")) {
		return -1;
	} else if (cmdRet.find_first_of("OK") == std::string::npos && cmdRet.find_first_of("ok") == std::string::npos) {
		return -11;
	}

	// 2. set network psk
	memset(cmd, 0, sizeof(cmd));
	if ("" == psk || encryp == NONE) {
		snprintf(cmd, sizeof(cmd), "wpa_cli -iwlan0 set_network %d key_mgmt NONE", id);
		cmdRet = exec(cmd);
		if (0 == cmdRet.compare("fail")) {
			return -2;
		} else if (cmdRet.find_first_of("OK") == std::string::npos && cmdRet.find_first_of("ok") == std::string::npos) {
			return -21;
		}
	} else if (encryp == WPA) {
		snprintf(cmd, sizeof(cmd), "wpa_cli -iwlan0 set_network %d psk \\\"%s\\\"", id, psk.c_str());
		cmdRet = exec(cmd);
		if (0 == cmdRet.compare("fail")) {
			return -2;
		} else if (cmdRet.find_first_of("OK") == std::string::npos && cmdRet.find_first_of("ok") == std::string::npos) {
			return -21;
		}
	} else if (encryp == WEP) {
		snprintf(cmd, sizeof(cmd), "wpa_cli -iwlan0 set_network %d key_mgmt NONE", id);
		cmdRet = exec(cmd);
		if (0 == cmdRet.compare("fail")) {
			return -2;
		} else if (cmdRet.find_first_of("OK") == std::string::npos && cmdRet.find_first_of("ok") == std::string::npos) {
			return -21;
		}

		memset(cmd, 0, sizeof(cmd));
		snprintf(cmd, sizeof(cmd), "wpa_cli -iwlan0 set_network %d wep_key0 \\\"%s\\\"", id, psk.c_str());
		cmdRet = exec(cmd);
		if (0 == cmdRet.compare("fail")) {
			return -22;
		} else if (cmdRet.find_first_of("OK") == std::string::npos && cmdRet.find_first_of("ok") == std::string::npos) {
			return -221;
		}
	}

	return 0;
}

int WifiManager::selectNetwork(const int id) {
	char cmd[128];
	std::string cmdRet;

	memset(cmd, 0, sizeof(cmd));
	snprintf(cmd, sizeof(cmd), "wpa_cli -iwlan0 select_network %d", id);
	cmdRet = exec(cmd);

	if (0 == cmdRet.compare("fail")) {
		return -1;
	} else if (cmdRet.find_first_of("OK") == std::string::npos && cmdRet.find_first_of("ok") == std::string::npos) {
		return -2;
	}

	return 0;
}

int WifiManager::enableNetwork(const int id) {
	char cmd[128];
	std::string cmdRet;

	memset(cmd, 0, sizeof(cmd));
	snprintf(cmd, sizeof(cmd), "wpa_cli -iwlan0 enable_network %d", id);
	cmdRet = exec(cmd);

	if (0 == cmdRet.compare("fail")) {
		return -1;
	} else if (cmdRet.find_first_of("OK") == std::string::npos && cmdRet.find_first_of("ok") == std::string::npos) {
		return -2;
	}

	return 0;
}

int WifiManager::udhcpc() {
	char cmd[128];
	std::string cmdRet;

	memset(cmd, 0, sizeof(cmd));
	snprintf(cmd, sizeof(cmd), "killall udhcpc");
	cmdRet = exec(cmd);
	if (0 == cmdRet.compare("fail"))
		return -1;

	memset(cmd, 0, sizeof(cmd));
	snprintf(cmd, sizeof(cmd), "udhcpc -n -t 10 -i wlan0");
	cmdRet = exec(cmd);
	if (0 == cmdRet.compare("fail"))
		return -2;

	return 0;
}

int WifiManager::saveConfiguration() {
	system("wpa_cli enable_network all");
	system("wpa_cli save_config");

	return 0;
}

int WifiManager::connect(const std::string& ssid, const std::string& pwd, const Encryp encryp, const int hide) {
	int id, ret;

	id = addNetwork();
	if (id < 0) {
		printf("%s: addNetwork (%s, %s) failed, id = %d\n", __func__, ssid.c_str(), pwd.c_str(), id);
		return -1;
	}

	ret = setNetwork(id, ssid, pwd, encryp);
	if (0 != ret) {
		printf("%s: setNetwork (%s, %s) failed, ret = %d\n", __func__, ssid.c_str(), pwd.c_str(), ret);
		return -2;
	}

	ret = selectNetwork(id);
	if (0 != ret) {
		printf("%s: selectNetwork (%s, %s) failed, ret = %d\n", __func__, ssid.c_str(), pwd.c_str(), ret);
		return -3;
	}

	ret = enableNetwork(id);
	if (0 != ret) {
		printf("%s: enableNetwork (%s, %s) failed, ret = %d\n", __func__, ssid.c_str(), pwd.c_str(), ret);
		return -4;
	}

	std::string pid;
	pid = exec("pidof dhcpcd");
	if (0 == pid.compare("fail") || pid.empty()) {
		exec("/sbin/dhcpcd -f /etc/dhcpcd.conf");;
	}

	return ret;
}

int WifiManager::disconnect() {
	return 0;
}

static std::string ltrim(const std::string& str) {
	std::string::size_type s = str.find_first_not_of(TRIM_DELIMITERS);
	if (s == std::string::npos) {
		return "";
	}
	return str.substr(s);
}

static std::string rtrim(const std::string& str) {
	std::string::size_type s = str.find_last_not_of(TRIM_DELIMITERS);
	if (s == std::string::npos) {
		return "";
	}
	return str.substr(0, s+1);
}

static std::string trim(const std::string& str) {
	return rtrim(ltrim(str));
}

static bool isProperty(const std::string& str) {
	std::string trimmedStr = ltrim(str);
	std::string::size_type s = trimmedStr.find_first_of("=");
	if (s == std::string::npos) {
		return false;
	}
	std::string key = trim(trimmedStr.substr(0, s));
	// key can't be empty
	if (key == "") {
		return false;
	}
	return true;
}

static std::pair<std::string, std::string> parse(const std::string& str) {
	std::string trimmedStr = trim(str);
	std::string::size_type s = trimmedStr.find_first_of("=");
	std::string key = rtrim(trimmedStr.substr(0, s));
	std::string value = ltrim(trimmedStr.substr(s + 1));

	return std::pair<std::string, std::string>(key, value);
}

WifiInfo* WifiManager::getConnectionInfo() {
	WifiInfo* info = new WifiInfo();
	std::string status;
	std::list<std::string> strList;
	std::list<std::string>::iterator strIte;

	status = exec("wpa_cli -iwlan0 status");
	if (0 == status.compare("fail"))
		return info;

	strList = strToList((const char*) status.c_str());
	for (strIte = strList.begin(); strIte != strList.end(); strIte++) {
		std::string strItem = *strIte;
		if (isProperty(strItem)) {
			std::pair<std::string, std::string> prop = parse(strItem);
			if (0 == prop.first.compare("id")) {
				info->setNetworkId(std::stoi(prop.second));
			} else if (0 == prop.first.compare("bssid")) {
				info->setBssid(prop.second);
			} else if (0 == prop.first.compare("ssid")) {
				info->setSsid(prop.second);
			} else if (0 == prop.first.compare("freq")) {
				info->setFrequency(std::stoi(prop.second));
			} else if (0 == prop.first.compare("mode")) {
				info->setMode(prop.second);
			} else if (0 == prop.first.compare("wpa_state")) {
				info->setWpaState(prop.second);
			} else if (0 == prop.first.compare("ip_address")) {
				info->setIpAddress(prop.second);
			} else if (0 == prop.first.compare("address")) {
				info->setMacAddress(prop.second);
			}
		}
	}

	return info;
}

} // end of namespace DeviceIOFramework
