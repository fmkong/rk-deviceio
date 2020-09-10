/*
 * Copyright (c) 2014 Fredy Wijaya
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include <mutex>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <unistd.h>
#include "rapidjson/rapidjson.h"
#include "rapidjson/document.h"
#include "rapidjson/filereadstream.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/prettywriter.h"
#include <DeviceIo/Rk_wifi.h>
#include "UdpServer.h"

const char* MSG_BROADCAST_AP_MODE = "{\"method\":\"softAP\", \"magic\":\"KugouMusic\", \"params\":\"ap_wifi_mode\"}";
const char* MSG_WIFI_CONNECTING = "{\"method\":\"softAP\", \"magic\":\"KugouMusic\", \"params\":\"wifi_connecting\"}";
const char* MSG_WIFI_CONNECTED = "{\"method\":\"softAP\", \"magic\":\"KugouMusic\", \"params\":\"wifi_connected\"}";
const char* MSG_WIFI_FAILED = "{\"method\":\"softAP\", \"magic\":\"KugouMusic\", \"params\":\"wifi_failed\"}";
const char* MSG_WIFI_LIST_FORMAT = "{\"method\":\"softAP\", \"magic\":\"KugouMusic\", \"params\":{\"wifilist\":%s}}";

namespace DeviceIOFramework {

static std::string m_broadcastMsg = "";
static bool m_isConnecting = false;
static RK_SOFTAP_STATE_CALLBACK m_cb = NULL;
static int m_fd_broadcast = -1;
static sockaddr_in m_addrto;
static RK_SOFTAP_STATE m_state = RK_SOFTAP_STATE_IDLE;

UdpServer* UdpServer::m_instance;
UdpServer* UdpServer::getInstance() {
	if (m_instance == NULL) {
		static std::mutex mt;
		mt.lock();
		if (m_instance == NULL)
			m_instance = new UdpServer();

		mt.unlock();
	}
	return m_instance;
}

UdpServer::UdpServer() {
	m_wifiManager = WifiManager::getInstance();
	m_thread_broadcast = -1;
	m_thread = 0;
	m_fd_broadcast = -1;
	m_state = RK_SOFTAP_STATE_IDLE;
}

bool UdpServer::isRunning() {
	return (m_thread >= 0);
}

static void sendState(RK_SOFTAP_STATE state, const char* data) {
	if(m_cb != NULL)
		m_cb(state, data);
}

static int initSocket(const unsigned int port) {
	int ret, fd_socket;
	struct sockaddr_in server_addr;

	/* create a socket */
	fd_socket = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd_socket < 0) {
		return -1;
	}

	/*  initialize server address */
	bzero(&server_addr, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	server_addr.sin_port = htons(port);

	/* bind with the local file */
	ret = bind(fd_socket, (struct sockaddr *)&server_addr, sizeof(server_addr));
	if (ret < 0) {
		close(fd_socket);
		return -2;
	}

	return fd_socket;
}

void* checkWifi(void *arg) {
	printf("Wifi check thread start...\n");
	WifiManager* wifiManager = WifiManager::getInstance();

	prctl(PR_SET_NAME,"checkWifi");

	bool ret = false;
	int time;
	for (time = 0; time < 60; time++) {
		if (wifiManager->isWifiConnected()) {
			ret = true;
			break;
		}
		sleep(1);
	}

	m_broadcastMsg = (ret ? MSG_WIFI_CONNECTED : MSG_WIFI_FAILED);
	printf("UDP broadcast sendto \"%s\"\n", m_broadcastMsg.c_str());
	sendto(m_fd_broadcast, m_broadcastMsg.c_str(), strlen(m_broadcastMsg.c_str()), 0,
			(struct sockaddr*)&m_addrto, sizeof(m_addrto));
	m_isConnecting = false;
	printf("Wifi connect result %d\n", ret ? 1 : 0);
	return NULL;
}

