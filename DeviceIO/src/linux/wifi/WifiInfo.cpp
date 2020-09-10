#include <stdio.h>
#include "DeviceIo/WifiInfo.h"

namespace DeviceIOFramework {

WifiInfo::WifiInfo() {
	this->networkId = -1;
}

void WifiInfo::setNetworkId(const int networkId) {
	this->networkId = networkId;
}

int WifiInfo::getNetworkId() {
	return this->networkId;
}

void WifiInfo::setBssid(const std::string& bssid) {
	this->bssid = bssid;
}

std::string WifiInfo::getBssid() {
	return this->bssid;
}

void WifiInfo::setSsid(const std::string& ssid) {
	this->ssid = ssid;
}

std::string WifiInfo::getSsid() {
	return this->ssid;
}

void WifiInfo::setFrequency(const int frequency) {
	this->frequency = frequency;
}

int WifiInfo::getFrequency() {
	return this->frequency;
}

void WifiInfo::setMode(const std::string& mode) {
	this->mode = mode;
}

std::string WifiInfo::getMode() {
	return this->mode;
}

void WifiInfo::setWpaState(const std::string& wpaState) {
	this->wpaState = wpaState;
}

std::string WifiInfo::getWpaState() {
	return this->wpaState;
}

void WifiInfo::setIpAddress(const std::string& ipAddress) {
	this->ipAddress = ipAddress;
}

std::string WifiInfo::getIpAddress() {
	return this->ipAddress;
}

void WifiInfo::setMacAddress(const std::string& macAddress) {
	this->macAddress = macAddress;
}

std::string WifiInfo::getMacAddress() {
	return this->macAddress;
}

std::string WifiInfo::toString() {
	std::string ret;
	ret = "{\"networkId\":" + std::to_string(this->networkId)
		+ ", \"bssid\":\"" + this->bssid + "\""
		+ ", \"ssid\":\"" + this->ssid + "\""
		+ ", \"frequency\":" + std::to_string(this->frequency)
		+ ", \"wpaState\":" + this->wpaState + "\""
		+ ", \"ipAddress\":\"" + this->ipAddress + "\""
		+ ", \"macAddress\":\"" + this->macAddress + "\"}";

	return ret;
}

WifiInfo::~WifiInfo() {
}

} // end of namespace DeviceIOFramework
