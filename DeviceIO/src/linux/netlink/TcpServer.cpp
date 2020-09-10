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
#include "TcpServer.h"

#define REQUEST_WIFI_LIST					"/provision/wifiListInfo"
#define REQUEST_WIFI_SET_UP					"/provision/wifiSetup"
#define REQUEST_IS_WIFI_CONNECTED			"/provision/wifiState"
#define REQUEST_POST_CONNECT_RESULT			"/provision/connectResult"

#define MSG_BUFF_LEN 8888
static char HTTP_RESPOSE_MESSAGE[] = "HTTP/1.1 200 OK\r\nContent-Type:text/html\r\nContent-Length:%d\r\n\r\n%s";

namespace DeviceIOFramework {

static bool m_isConnecting = false;
static RK_SOFTAP_STATE_CALLBACK m_cb = NULL;
static RK_SOFTAP_STATE m_state = RK_SOFTAP_STATE_IDLE;
static int fd_server = -1;

TcpServer* TcpServer::m_instance;
TcpServer* TcpServer::getInstance() {
	if (m_instance == NULL) {
		static std::mutex mt;
		mt.lock();
		if (m_instance == NULL)
			m_instance = new TcpServer();

		mt.unlock();
	}
	return m_instance;
}

TcpServer::TcpServer() {
	m_wifiManager = WifiManager::getInstance();
	m_thread = 0;
	m_state = RK_SOFTAP_STATE_IDLE;
}

bool TcpServer::isRunning() {
	return (m_thread > 0);
}

static void sendState(RK_SOFTAP_STATE state, const char* data) {
	if(m_cb != NULL)
		m_cb(state, data);
}

static int initSocket(const unsigned int port) {
	int ret, fd_socket, val = 1;
	struct sockaddr_in server_addr;

	/* create a socket */
	fd_socket = socket(AF_INET, SOCK_STREAM, 0);
	if (fd_socket < 0) {
		printf("%s: create socket failed\n", __func__);
		return -1;
	}

	/* set socket non-blocking */
	//int flags;
	//flags = fcntl(fd_socket, F_GETFL, 0);
	//fcntl(sock, F_SETFL, flags | O_NONBLOCK);

	ret = setsockopt(fd_socket, SOL_SOCKET, SO_REUSEADDR, (void *)&val, sizeof(int));
	if (ret < 0) {
		printf("%s: setsockopt failed, ret: %d\n", __func__, ret);
		return -2;
	}

	/* initialize server address */
	bzero(&server_addr, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	server_addr.sin_port = htons(port);

	/* bind with the local file */
	ret = bind(fd_socket, (struct sockaddr *)&server_addr, sizeof(server_addr));
	if (ret < 0) {
		printf("%s: bind failed, ret: %d\n", __func__, ret);
		close(fd_socket);
		return -3;
	}

	/* listen */
	ret = listen(fd_socket, 1);
	if (ret < 0) {
		printf("%s: listen failed, ret: %d\n", __func__, ret);
		close(fd_socket);
		return -4;
	}

	return fd_socket;
}

static bool sendWifiList(const int fd, const char* buf) {
	WifiManager* wifiManager;
	std::list<ScanResult*> scanResults;
	std::list<ScanResult*>::iterator iterator;
	char msg[MSG_BUFF_LEN] = {0};
	size_t i, size = 0;
	std::string json;
	json = "{\"type\":\"WifiList\", \"content\":[";

	wifiManager = WifiManager::getInstance();

scan_retry:
	wifiManager->startScan();
	sleep(1);
	scanResults = wifiManager->getScanResults();

	size = scanResults.size();
	if(size <= 0)
		goto scan_retry;

	for (iterator = scanResults.begin(), i = 0; iterator != scanResults.end(); iterator++, i++) {
		ScanResult* item = *iterator;
		json += item->toString();

		if (i < size - 1) {
			json += ", ";
		}
	}

	json += "]}";

	snprintf(msg, sizeof(msg), HTTP_RESPOSE_MESSAGE, strlen(json.c_str()), json.c_str());
	if (send(fd, msg, sizeof(msg), 0) < 0) {
		return false;
	}

	return true;
}

static bool isWifiConnected(const int fd, const char* buf) {
	char msg[MSG_BUFF_LEN] = {0};
	WifiManager* wifiManager;
	bool isConn;

	wifiManager = WifiManager::getInstance();
	isConn = wifiManager->isWifiConnected();

	memset(msg, 0, sizeof(msg));
	snprintf(msg, sizeof(msg), HTTP_RESPOSE_MESSAGE, 1, isConn ? "1" : "0");

	m_isConnecting = false;

	if (send(fd, msg, sizeof(msg), 0) < 0) {
		return false;
	}

	return true;
}

static bool wifiSetup(const int fd, const char* buf) {
	char msg[MSG_BUFF_LEN] = {0};
	WifiManager* wifiManager;

	printf("enter %s\n", __func__);

	memset(msg, 0, sizeof(msg));
	snprintf(msg, sizeof(msg), HTTP_RESPOSE_MESSAGE, 0, "");
	if (send(fd, msg, sizeof(msg), 0) < 0) {
		return false;
	}

	std::string json(buf);
	json = json.substr(json.find('{'));

	rapidjson::Document document;
	if (document.Parse(json.c_str()).HasParseError()) {
		printf("parseJsonFailed \n");
		return false;
	}
	std::string ssid;
	std::string psk;
	//std::string userdata;
	auto ssidIterator = document.FindMember("ssid");
	if (ssidIterator != document.MemberEnd() && ssidIterator->value.IsString()) {
		ssid = ssidIterator->value.GetString();
	}

	auto pskIterator = document.FindMember("pwd");
	if (pskIterator != document.MemberEnd() && pskIterator->value.IsString()) {
		psk = pskIterator->value.GetString();
	}
/*
	auto dataIterator = document.FindMember("userdata");
	if (dataIterator != document.MemberEnd() && dataIterator->value.IsString()) {
		userdata = dataIterator->value.GetString();
	}
*/
	printf("do connect ssid:\"%s\", psk:\"%s\", isConnecting:%d\n", ssid.c_str(), psk.c_str(), m_isConnecting);

	if(!m_isConnecting) {
		if(ssid.empty() || psk.empty()){
			printf("userName or password empty. \n");
			return false;
		}

		sendState(RK_SOFTAP_STATE_CONNECTTING, NULL/*userdata.c_str()*/);
		m_state = RK_SOFTAP_STATE_CONNECTTING;

		wifiManager = WifiManager::getInstance();
		int id = wifiManager->connect(ssid, psk);
		if (0 != id) {
			sendState(RK_SOFTAP_STATE_FAIL, NULL);
			m_state = RK_SOFTAP_STATE_FAIL;
			m_isConnecting = false;
			return false;
		}

		m_isConnecting = true;
	}

	printf("exit %s\n", __func__);
	return true;
}

static bool doConnectResult(const int fd, const char* buf) {
	char msg[MSG_BUFF_LEN] = {0};
	WifiManager* wifiManager;

	memset(msg, 0, sizeof(msg));
	snprintf(msg, sizeof(msg), HTTP_RESPOSE_MESSAGE, 0, "");
	if (send(fd, msg, sizeof(msg), 0) < 0) {
		return false;
	}

	std::string json(buf);
	json = json.substr(json.find('{'));

	rapidjson::Document document;
	if (document.Parse(json.c_str()).HasParseError()) {
		printf("doConnectResult parseJsonFailed \n");
		return false;
	}

	std::string result;
	auto resultIterator = document.FindMember("result");
	if (resultIterator != document.MemberEnd() && resultIterator->value.IsString()) {
		result = resultIterator->value.GetString();
	}
	if (0 == result.compare("1")) { // connect success, disable ap
		sendState(RK_SOFTAP_STATE_SUCCESS, NULL);
		m_state = RK_SOFTAP_STATE_SUCCESS;
		wifiManager = WifiManager::getInstance();
		wifiManager->disableWifiAp();
	} else {
		sendState(RK_SOFTAP_STATE_FAIL, NULL);
		m_state = RK_SOFTAP_STATE_FAIL;
	}

	return true;
}

static void handleRequest(const int fd_client) {
	size_t n;
	char buf[2048] = {0};

	n = recv(fd_client, buf, sizeof(buf), 0);
	if (n <= 0) {
		close(fd_client);
		return;
	}
	buf[n] = '\0';
	printf("TcpServer recv buf:\n%s\n", buf);

	if (strstr(buf, REQUEST_WIFI_LIST)) {
		sendWifiList(fd_client, buf);
	} else if (strstr(buf, REQUEST_WIFI_SET_UP)) {
		wifiSetup(fd_client, buf);
	} else if (strstr(buf, REQUEST_IS_WIFI_CONNECTED)) {
		isWifiConnected(fd_client, buf);
	} else if (strstr(buf, REQUEST_POST_CONNECT_RESULT)) {
		doConnectResult(fd_client, buf);
	}

	close(fd_client);
}

void* TcpServer::threadAccept(void *arg) {
	int fd_client, port;
	struct sockaddr_in addr_client;
	socklen_t len_addr_client;
	len_addr_client = sizeof(addr_client);

	prctl(PR_SET_NAME,"threadAccept");

	port = *(int*) arg;

	printf("threadAccept port = %d\n", port);
	fd_server = initSocket(port);
	if (fd_server < 0) {
		printf("TcpServer::threadAccept init tcp socket port %d fail. error:%d\n", port, fd_server);
		goto end;
	}

	/* Accept connection all time */
	while (1) {
		printf("accept...\n");
		fd_client = accept(fd_server, (struct sockaddr *)&addr_client, &len_addr_client);
		printf("accept fd_client = %d\n", fd_client);
		if (fd_client < 0)
			goto end;

		handleRequest(fd_client);
	}

end:
	printf("Exit Tcp accept thread\n");
	return NULL;
}

int TcpServer::startTcpServer(const unsigned int port) {
	int ret;

	m_port = port;
	printf("startTcpServer m_port = %d\n", m_port);
	ret = pthread_create(&m_thread, NULL, threadAccept, &m_port);
	if (0 != ret) {
		m_thread = 0;
	}
	return ret;
}

int TcpServer::stopTcpServer() {
	if (m_thread <= 0)
		return 0;

	if (fd_server >= 0) {
		shutdown(fd_server, SHUT_RDWR);
		close(fd_server);
		fd_server = -1;
	}

	if (0 != pthread_join(m_thread, NULL)) {
		return -1;
	}

	m_thread = 0;
	printf("stopTcpServer success\n");
	return 0;
}

void TcpServer::registerCallback(RK_SOFTAP_STATE_CALLBACK cb) {
	m_cb = cb;
}

RK_SOFTAP_STATE TcpServer::getState() {
	return m_state;
}

} // namespace framework