static void handleRequest(const char* buff) {
	rapidjson::Document document;
	rapidjson::Value params;
	std::string ssid;
	std::string passwd;
	std::string cmd;
	std::string userdata;

	if (document.Parse(buff).HasParseError()) {
		printf("UdpServer handleRequest parse error \"%s\"", buff);
		return;
	}

	if (document.HasMember("params")) {
		params = document["params"];

		if (params.IsString()) {
			std::string para;

			para = params.GetString();
			if (0 == strcmp(para.c_str(), "wifi_connected")) {
				m_broadcastMsg = "";
				sendState(RK_SOFTAP_STATE_SUCCESS, NULL);
				m_state = RK_SOFTAP_STATE_SUCCESS;
			} else if (0 == strcmp(para.c_str(), "wifi_failed")) {
				m_broadcastMsg = "";
				sendState(RK_SOFTAP_STATE_FAIL, NULL);
				m_state = RK_SOFTAP_STATE_FAIL;
			}
			return;
		}

		if (!params.IsObject())
			return;

		if (params.HasMember("cmd") && params["cmd"].IsString()) {
			cmd = params["cmd"].GetString();
			if (cmd.empty())
				return;

			if (0 == strcmp(cmd.c_str(), "getWifilists")) {
				char *wifilist;
				m_broadcastMsg = "";

				system("rm -rf /tmp/scan_r");
				RK_wifi_scan();
				int i = 0;
				FILE *fi;
				while (i < 3) {
					fi = fopen("/tmp/scan_r", "rb");
					if (fi) {
						fclose(fi);
						break;
					}
					i--;
					sleep(1);
				}
				wifilist = RK_wifi_scan_r_sec(0x14);

				printf("handle getWifilists: \"%s\"\n", wifilist);
				if (strlen(wifilist) > 2) {
					char tmp[strlen(wifilist) + 512];
					memset(tmp, 0, sizeof(tmp));

					snprintf(tmp, sizeof(tmp), MSG_WIFI_LIST_FORMAT, wifilist);

					m_broadcastMsg = tmp;
					sendto(m_fd_broadcast, m_broadcastMsg.c_str(), strlen(m_broadcastMsg.c_str()), 0,
						(struct sockaddr*)&m_addrto, sizeof(m_addrto));
				}
				free(wifilist);
			}
		} else {
			if (params.HasMember("ssid") && params["ssid"].IsString()) {
				ssid = params["ssid"].GetString();
			}
			if (params.HasMember("passwd") && params["passwd"].IsString()) {
				passwd = params["passwd"].GetString();
			}
			if (params.HasMember("userdata") && params["userdata"].IsString()) {
				userdata = params["userdata"].GetString();
			}
			printf("do connect ssid:\"%s\", psk:\"%s\", isConnecting:%d\n", ssid.c_str(), passwd.c_str(), m_isConnecting);
			if (!m_isConnecting && !ssid.empty()) {
				m_broadcastMsg = MSG_WIFI_CONNECTING;
				printf("UDP broadcast sendto \"%s\"\n", m_broadcastMsg.c_str());
				sendto(m_fd_broadcast, m_broadcastMsg.c_str(), strlen(m_broadcastMsg.c_str()), 0,
						(struct sockaddr*)&m_addrto, sizeof(m_addrto));
				sendState(RK_SOFTAP_STATE_CONNECTTING, userdata.c_str());
				m_state = RK_SOFTAP_STATE_CONNECTTING;
				WifiManager* wifiManager = WifiManager::getInstance();
				int id = wifiManager->connect(ssid, passwd);
				if (0 != id) {
					printf("wifi connect failed %d. ssid:\"%s\", id, psk:\"%s\"\n", id, ssid.c_str(), passwd.c_str());
					m_broadcastMsg = MSG_WIFI_FAILED;
					sendto(m_fd_broadcast, m_broadcastMsg.c_str(), strlen(m_broadcastMsg.c_str()), 0,
							(struct sockaddr*)&m_addrto, sizeof(m_addrto));
					sendState(RK_SOFTAP_STATE_FAIL, NULL);
					m_state = RK_SOFTAP_STATE_FAIL;
					m_isConnecting = false;
					return;
				}
				m_isConnecting = true;
				pthread_t pid;
				pthread_create(&pid, NULL, checkWifi, NULL);
				pthread_detach(pid);
			}
		}
	}
}

