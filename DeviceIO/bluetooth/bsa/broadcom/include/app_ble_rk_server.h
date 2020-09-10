/*****************************************************************************
**
**  Name:           app_ble_wifi_introducer.h
**
**  Description:    Bluetooth BLE Rockchip include file
**
**  Copyright (c) 2018, Cypress Corp., All Rights Reserved.
**  Cypress Bluetooth Core. Proprietary and confidential.
**
*****************************************************************************/
#ifndef APP_BLE_RK_SERVER_H
#define APP_BLE_RK_SERVER_H

#include "bsa_api.h"
#include "app_ble.h"
#include "bluetooth_bsa.h"

#define APP_BLE_RK_SERVER_TIMEOUT 60 /* 6 seconds */
#define APP_BLE_RK_SERVER_GATT_ATTRIBUTE_SIZE (22)

#ifndef APP_BLE_RK_SERVER_ATTRIBUTE_MAX
#define APP_BLE_RK_SERVER_ATTRIBUTE_MAX BSA_BLE_ATTRIBUTE_MAX
#endif

typedef struct
{
    tBT_UUID            attr_UUID;
    INT8                uuid_string[38]; //save string uuid
    UINT16              service_id;
    UINT16              attr_id;
    UINT8               attr_type;
    tBSA_BLE_CHAR_PROP  prop;
    tBSA_BLE_PERM       perm;
    BOOLEAN             is_pri;
    BOOLEAN             wait_flag;
} tAPP_BLE_RK_SERVER_ATTRIBUTE;

typedef struct
{
    tBSA_BLE_IF         server_if;
    UINT16              conn_id;
    tAPP_BLE_RK_SERVER_ATTRIBUTE  attr[APP_BLE_RK_SERVER_ATTRIBUTE_MAX]; //service + characteristic + descriptor
} tAPP_BLE_RK_SERVER_CB;

/*******************************************************************************
 **
 ** Function        app_ble_rk_server_init
 **
 ** Description     APP BLE server control block init
 **
 ** Parameters      None
 **
 ** Returns         None
 **
 *******************************************************************************/
void app_ble_rk_server_init(void);

/*******************************************************************************
 **
 ** Function        app_ble_rk_server_gatt_server_init
 **
 ** Description     APP BLE server GATT Server init
 **
 ** Parameters
 **
 ** Returns
 **
 *******************************************************************************/
int app_ble_rk_server_gatt_server_init(RkBleContent *ble_content);

/*******************************************************************************
 **
 ** Function        app_ble_rk_server_send_message
 **
 ** Description     Check if client has registered for notification/indication
 **                       and send message if appropriate
 **
 ** Parameters      data, len
 **
 ** Returns          None
 **
 *******************************************************************************/
void app_ble_rk_server_send_message(const char *uuid, char *data, UINT16 len);

/*******************************************************************************
 **
 ** Function        app_ble_rk_server_open
 **
 ** Description     APP BLE server open
 **
 ** Parameters
 **
 ** Returns
 **
 *******************************************************************************/
int app_ble_rk_server_open(RkBleContent *ble_content);

/*******************************************************************************
 **
 ** Function        app_ble_rk_server_close
 **
 ** Description     APP BLE server close
 **
 ** Parameters      None
 **
 ** Returns         None
 **
 *******************************************************************************/
void app_ble_rk_server_close(void);

/*******************************************************************************
 **
 ** Function        app_ble_rk_server_recv_data_callback
 **
 ** Description     register send data callback
 **
 ** Parameters      None
 **
 ** Returns
 **
 *******************************************************************************/
void app_ble_rk_server_recv_data_callback(RK_BLE_RECV_CALLBACK cb);

/*******************************************************************************
**
** Function         app_ble_rk_server_register_cb
**
** Description      Register ble status notify
**
** Parameters       Notify callback
**
** Returns          void
**
*******************************************************************************/
void app_ble_rk_server_register_cb(RK_BLE_STATE_CALLBACK cb);

/*******************************************************************************
**
** Function         app_ble_rk_server_deregister_cb
**
** Description      DeRegister ble status notify
**
** Parameters       Notify callback
**
** Returns          void
**
*******************************************************************************/
void app_ble_rk_server_deregister_cb();

void app_ble_rk_server_get_state(RK_BLE_STATE *p_state);

/*******************************************************************************
 **
 ** Function        app_ble_rk_server_disconnect
 **
 ** Description     This is the ble close connection
 **
 ** Parameters      None
 **
 ** Returns         status: 0 if success / -1 otherwise
 **
 *******************************************************************************/
int app_ble_rk_server_disconnect(void);

void app_ble_rk_server_set_local_privacy(BOOLEAN local_privacy);

int app_ble_rk_server_set_adv_interval(UINT16 adv_int_min, UINT16 adv_int_max);

void app_ble_register_mtu_callback(RK_BT_MTU_CALLBACK cb);

void app_ble_rk_server_request_data_callback(RK_BLE_REQUEST_DATA cb);
#endif
