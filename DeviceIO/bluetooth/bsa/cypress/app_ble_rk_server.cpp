/*****************************************************************************
**
**  Name:           app_ble_wifi_introducer.c
**
**  Description:    Bluetooth BLE Rockchip main application
**
**  Copyright (c) 2014, Cypress Corp., All Rights Reserved.
**  Cypress Bluetooth Core. Proprietary and confidential.
**
*****************************************************************************/
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "bsa_api.h"
#include "app_ble.h"
#include "app_thread.h"
#include "app_mutex.h"
#include "app_xml_param.h"
#include "app_xml_utils.h"
#include "app_utils.h"
#include "app_dm.h"
#include "app_manager.h"
#include "app_ble_rk_server.h"

/*
* Defines
*/
#define APP_BLE_SERVER_UUID              0xFEAA
#define APP_BLE_ADV_INT_MIN                 32 //20ms
#define BSA_BLE_GAP_ADV_SLOW_INT            48 //30ms
#define BSA_BLE_CONNECT_EVT                 BTA_BLE_CONNECT_EVT
#define BTM_BLE_ADVERT_TYPE_NAME_COMPLETE   0x09

typedef struct {
    RK_BLE_RECV_CALLBACK recv_data_cb;
    RK_BLE_REQUEST_DATA request_data_cb;
    RK_BLE_STATE_CALLBACK state_cb;
    RK_BT_MTU_CALLBACK mtu_cb;
    RK_BLE_STATE state;
    BOOLEAN local_privacy;
    BD_NAME ble_device_name;
    char bd_addr[BT_DEVICE_ADDRESS_LEN];
} tAPP_BLE_INFO;

/*
 * Local Variables
 */
#define APP_BLE_RK_SERVER_DESCRIPTOR_UUID     0x2902
#define APP_BLE_RK_SERVER_DESCRIPTOR_STRING_UUID     "2902"

static tAPP_BLE_RK_SERVER_CB app_ble_rk_server_cb;

static tAPP_BLE_INFO app_ble_info = {
    NULL, NULL, NULL, NULL, RK_BLE_STATE_IDLE, TRUE,
};

static tBSA_DM_BLE_ADV_PARAM app_ble_adv_param = {
    0, 0
};

static void app_ble_rk_server_send_state(BD_ADDR bd_addr, RK_BLE_STATE state) {
    char address[BT_DEVICE_ADDRESS_LEN];

    app_ble_info.state = state;

    if(!app_ble_info.state_cb)
        return;

    memset(address, 0, BT_DEVICE_ADDRESS_LEN);
    if(bd_addr)
        app_mgr_bd2str(bd_addr, address, BT_DEVICE_ADDRESS_LEN);

    if(state == RK_BLE_STATE_CONNECT)
        memcpy(app_ble_info.bd_addr, address, BT_DEVICE_ADDRESS_LEN);

    app_ble_info.state_cb(address, address, state);
}

/*
 * Local functions
 */
static int app_ble_rk_server_register(void);
static int app_ble_rk_server_set_advertisement_data(const char *ble_name);
static int app_ble_rk_server_create_gatt_database(RkBleContent *ble_content);
static int app_ble_rk_server_create_service(tBT_UUID *service_uuid,
               UINT16 num_handle);
static int app_ble_rk_server_start_service(UINT16 service_id);
static int app_ble_rk_server_add_char(tAPP_BLE_RK_SERVER_ATTRIBUTE *attr);
static void app_ble_rk_server_profile_cback(tBSA_BLE_EVT event,
                    tBSA_BLE_MSG *p_data);
static int app_ble_rk_server_find_free_attr(void);
static int app_ble_rk_server_find_attr_index_by_attr_id(UINT16 attr_id);
static int app_ble_rk_server_send_notification(const char *uuid, char *data, UINT16 len);
static void app_ble_rk_server_recv_data(int attr_index, unsigned char *data, int len);

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
void app_ble_rk_server_register_cb(RK_BLE_STATE_CALLBACK cb)
{
	app_ble_info.state_cb = cb;
}

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
void app_ble_rk_server_deregister_cb()
{
    app_ble_info.state_cb = NULL;
}

/*
 * BLE common functions
 */
static void app_ble_rk_server_set_device_name(const char *ble_name)
{
    memset((char *)app_ble_info.ble_device_name, 0, BD_NAME_LEN + 1);
    if(ble_name) {
        sprintf((char *)app_ble_info.ble_device_name, "%s", ble_name);
    } else {
        strcpy((char *)app_ble_info.ble_device_name, (char *)app_xml_config.name);
    }

    app_ble_info.ble_device_name[sizeof(app_ble_info.ble_device_name) - 1] = '\0';
    APP_DEBUG1("ble_device_name: %s", app_ble_info.ble_device_name);
}

/*******************************************************************************
 **
 ** Function        app_ble_rk_server_register
 **
 ** Description     Register app
 **
 ** Parameters      None
 **
 ** Returns         status: 0 if success / -1 otherwise
 **
 *******************************************************************************/
static int app_ble_rk_server_register()
{
    tBSA_STATUS status;
    tBSA_BLE_SE_REGISTER ble_register_param;

    status = BSA_BleSeAppRegisterInit(&ble_register_param);
    if (status != BSA_SUCCESS)
    {
        APP_ERROR1("BSA_BleSeAppRegisterInit failed status = %d", status);
        return -1;
    }

    ble_register_param.uuid.len = LEN_UUID_16;
    ble_register_param.uuid.uu.uuid16 = APP_BLE_SERVER_UUID;
    ble_register_param.p_cback = app_ble_rk_server_profile_cback;

    status = BSA_BleSeAppRegister(&ble_register_param);
    if (status != BSA_SUCCESS)
    {
        APP_ERROR1("BSA_BleSeAppRegister failed status = %d", status);
        return -1;
    }

    app_ble_rk_server_cb.server_if = ble_register_param.server_if;
    APP_INFO1("server_if:%d", app_ble_rk_server_cb.server_if);
    return 0;
}

/*******************************************************************************
 **
 ** Function        app_ble_rk_server_deregister
 **
 ** Description     Register app
 **
 ** Parameters      None
 **
 ** Returns         status: 0 if success / -1 otherwise
 **
 *******************************************************************************/
static int app_ble_rk_server_deregister()
{
    tBSA_STATUS status;
    tBSA_BLE_SE_DEREGISTER ble_deregister_param;

    status = BSA_BleSeAppDeregisterInit(&ble_deregister_param);
    if (status != BSA_SUCCESS)
    {
        APP_ERROR1("BSA_BleSeAppDeregisterInit failed status = %d", status);
        return -1;
    }

    ble_deregister_param.server_if = app_ble_rk_server_cb.server_if;

    status = BSA_BleSeAppDeregister(&ble_deregister_param);
    if (status != BSA_SUCCESS)
    {
        APP_ERROR1("BSA_BleSeAppDeregister failed status = %d", status);
        return -1;
    }

    return 0;
}

/************************************* ******************************************
 **
 ** Function        app_ble_rk_server_set_advertisement_data
 **
 ** Description     Setup advertisement data with 16 byte UUID and device name
 **
 ** Parameters      None
 **
 ** Returns         status: 0 if success / -1 otherwise
 **
 *******************************************************************************/
