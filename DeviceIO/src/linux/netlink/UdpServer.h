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

#ifndef DEVICEIO_FRAMEWORK_NETLINK_UDPSERVER_H_
#define DEVICEIO_FRAMEWORK_NETLINK_UDPSERVER_H_

#include <string>
#include "DeviceIo/Properties.h"
#include "DeviceIo/WifiManager.h"
#include "DeviceIo/Rk_softap.h"

namespace DeviceIOFramework {

class UdpServer {
public:
	/**
	 * Get single instance of UdpServer
	 */
	static UdpServer* getInstance();
	bool isRunning();
	int startUdpServer(const unsigned int port = 9877);
	int startBroadcastThread(const unsigned int port = 9876);
	int stopBroadcastThread();
	int stopUdpServer();
	void registerCallback(RK_SOFTAP_STATE_CALLBACK cb);
	RK_SOFTAP_STATE getState();

	virtual ~UdpServer(){};
private:
	UdpServer();
	UdpServer(const UdpServer&){};
	UdpServer& operator=(const UdpServer&){return *this;};

	static void* threadAccept(void* arg);
	static void* threadBroadcast(void* arg);

	/* UdpServer single instance */
	static UdpServer* m_instance;
	pthread_t m_thread;
	pthread_t m_thread_broadcast;
	int m_port;
	int m_port_broadcast;
	WifiManager* m_wifiManager;
	Properties* m_properties;
};
} // namespace framework

#endif /* DEVICEIO_FRAMEWORK_NETLINK_UDPSERVER_H_ */
