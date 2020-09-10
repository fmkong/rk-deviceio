#ifndef DEVICEIO_FRAMEWORK_HOSTAPD_H_
#define DEVICEIO_FRAMEWORK_HOSTAPD_H_

#ifdef __cplusplus
extern "C" {
#endif


int wifi_rtl_start_hostapd(const char* ssid, const char* psk, const char* ip);
int wifi_rtl_stop_hostapd();


#ifdef __cplusplus
}
#endif


#endif // DEVICEIO_FRAMEWORK_HOSTAPD_H_