static int app_ble_rk_server_set_advertisement_data(const char *ble_name)
{
    int i, service_uuid_len = 0;
    tBSA_DM_BLE_AD_MASK data_mask;
    tBSA_DM_SET_CONFIG bt_config;
    tBSA_DM_SET_CONFIG bt_scan_rsp;
    tBSA_STATUS bsa_status;
    UINT8 len = 0;

    APP_DEBUG1("ble_name: %s", ble_name);

    for (i = 0; i < APP_BLE_RK_SERVER_ATTRIBUTE_MAX; i++) {
        if (app_ble_rk_server_cb.attr[i].attr_type == BSA_GATTC_ATTR_TYPE_SRVC) {
            service_uuid_len = app_ble_rk_server_cb.attr[i].attr_UUID.len;
            APP_DEBUG1("attr_UUID %d is service uuid, service_uuid_len: %d", i, service_uuid_len);
            break;
        }
    }

    if(i >= APP_BLE_RK_SERVER_ATTRIBUTE_MAX) {
        APP_ERROR0("No valid service was found");
        return -1;
    }

    /* Set Bluetooth configuration */
    BSA_DmSetConfigInit(&bt_config);

    /* Obviously */
    bt_config.enable = TRUE;

    /* Configure the Advertisement Data parameters */
    bt_config.config_mask = BSA_DM_CONFIG_BLE_ADV_CONFIG_MASK;

    bt_config.adv_config.is_scan_rsp = FALSE;

    /* Use services flag to show above services if required on the peer device */
    if(service_uuid_len == LEN_UUID_16)
        data_mask = BSA_DM_BLE_AD_BIT_FLAGS | BSA_DM_BLE_AD_BIT_PROPRIETARY | BSA_DM_BLE_AD_BIT_SERVICE;
    else if (service_uuid_len == LEN_UUID_128)
        data_mask = BSA_DM_BLE_AD_BIT_FLAGS | BSA_DM_BLE_AD_BIT_SERVICE_128;
    bt_config.adv_config.adv_data_mask = data_mask;

    bt_config.adv_config.flag = BSA_DM_BLE_ADV_FLAG_MASK;
    len += 2;

    if(service_uuid_len == LEN_UUID_16) {
        bt_config.adv_config.num_service = 1;
        bt_config.adv_config.uuid_val[0] = app_ble_rk_server_cb.attr[i].attr_UUID.uu.uuid16;
        //len += (bt_config.adv_config.num_service * sizeof(UINT16) + 1);

        bt_config.adv_config.proprietary.num_elem = 1;
        bt_config.adv_config.proprietary.elem[0].adv_type = BTM_BLE_ADVERT_TYPE_NAME_COMPLETE;
        bt_config.adv_config.proprietary.elem[0].len = strlen(ble_name);
        strcpy((char *)bt_config.adv_config.proprietary.elem[0].val, ble_name);
        //len += (bt_config.adv_config.proprietary.elem[0].len + 1);

        //bt_config.adv_config.len = len;

        bsa_status = BSA_DmSetConfig(&bt_config);
        if (bsa_status != BSA_SUCCESS)
        {
            APP_ERROR1("BSA_DmSetConfig failed status:%d ", bsa_status);
            return -1;
        }
    } else if (service_uuid_len == LEN_UUID_128) {
        memcpy(bt_config.adv_config.services_128b.uuid128,
                       app_ble_rk_server_cb.attr[i].attr_UUID.uu.uuid128, LEN_UUID_128);
        bt_config.adv_config.services_128b.list_cmpl = TRUE;
        len += (LEN_UUID_128 + 1);

        bsa_status = BSA_DmSetConfig(&bt_config);
        if (bsa_status != BSA_SUCCESS)
        {
            APP_ERROR1("BSA_DmSetConfig failed status:%d ", bsa_status);
            return -1;
        }

        /*set scan response*/
        len = 0;

        /* Set Bluetooth configuration */
        BSA_DmSetConfigInit(&bt_scan_rsp);

        /* Obviously */
        bt_scan_rsp.enable = TRUE;

        /* Configure the Advertisement Data parameters */
        bt_scan_rsp.config_mask = BSA_DM_CONFIG_BLE_ADV_CONFIG_MASK;

        bt_scan_rsp.adv_config.is_scan_rsp = TRUE;

        /* Use services flag to show above services if required on the peer device */
        data_mask = BSA_DM_BLE_AD_BIT_PROPRIETARY;
        bt_scan_rsp.adv_config.adv_data_mask = data_mask;

        bt_scan_rsp.adv_config.proprietary.num_elem = 1;
        bt_scan_rsp.adv_config.proprietary.elem[0].adv_type = BTM_BLE_ADVERT_TYPE_NAME_COMPLETE;
        bt_scan_rsp.adv_config.proprietary.elem[0].len = strlen(ble_name);
        strcpy((char *)bt_scan_rsp.adv_config.proprietary.elem[0].val, ble_name);
        len += (bt_scan_rsp.adv_config.proprietary.elem[0].len + 1);

        bt_scan_rsp.adv_config.len = len;

        bsa_status = BSA_DmSetConfig(&bt_scan_rsp);
        if (bsa_status != BSA_SUCCESS)
        {
            APP_ERROR1("BSA_DmSetConfig failed status:%d ", bsa_status);
            return -1;
        }
    }

    return 0;
}

static int app_ble_rk_server_set_adv_config(tBSA_DM_BLE_ADV_CONFIG *adv_config, UINT8 *adv_data, int data_len)
{
    int i;
    UINT8 len, type;
    tBSA_DM_BLE_AD_MASK data_mask = 0;

    if(adv_data == NULL || data_len == 0)
        return -1;

    for(i = 1; i < data_len;) {
        len = adv_data[i];
        type = adv_data[i + 1];

        if(len == 0) {
            APP_DEBUG0("non-significant, exit adv config");
            break;
        }

        //APP_DEBUG1("len: %2x", len);
        //APP_DEBUG1("type: %2x", type);

        switch(type) {
            case 0x01:
                APP_INFO0("Device LE physical connection");
                data_mask |= BSA_DM_BLE_AD_BIT_FLAGS;
                adv_config->flag = adv_data[i + 2];
                break;

            case 0x02:
            case 0x03:
                APP_INFO0("16 bit UUID");
                data_mask |= BSA_DM_BLE_AD_BIT_SERVICE;
                memcpy((char *)adv_config->uuid_val, &adv_data[i + 2], len - 1);
                adv_config->num_service = (len - 1) / sizeof(UINT16);
                APP_INFO1("num_service: %d", adv_config->num_service);
                break;

            case 0x04:
            case 0x05:
                APP_DEBUG0("Not support 32 bit UUID");
                break;

            case 0x06:
            case 0x07:
                APP_INFO0("128 bit UUID");
                data_mask |= BSA_DM_BLE_AD_BIT_SERVICE_128;
                memcpy(adv_config->services_128b.uuid128, &adv_data[i + 2], LEN_UUID_128);
                adv_config->services_128b.list_cmpl = TRUE;
                break;

            case 0x0a:
                APP_INFO0("TX Power Level");
                data_mask |= BSA_DM_BLE_AD_BIT_TX_PWR;
                adv_config->tx_power = adv_data[i + 2];
                APP_DEBUG1("adv_config->tx_power: %02x", adv_config->tx_power);
                break;

            case 0x12:
                APP_INFO0("Peripheral(Slave) connection interval range");
                APP_DEBUG1("int_range data len = %d bypes", sizeof(tBSA_DM_BLE_INT_RANGE));
                data_mask |= BSA_DM_BLE_AD_BIT_INT_RANGE;
                memcpy((char *)&adv_config->int_range, &adv_data[i + 2], sizeof(tBSA_DM_BLE_INT_RANGE));
                break;

            case 0x19:
                APP_INFO0("Appearance");
                data_mask |= BSA_DM_BLE_AD_BIT_APPEARANCE;
                memcpy((char *)&adv_config->appearance_data, &adv_data[i + 2], sizeof(UINT16));
                break;

            default:
                APP_DEBUG1("AD type: %02x", type);
                data_mask |= BSA_DM_BLE_AD_BIT_PROPRIETARY;
                adv_config->proprietary.elem[adv_config->proprietary.num_elem].adv_type = type;
                adv_config->proprietary.elem[adv_config->proprietary.num_elem].len = len - 1;
                memcpy((char *)adv_config->proprietary.elem[adv_config->proprietary.num_elem].val, &adv_data[i + 2], len - 1);
                adv_config->proprietary.num_elem += 1;
                break;
        }

        i += (len + 1);
    }

    adv_config->adv_data_mask = data_mask;

    return 0;
}

