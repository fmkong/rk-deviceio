#include "Logger.h"
#include <iostream>
#include <mutex>
#include <thread>

#define _2K   2048
char logBuffer[_2K];
std::mutex g_logBufferMutex;
