#ifndef _RK_VOICE_PRINT_H_
#define _RK_VOICE_PRINT_H_

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*VP_SSID_PSK_CALLBACK)(char* ssid, char* psk);

int voice_print_start(void);
int voice_print_stop(void);
void voice_print_register_callback(VP_SSID_PSK_CALLBACK cb);

#ifdef __cplusplus
}
#endif

#endif