static int app_ble_rk_server_set_user_adv_data(RkBleContent *ble_content)
{
    //int i, j;
    tBSA_DM_SET_CONFIG bt_config;
    tBSA_DM_SET_CONFIG bt_scan_rsp;
    tBSA_STATUS bsa_status;

    APP_DEBUG1("advDataLen: %d, respDataLen: %d",
        ble_content->advDataLen, ble_content->respDataLen);

    if(ble_content->advDataLen == 0) {
        APP_ERROR0("advDataType is BLE_ADVDATA_TYPE_USER, must set advData");
        return -1;
    }

    /* Set Bluetooth configuration */
    BSA_DmSetConfigInit(&bt_config);

    /* Obviously */
    bt_config.enable = TRUE;

    /* Configure the Advertisement Data parameters */
    bt_config.config_mask = BSA_DM_CONFIG_BLE_ADV_CONFIG_MASK;

    bt_config.adv_config.is_scan_rsp = FALSE;

#if 0
    printf("---------advData--------\n");
    for(i = 0; i < ble_content->advDataLen; i++) {
        printf("%02x ", ble_content->advData[i]);
    }
    printf("\n---------advData--------\n");
#endif

    app_ble_rk_server_set_adv_config(&bt_config.adv_config, ble_content->advData, ble_content->advDataLen);

#if 0
    APP_DEBUG1("adv_data_mask: %x", bt_config.adv_config.adv_data_mask);
    APP_DEBUG1("flag: %02x", bt_config.adv_config.flag);
    APP_DEBUG1("proprietary.num_elem: %d", bt_config.adv_config.proprietary.num_elem);
    for(i = 0; i < bt_config.adv_config.proprietary.num_elem; i++) {
        APP_DEBUG1("adv_type: %02x", bt_config.adv_config.proprietary.elem[i].adv_type);
        APP_DEBUG1("len: %02x", bt_config.adv_config.proprietary.elem[i].len);
        for(j = 0; j < bt_config.adv_config.proprietary.elem[i].len; j++) {
            printf("%02x ", bt_config.adv_config.proprietary.elem[i].val[j]);
        }
        printf("\n");
    }
#endif

    bsa_status = BSA_DmSetConfig(&bt_config);
    if (bsa_status != BSA_SUCCESS)
    {
        APP_ERROR1("BSA_DmSetConfig failed status:%d ", bsa_status);
        return -1;
    }

    /*set scan response*/
    if(ble_content->respDataLen > 0) {
        /* Set Bluetooth configuration */
        BSA_DmSetConfigInit(&bt_scan_rsp);

        /* Obviously */
        bt_scan_rsp.enable = TRUE;

        /* Configure the Advertisement Data parameters */
        bt_scan_rsp.config_mask = BSA_DM_CONFIG_BLE_ADV_CONFIG_MASK;

        bt_scan_rsp.adv_config.is_scan_rsp = TRUE;
#if 0
        printf("--------respData---------\n");
        for(i = 0; i < ble_content->respDataLen; i++) {
            printf("%02x ", ble_content->respData[i]);
        }
        printf("\n--------respData---------\n");
#endif

        app_ble_rk_server_set_adv_config(&bt_scan_rsp.adv_config, ble_content->respData, ble_content->respDataLen);

#if 0
        APP_DEBUG1("adv_data_mask: %x", bt_scan_rsp.adv_config.adv_data_mask);
        APP_DEBUG1("flag: %02x", bt_scan_rsp.adv_config.flag);
        APP_DEBUG1("proprietary.num_elem: %d", bt_scan_rsp.adv_config.proprietary.num_elem);
        for(i = 0; i < bt_scan_rsp.adv_config.proprietary.num_elem; i++) {
            APP_DEBUG1("adv_type: %02x", bt_scan_rsp.adv_config.proprietary.elem[i].adv_type);
            APP_DEBUG1("len: %02x", bt_scan_rsp.adv_config.proprietary.elem[i].len);
            for(j = 0; j < bt_scan_rsp.adv_config.proprietary.elem[i].len; j++) {
                printf("%02x ", bt_scan_rsp.adv_config.proprietary.elem[i].val[j]);
            }
            printf("\n");
        }
#endif

        bsa_status = BSA_DmSetConfig(&bt_scan_rsp);
        if (bsa_status != BSA_SUCCESS)
        {
            APP_ERROR1("BSA_DmSetConfig failed status:%d ", bsa_status);
            return -1;
        }
    }
    return 0;
}

/*******************************************************************************
 **
 ** Function        app_ble_rk_server_create_gatt_database
 **
 ** Description     This is the GATT database for the WiFi Introducer Sensor application.
 **                       It defines services, characteristics and descriptors supported by the sensor.
 **
 ** Parameters      None
 **
 ** Returns         status: 0 if success / -1 otherwise
 **
 *******************************************************************************/
static int app_ble_rk_server_set_uuid(tBT_UUID *app_ble_uuid, Ble_Uuid_Type_t uu)
{
    APP_DEBUG1("len: %d, uuid: %s", uu.len, uu.uuid);
    app_ble_uuid->len = uu.len;
    if(uu.len == LEN_UUID_16) {
        //"1111"
        app_ble_string_to_uuid16(&app_ble_uuid->uu.uuid16, uu.uuid);
        APP_DEBUG1("uu.uuid16: 0x%x", app_ble_uuid->uu.uuid16);
    } else if (uu.len == LEN_UUID_128) {
        //"0000180A-0000-1000-8000-00805F9B34FB"
        app_ble_string_to_uuid128(app_ble_uuid->uu.uuid128, uu.uuid, FALSE);

        printf("uu.uuid128: ");
        for (int i = 0; i < MAX_UUID_SIZE; i++)
            printf("0x%x ", app_ble_uuid->uu.uuid128[i]);
        printf("\n");
    } else {
        APP_ERROR1("not support, uuid len: %d\n", uu.len);
        return -1;
    }

    return 0;
}

