#include <string>
#include <stdio.h>
#include <stdlib.h>
#include "DeviceIo/Rk_softap.h"
#include "DeviceIo/WifiManager.h"
#include "UdpServer.h"
#include "TcpServer.h"

typedef struct {
	RK_SOFTAP_STATE_CALLBACK callback;
	RK_SOFTAP_SERVER_TYPE server_type;
} RkSoftAp;

RkSoftAp m_softap;

int RK_softap_register_callback(RK_SOFTAP_STATE_CALLBACK cb) {
	m_softap.callback = cb;
	return 0;
}

int RK_softap_start(char* name, RK_SOFTAP_SERVER_TYPE server_type) {
	int ret;
	DeviceIOFramework::WifiManager* wifiManager = DeviceIOFramework::WifiManager::getInstance();
	// check whether wifi enabled, if not enable first
	if (!wifiManager->isWifiEnabled()) {
		wifiManager->setWifiEnabled(true);
	}
	// start ap mode
	wifiManager->enableWifiAp(name);

	m_softap.server_type = server_type;
	if(server_type == RK_SOFTAP_UDP_SERVER) {
		// start udp server
		DeviceIOFramework::UdpServer* server = DeviceIOFramework::UdpServer::getInstance();
		server->registerCallback(m_softap.callback);
		ret = server->startUdpServer();
	} else {
		// start tcp server
		DeviceIOFramework::TcpServer* server = DeviceIOFramework::TcpServer::getInstance();
		server->registerCallback(m_softap.callback);
		ret = server->startTcpServer();
	}

	return ret;
}

int RK_softap_stop(void) {
	int ret;

	if(m_softap.server_type == RK_SOFTAP_UDP_SERVER) {
		// stop udp server
		DeviceIOFramework::UdpServer* server = DeviceIOFramework::UdpServer::getInstance();
		ret = server->stopUdpServer();
	} else {
		// stop tcp server
		DeviceIOFramework::TcpServer* server = DeviceIOFramework::TcpServer::getInstance();
		ret = server->stopTcpServer();
	}

	// stop ap mode
	DeviceIOFramework::WifiManager* wifiManager = DeviceIOFramework::WifiManager::getInstance();
	ret = wifiManager->disableWifiAp();

	return ret;
}

int RK_softap_getState(RK_SOFTAP_STATE* pState) {
	if(m_softap.server_type == RK_SOFTAP_UDP_SERVER) {
		DeviceIOFramework::UdpServer* server = DeviceIOFramework::UdpServer::getInstance();
		*pState = server->getState();
	} else {
		DeviceIOFramework::TcpServer* server = DeviceIOFramework::TcpServer::getInstance();
		*pState = server->getState();
	}
	return 0;
}
