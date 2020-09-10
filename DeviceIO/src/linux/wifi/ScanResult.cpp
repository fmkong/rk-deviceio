#include <stdio.h>
#include "DeviceIo/ScanResult.h"

namespace DeviceIOFramework {

ScanResult::ScanResult() {
}

ScanResult::ScanResult(const std::string& bssid, int frequency, int level, const std::string& flags, const std::string& ssid) {
	this->bssid = bssid;
	this->frequency = frequency;
	this->level = level;
	this->flags = flags;
	this->ssid = ssid;
}

void ScanResult::setBssid(const std::string& bssid) {
	this->bssid = bssid;
}

std::string ScanResult::getBssid() {
	return this->bssid;
}

void ScanResult::setSsid(const std::string& ssid) {
	this->ssid = ssid;
}

std::string ScanResult::getSsid() {
	return this->ssid;
}

void ScanResult::setFlags(const std::string& flags) {
	this->flags = flags;
}

std::string ScanResult::getFlags() {
	return this->flags;
}

void ScanResult::setLevel(const int level) {
	this->level = level;
}

int ScanResult::getLevel() {
	return this->level;
}

void ScanResult::setFrequency(const int frequency) {
	this->frequency = frequency;
}

int ScanResult::getFrequency() {
	return this->frequency;
}

std::string ScanResult::toString() {
	std::string ret;
	ret = "{\"bssid\":\"" + this->bssid + "\""
		+ ", \"frequency\":\"" + std::to_string(this->frequency) + "\""
		+ ", \"signalLevel\":\"" + std::to_string(this->level) + "\""
		+ ", \"flags\":\"" + this->flags + "\""
		+ ", \"ssid\":\"" + this->ssid + "\"}";

	return ret;
}

ScanResult::~ScanResult() {
}

} // end of namespace DeviceIOFramework
