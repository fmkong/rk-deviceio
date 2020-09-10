#include <iostream>
#include <stdio.h>
#include "DeviceIo/WifiManager.h"
#include "DeviceIo/Rk_softap.h"
#include "../TcpServer.h"

static void help(void) {
	printf("Usage: deviceio_netlink_test INTERFACE [ARGS...]\n\n");
	printf("  startTcpServer [port]     start tcp server with port\n");
}

static bool isRunning(DeviceIOFramework::TcpServer* server)
{
	bool isRun = server->isRunning();
	if (isRun) {
		printf("Tcp server is running!\n");
	} else {
		printf("Tcp server is not running!\n");
	}

	return isRun;
}

static int state_callback(RK_SOFTAP_STATE state, const char* data)
{
	printf("State called back %d.\n", state);

	return 0;
}

static int startTcpServer(DeviceIOFramework::TcpServer* server, const unsigned int port = 8443)
{
#if 0
	int ret = server->startTcpServer(port);
	if (ret != 0) {
		printf("Tcp server start fail! port:%u\n; error:%d.", port, ret);
	} else {
		printf("Tcp server start success! port:%u\n", port);
	}
	return ret;
#else
	// register state callback first
	RK_softap_register_callback(state_callback);
	// start softap
	RK_softap_start("Rockchip-Echo-SoftAp", RK_SOFTAP_TCP_SERVER);

	int len;
	char data[512];
	RK_SOFTAP_STATE state;

	// loop to get state for test
	/*for (;;) {
		memset(data, 0, sizeof(data));
		RK_softap_getState(&state);
		RK_softap_get_exdata(data, &len);
		printf("Current state %d.\n", state);
		printf("userdata len:%d; data:%s\n", len, data);
		sleep(1);
	}
	RK_softap_stop();*/

	return 0;
#endif
}

static int stopTcpServer(DeviceIOFramework::TcpServer* server)
{
	int ret = server->stopTcpServer();
	if (0 != ret) {
		printf("Tcp server stop fail!\n");
	} else {
		printf("Tcp server stop success!\n");
	}
	return ret;
}

#if 0
int main(int argc, char** argv)
{
	DeviceIOFramework::WifiManager* manager = DeviceIOFramework::WifiManager::getInstance();
	DeviceIOFramework::TcpServer* server = DeviceIOFramework::TcpServer::getInstance();

	if (argc < 2 || 0 == strcmp(argv[1], "-h") || 0 == strcmp(argv[1], "--help")) {
		help();
		return 0;
	}

	if (0 == strcmp(argv[1], "startTcpServer")) {
		if (0 != manager->enableWifiAp("Rockchip-Echo-Test")) {
			printf("Enable wifi ap mode failed...\n");
			return 0;
		}
		if (argc < 3) {
			startTcpServer(server);
		} else {
			startTcpServer(server, std::stoi(argv[2]));
		}
	}
	for (;;);

	delete server;
	manager->disableWifiAp();
	delete manager;
	return 0;
}
#endif