void* UdpServer::threadAccept(void *arg) {
	int fd_server, port;
	struct sockaddr_in addr_client;
	socklen_t len_addr_client;
	len_addr_client = sizeof(addr_client);
	char buff[512 + 1];
	int n;

	prctl(PR_SET_NAME,"udp threadAccept");

	port = *(int*) arg;
	fd_server = initSocket(port);

	if (fd_server < 0) {
		printf("UdpServer::threadAccept init tcp socket port %d fail. error:%d\n", port, fd_server);
		goto end;
	}

	/* Recv from all time */
	while (1) {
		memset(buff, 0, sizeof(buff));
		n = recvfrom(fd_server, buff, sizeof(buff) - 1, 0, (struct sockaddr*)&addr_client, &len_addr_client);
		if (n < 0)
			goto end;

		printf("UDP broadcast recvfrom \"%s\"\n", buff);
		handleRequest(buff);
	}

end:
	if (fd_server >= 0)
		close(fd_server);
	m_instance->m_thread = 0;

	return NULL;
}

int UdpServer::startUdpServer(const unsigned int port) {
	int ret;

	startBroadcastThread();

	m_port = port;
	ret = pthread_create(&m_thread, NULL, threadAccept, &m_port);
	if (0 != ret) {
		m_thread = 0;
	}
	return ret;
}

void* UdpServer::threadBroadcast(void* arg) {
	int sock, port, ret;
	const int opt = 1;
	struct sockaddr_in addrto;

	prctl(PR_SET_NAME,"udp threadBroadcast");

	port = *(int*) arg;
	bzero(&addrto, sizeof(struct sockaddr_in));
	addrto.sin_family = AF_INET;
	addrto.sin_addr.s_addr = htonl(INADDR_BROADCAST);
	addrto.sin_port = htons(port);

	if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		printf("create udp broadcast socket of port %d failed. error:%d\n", port, sock);
		goto end;
	}
	m_fd_broadcast = sock;
	m_addrto = addrto;

	ret = setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (char *)&opt, sizeof(opt));
	if (ret < 0) {
		printf("udp broadcast setsockopt failed. error:%d\n", ret);
		goto end;
	}

	m_broadcastMsg = MSG_BROADCAST_AP_MODE;
	while (true) {
		if (!m_broadcastMsg.empty()) {
			printf("UDP broadcast sendto \"%s\"\n", m_broadcastMsg.c_str());
			ret = sendto(sock, m_broadcastMsg.c_str(), m_broadcastMsg.size(), 0, (struct sockaddr*)&addrto, sizeof(addrto));
			if (ret < 0) {
				printf("udp send broadcast failed. error:%d\n", ret);
			}
		}
		sleep(1);
	}

end:
	if (sock >= 0)
		close(sock);
	m_fd_broadcast = -1;
	m_instance->m_thread_broadcast = -1;

	return NULL;
}

int UdpServer::startBroadcastThread(const unsigned int port) {
	int ret;

	m_port_broadcast = port;
	ret = pthread_create(&m_thread_broadcast, NULL, threadBroadcast, &m_port_broadcast);
	if (0 != ret) {
		m_thread_broadcast = -1;
	}
	return ret;
}

void UdpServer::registerCallback(RK_SOFTAP_STATE_CALLBACK cb) {
	m_cb = cb;
}

RK_SOFTAP_STATE UdpServer::getState() {
	return m_state;
}

int UdpServer::stopBroadcastThread() {
	if (m_thread_broadcast < 0)
		return 0;

	if (0 != pthread_cancel(m_thread_broadcast)) {
		return -1;
	}

	if (0 != pthread_join(m_thread_broadcast, NULL)) {
		return -1;
	}

	m_thread_broadcast = -1;
	return 0;
}

int UdpServer::stopUdpServer() {
	stopBroadcastThread();
	if (m_thread <= 0)
		return 0;

	if (0 != pthread_cancel(m_thread)) {
		return -1;
	}

	if (0 != pthread_join(m_thread, NULL)) {
		return -1;
	}

	m_thread = 0;
	return 0;
}

} // namespace framework
