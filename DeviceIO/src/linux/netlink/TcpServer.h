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

#ifndef DEVICEIO_FRAMEWORK_NETLINK_TCPSERVER_H_
#define DEVICEIO_FRAMEWORK_NETLINK_TCPSERVER_H_

#include <string>
#include "DeviceIo/Properties.h"
#include "DeviceIo/WifiManager.h"
#include "DeviceIo/Rk_softap.h"

namespace DeviceIOFramework {

class TcpServer {
public:
	/**
	 * Get single instance of TcpServer
	 */
	static TcpServer* getInstance();
	bool isRunning();
	int startTcpServer(const unsigned int port = 8443);
	int stopTcpServer();
	void registerCallback(RK_SOFTAP_STATE_CALLBACK cb);
	RK_SOFTAP_STATE getState();

	virtual ~TcpServer(){};
private:
	TcpServer();
	TcpServer(const TcpServer&){};
	TcpServer& operator=(const TcpServer&){return *this;};

	static void* threadAccept(void *arg);

	/* TcpServer single instance */
	static TcpServer* m_instance;
	pthread_t m_thread;
	int m_port;
	WifiManager* m_wifiManager;
	Properties* m_properties;
};
} // namespace framework

#endif /* DEVICEIO_FRAMEWORK_NETLINK_TCPSERVER_H_ */
