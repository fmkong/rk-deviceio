/*****************************************************************************
 **
 **  Name:           bluetooth_bsa.c
 **
 **  Description:    Bluetooth API
 **
 **  Copyright (c) 2019, Rockchip Corp., All Rights Reserved.
 **  Rockchip Bluetooth Core. Proprietary and confidential.
 **
 *****************************************************************************/
#include <signal.h>
#include <pthread.h>
#include <unistd.h>
#include "bsa_api.h"
#include "bsa_disc_api.h"
#include "bsa_pan_api.h"
#include "app_xml_utils.h"
#include "app_dm.h"
#include "app_mgt.h"
#include "app_utils.h"
#include "app_disc.h"
#include "app_manager.h"
#include "app_avk.h"
#include "app_av.h"
#include "app_dg.h"
#include "app_ble_rk_server.h"
#include "app_hs.h"
#include "app_ble_client.h"
#include "app_pbc.h"
#include "../bluetooth.h"
#include "bluetooth_bsa.h"
#include "utility.h"

#ifdef BROADCOM_BSA
#include "app_pan.h"
#endif

typedef struct {
    bool is_bt_open;
    bool is_ble_open;
    bool is_ble_client_open;
    bool is_a2dp_sink_open;
    bool is_a2dp_source_open;
    bool is_spp_open;
    bool is_hfp_open;
    bool is_pbap_open;
    bool is_pan_open;
    RK_BT_STATE_CALLBACK bt_state_cb;
    RK_BT_BOND_CALLBACK bt_bond_cb;
} bt_control_t;

volatile bt_control_t g_bt_control = {
    false, false, false, false, false, false, false, false, false, NULL, NULL,
};

static char *g_bsa_server_path = NULL;

static bool bt_is_open();
static bool ble_is_open();
static bool ble_client_is_open();
static bool a2dp_sink_is_open();
static bool a2dp_source_is_open();
static bool spp_is_open();
static bool hfp_is_open();
static int bt_source_reconnect();

static void bsa_bt_state_send(RK_BT_STATE state)
{
    if(g_bt_control.bt_state_cb)
        g_bt_control.bt_state_cb(state);
}

static void bsa_bt_bond_state_send(const char *address, const char *name, RK_BT_BOND_STATE state)
{
    if(g_bt_control.bt_bond_cb)
        g_bt_control.bt_bond_cb(address, name, state);
}

static void bt_mgr_notify_callback(BD_ADDR bd_addr, char *name, tBSA_MGR_EVT evt)
{
    char address[18];

    if(app_mgr_bd2str(bd_addr, address, 18) < 0)
        memcpy(address, "unknown", strlen("unknown"));

    switch(evt) {
        case BT_LINK_UP_EVT:
            APP_DEBUG0("BT_LINK_UP_EVT\n");
            break;
        case BT_LINK_DOWN_EVT:
            APP_DEBUG0("BT_LINK_DOWN_EVT\n");
            break;
        case BT_WAIT_PAIR_EVT:
            APP_DEBUG0("BT_WAIT_PAIR_EVT\n");
            bsa_bt_bond_state_send(address, name, RK_BT_BOND_STATE_BONDING);
            break;
        case BT_PAIR_SUCCESS_EVT:
            APP_DEBUG0("BT_PAIR_SUCCESS_EVT\n");
            bsa_bt_bond_state_send(address, name, RK_BT_BOND_STATE_BONDED);
            break;
        case BT_PAIR_FAILED_EVT:
            APP_DEBUG0("BT_PAIR_FAILED_EVT\n");
            bsa_bt_bond_state_send(address, name, RK_BT_BOND_STATE_NONE);
            break;
        case BT_UNPAIR_SUCCESS_EVT:
            APP_DEBUG0("BT_UNPAIR_SUCCESS_EVT\n");
            bsa_bt_bond_state_send(address, name, RK_BT_BOND_STATE_NONE);
            break;
    }
}

static void bsa_get_bt_mac(char *bt_mac, int len)
{
    BD_ADDR bd_addr;

    if(!bt_mac)
        return;

    app_mgr_get_bt_config(NULL, 0, (char *)bd_addr, BD_ADDR_LEN);
    app_mgr_bd2str(bd_addr, bt_mac, len);
}

typedef void (*sighandler_t)(int);
static int rk_system(const char *cmd_line)
{
   int ret = 0;
   sighandler_t old_handler;

   old_handler = signal(SIGCHLD, SIG_DFL);
   ret = system(cmd_line);
   signal(SIGCHLD, old_handler);

   return ret;
}

static int check_bsa_server_exist()
{
    int cnt = 20;

    while(cnt--) {
        if (get_ps_pid("bsa_server")) {
            APP_DEBUG0("bsa_server has been opened");
            break;
        }

        usleep(500 * 1000);
        APP_DEBUG0("wait bsa_server open");
    }

    if(cnt <= 0) {
        APP_DEBUG0("bsa_server open failed");
        return -1;
    }

    return 0;
}

static int check_bsa_server_exit()
{
    int cnt = 20;

    while(cnt--) {
        if (!get_ps_pid("bsa_server")) {
            APP_DEBUG0("bsa_server has been closed");
            break;
        }

        usleep(500 * 1000);
        APP_DEBUG0("wait bsa_server close");
    }

    if(cnt <= 0)
        return -1;

    return 0;
}

static bool bt_is_open()
{
    return g_bt_control.is_bt_open;
}