static int app_ble_rk_server_create_gatt_database(RkBleContent *ble_content)
{
    int i;
    int srvc_attr_index, char_attr_index;
    tBT_UUID service_uuid;
    tAPP_BLE_RK_SERVER_ATTRIBUTE attr;

    //service count * 2 + characteristics count * 2 + descriptor count * 1
    UINT16 num_handle = 2 + ble_content->chr_cnt * 2 + ble_content->chr_cnt;

    APP_INFO1("app_ble_rk_server_create_gatt_database, num_handle: %d", num_handle);

    if(app_ble_rk_server_set_uuid(&service_uuid, ble_content->server_uuid) < 0) {
        APP_ERROR0("set service uuid failed");
        return -1;
    }
    srvc_attr_index = app_ble_rk_server_create_service(&service_uuid, num_handle);
    if (srvc_attr_index < 0) {
        APP_ERROR0("Wifi Config Service Create Fail");
        return -1;
    }
    APP_DEBUG1("srvc_attr_index: %d", srvc_attr_index);
    //save service uuid string
    strcpy((char *)app_ble_rk_server_cb.attr[srvc_attr_index].uuid_string, ble_content->server_uuid.uuid);
    
    for(i = 0; i < ble_content->chr_cnt; i++) {
        memset(&attr, 0, sizeof(tAPP_BLE_RK_SERVER_ATTRIBUTE));
        if(app_ble_rk_server_set_uuid(&attr.attr_UUID, ble_content->chr_uuid[i]) < 0) {
            APP_ERROR0("set characteristic uuid failed");
            return -1;
        }

        attr.service_id = app_ble_rk_server_cb.attr[srvc_attr_index].service_id;
        attr.perm = BSA_GATT_PERM_READ | BSA_GATT_PERM_WRITE; //17
        attr.prop = BSA_GATT_CHAR_PROP_BIT_READ | BSA_GATT_CHAR_PROP_BIT_WRITE |
                        BSA_GATT_CHAR_PROP_BIT_NOTIFY | BSA_GATT_CHAR_PROP_BIT_INDICATE |
                        BSA_GATT_CHAR_PROP_BIT_WRITE_NR;
        attr.attr_type = BSA_GATTC_ATTR_TYPE_CHAR;
        char_attr_index = app_ble_rk_server_add_char(&attr);
        if (char_attr_index < 0) {
            APP_ERROR0("add characteristic failed");
            return -1;
        }
        //save char uuid string
        strcpy((char *)app_ble_rk_server_cb.attr[char_attr_index].uuid_string, ble_content->chr_uuid[i].uuid);

        /* Declare client characteristic configuration descriptor
         * Value of the descriptor can be modified by the client
         * Value modified shall be retained during connection and across connection
         * for bonded devices.  Setting value to 1 tells this application to send notification
         * when value of the characteristic changes.  Value 2 is to allow indications.
         */
        memset(&attr, 0, sizeof(tAPP_BLE_RK_SERVER_ATTRIBUTE));
        attr.attr_UUID.len = LEN_UUID_16;
        attr.attr_UUID.uu.uuid16 = APP_BLE_RK_SERVER_DESCRIPTOR_UUID;
        attr.service_id = app_ble_rk_server_cb.attr[srvc_attr_index].service_id;
        attr.perm = BSA_GATT_PERM_READ | BSA_GATT_PERM_WRITE; //17
        attr.attr_type = BSA_GATTC_ATTR_TYPE_CHAR_DESCR;
        char_attr_index = app_ble_rk_server_add_char(&attr);
        if (char_attr_index < 0) {
            APP_ERROR0("add descriptor failed");
            return -1;
        }
        //save descriptor uuid string
        strcpy((char *)app_ble_rk_server_cb.attr[char_attr_index].uuid_string, APP_BLE_RK_SERVER_DESCRIPTOR_STRING_UUID);
    }

    return 0;
}

/*******************************************************************************
 **
 ** Function        app_ble_rk_server_create_service
 **
 ** Description     create service
 **
 ** Parameters     service UUID
 **                       number of handle for reserved
 **
 ** Returns         status: 0 if success / -1 otherwise
 **
 *******************************************************************************/
static int app_ble_rk_server_create_service(tBT_UUID *service_uuid,
               UINT16 num_handle)
{
    tBSA_STATUS status;
    tBSA_BLE_SE_CREATE ble_create_param;
    int attr_index = -1;
    UINT32 timeout = APP_BLE_RK_SERVER_TIMEOUT;

    attr_index = app_ble_rk_server_find_free_attr();
    if (attr_index < 0)
    {
        APP_ERROR1("Wrong attr number! = %d", attr_index);
        return -1;
    }

    status = BSA_BleSeCreateServiceInit(&ble_create_param);
    if (status != BSA_SUCCESS)
    {
        APP_ERROR1("BSA_BleSeCreateServiceInit failed status = %d", status);
        return -1;
    }

    memcpy(&ble_create_param.service_uuid, service_uuid, sizeof(tBT_UUID));
    ble_create_param.server_if = app_ble_rk_server_cb.server_if;
    ble_create_param.num_handle = num_handle;
    ble_create_param.is_primary = TRUE;

    app_ble_rk_server_cb.attr[attr_index].wait_flag = TRUE;

    if ((status = BSA_BleSeCreateService(&ble_create_param)) == BSA_SUCCESS)
    {
        while (app_ble_rk_server_cb.attr[attr_index].wait_flag && timeout)
        {
            GKI_delay(100);
            timeout--;
        }
    }
    if ((status != BSA_SUCCESS) || (timeout == 0))
    {
        APP_ERROR1("BSA_BleSeCreateService failed status = %d", status);
        app_ble_rk_server_cb.attr[attr_index].wait_flag = FALSE;
        return -1;
    }

    /* store information on control block */
    memcpy(&app_ble_rk_server_cb.attr[attr_index].attr_UUID, service_uuid,
                    sizeof(tBT_UUID));
    app_ble_rk_server_cb.attr[attr_index].is_pri = ble_create_param.is_primary;
    app_ble_rk_server_cb.attr[attr_index].attr_type = BSA_GATTC_ATTR_TYPE_SRVC;

    //APP_DEBUG1("service attr_index: %d", attr_index);
    return attr_index;
}

/*******************************************************************************
 **
 ** Function        app_ble_rk_server_start_service
 **
 ** Description     Start Service
 **
 ** Parameters      service_id : attr id
 **
 ** Returns         status: 0 if success / -1 otherwise
 **
 *******************************************************************************/
static int app_ble_rk_server_start_service(UINT16 service_id)
{
    tBSA_STATUS status;
    tBSA_BLE_SE_START ble_start_param;

    status = BSA_BleSeStartServiceInit(&ble_start_param);
    if (status != BSA_SUCCESS)
    {
        APP_ERROR1("BSA_BleSeStartServiceInit failed status = %d", status);
        return -1;
    }

    ble_start_param.service_id = service_id;
    ble_start_param.sup_transport = BSA_BLE_GATT_TRANSPORT_LE;

    APP_INFO1("service_id:%d", ble_start_param.service_id);

    status = BSA_BleSeStartService(&ble_start_param);
    if (status != BSA_SUCCESS)
    {
        APP_ERROR1("BSA_BleSeStartService failed status = %d", status);
        return -1;
    }
    return 0;
}

/*******************************************************************************
 **
 ** Function        app_ble_rk_server_stop_service
 **
 ** Description     Stop Service
 **
 ** Parameters      service_id : attr id
 **
 ** Returns         status: 0 if success / -1 otherwise
 **
 *******************************************************************************/
static int app_ble_rk_server_stop_service(UINT16 service_id)
{
    tBSA_STATUS status;
    tBSA_BLE_SE_STOP ble_stop_param;

    status = BSA_BleSeStopServiceInit(&ble_stop_param);
    if (status != BSA_SUCCESS)
    {
        APP_ERROR1("BSA_BleSeStopServiceInit failed status = %d", status);
        return -1;
    }

    ble_stop_param.service_id = service_id;

    status = BSA_BleSeStopService(&ble_stop_param);
    if (status != BSA_SUCCESS)
    {
        APP_ERROR1("BSA_BleSeStopService failed status = %d", status);
        return -1;
    }
    return 0;
}

/*******************************************************************************
 **
 ** Function        app_ble_rk_server_add_char
 **
 ** Description     Add character to service
 **
 ** Parameters      tAPP_BLE_RK_SERVER_ATTRIBUTE
 **
 ** Returns         status: 0 if success / -1 otherwise
 **
 *******************************************************************************/
