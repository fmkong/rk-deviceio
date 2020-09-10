#include <list>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "DeviceIo/ScanResult.h"
#include "DeviceIo/WifiInfo.h"
#include "DeviceIo/WifiManager.h"

static void help(void) {
	printf("Usage: WifiManagerTest INTERFACE [ARGS...]\n\n");
	printf("  isWifiEnabled             return whether wifi enabled\n");
	printf("  setWifiEnabled true/false set wifi enabled or not\n");
	printf("  enableWifiAp ssid [psk] [ip]\n");
	printf("                            enter ap mode");
	printf("  disableWifiAp             exit ap mode");
	printf("  startScan  				start scan wifi\n");
	printf("  getScanResults            return scaned wifi list\n");
	printf("  connect ssid psk [WPA, WEP, NONE] [0, 1] \n");
	printf("                            to connect wifi\n");
	printf("  disconnect                to disconnect wifi\n");
	printf("  getConnectionInfo         get connection info\n");
}

static bool isWifiEnabled() {
	bool enable;
	DeviceIOFramework::WifiManager* wifiManager = DeviceIOFramework::WifiManager::getInstance();
	enable = wifiManager->isWifiEnabled();

	printf("%s\n", enable ? "true" : "false");

	delete wifiManager;
	return enable;
}

static int setWifiEnabled(bool enable) {
	DeviceIOFramework::WifiManager* wifiManager = DeviceIOFramework::WifiManager::getInstance();
	int ret = wifiManager->setWifiEnabled(enable);

	if (0 == ret)
		printf("OK\n");

	delete wifiManager;
	return ret;
}

static int enableWifiAp(const std::string& ssid) {
	DeviceIOFramework::WifiManager* wifiManager = DeviceIOFramework::WifiManager::getInstance();
	int ret = wifiManager->enableWifiAp(ssid);
	if (0 == ret)
		printf("OK\n");

	delete wifiManager;
	return ret;
}

static int disableWifiAp() {
	DeviceIOFramework::WifiManager* wifiManager = DeviceIOFramework::WifiManager::getInstance();
	int ret = wifiManager->disableWifiAp();

	if (0 == ret)
		printf("OK\n");

	delete wifiManager;
	return ret;
}

static int startScan() {
	DeviceIOFramework::WifiManager* wifiManager = DeviceIOFramework::WifiManager::getInstance();
	int ret = wifiManager->startScan();

	if (0 == ret)
		printf("OK\n");

	delete wifiManager;
	return ret;
}

static std::list<DeviceIOFramework::ScanResult*> getScanResults() {
	DeviceIOFramework::WifiManager* wifiManager = DeviceIOFramework::WifiManager::getInstance();
	std::list<DeviceIOFramework::ScanResult*> scanResults = wifiManager->getScanResults();
	std::list<DeviceIOFramework::ScanResult*>::iterator iterator;
	for (iterator = scanResults.begin(); iterator != scanResults.end(); iterator++) {
		DeviceIOFramework::ScanResult* item = *iterator;
		printf("%s\n", item->toString().c_str());
	}

	delete wifiManager;
	return scanResults;
}

static int connect(const std::string& ssid,
	const std::string& psk = "",
	const DeviceIOFramework::WifiManager::Encryp encryp = DeviceIOFramework::WifiManager::WPA,
	const int hide = 0) {
	int ret;
	DeviceIOFramework::WifiManager* wifiManager = DeviceIOFramework::WifiManager::getInstance();
	ret = wifiManager->connect(ssid, psk, encryp, hide);

	if (0 == ret)
		printf("OK\n");

	delete wifiManager;
	return 0;
}

static int disconnect() {
	int ret;
	DeviceIOFramework::WifiManager* wifiManager = DeviceIOFramework::WifiManager::getInstance();
	ret = wifiManager->disconnect();

	if (0 == ret)
		printf("OK\n");

	delete wifiManager;
	return ret;
}

static DeviceIOFramework::WifiInfo* getConnectionInfo() {
	DeviceIOFramework::WifiManager* wifiManager = DeviceIOFramework::WifiManager::getInstance();
	DeviceIOFramework::WifiInfo* info = wifiManager->getConnectionInfo();

	printf("%s\n", info->toString().c_str());
	delete wifiManager;
	return info;
}

int main(int argc, char** argv) {
	if (argc < 2 || 0 == strcmp(argv[1], "-h") || 0 == strcmp(argv[1], "--help")) {
		help();
		return 0;
	}
	if (0 == strcmp(argv[1], "isWifiEnabled")) {
		isWifiEnabled();
	} else if (0 == strcmp(argv[1], "setWifiEnabled")) {
		bool enable;
		if (argc < 3) {
			help();
			return 0;
		}
		if (0 == strcmp(argv[2], "true")) {
			enable = true;
		} else if (0 == strcmp(argv[2], "false")) {
			enable = false;
		} else {
			help();
			return 0;
		}

		setWifiEnabled(enable);
	} else if (0 == strcmp(argv[1], "enableWifiAp")) {
		if (argc < 3) {
			help();
			return 0;
		}
		enableWifiAp(argv[2]);
	} else if (0 == strcmp(argv[1], "disableWifiAp")) {
		disableWifiAp();
	} else if (0 == strcmp(argv[1], "startScan")) {
		startScan();
	} else if (0 == strcmp(argv[1], "getScanResults")) {
		getScanResults();
	} else if (0 == strcmp(argv[1], "connect")) {
		if (argc < 4) {
			help();
			return 0;
		}
		connect(argv[2], argv[3]);
	} else if (0 == strcmp(argv[1], "disconnect")) {
		disconnect();
	} else if (0 == strcmp(argv[1], "getConnectionInfo")) {
		getConnectionInfo();
	} else {
		help();
		return 0;
	}

	return 0;
}