static int bt_bsa_server_open()
{
    char cmd[256];

    memset(cmd, 0, 256);
    if(g_bsa_server_path)
        sprintf(cmd, "%s %s", g_bsa_server_path, "start &");
    else
        memcpy(cmd, "/usr/bin/bsa_server.sh start &", strlen("/usr/bin/bsa_server.sh start &"));

    APP_DEBUG1("%s", cmd);
    if(0 != rk_system(cmd)) {
        APP_DEBUG1("Start bsa_server failed: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

static int bt_bsa_server_close()
{
    char cmd[256];

    memset(cmd, 0, 256);
    if(g_bsa_server_path)
        sprintf(cmd, "%s %s", g_bsa_server_path, "stop &");
    else
        memcpy(cmd, "/usr/bin/bsa_server.sh stop &", strlen("/usr/bin/bsa_server.sh stop &"));

    APP_DEBUG1("%s", cmd);
    if(0 != rk_system(cmd)) {
        APP_DEBUG1("Stop bsa_server failed: %s", strerror(errno));
        return -1;
    }
    return 0;
}

void rk_bt_set_bsa_server_path(char *path)
{
	int len;

	if(!path) {
		APP_DEBUG0("Invalid bsa server path");
		return;
	}

	if(g_bsa_server_path) {
		free(g_bsa_server_path);
		g_bsa_server_path = NULL;
	}

	len = strlen(path) + 1;
	g_bsa_server_path = (char*)malloc(len);
	if(!g_bsa_server_path) {
		APP_DEBUG0("malloc bsa server path failed");
		return;
	}

	memset(g_bsa_server_path, 0, len);
	memcpy(g_bsa_server_path, path, len - 1);
	APP_DEBUG1("%s", g_bsa_server_path);
}

int rk_bt_is_connected()
{
    if(!bt_is_open())
        return 0;

    if(a2dp_sink_is_open()) {
        RK_BT_SINK_STATE sink_state;
        rk_bt_sink_get_state(&sink_state);
        if(sink_state != RK_BT_SINK_STATE_DISCONNECT && sink_state != RK_BT_SINK_STATE_IDLE)
            return 1;
    }

    if(a2dp_source_is_open()) {
        RK_BT_SOURCE_STATUS source_state;
        rk_bt_source_get_status(&source_state, NULL, 0, NULL, 0);
        if (source_state == BT_SOURCE_STATUS_CONNECTED)
            return 1;
    }

    if(ble_is_open()) {
        RK_BLE_STATE ble_state;
        rk_ble_get_state(&ble_state);
        if(ble_state == RK_BLE_STATE_CONNECT)
            return 1;
    }

    if(hfp_is_open()) {
        RK_BT_HFP_EVENT hfp_state;
        app_hs_get_state(&hfp_state);
        if(hfp_state == RK_BT_HFP_CONNECT_EVT)
            return 1;
    }

    if(spp_is_open()) {
        RK_BT_SPP_STATE spp_state;
        rk_bt_spp_get_state(&spp_state);
        if(spp_state == RK_BT_SPP_STATE_CONNECT)
            return 1;
    }

    return 0;
}

void rk_bt_register_state_callback(RK_BT_STATE_CALLBACK cb)
{
    g_bt_control.bt_state_cb = cb;
}

void rk_bt_register_bond_callback(RK_BT_BOND_CALLBACK cb)
{
    g_bt_control.bt_bond_cb = cb;
}

int rk_bt_init(RkBtContent *p_bt_content)
{
    if(!p_bt_content) {
        APP_ERROR0("bt content is null");
        return -1;
    }

    if (bt_is_open()) {
        APP_DEBUG0("bluetooth has been opened.");
        return 0;
    }

    bsa_bt_state_send(RK_BT_STATE_TURNING_ON);

    /* start bsa_server */
    if(bt_bsa_server_open() < 0) {
        APP_DEBUG0("bsa server open failed.");
        return -1;
    }

    if(check_bsa_server_exist() < 0)
		return -1;

    APP_DEBUG1("p_bt_content->bt_name: %s", p_bt_content->bt_name);

    /* Init App manager */
    if(app_manager_init(p_bt_content->bt_name, p_bt_content->bt_addr, bt_mgr_notify_callback) < 0) {
        APP_DEBUG0("app_manager init failed.");
        return -1;
    }

    g_bt_control.is_bt_open = true;
    bsa_bt_state_send(RK_BT_STATE_ON);
    return 0;
}

int rk_bt_deinit()
{
    if (!bt_is_open()) {
        APP_DEBUG0("bluetooth has been closed.");
        return -1;
    }

    bsa_bt_state_send(RK_BT_STATE_TURNING_OFF);

    rk_bt_obex_pbap_deinit();
    rk_bt_sink_close();
    rk_ble_stop();
    rk_ble_client_close();
    rk_bt_source_close();
    rk_bt_spp_close();
    rk_bt_hfp_close();
    rk_bt_pan_close();

    /* Close BSA before exiting (to release resources) */
    app_manager_deinit();
    app_disc_clean();

    /* stop bsa_server */
    bt_bsa_server_close();
    check_bsa_server_exit();

    app_mgr_deregister_disc_cb();
    app_mgr_deregister_dev_found_cb();
    app_mgr_deregister_name_callback();
    g_bt_control.bt_bond_cb = NULL;

    bsa_bt_state_send(RK_BT_STATE_OFF);
    g_bt_control.bt_state_cb = NULL;

    if(g_bsa_server_path) {
        free(g_bsa_server_path);
        g_bsa_server_path = NULL;
    }

    g_bt_control.is_bt_open = false;
    return 0;
}

int rk_bt_set_class(int value)
{
    if(!bt_is_open()) {
        APP_DEBUG0("bluetooth is not inited, please init");
        return -1;
    }

    return app_mgt_set_cod(value);
}

int rk_bt_set_sleep_mode()
{
    return app_mgr_set_sleep_mode_param();
}

int rk_bt_enable_reconnect(int enable)
{
    return app_mgr_set_auto_reconnect(enable);
}

void rk_bt_register_discovery_callback(RK_BT_DISCOVERY_CALLBACK cb)
{
    app_mgr_register_disc_cb(cb);
}

void rk_bt_register_dev_found_callback(RK_BT_DEV_FOUND_CALLBACK cb)
{
    app_mgr_register_dev_found_cb(cb);
}


void rk_bt_register_name_change_callback(RK_BT_NAME_CHANGE_CALLBACK cb)
{
    app_mgr_register_name_callback(cb);
}

static void bt_disc_cback(tBSA_DISC_EVT event, tBSA_DISC_MSG *p_data)
{
    if(event == BSA_DISC_CMPL_EVT) {
        bt_source_reconnect();
        app_av_set_reconnect_tag(false);
    }
}

int rk_bt_start_discovery(unsigned int mseconds, RK_BT_SCAN_TYPE scan_type)
{
    int duration;

    if(!bt_is_open()) {
        APP_DEBUG0("bluetooth is not inited, please init");
        return -1;
    }

    if(app_disc_complete() != APP_DISCOVERYING) {
        duration = mseconds/1000 + mseconds%1000;
        switch(scan_type) {
            case SCAN_TYPE_AUTO:
                return app_disc_start_regular(bt_disc_cback, duration);
            case SCAN_TYPE_BREDR:
                return app_disc_start_bredr_regular(bt_disc_cback, duration);
            case SCAN_TYPE_LE:
                return app_disc_start_ble_regular(NULL, duration);
            case SCAN_TYPE_PAN:
                return app_disc_start_services(BSA_PANU_SERVICE_MASK |
                    BSA_NAP_SERVICE_MASK | BSA_GN_SERVICE_MASK, duration);
            default:
                APP_DEBUG1("invalid scan_type(%d)", scan_type);
                return -1;
        }
    } else {
        APP_DEBUG0("devices scanning\n");
        return -1;
    }
}

int rk_bt_cancel_discovery()
{
    if(!bt_is_open()) {
        APP_DEBUG0("bluetooth is not inited, please init");
        return -1;
    }

    app_av_set_reconnect_tag(false);
    if(app_disc_complete() == APP_DISCOVERYING)
        return app_disc_abort();
    else
        return -1;
}

bool rk_bt_is_discovering()
{
    int disc = app_disc_complete();
    if(disc == APP_DISCOVERYING)
        return true;
    else
        return false;
}

int rk_bt_get_scaned_devices(RkBtScanedDevice **dev_list, int *count)
{
    *count = 0;
    if(!bt_is_open()) {
        APP_DEBUG0("bluetooth is not inited, please init");
        return -1;
    }

    return app_mgr_get_scaned_devices(dev_list, count, false);
}

int rk_bt_free_scaned_devices(RkBtScanedDevice *dev_list)
{
    if(!bt_is_open()) {
        APP_DEBUG0("bluetooth is not inited, please init");
        return -1;
    }

    return app_mgr_free_scaned_devices(dev_list);
}

void rk_bt_display_devices()
{
    if(!bt_is_open()) {
        APP_DEBUG0("bluetooth is not inited, please init");
        return;
    }

    app_disc_display_devices();
}

void rk_bt_display_paired_devices()
{
    if(!bt_is_open()) {
        APP_DEBUG0("bluetooth is not inited, please init");
        return;
    }

    app_mgr_xml_display_devices();
}

int rk_bt_pair_by_addr(char *addr)
{
    BD_ADDR bd_addr;

    if(!addr || (strlen(addr) < 17)) {
        APP_ERROR0("invalid address");
        return -1;
    }

    if(!bt_is_open()) {
        APP_DEBUG0("bluetooth is not inited, please init");
        return -1;
    }

    if(app_mgr_str2bd(addr, bd_addr) < 0) {
        APP_ERROR1("pair device(%s)failed", addr);
        return -1;
    }

    return app_mgr_sec_bond(bd_addr);
}

int rk_bt_unpair_by_addr(char *addr)
{
    BD_ADDR bd_addr;

    if(!addr || (strlen(addr) < 17)) {
        APP_ERROR0("invalid address");
        return -1;
    }

    if(!bt_is_open()) {
        APP_DEBUG0("bluetooth is not inited, please init");
        return -1;
    }

    if(app_mgr_str2bd(addr, bd_addr) < 0) {
        APP_ERROR1("unpair device(%s)failed", addr);
        return -1;
    }

    return app_mgr_sec_unpair(bd_addr);
}

int rk_bt_set_device_name(char *name)
{
    if(!bt_is_open()) {
        APP_DEBUG0("bluetooth is not inited, please init");
        return -1;
    }

    return app_mgt_set_device_name(name);
}

int rk_bt_get_device_name(char *name, int len)
{
    if(!bt_is_open()) {
        APP_DEBUG0("bluetooth is not inited, please init");
        return -1;
    }

    if (!name || (len <= 0))
        return -1;

    memset(name, 0, len);
    app_mgr_get_bt_config(name, len, NULL, 0);

    return 0;
}

int rk_bt_get_device_addr(char *addr, int len)
{
    if(!bt_is_open()) {
        APP_DEBUG0("bluetooth is not inited, please init");
        return -1;
    }

    if (!addr || (len < 17))
        return -1;

    bsa_get_bt_mac(addr, len);
    return 0;
}

int rk_bt_get_paired_devices(RkBtScanedDevice **dev_list, int *count)
{
    *count = 0;
    if(!bt_is_open()) {
        APP_DEBUG0("bluetooth is not inited, please init");
        return -1;
    }

    return app_mgr_get_scaned_devices(dev_list, count, true);
}

int rk_bt_free_paired_devices(RkBtScanedDevice *dev_list)
{
    if(!bt_is_open()) {
        APP_DEBUG0("bluetooth is not inited, please init");
        return -1;
    }

    return app_mgr_free_scaned_devices(dev_list);
}

RK_BT_PLAYROLE_TYPE rk_bt_get_playrole_by_addr(char *addr)
{
    BD_ADDR bd_addr;

    if(!addr || (strlen(addr) < 17)) {
        APP_ERROR0("invalid address");
        return -1;
    }

    if(!bt_is_open()) {
        APP_DEBUG0("bluetooth is not inited, please init");
        return -1;
    }

    if(app_mgr_str2bd(addr, bd_addr) < 0) {
        APP_ERROR1("unpair device(%s)failed", addr);
        return -1;
    }

    return app_mgr_get_playrole_by_addr(bd_addr);
}

int rk_bt_set_visibility(const int visiable, const int connect)
{
    bool discoverable, connectable;

    discoverable = visiable == 0 ? false : true;
    connectable = connect == 0 ? false : true;
    return app_dm_set_visibility(discoverable, connectable);
}

bool rk_bt_get_connected_properties(char *addr)
{
	APP_DEBUG1("bsa don't support %s", __func__);
	return false;
}

int rk_bt_read_remote_device_name(char *addr, int transport)
{
    BD_ADDR bd_addr;

    if(!bt_is_open()) {
        APP_DEBUG0("bluetooth is not inited, please init");
        return -1;
    }

    if(app_mgr_str2bd(addr, bd_addr) < 0) {
        APP_ERROR1("read remote device(%s) name failed", addr);
        return -1;
    }

    if(transport < RK_BT_TRANSPORT_UNKNOWN || transport > RK_BT_TRANSPORT_LE) {
        APP_ERROR1("invaild transport(%d)", transport);
        return -1;
    }

    return app_disc_read_remote_device_name(bd_addr, tBSA_TRANSPORT(transport));
}

/******************************************/
/*               A2DP SINK                */
/******************************************/
static bool a2dp_sink_is_open()
{
    return g_bt_control.is_a2dp_sink_open;
}

int rk_bt_sink_register_callback(RK_BT_SINK_CALLBACK cb)
{
    app_avk_register_cb(cb);
    return 0;
}

int rk_bt_sink_register_volume_callback(RK_BT_SINK_VOLUME_CALLBACK cb)
{
    app_avk_register_volume_cb(cb);
    return 0;
}

int rk_bt_sink_register_track_callback(RK_BT_AVRCP_TRACK_CHANGE_CB cb)
{
    app_avk_register_track_cb(cb);
    return 0;
}

int rk_bt_sink_register_position_callback(RK_BT_AVRCP_PLAY_POSITION_CB cb)
{
    app_avk_register_position_cb(cb);
    return 0;
}

void rk_bt_sink_register_underurn_callback(RK_BT_SINK_UNDERRUN_CB cb)
{
    APP_DEBUG1("bsa don't support %s", __func__);
}

int rk_bt_sink_get_default_dev_addr(char *addr, int len)
{
    APP_DEBUG1("bsa don't support %s", __func__);
    return 0;
}

int rk_bt_sink_open()
{
    if(!bt_is_open()) {
        APP_DEBUG0("bluetooth is not inited, please init");
        return -1;
    }

    if (a2dp_source_is_open()) {
        APP_DEBUG0("a2dp source has been opened, close a2dp source");
        rk_bt_source_close();
    }

    if (a2dp_sink_is_open()) {
        APP_DEBUG0("a2dp sink has been opened.");
        return 0;
    }

    if (app_avk_start() < 0) {
        APP_DEBUG0("app_avk_start failed");
        return -1;
    }

    //rk_bt_set_visibility(1, 1);
    g_bt_control.is_a2dp_sink_open = true;
    return 0 ;
}

int rk_bt_sink_close()
{
    if(!a2dp_sink_is_open()) {
        APP_DEBUG0("a2dp sink has been closed.");
        return 0;
    }

    app_avk_stop();

    //rk_bt_set_visibility(0, 0);
    g_bt_control.is_a2dp_sink_open = false;
    return 0;
}

int rk_bt_sink_play()
{
    app_avk_rc_send_cmd((int)APP_AVK_PLAY_START);
    return 0;
}

int rk_bt_sink_pause()
{
    app_avk_rc_send_cmd((int)APP_AVK_PLAY_PAUSE);
    return 0;
}

int rk_bt_sink_stop()
{
    app_avk_rc_send_cmd((int)APP_AVK_PLAY_STOP);
    return 0;
}

int rk_bt_sink_prev()
{
    app_avk_rc_send_cmd((int)APP_AVK_PLAY_PREVIOUS_TRACK);
    return 0;
}

int rk_bt_sink_next()
{
    app_avk_rc_send_cmd((int)APP_AVK_PLAY_NEXT_TRACK);
    return 0;
}

int rk_bt_sink_volume_up()
{
    app_avk_rc_send_cmd((int)APP_AVK_VOLUME_UP);
    return 0;
}

int rk_bt_sink_volume_down()
{
    app_avk_rc_send_cmd((int)APP_AVK_VOLUME_DOWN);
    return 0;
}

int rk_bt_sink_set_volume(int volume)
{
    return app_avk_set_volume(volume);
}

int rk_bt_sink_get_state(RK_BT_SINK_STATE *pState)
{
    return app_avk_get_state(pState);
}

int rk_bt_sink_get_play_status()
{
    return app_avk_get_play_status();
}

bool rk_bt_sink_get_poschange()
{
    return app_avk_get_pos_change();
}

int rk_bt_sink_disconnect()
{
    if(!a2dp_sink_is_open()) {
        APP_ERROR0("sink don't open, please open");
        return -1;
    }

    /* Close AVK connection (disconnect device) */
    app_avk_close_all();
    return 0;
}

int rk_bt_sink_connect_by_addr(char *addr)
{
    BD_ADDR bd_addr;

    if(!addr || (strlen(addr) < 17)) {
        APP_ERROR0("invalid address");
        return -1;
    }

    if(!a2dp_sink_is_open()) {
        APP_ERROR0("sink don't open, please open");
        return -1;
    }

    if(app_mgr_str2bd(addr, bd_addr) < 0) {
        APP_ERROR1("connect device(%s)failed", addr);
        return -1;
    }

    return app_avk_open(bd_addr, NULL);
}

int rk_bt_sink_disconnect_by_addr(char *addr)
{
    BD_ADDR bd_addr;

    if(!addr || (strlen(addr) < 17)) {
        APP_ERROR0("invalid address");
        return -1;
    }

    if(!a2dp_sink_is_open()) {
        APP_ERROR0("sink don't open, please open");
        return -1;
    }

    if(app_mgr_str2bd(addr, bd_addr) < 0) {
        APP_ERROR1("disconnect device(%s)failed", addr);
        return -1;
    }

    return app_avk_close(bd_addr);
}

void rk_bt_sink_set_alsa_device(char *alsa_dev)
{
    app_avk_set_alsa_device(alsa_dev);
}

/******************************************/
/***************** BLE ********************/
/******************************************/
static bool ble_is_open()
{
    return g_bt_control.is_ble_open;
}

int rk_ble_register_status_callback(RK_BLE_STATE_CALLBACK cb)
{
    app_ble_rk_server_register_cb(cb);
    return 0;
}

int rk_ble_register_recv_callback(RK_BLE_RECV_CALLBACK cb)
{
    app_ble_rk_server_recv_data_callback(cb);
    return 0;
}

void rk_ble_register_request_data_callback(RK_BLE_REQUEST_DATA cb)
{
	app_ble_rk_server_request_data_callback(cb);
}

void rk_ble_register_mtu_callback(RK_BT_MTU_CALLBACK cb)
{
    app_ble_register_mtu_callback(cb);
}

int rk_ble_get_state(RK_BLE_STATE *p_state)
{
    app_ble_rk_server_get_state(p_state);
    return 0;
}

int rk_bt_ble_set_visibility(const int visiable, const int connect)
{
    bool discoverable, connectable;

    discoverable = visiable == 0 ? false : true;
    connectable = connect == 0 ? false : true;
    return app_dm_set_ble_visibility(discoverable, connectable);
}

int rk_ble_start(RkBleContent *ble_content)
{
    if(!bt_is_open()) {
        APP_DEBUG0("bluetooth is not inited, please init");
        return -1;
    }

    if(ble_is_open()) {
        APP_DEBUG0("ble has been opened.");
        return 0;
    }

    if(ble_client_is_open()) {
        APP_DEBUG0("ble client has been opened, close ble client");
        rk_ble_client_close();
    }

    if(app_ble_rk_server_open(ble_content) < 0) {
        APP_DEBUG0("ble open failed");
        return -1;
    }

    g_bt_control.is_ble_open = true;
    return 0;
}

int rk_ble_stop()
{
    if(!ble_is_open()) {
        APP_DEBUG0("ble has been closed.");
        return 0;
    }

    app_ble_rk_server_close();
    g_bt_control.is_ble_open = false;
    return 0;
}

int rk_ble_write(const char *uuid, char *data, int len)
{
    if(!ble_is_open()) {
        APP_ERROR0("ble don't start");
        return -1;
    }

    app_ble_rk_server_send_message(uuid, data, len);
    return 0;
}

int rk_ble_disconnect()
{
    if(!ble_is_open()) {
        APP_ERROR0("ble don't start");
        return -1;
    }

    return app_ble_rk_server_disconnect();
}

void rk_ble_set_local_privacy(bool local_privacy)
{
    app_ble_rk_server_set_local_privacy(local_privacy);
}

int rk_ble_set_adv_interval(unsigned short adv_int_min, unsigned short adv_int_max)
{
    return app_ble_rk_server_set_adv_interval(adv_int_min, adv_int_max);
}

int rk_ble_set_address(char *address)
{
    APP_DEBUG1("bsa don't support %s", __func__);
    return 0;
}
/*****************************************************
 *                   BLE CLIENT                      *
 *****************************************************/
static bool ble_client_is_open()
{
    return g_bt_control.is_ble_client_open;
}

void rk_ble_client_register_state_callback(RK_BLE_CLIENT_STATE_CALLBACK cb)
{
    app_ble_client_register_state_callback(cb);
}

void rk_ble_client_register_recv_callback(RK_BLE_CLIENT_RECV_CALLBACK cb)
{
    app_ble_client_register_recv_callback(cb);
}

void rk_ble_client_register_mtu_callback(RK_BT_MTU_CALLBACK cb)
{
    app_ble_client_register_mtu_callback(cb);
}

int rk_ble_client_open(bool mtu_change)
{
    if(!bt_is_open()) {
        APP_DEBUG0("bluetooth is not inited, please init");
        return -1;
    }

    if(ble_client_is_open()) {
        APP_DEBUG0("ble client has been opened.");
        return 0;
    }

    if(ble_is_open()) {
        APP_DEBUG0("ble server has been opened, close ble server");
        rk_ble_stop();
    }

    if(app_ble_client_start(mtu_change) < 0) {
        APP_ERROR0("ble client failed");
        return -1;
    }

    g_bt_control.is_ble_client_open = true;
    return 0;
}

void rk_ble_client_close()
{
    if(!ble_client_is_open()) {
        APP_DEBUG0("ble client has been closed.");
        return;
    }

    app_ble_client_stop();
    g_bt_control.is_ble_client_open = false;
}

RK_BLE_CLIENT_STATE rk_ble_client_get_state()
{
    if(!ble_client_is_open()) {
        APP_DEBUG0("ble client don't open, please open");
        return -1;
    }

    return app_ble_client_get_state();
}

int rk_ble_client_connect(char *addr)
{
    BD_ADDR bd_addr;

    if(!addr || (strlen(addr) < 17)) {
        APP_ERROR0("invalid address");
        return -1;
    }

    if(!ble_client_is_open()) {
        APP_ERROR0("ble client don't open, please open");
        return -1;
    }

    if(app_mgr_str2bd(addr, bd_addr) < 0) {
        APP_ERROR1("address string to BD_ADDR(%s)failed", addr);
        return -1;
    }

    return app_ble_client_open(bd_addr);
}

int rk_ble_client_disconnect(char *addr)
{
    BD_ADDR bd_addr;

    if(!addr || (strlen(addr) < 17)) {
        APP_ERROR0("invalid address");
        return -1;
    }

    if(!ble_client_is_open()) {
        APP_ERROR0("ble client don't open, please open");
        return -1;
    }

    if(app_mgr_str2bd(addr, bd_addr) < 0) {
        APP_ERROR1("address string to BD_ADDR(%s)failed", addr);
        return -1;
    }

    return app_ble_client_close(bd_addr);
}

int rk_ble_client_get_service_info(char *addr, RK_BLE_CLIENT_SERVICE_INFO *info)
{
    BD_ADDR bd_addr;

    if(!addr || (strlen(addr) < 17)) {
        APP_ERROR0("invalid address");
        return -1;
    }

    if(!ble_client_is_open()) {
        APP_ERROR0("ble client isn't open, please open\n");
        return -1;
    }

    if(!info) {
        APP_ERROR0("Invaild info");
        return -1;
    }

    if(app_mgr_str2bd(addr, bd_addr) < 0) {
        APP_ERROR1("address string to BD_ADDR(%s)failed", addr);
        return -1;
    }

    return app_ble_client_db_get_service_info(bd_addr, info);
}

int rk_ble_client_read(const char *uuid)
{
    if(!ble_client_is_open()) {
        APP_DEBUG0("ble client don't open, please open");
        return -1;
    }

    if(!uuid) {
        APP_DEBUG0("Invalid uuid");
        return -1;
    }

    return app_ble_client_read(uuid);
}

int rk_ble_client_write(const char *uuid, char *data, int data_len)
{
    if(!ble_client_is_open()) {
        APP_DEBUG0("ble client don't open, please open");
        return -1;
    }

    if(!uuid) {
        APP_DEBUG0("Invalid uuid");
        return -1;
    }

    return app_ble_client_write(uuid, data, data_len, false);
}

bool rk_ble_client_is_notifying(const char *uuid)
{
    APP_DEBUG1("bsa don't support %s", __func__);
    return false;
}

int rk_ble_client_notify(const char *uuid, bool is_indicate, bool enable)
{
    char value[2];

    if(!ble_client_is_open()) {
        APP_DEBUG0("ble client don't open, please open");
        return -1;
    }

    if(!uuid) {
        APP_DEBUG0("Invalid uuid");
        return -1;
    }

    memset(value, 0, 2);
    if(enable) {
        if(app_ble_client_register_notification(uuid) < 0)
            return -1;

        if(is_indicate)
            value[0] = 2;
        else
            value[0] = 1;
    } else {
        if(app_ble_client_deregister_notification(uuid) < 0)
            return -1;
    }

    return app_ble_client_write(uuid, value, 2, true);
}

int rk_ble_client_get_eir_data(char *addr, char *eir_data, int len)
{
    BD_ADDR bd_addr;

    if(!addr || (strlen(addr) < 17)) {
        APP_ERROR0("invalid address");
        return -1;
    }

    if(!eir_data || len <= 0) {
        APP_ERROR1("invalid eir_data buf, len = %d", len);
        return -1;
    }

    if(!ble_client_is_open()) {
        APP_ERROR0("ble client isn't open, please open\n");
        return -1;
    }

    if(app_mgr_str2bd(addr, bd_addr) < 0) {
        APP_ERROR1("address string to BD_ADDR(%s)failed", addr);
        return -1;
    }

    return app_ble_client_get_eir_data(bd_addr, eir_data, len);
}

int rk_ble_client_default_data_length()
{
    return app_mgr_send_hci_cmd("2420041b004801");
}

/******************************************/
/*              A2DP SOURCE               */
/******************************************/
static bool a2dp_source_is_open()
{
    return g_bt_control.is_a2dp_source_open;
}

int rk_bt_source_register_status_cb(void *userdata, RK_BT_SOURCE_CALLBACK cb)
{
    app_av_register_cb(userdata, cb);
    return 0;
}

int rk_bt_source_get_status(RK_BT_SOURCE_STATUS *pstatus, char *name, int name_len,
                                    char *address, int addr_len)
{
    app_av_get_status(pstatus, name, name_len, address, addr_len);
    return 0;
}

/*
 * Turn on Bluetooth and scan SINK devices.
 * Features:
 *     1. enter the master mode
 *     2. Scan surrounding SINK type devices
 *     3. If the SINK device is found, the device with the strongest
 *        signal is automatically connected.(自动连接信号最强的设备)
 * Return:
 *     0: The function is executed successfully and needs to listen
 *        for Bluetooth connection events.
 *    -1: Function execution failed.
 */
int rk_bt_source_auto_connect_start(void *userdata, RK_BT_SOURCE_CALLBACK cb)
{
    if(!bt_is_open()) {
        APP_DEBUG0("bluetooth is not inited, please init");
        return -1;
    }

    if (a2dp_sink_is_open()) {
        APP_DEBUG0("a2dp sink has been opened, close a2dp sink");
        rk_bt_sink_close();
    }

    if (a2dp_source_is_open()) {
        APP_DEBUG0("a2dp source has been opened.");
        return 0;
    }

    if(app_av_auto_connect_start(userdata, cb) < 0) {
        APP_DEBUG0("app_av_auto_connect_start failed");
        app_av_auto_connect_stop();
        return -1;
    }

    g_bt_control.is_a2dp_source_open = true;
    return 0;
}

int rk_bt_source_auto_connect_stop()
{
    if(!a2dp_source_is_open()) {
        APP_DEBUG0("a2dp source has been closed.");
        return 0;
    }

    app_av_auto_connect_stop();

    g_bt_control.is_a2dp_source_open = false;
    return 0;
}

int rk_bt_source_open()
{
    if(!bt_is_open()) {
        APP_DEBUG0("bluetooth is not inited, please init");
        return -1;
    }

    if (a2dp_sink_is_open()) {
        APP_DEBUG0("a2dp sink has been opened, close a2dp sink");
        rk_bt_sink_close();
    }

    if (a2dp_source_is_open()) {
        APP_DEBUG0("a2dp source has been opened.");
        return 0;
    }

    if(app_av_initialize() < 0) {
        APP_DEBUG0("app_av_initialize failed");
        app_av_deinitialize();
        return -1;
    }

    if(app_mgr_is_reconnect())
        app_av_set_reconnect_tag(true);

    g_bt_control.is_a2dp_source_open = true;
    return 0;
}

int rk_bt_source_close()
{
    rk_bt_cancel_discovery();

    if(!a2dp_source_is_open()) {
        APP_DEBUG0("a2dp source has been closed.");
        return 0;
    }

    app_av_set_reconnect_tag(false);
    app_av_deinitialize();

    g_bt_control.is_a2dp_source_open = false;
    return 0;
}

int rk_bt_source_scan(BtScanParam *data)
{
    if(!a2dp_source_is_open()) {
        APP_DEBUG0("a2dp source is not inited, please init");
        return -1;
    }

    app_av_set_reconnect_tag(false);
    if(app_av_scan(data) < 0) {
        APP_DEBUG0("app_av_scan failed");
        return -1;
    }

    return 0;
}

int rk_bt_source_connect_by_addr(char *address)
{
    if(!a2dp_source_is_open()) {
        APP_DEBUG0("a2dp source is not inited, please init");
        return -1;
    }

    if(app_av_connect(address) < 0) {
        APP_ERROR1("app_av_connect failed, address: %s", address);
        return -1;
    }

    return 0;
}

int rk_bt_source_disconnect_by_addr(char *address)
{
    if(!a2dp_source_is_open()) {
        APP_DEBUG0("a2dp source has been closed.");
        return 0;
    }

    return app_av_disconnect(address);
}

static int bt_source_reconnect()
{
    if(!a2dp_source_is_open())
        return -1;

    return app_av_reconnect();
}

int rk_bt_source_disconnect()
{
    if(!a2dp_source_is_open()) {
        APP_DEBUG0("a2dp source has been closed.");
        return 0;
    }

    return app_av_disconnect_all();
}

int rk_bt_source_remove(char *address)
{
    if (app_av_remove(address) < 0) {
        APP_ERROR1("app_av_remvoe failed, address: %s", address);
        return -1;
    }
    return 0;
}

int rk_bt_source_get_device_name(char *name, int len)
{
    return rk_bt_get_device_name(name, len);
}

int rk_bt_source_get_device_addr(char *addr, int len)
{
    return rk_bt_get_device_addr(addr, len);
}

int rk_bt_source_resume()
{
    return app_av_resume();
}

int rk_bt_source_stop()
{
    return app_av_stop();
}

int rk_bt_source_pause()
{
    return app_av_pause();
}

int rk_bt_source_vol_up()
{
    return app_av_vol_up();
}

int rk_bt_source_vol_down()
{
    return app_av_vol_down();
}

/*****************************************************************
 *                     BLUETOOTH SPP API                         *
 *****************************************************************/
static bool spp_is_open()
{
    return g_bt_control.is_spp_open;
}

int rk_bt_spp_register_status_cb(RK_BT_SPP_STATUS_CALLBACK cb)
{
    app_dg_register_cb(cb);
    return 0;
}

int rk_bt_spp_register_recv_cb(RK_BT_SPP_RECV_CALLBACK cb)
{
    app_dg_register_recv_cb(cb);
    return 0;
}

int rk_bt_spp_open()
{
    if(!bt_is_open()) {
        APP_DEBUG0("bluetooth is not inited, please init");
        return -1;
    }

    if(spp_is_open()) {
        APP_DEBUG0("bt spp has been opened.");
        return 0;
    }

    if(app_dg_spp_open() < 0) {
        APP_DEBUG0("app_dg_spp_open failed");
        return -1;
    }

    g_bt_control.is_spp_open = true;
    return 0;
}

int rk_bt_spp_close()
{
    if(!spp_is_open()) {
        APP_DEBUG0("bt spp has been closed.");
        return 0;
    }

    app_dg_spp_close();

    g_bt_control.is_spp_open = false;
    return 0;
}

int rk_bt_spp_get_state(RK_BT_SPP_STATE *pState)
{
    *pState = app_dg_get_status();
    return 0;
}

int rk_bt_spp_write(char *data, int len)
{
    if(app_dg_write_data(data, len) < 0) {
        APP_DEBUG0("rk_bt_spp_write failed");
        return -1;
    }

    return 0;
}

/*****************************************************************
 *                     BLUETOOTH HEADSET API                     *
 *****************************************************************/
static bool hfp_is_open()
{
    return g_bt_control.is_hfp_open;
}

void rk_bt_hfp_register_callback(RK_BT_HFP_CALLBACK cb)
{
    app_hs_register_cb(cb);
}

int rk_bt_hfp_sink_open()
{
    rk_bt_sink_open();
    rk_bt_hfp_open();
    return 0 ;
}

int rk_bt_hfp_open()
{
    if(!bt_is_open()) {
        APP_DEBUG0("bluetooth is not inited, please init");
        return -1;
    }

    if(hfp_is_open()) {
        APP_DEBUG0("bt hfp has been opened.");
        return 0;
    }

    if(app_hs_initialize() < 0) {
        APP_DEBUG0("app_hs_initialize failed");
        return -1;
    }

    g_bt_control.is_hfp_open = true;
    return 0;
}

int rk_bt_hfp_close()
{
    if(!hfp_is_open()) {
        APP_DEBUG0("bt hfp has been closed.");
        return 0;
    }

    app_hs_deinitialize();

    g_bt_control.is_hfp_open = false;
    return 0;
}

int rk_bt_hfp_pickup()
{
    return app_hs_pick_up();
}

int rk_bt_hfp_hangup()
{
    return app_hs_hang_up();
}

int rk_bt_hfp_set_volume(int volume)
{
    return app_hs_set_vol(volume);
}

int rk_bt_hfp_redial(void)
{
    return app_hs_last_num_dial();
}

int rk_bt_hfp_dial_number(char *number)
{
    return app_hs_dial_num(number);
}

int rk_bt_hfp_report_battery(int value)
{
    return app_hs_report_battery(value);
}

void rk_bt_hfp_enable_cvsd()
{
    app_hs_set_cvsd(true);
}

void rk_bt_hfp_disable_cvsd()
{
    app_hs_set_cvsd(false);
}

int rk_bt_hfp_disconnect()
{
    if(!hfp_is_open()) {
        APP_ERROR0("hfp don't open, please open");
        return -1;
    }

    /* release mono headset connections */
    return app_hs_close_all();
}

/*****************************************************************
 *            Rockchip bluetooth obex api                        *
 *****************************************************************/
static bool pbap_is_open()
{
    return g_bt_control.is_pbap_open;
}

void rk_bt_obex_register_status_cb(RK_BT_OBEX_STATE_CALLBACK cb)
{
    app_pbc_register_status_cb(cb);
}

int rk_bt_obex_init(char *path)
{
    if(!bt_is_open()) {
        APP_DEBUG0("bluetooth is not inited, please init");
        return -1;
    }

    APP_DEBUG1("bsa don't support %s", __func__);
    return 0;
}

int rk_bt_obex_pbap_init()
{
    if(!bt_is_open()) {
        APP_DEBUG0("bluetooth is not inited, please init");
        return -1;
    }

    if(pbap_is_open()) {
        APP_ERROR0("pbap has been opened");
        return 0;
    }

    if(app_pbc_enable() < 0) {
        APP_ERROR0("app_pbc_init failed");
        return -1;
    }

    g_bt_control.is_pbap_open = true;
    return 0;
}

int rk_bt_obex_pbap_connect(char *addr)
{
    if(!pbap_is_open()) {
        APP_ERROR0("pbap don't open, please open");
        return -1;
    }

    return app_pbc_connect(addr);
}

int rk_bt_obex_pbap_get_vcf(char *dir_name, char *dir_file)
{
    if(!pbap_is_open()) {
        APP_ERROR0("pbap don't open, please open");
        return -1;
    }

    return app_pbc_get_vcf(dir_name, dir_file);
}

int rk_bt_obex_pbap_disconnect(char *addr)
{
    if(!pbap_is_open()) {
        APP_ERROR0("pbap don't open, please open");
        return -1;
    }

    return app_pbc_disconnect(addr);
}

int rk_bt_obex_pbap_deinit()
{
    if(!pbap_is_open()) {
        APP_DEBUG0("bt pbap has been closed.");
        return 0;
    }

    if(app_pbc_disable() < 0) {
        APP_ERROR0("app_pbc_deinit failed");
        return -1;
    }

    g_bt_control.is_pbap_open = false;
    app_pbc_deregister_status_cb();
    return 0;
}

int rk_bt_obex_deinit()
{
    APP_DEBUG1("bsa don't support %s", __func__);
    return 0;
}

/*****************************************************************
 *            Rockchip bluetooth pan api                        *
 *****************************************************************/
bool pan_is_open()
{
    return g_bt_control.is_pan_open;
}

void rk_bt_pan_register_event_cb(RK_BT_PAN_EVENT_CALLBACK cb)
{
#ifdef BROADCOM_BSA
	app_pan_register_event_cb(cb);
#else
    APP_DEBUG1("cypress bsa don't support %s", __func__);
#endif
}

int rk_bt_pan_open()
{
    if(!bt_is_open()) {
        APP_DEBUG0("bluetooth is not inited, please init");
        return -1;
    }

    if(pan_is_open()) {
        APP_ERROR0("PAN has been opened");
        return 0;
    }

#ifdef BROADCOM_BSA
    if (app_pan_init() < 0) {
        APP_ERROR0("Unable to init PAN");
        return -1;
    }

    if (app_pan_start() < 0) {
        APP_ERROR0("Unable to start PAN");
        return -1;
    }

    app_pan_set_role(BSA_PAN_ROLE_PANU);
    g_bt_control.is_pan_open = true;
#else
    APP_DEBUG1("cypress bsa don't support %s", __func__);
#endif

    return 0;
}

int rk_bt_pan_close()
{
    if(!pan_is_open()) {
        APP_ERROR0("PAN has been closed");
        return 0;
    }

#ifdef BROADCOM_BSA
    if (app_pan_stop() < 0) {
        APP_ERROR0("Unable to stop PAN");
        return -1;
    }

    g_bt_control.is_pan_open = false;
#else
    APP_DEBUG1("cypress bsa don't support %s", __func__);
#endif

    return 0;
}

int rk_bt_pan_connect(char *address)
{
    BD_ADDR bd_addr;

    if(!pan_is_open()) {
        APP_ERROR0("PAN don't open, please open");
        return -1;
    }

#ifdef BROADCOM_BSA
    if(address == NULL) {
        APP_ERROR0("address is null");
        return -1;
    }

    if(app_mgr_str2bd(address, bd_addr) < 0)
        return -1;

    APP_ERROR1("connect bd_addr: %02X:%02X:%02X:%02X:%02X:%02X",
        bd_addr[0], bd_addr[1], bd_addr[2],
        bd_addr[3], bd_addr[4], bd_addr[5]);

    return app_pan_open(bd_addr);
#else
    APP_DEBUG1("cypress bsa don't support %s", __func__);
    return 0;
#endif
}

int rk_bt_pan_disconnect(char *address)
{
    BD_ADDR bd_addr;

    if(!pan_is_open()) {
        APP_ERROR0("PAN don't open, please open");
        return -1;
    }

#ifdef BROADCOM_BSA
    if(address == NULL) {
        APP_ERROR0("address is null");
        return -1;
    }

    if(app_mgr_str2bd(address, bd_addr) < 0)
        return -1;

    APP_ERROR1("connect bd_addr: %02X:%02X:%02X:%02X:%02X:%02X",
        bd_addr[0], bd_addr[1], bd_addr[2],
        bd_addr[3], bd_addr[4], bd_addr[5]);

    return app_pan_close(bd_addr);
#else
    APP_DEBUG1("cypress bsa don't support %s", __func__);
    return 0;
#endif
}

int rk_bt_control(DeviceIOFramework::BtControl cmd, void *data, int len)
{
    using BtControl_rep_type = std::underlying_type<DeviceIOFramework::BtControl>::type;
    RkBleConfig *ble_cfg;
    RkBtContent bt_content;
    RkBleContent ble_content;
    int ret = 0;
    bool scan;

    APP_DEBUG1("cmd: %d", cmd);

    switch (cmd) {
    case DeviceIOFramework::BtControl::BT_OPEN:
        bt_content = *((RkBtContent *)data);
        ret = rk_bt_init(&bt_content);
        break;

    case DeviceIOFramework::BtControl::BT_CLOSE:
        rk_bt_deinit();
        break;

    case DeviceIOFramework::BtControl::BT_SINK_OPEN:
        ret = rk_bt_sink_open();
        break;

    case DeviceIOFramework::BtControl::BT_SINK_CLOSE:
        ret = rk_bt_sink_close();
        break;

    case DeviceIOFramework::BtControl::BT_SINK_IS_OPENED:
        ret = (int)a2dp_sink_is_open();
        break;

    case DeviceIOFramework::BtControl::BT_BLE_OPEN:
        ble_content = *((RkBleContent *)data);
        ret = rk_ble_start(&ble_content);
        break;

    case DeviceIOFramework::BtControl::BT_BLE_COLSE:
        ret = rk_ble_stop();
        break;

    case DeviceIOFramework::BtControl::BT_BLE_IS_OPENED:
        ret = (int)ble_is_open();
        break;

    case DeviceIOFramework::BtControl::BT_BLE_WRITE:
        ble_cfg = (RkBleConfig *)data;
        rk_ble_write(ble_cfg->uuid, (char *)ble_cfg->data, ble_cfg->len);
        break;

    case DeviceIOFramework::BtControl::BT_SOURCE_OPEN:
        ret = rk_bt_source_open();
        break;

    case DeviceIOFramework::BtControl::BT_SOURCE_CLOSE:
        rk_bt_source_close();
        break;

    case DeviceIOFramework::BtControl::BT_SOURCE_IS_OPENED:
        ret = a2dp_source_is_open();
        break;

    case DeviceIOFramework::BtControl::BT_VOLUME_UP:
        ret = rk_bt_sink_volume_up();
        break;

    case DeviceIOFramework::BtControl::BT_VOLUME_DOWN:
        ret = rk_bt_sink_volume_down();
        break;

    case DeviceIOFramework::BtControl::BT_PLAY:
    case DeviceIOFramework::BtControl::BT_RESUME_PLAY:
        ret = rk_bt_sink_play();
        break;

    case DeviceIOFramework::BtControl::BT_PAUSE_PLAY:
        ret = rk_bt_sink_pause();
        break;

    case DeviceIOFramework::BtControl::BT_AVRCP_FWD:
        ret = rk_bt_sink_prev();
        break;

    case DeviceIOFramework::BtControl::BT_AVRCP_BWD:
        ret = rk_bt_sink_next();
        break;

    case DeviceIOFramework::BtControl::BT_VISIBILITY:
        scan = (*(bool *)data);
        if(scan)
            rk_bt_set_visibility(1, 1);
        else
            rk_bt_set_visibility(0, 0);
        break;

    default:
        APP_DEBUG1("cmd <%d> is not implemented.", static_cast<BtControl_rep_type>(cmd));
        break;
    }

    return ret;
}