static int app_ble_rk_server_add_char(tAPP_BLE_RK_SERVER_ATTRIBUTE *attr)
{
    tBSA_STATUS status;
    tBSA_BLE_SE_ADDCHAR ble_addchar_param;
    int attr_index = -1;
    UINT32 timeout = APP_BLE_RK_SERVER_TIMEOUT;

    attr_index = app_ble_rk_server_find_free_attr();
    if (attr_index < 0)
    {
        APP_ERROR1("Wrong attr index! = %d", attr_index);
        return -1;
    }

    APP_DEBUG1("attr_type: %d, attr_index: %d", attr->attr_type, attr_index);

    status = BSA_BleSeAddCharInit(&ble_addchar_param);
    if (status != BSA_SUCCESS)
    {
        APP_ERROR1("BSA_BleSeAddCharInit failed status = %d", status);
        return -1;
    }

    /* characteristic */
    ble_addchar_param.service_id = attr->service_id;
    ble_addchar_param.is_descr = (attr->attr_type == BSA_GATTC_ATTR_TYPE_CHAR_DESCR);
    memcpy(&ble_addchar_param.char_uuid, &attr->attr_UUID, sizeof(tBT_UUID));
    ble_addchar_param.perm = attr->perm;
    ble_addchar_param.property = attr->prop;

    //APP_INFO1("app_ble_rk_server_add_char service_id:%d", ble_addchar_param.service_id);
    app_ble_rk_server_cb.attr[attr_index].wait_flag = TRUE;

    if ((status = BSA_BleSeAddChar(&ble_addchar_param)) == BSA_SUCCESS)
    {
        while (app_ble_rk_server_cb.attr[attr_index].wait_flag && timeout)
        {
            GKI_delay(100);
            timeout--;
        }
    }
    if ((status != BSA_SUCCESS) || (timeout == 0))
    {
        APP_ERROR1("BSA_BleSeAddChar failed status = %d", status);
        return -1;
    }

    /* store information on control block */
    memcpy(&app_ble_rk_server_cb.attr[attr_index].attr_UUID, &attr->attr_UUID,
                    sizeof(tBT_UUID));
    app_ble_rk_server_cb.attr[attr_index].prop = attr->prop;
    app_ble_rk_server_cb.attr[attr_index].attr_type = attr->attr_type;
    return attr_index;
}

static void app_ble_rk_server_send_data_test(const char *uuid) {
    static char ble_test[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    int i, offset = 0;
    int len = strlen(ble_test);
    char buf[20];

    //APP_DEBUG1("ble bt len: %d\n", len);
    for (i=1; ; i++) {
        memset(buf, 0, 20);

        if(offset + i < len) {
            strncpy(buf, ble_test + offset, i);
            //APP_DEBUG1("buf: %s\n", buf);
            app_ble_rk_server_send_message(uuid, buf, i);
            offset += i;
        } else {
            strncpy(buf, ble_test + offset, len - offset);
            //APP_DEBUG1("buf: %s\n", buf);
            app_ble_rk_server_send_message(uuid, buf, len - offset);
            offset = 0;
            break;
        }

        //wait for 1s
        sleep(1);
    }
}

/*******************************************************************************
**
** Function         app_ble_rk_server_profile_cback
**
** Description      APP BLE server Profile callback.
**
** Returns          void
**
*******************************************************************************/
static void app_ble_rk_server_profile_cback(tBSA_BLE_EVT event,
                   tBSA_BLE_MSG *p_data)
{
    int attr_index;
    tBSA_BLE_SE_SENDRSP send_server_resp;
    tBSA_DM_BLE_ADV_PARAM adv_param;
    UINT8 attribute_value[BSA_BLE_MAX_ATTR_LEN]={0x11,0x22,0x33,0x44};

    APP_DEBUG1("event = %d ", event);

    switch (event)
    {
    case BSA_BLE_SE_DEREGISTER_EVT:
        APP_INFO1("BSA_BLE_SE_DEREGISTER_EVT server_if:%d status:%d",
            p_data->ser_deregister.server_if, p_data->ser_deregister.status);
        if (p_data->ser_deregister.server_if != app_ble_rk_server_cb.server_if)
        {
            APP_ERROR0("wrong deregister interface!!");
            break;
        }

        app_ble_rk_server_cb.server_if = BSA_BLE_INVALID_IF;
        for (attr_index = 0; attr_index < APP_BLE_RK_SERVER_ATTRIBUTE_MAX; attr_index++)
        {
            memset(&app_ble_rk_server_cb.attr[attr_index], 0,
                              sizeof(tAPP_BLE_RK_SERVER_ATTRIBUTE));
        }
        break;

    case BSA_BLE_SE_CREATE_EVT:
        APP_INFO1("BSA_BLE_SE_CREATE_EVT server_if:%d status:%d service_id:%d",
            p_data->ser_create.server_if, p_data->ser_create.status, p_data->ser_create.service_id);

        /* search interface number */
        if (p_data->ser_create.server_if != app_ble_rk_server_cb.server_if)
        {
            APP_ERROR0("interface wrong!!");
            break;
        }

        /* search attribute number */
        for (attr_index = 0; attr_index < APP_BLE_RK_SERVER_ATTRIBUTE_MAX; attr_index++)
        {
            if (app_ble_rk_server_cb.attr[attr_index].wait_flag == TRUE)
            {
                APP_INFO1("BSA_BLE_SE_CREATE_EVT attr_index:%d", attr_index);
                if (p_data->ser_create.status == BSA_SUCCESS)
                {
                    app_ble_rk_server_cb.attr[attr_index].service_id = p_data->ser_create.service_id;
                    app_ble_rk_server_cb.attr[attr_index].attr_id = p_data->ser_create.service_id;
                    app_ble_rk_server_cb.attr[attr_index].wait_flag = FALSE;
                    break;
                }
                else  /* if CREATE fail */
                {
                    memset(&app_ble_rk_server_cb.attr[attr_index], 0, sizeof(tAPP_BLE_RK_SERVER_ATTRIBUTE));
                    break;
                }
            }
        }
        if (attr_index >= APP_BLE_RK_SERVER_ATTRIBUTE_MAX)
        {
            APP_ERROR0("BSA_BLE_SE_CREATE_EVT no waiting!!");
            break;
        }
        break;

    case BSA_BLE_SE_ADDCHAR_EVT:
        APP_INFO1("BSA_BLE_SE_ADDCHAR_EVT status:%d", p_data->ser_addchar.status);
        APP_INFO1("attr_id:0x%x", p_data->ser_addchar.attr_id);

        /* search attribute number */
        for (attr_index = 0; attr_index < APP_BLE_RK_SERVER_ATTRIBUTE_MAX; attr_index++)
        {
            if (app_ble_rk_server_cb.attr[attr_index].wait_flag == TRUE)
            {
                APP_INFO1("BSA_BLE_SE_ADDCHAR_EVT attr_index:%d", attr_index);
                if (p_data->ser_addchar.status == BSA_SUCCESS)
                {
                    app_ble_rk_server_cb.attr[attr_index].service_id = p_data->ser_addchar.service_id;
                    app_ble_rk_server_cb.attr[attr_index].attr_id = p_data->ser_addchar.attr_id;
                    app_ble_rk_server_cb.attr[attr_index].wait_flag = FALSE;
                    break;
                }
                else  /* if ADD fail */
                {
                    memset(&app_ble_rk_server_cb.attr[attr_index], 0, sizeof(tAPP_BLE_RK_SERVER_ATTRIBUTE));
                    break;
                }
            }
        }
        if (attr_index >= APP_BLE_RK_SERVER_ATTRIBUTE_MAX)
        {
            APP_ERROR0("BSA_BLE_SE_ADDCHAR_EVT no waiting!!");
            break;
        }
        break;

    case BSA_BLE_SE_START_EVT:
        APP_INFO1("BSA_BLE_SE_START_EVT status:%d", p_data->ser_start.status);
        break;

    case BSA_BLE_SE_STOP_EVT:
        APP_INFO1("BSA_BLE_SE_STOP_EVT status:%d", p_data->ser_stop.status);
        break;

    case BSA_BLE_SE_READ_EVT:
        APP_INFO1("BSA_BLE_SE_READ_EVT status:%d, handle:%d", p_data->ser_read.status, p_data->ser_read.handle);

        attr_index = app_ble_rk_server_find_attr_index_by_attr_id(p_data->ser_read.handle);
        APP_INFO1("BSA_BLE_SE_READ_EVT attr_index:%d", attr_index);
        if (attr_index < 0) {
            APP_ERROR0("Cannot find matched attr_id");
            break;
        }

        BSA_BleSeSendRspInit(&send_server_resp);
        send_server_resp.conn_id = p_data->ser_read.conn_id;
        send_server_resp.trans_id = p_data->ser_read.trans_id;
        send_server_resp.status = p_data->ser_read.status;
        send_server_resp.handle = p_data->ser_read.handle;
        send_server_resp.offset = p_data->ser_read.offset;
        send_server_resp.len = 0;
        send_server_resp.auth_req = GATT_AUTH_REQ_NONE;
        //memcpy(send_server_resp.value, attribute_value, BSA_BLE_MAX_ATTR_LEN);
        //APP_INFO1("BSA_BLE_SE_READ_EVT: send_server_resp.conn_id:%d, send_server_resp.trans_id:%d, send_server_resp.status:%d", send_server_resp.conn_id, send_server_resp.trans_id, send_server_resp.status);
        //APP_INFO1("BSA_BLE_SE_READ_EVT: send_server_resp.status:%d,send_server_resp.auth_req:%d", send_server_resp.status,send_server_resp.auth_req);
        //APP_INFO1("BSA_BLE_SE_READ_EVT: send_server_resp.handle:%d, send_server_resp.offset:%d, send_server_resp.len:%d", send_server_resp.handle,send_server_resp.offset,send_server_resp.len );
        BSA_BleSeSendRsp(&send_server_resp);

        if(app_ble_info.request_data_cb) {
            msleep(20);
            app_ble_info.request_data_cb((char *)app_ble_rk_server_cb.attr[attr_index].uuid_string);
        }

        break;

    case BSA_BLE_SE_WRITE_EVT:
        APP_INFO1("BSA_BLE_SE_WRITE_EVT status:%d", p_data->ser_write.status);
        APP_INFO1("p_data->ser_write.len: %d", p_data->ser_write.len);
        APP_DUMP("Write value", p_data->ser_write.value, p_data->ser_write.len);
        APP_INFO1("BSA_BLE_SE_WRITE_EVT trans_id:%d, conn_id:%d, handle:%d",
            p_data->ser_write.trans_id, p_data->ser_write.conn_id, p_data->ser_write.handle);

        attr_index = app_ble_rk_server_find_attr_index_by_attr_id(p_data->ser_write.handle);
        APP_INFO1("BSA_BLE_SE_WRITE_EVT attr_index:%d", attr_index);
        if (attr_index < 0)
        {
            APP_ERROR0("Cannot find matched attr_id");
            break;
        }

        if (p_data->ser_write.need_rsp)
        {
            BSA_BleSeSendRspInit(&send_server_resp);
            send_server_resp.conn_id = p_data->ser_write.conn_id;
            send_server_resp.trans_id = p_data->ser_write.trans_id;
            send_server_resp.status = p_data->ser_write.status;
            send_server_resp.handle = p_data->ser_write.handle;
            send_server_resp.len = 0;
            APP_INFO1("BSA_BLE_SE_WRITE_EVT: send_server_resp.conn_id:%d, send_server_resp.trans_id:%d, send_server_resp.status:%d", send_server_resp.conn_id, send_server_resp.trans_id, send_server_resp.status);
            APP_INFO1("BSA_BLE_SE_WRITE_EVT: send_server_resp.status:%d,send_server_resp.auth_req:%d", send_server_resp.status,send_server_resp.auth_req);
            APP_INFO1("BSA_BLE_SE_WRITE_EVT: send_server_resp.handle:%d, send_server_resp.offset:%d, send_server_resp.len:%d", send_server_resp.handle,send_server_resp.offset,send_server_resp.len);
            BSA_BleSeSendRsp(&send_server_resp);
        }

        app_ble_rk_server_recv_data(attr_index, p_data->ser_write.value, p_data->ser_write.len);

        //only test
        //app_ble_rk_server_send_data_test((char *)app_ble_rk_server_cb.attr[attr_index].uuid_string);
        break;

    case BSA_BLE_SE_EXEC_WRITE_EVT:
        APP_INFO1("BSA_BLE_SE_EXEC_WRITE_EVT trans_id:%d, conn_id:%d, flag:%d",
            p_data->ser_exec_write.trans_id, p_data->ser_exec_write.conn_id,
            p_data->ser_exec_write.flag);

#if 0
        BSA_BleSeSendRspInit(&send_server_resp);
        send_server_resp.conn_id = p_data->ser_exec_write.conn_id;
        send_server_resp.trans_id = p_data->ser_exec_write.trans_id;
        send_server_resp.status = p_data->ser_exec_write.status;
        send_server_resp.handle = p_data->ser_exec_write.handle;
        send_server_resp.len = 0;
        APP_INFO1("conn_id:%d, trans_id:%d",
            send_server_resp.conn_id, send_server_resp.trans_id);
        APP_INFO1("status:%d, auth_req:%d",
            send_server_resp.status,send_server_resp.auth_req);
        APP_INFO1("handle:%d, offset:%d, len:%d",
            send_server_resp.handle,send_server_resp.offset,send_server_resp.len );
        BSA_BleSeSendRsp(&send_server_resp);
#endif
        break;

    case BSA_BLE_SE_OPEN_EVT:
        APP_INFO1("BSA_BLE_SE_OPEN_EVT status:%d", p_data->ser_open.reason);
        if (p_data->ser_open.reason == BSA_SUCCESS)
        {
            APP_INFO1("app_ble_rk_server_conn_up conn_id:0x%x", p_data->ser_open.conn_id);
            app_ble_rk_server_cb.conn_id = p_data->ser_open.conn_id;

            APP_INFO1("app_ble_rk_server_conn_up connected to [%02X:%02X:%02X:%02X:%02X:%02X]",
                          p_data->ser_open.remote_bda[0],
                          p_data->ser_open.remote_bda[1],
                          p_data->ser_open.remote_bda[2],
                          p_data->ser_open.remote_bda[3],
                          p_data->ser_open.remote_bda[4],
                          p_data->ser_open.remote_bda[5]);

            /* Stop advertising */
            app_dm_set_ble_visibility(FALSE, FALSE);
            app_ble_rk_server_send_state(p_data->ser_open.remote_bda, RK_BLE_STATE_CONNECT);
            APP_INFO0("Stopping Advertisements");
        }
        break;

    case BSA_BLE_SE_MTU_EVT:
        APP_INFO1("BSA_BLE_SE_MTU_EVT  :conn_id:0x%x, mtu:%d",
            p_data->ser_mtu.conn_id, p_data->ser_mtu.att_mtu);

        if(app_ble_info.mtu_cb)
            app_ble_info.mtu_cb(app_ble_info.bd_addr ,p_data->ser_mtu.att_mtu);
        break;

    case BSA_BLE_SE_CONGEST_EVT:
        APP_INFO1("BSA_BLE_SE_CONGEST_EVT  :conn_id:0x%x, congested:%d",
            p_data->ser_congest.conn_id, p_data->ser_congest.congested);
        break;

    case BSA_BLE_SE_CLOSE_EVT:
        APP_INFO1("BSA_BLE_SE_CLOSE_EVT status:%d", p_data->ser_close.reason);
        APP_INFO1("conn_id:0x%x", p_data->ser_close.conn_id);
        APP_INFO1("app_ble_rk_server_connection_down  conn_id:%d reason:%d", p_data->ser_close.conn_id, p_data->ser_close.reason);

        app_ble_rk_server_cb.conn_id = BSA_BLE_INVALID_CONN;

        /* start low advertisements */
        /* Set ADV params */
        memset(&adv_param, 0, sizeof(tBSA_DM_BLE_ADV_PARAM));
        adv_param.adv_type = BSA_BLE_CONNECT_EVT;
        if(app_ble_adv_param.adv_int_min >= APP_BLE_ADV_INT_MIN)
            adv_param.adv_int_min = app_ble_adv_param.adv_int_min;
        else
            adv_param.adv_int_min = BSA_BLE_GAP_ADV_SLOW_INT;

        if(app_ble_adv_param.adv_int_max >= APP_BLE_ADV_INT_MIN){
            if(app_ble_adv_param.adv_int_max < app_ble_adv_param.adv_int_min)
                app_ble_adv_param.adv_int_max = app_ble_adv_param.adv_int_min;
            adv_param.adv_int_max = app_ble_adv_param.adv_int_max;
        } else {
            adv_param.adv_int_max = BSA_BLE_GAP_ADV_SLOW_INT;
        }
        app_dm_set_ble_adv_param(&adv_param);

        /* Set visisble and connectable */
        app_dm_set_ble_visibility(TRUE, TRUE);
        app_ble_rk_server_send_state(p_data->ser_close.remote_bda, RK_BLE_STATE_DISCONNECT);
        break;

    case BSA_BLE_SE_CONFIRM_EVT:
        APP_INFO1("BSA_BLE_SE_CONFIRM_EVT  :conn_id:0x%x, status:%d",
            p_data->ser_confirm.conn_id, p_data->ser_confirm.status);
        break;

    default:
        break;
    }
}

/*******************************************************************************
 **
 ** Function         app_ble_rk_server_init
 **
 ** Description     APP BLE server control block init
 **
 ** Parameters     None
 **
 ** Returns          None
 **
 *******************************************************************************/
void app_ble_rk_server_init(void)
{
    memset(&app_ble_rk_server_cb, 0, sizeof(app_ble_rk_server_cb));
    app_ble_rk_server_cb.conn_id = BSA_BLE_INVALID_CONN;
}

/*******************************************************************************
 **
 ** Function         app_ble_rk_server_gatt_server_init
 **
 ** Description     APP BLE server GATT Server init
 **
 ** Parameters     None
 **
 ** Returns          None
 **
 *******************************************************************************/
int app_ble_rk_server_gatt_server_init(RkBleContent *ble_content)
{
    int ret, index;

    APP_INFO0("wifi_introducer_gatt_server_init");
    /* register BLE server app */
    /* Register with stack to receive GATT callback */
    ret = app_ble_rk_server_register();
    if(ret < 0) {
        APP_ERROR0("register BLE server app failed");
        return -1;
    }
    GKI_delay(1000);

    ret = app_ble_rk_server_create_gatt_database(ble_content);
    if(ret < 0) {
        APP_ERROR0("create gatt database failed");
        return -1;
    }

    /* start service */
    for (index = 0; index < APP_BLE_RK_SERVER_ATTRIBUTE_MAX; index++)
    {
        if (app_ble_rk_server_cb.attr[index].attr_type == BSA_GATTC_ATTR_TYPE_SRVC)
        {
            app_ble_rk_server_start_service(app_ble_rk_server_cb.attr[index].service_id);
        }
    }

    GKI_delay(1000);

    /* Set the advertising parameters */
    if(ble_content->advDataType == BLE_ADVDATA_TYPE_USER)
        ret = app_ble_rk_server_set_user_adv_data(ble_content);
    else
        ret = app_ble_rk_server_set_advertisement_data((char *)app_ble_info.ble_device_name);
    if(ret < 0) {
        APP_ERROR0("Set the advertising parameters failed");
        return -1;
    }

    return 0;
}

/*******************************************************************************
 **
 ** Function         app_ble_rk_server_find_free_attr
 **
 ** Description      find free attr for APP BLE server application
 **
 ** Parameters
 **
 ** Returns          positive number(include 0) if successful, error code otherwise
 **
 *******************************************************************************/
static int app_ble_rk_server_find_free_attr(void)
{
    int index;

    for (index = 0; index < APP_BLE_RK_SERVER_ATTRIBUTE_MAX; index++)
    {
        //APP_DEBUG1("index: %d, uuid16: 0x%x", index, app_ble_rk_server_cb.attr[index].attr_UUID.uu.uuid16);
        if (!app_ble_rk_server_cb.attr[index].attr_UUID.uu.uuid16)
        {
            return index;
        }
    }
    return -1;
}

/*******************************************************************************
 **
 ** Function         app_ble_rk_server_find_free_attr
 **
 ** Description      find free attr for APP BLE server application
 **
 ** Parameters     attr_id
 **
 ** Returns          positive number(include 0) if successful, error code otherwise
 **
 *******************************************************************************/
static int app_ble_rk_server_find_attr_index_by_attr_id(UINT16 attr_id)
{
    int index;

    for (index = 0; index < APP_BLE_RK_SERVER_ATTRIBUTE_MAX; index++)
    {
        if (app_ble_rk_server_cb.attr[index].attr_id == attr_id)
        {
            return index;
        }
    }
    return -1;
}

/*******************************************************************************
 **
 ** Function         app_ble_rk_server_send_message
 **
 ** Description     Check if client has registered for notification/indication
 **                       and send message if appropriate
 **
 ** Parameters     None
 **
 ** Returns          None
 **
 *******************************************************************************/
void app_ble_rk_server_send_message(const char *uuid, char *data, UINT16 len)
{
    APP_DEBUG1("conn id : 0x%x",  app_ble_rk_server_cb.conn_id);
    /* If no client connectted or client has not registered for indication or notification, no action */
    if (app_ble_rk_server_cb.conn_id != BSA_BLE_INVALID_CONN) {
        APP_INFO0("Sending Notification");
        if (app_ble_rk_server_send_notification(uuid, data, len) == -1)
            APP_ERROR0("Sent Notification Fail");
    } else {
        APP_INFO0("No Action");
    }
    return;
}

/*******************************************************************************
 **
 ** Function        app_ble_rk_server_send_notification
 **
 ** Description     Send notification to client
 **
 ** Parameters      None
 **
 ** Returns         status: 0 if success / -1 otherwise
 **
 *******************************************************************************/
static int app_ble_rk_server_send_notification(const char *uuid, char *data, UINT16 len)
{
    int i;
    UINT8 attr_index_notify = APP_BLE_RK_SERVER_ATTRIBUTE_MAX;
    tBSA_STATUS status;
    tBSA_BLE_SE_SENDIND ble_sendind_param;

    APP_INFO0("app_ble_rk_server_send_notification");
    status = BSA_BleSeSendIndInit(&ble_sendind_param);
    if (status != BSA_SUCCESS) {
        APP_ERROR1("BSA_BleSeSendIndInit failed status = %d", status);
        return -1;
    }

    ble_sendind_param.conn_id = app_ble_rk_server_cb.conn_id;

    APP_DEBUG1("uuid: %s", uuid);
    for (i = 0; i < APP_BLE_RK_SERVER_ATTRIBUTE_MAX; i++) {
        APP_DEBUG1("uuid_string: %s", app_ble_rk_server_cb.attr[i].uuid_string);
        if (strcmp((char *)app_ble_rk_server_cb.attr[i].uuid_string, uuid) == 0) {
            attr_index_notify = i;
            break;
        }
    }

    APP_DEBUG1("attr_index_notify: %d", attr_index_notify);

    if (attr_index_notify >= APP_BLE_RK_SERVER_ATTRIBUTE_MAX) {
        APP_ERROR0("Wrong attr_index_notify");
        return -1;
    }

    ble_sendind_param.attr_id = app_ble_rk_server_cb.attr[attr_index_notify].attr_id;
    if (len > BSA_BLE_SE_WRITE_MAX) {
        APP_ERROR1("Wrong Notification Value Length %d", len);
        len = BSA_BLE_SE_WRITE_MAX;
    }

    ble_sendind_param.data_len = len;
    memcpy(ble_sendind_param.value, data, len);
    ble_sendind_param.need_confirm = FALSE; // Notification

    APP_DUMP("send notification", (UINT8*)ble_sendind_param.value, ble_sendind_param.data_len);

    status = BSA_BleSeSendInd(&ble_sendind_param);
    if (status != BSA_SUCCESS) {
        APP_ERROR1("BSA_BleSeSendInd failed status = %d", status);
        return -1;
    }

    return 0;
}

void app_ble_register_mtu_callback(RK_BT_MTU_CALLBACK cb)
{
    app_ble_info.mtu_cb = cb;
}

int app_ble_rk_server_open(RkBleContent *ble_content)
{
    int ret;

    APP_DEBUG0("app_ble_rk_server_open");

    memset(&app_ble_rk_server_cb, 0, sizeof(tAPP_BLE_RK_SERVER_CB));

    app_ble_rk_server_send_state(NULL, RK_BLE_STATE_IDLE);
    memset(app_ble_info.bd_addr, 0, BT_DEVICE_ADDRESS_LEN);

    /* Initialize BLE application */
    ret = app_ble_init();
    if (ret < 0) {
       APP_ERROR0("Couldn't Initialize BLE app");
       return -1;
    }

    /* Start BLE application */
    ret = app_ble_start();
    if (ret < 0) {
        APP_ERROR0("Couldn't Start BLE app");
        return -1;
    }

    app_ble_rk_server_init();

    app_ble_rk_server_set_device_name(ble_content->ble_name);

    ret = app_ble_rk_server_gatt_server_init(ble_content);
    if (ret < 0) {
        APP_ERROR0("Couldn't Init gatt server");
        return -1;
    }

    if(app_ble_info.local_privacy)
        app_dm_set_ble_local_privacy(TRUE);

    app_dm_set_ble_visibility(TRUE, TRUE);

    return 0;
}

void app_ble_rk_server_close()
{
    int index;

    app_ble_info.recv_data_cb = NULL;
    app_ble_info.mtu_cb = NULL;
    app_ble_info.request_data_cb = NULL;
    memset(&app_ble_adv_param, 0, sizeof(tBSA_DM_BLE_ADV_PARAM));

    /* stop service */
    for (index = 0; index < APP_BLE_RK_SERVER_ATTRIBUTE_MAX; index++) {
        if (app_ble_rk_server_cb.attr[index].attr_type == BSA_GATTC_ATTR_TYPE_SRVC)
            app_ble_rk_server_stop_service(app_ble_rk_server_cb.attr[index].service_id);
    }

    app_ble_rk_server_deregister();

    /* Exit BLE mode */
    app_ble_exit();

    app_ble_rk_server_send_state(NULL, RK_BLE_STATE_IDLE);
    app_ble_rk_server_deregister_cb();

    app_ble_info.local_privacy = TRUE;
    app_dm_set_ble_local_privacy(FALSE);
    app_dm_set_ble_visibility(FALSE, FALSE);
}

/*******************************************************************************
 **
 ** Function        app_ble_rk_server_send_data
 **
 ** Description     Send receive data
 **
 ** Parameters
 **
 ** Returns         None
 **
 *******************************************************************************/
static void app_ble_rk_server_recv_data(int attr_index, unsigned char *data, int len)
{
    if(app_ble_info.recv_data_cb) {
        APP_DEBUG1("uuid_string: %s", (char *)app_ble_rk_server_cb.attr[attr_index].uuid_string);
        app_ble_info.recv_data_cb((char *)app_ble_rk_server_cb.attr[attr_index].uuid_string, (char *)data, len);
    }
}

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
void app_ble_rk_server_recv_data_callback(RK_BLE_RECV_CALLBACK cb)
{
    app_ble_info.recv_data_cb = cb;
}

void app_ble_rk_server_request_data_callback(RK_BLE_REQUEST_DATA cb)
{
    app_ble_info.request_data_cb = cb;
}

void app_ble_rk_server_get_state(RK_BLE_STATE *p_state)
{
    if (!p_state)
        return;

    *p_state = app_ble_info.state;
}

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
int app_ble_rk_server_disconnect(void)
{
    tBSA_STATUS status;
    tBSA_BLE_SE_CLOSE ble_close_param;

    if(app_ble_rk_server_cb.conn_id == BSA_BLE_INVALID_CONN) {
        APP_DEBUG0("There is no valid ble connection");
        return- 1;
    }

    status = BSA_BleSeCloseInit(&ble_close_param);
    if (status != BSA_SUCCESS) {
        APP_ERROR1("BSA_BleSeCloseInit failed status = %d", status);
        return -1;
    }

    ble_close_param.conn_id = app_ble_rk_server_cb.conn_id;
    status = BSA_BleSeClose(&ble_close_param);
    if (status != BSA_SUCCESS) {
        APP_ERROR1("BSA_BleSeClose failed status = %d", status);
        return -1;
    }

    return 0;
}

void app_ble_rk_server_set_local_privacy(BOOLEAN local_privacy)
{
    app_ble_info.local_privacy = local_privacy;
}

int app_ble_rk_server_set_adv_interval(UINT16 adv_int_min, UINT16 adv_int_max)
{
    tBSA_DM_BLE_ADV_PARAM adv_param;

    memset(&adv_param, 0, sizeof(tBSA_DM_BLE_ADV_PARAM));

    if(adv_int_min < 32) {
        APP_ERROR1("the minimum is 32(20ms), adv_int_min = %d", adv_int_min);
        adv_int_min = 32;
    }

    if(adv_int_max < adv_int_min)
        adv_int_max = adv_int_min;

    adv_param.adv_int_min = adv_int_min;
    adv_param.adv_int_max = adv_int_max;
    APP_DEBUG1("adv_param.adv_int_min: %d, adv_param.adv_int_max: %d", adv_param.adv_int_min, adv_param.adv_int_max);

    app_ble_adv_param.adv_int_min = adv_param.adv_int_min;
    app_ble_adv_param.adv_int_max = adv_param.adv_int_max;

    return app_dm_set_ble_adv_param(&adv_param);
}
