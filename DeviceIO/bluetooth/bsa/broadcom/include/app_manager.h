/*****************************************************************************
**
**  Name:           app_manager.h
**
**  Description:    Bluetooth Manager application
**
**  Copyright (c) 2010-2014, Broadcom Corp., All Rights Reserved.
**  Broadcom Bluetooth Core. Proprietary and confidential.
**
*****************************************************************************/
#ifndef __APP_MANAGER_H__
#define __APP_MANAGER_H__

#include "DeviceIo/RkBtBase.h"

#define BT_DEVICE_ADDRESS_LEN 18

typedef struct
{
    tBSA_DM_DUAL_STACK_MODE dual_stack_mode; /* Dual Stack Mode */
} tAPP_MGR_CB;

typedef enum {
    BT_LINK_UP_EVT,     /* A device is physically connected (for info) */
    BT_LINK_DOWN_EVT,   /* A device is physically disconnected (for info)*/
    BT_WAIT_PAIR_EVT,   /* Simple Pairing confirm request */
    BT_PAIR_SUCCESS_EVT,/* pairing/authentication success*/
    BT_PAIR_FAILED_EVT, /* pairing/authentication failed*/
    BT_UNPAIR_SUCCESS_EVT,
} tBSA_MGR_EVT;

typedef void (* app_mgr_callback)(BD_ADDR bd_addr, char *name, tBSA_MGR_EVT evt);

extern tAPP_XML_CONFIG         app_xml_config;
extern BD_ADDR                 app_sec_db_addr;    /* BdAddr of peer device requesting SP */

/*******************************************************************************
 **
 ** Function         app_mgr_read_config
 **
 ** Description      This function is used to read the XML bluetooth configuration file
 **
 ** Returns          void
 **
 *******************************************************************************/
int app_mgr_read_config(void);

/*******************************************************************************
 **
 ** Function         app_mgr_write_config
 **
 ** Description      This function is used to write the XML bluetooth configuration file
 **
 ** Parameters
 **
 ** Returns          void
 **
 *******************************************************************************/
int app_mgr_write_config(void);

/*******************************************************************************
 **
 ** Function         app_mgr_read_remote_devices
 **
 ** Description      This function is used to read the XML bluetooth remote device file
 **
 ** Returns          void
 **
 *******************************************************************************/
int app_mgr_read_remote_devices(void);

/*******************************************************************************
 **
 ** Function         app_mgr_write_remote_devices
 **
 ** Description      This function is used to write the XML bluetooth remote device file
 **
 ** Returns          void
 **
 *******************************************************************************/
int app_mgr_write_remote_devices(void);

/*******************************************************************************
 **
 ** Function         app_mgr_set_bt_config
 **
 ** Description      This function is used to get the bluetooth configuration
 **
 ** Parameters
 **
 ** Returns          void
 **
 *******************************************************************************/
int app_mgr_set_bt_config(BOOLEAN enable);

/*******************************************************************************
 **
 ** Function         app_mgr_get_bt_config
 **
 ** Description      This function is used to get the bluetooth configuration
 **
 ** Parameters
 **
 ** Returns          char *name, int name_len, char* bd_addr, int addr_len
 **
 *******************************************************************************/
int app_mgr_get_bt_config(char *name, int name_len, char* bd_addr, int addr_len);

/*******************************************************************************
 **
 ** Function         app_mgr_sp_cfm_reply
 **
 ** Description      Function used to accept/refuse Simple Pairing
 **
 ** Parameters
 **
 ** Returns          void
 **
 *******************************************************************************/
int app_mgr_sp_cfm_reply(BOOLEAN accept, BD_ADDR bd_addr);

/*******************************************************************************
 **
 ** Function         app_mgr_sec_bond
 **
 ** Description      Bond a device
 **
 ** Parameters
 **
 ** Returns          void
 **
 *******************************************************************************/
int app_mgr_sec_bond(BD_ADDR bd_addr);

/*******************************************************************************
 **
 ** Function         app_mgr_sec_bond_cancel
 **
 ** Description      Cancel a bonding procedure
 **
 ** Parameters
 **
 ** Returns          void
 **
 *******************************************************************************/
int app_mgr_sec_bond_cancel(void);

/*******************************************************************************
 **
 ** Function         app_mgr_sec_unpair
 **
 ** Description      Unpair a device
 **
 ** Parameters
 **
 ** Returns          0 if success / -1 if error
 **
 *******************************************************************************/
int app_mgr_sec_unpair(BD_ADDR bd_addr);

/*******************************************************************************
 **
 ** Function         app_mgr_set_discoverable
 **
 ** Description      Set the device discoverable for a specific time
 **
 ** Parameters
 **
 ** Returns          void
 **
 *******************************************************************************/
void app_mgr_set_discoverable(void);

/*******************************************************************************
 **
 ** Function         app_mgr_set_non_discoverable
 **
 ** Description      Set the device non discoverable
 **
 ** Parameters
 **
 ** Returns          void
 **
 *******************************************************************************/
void app_mgr_set_non_discoverable(void);

/*******************************************************************************
 **
 ** Function         app_mgr_set_connectable
 **
 ** Description      Set the device connectable for a specific time
 **
 ** Parameters
 **
 ** Returns          void
 **
 *******************************************************************************/
void app_mgr_set_connectable(void);

/*******************************************************************************
 **
 ** Function         app_mgr_set_non_connectable
 **
 ** Description      Set the device non connectable
 **
 ** Parameters
 **
 ** Returns          void
 **
 *******************************************************************************/
void app_mgr_set_non_connectable(void);

/*******************************************************************************
 **
 ** Function         app_mgr_di_discovery
 **
 ** Description      Perform a device Id discovery
 **
 ** Parameters
 **
 ** Returns          void
 **
 *******************************************************************************/
int app_mgr_di_discovery(void);

/*******************************************************************************
**
** Function         app_mgr_set_local_di
**
** Description      Set local device Id
**
** Parameters
**
** Returns          void
**
*******************************************************************************/
int app_mgr_set_local_di(void);

/*******************************************************************************
 **
 ** Function         app_mgr_get_local_di
 **
 ** Description      This function is used to read local primary DI record
 **
 ** Parameters
 **
 ** Returns          int
 **
 *******************************************************************************/
int app_mgr_get_local_di(void);

/*******************************************************************************
 **
 ** Function        app_mgr_change_dual_stack_mode
 **
 ** Description     Toggle Dual Stack Mode (BSA <=> MM/Kernel)
 **
 ** Parameters      None
 **
 ** Returns         Status
 **
 *******************************************************************************/
int app_mgr_change_dual_stack_mode(void);

/*******************************************************************************
 **
 ** Function         app_mgr_discovery_test
 **
 ** Description      This function performs a specific discovery
 **
 ** Parameters       None
 **
 ** Returns          int
 **
 *******************************************************************************/
int app_mgr_discovery_test(void);

/*******************************************************************************
 **
 ** Function         app_mgr_read_version
 **
 ** Description      This function is used to read BSA and FW version
 **
 ** Parameters
 **
 ** Returns          int
 **
 *******************************************************************************/
int app_mgr_read_version(void);

/*******************************************************************************
 **
 ** Function        app_mgr_get_dual_stack_mode_desc
 **
 ** Description     Get Dual Stack Mode description
 **
 ** Parameters      None
 **
 ** Returns         Description string
 **
 *******************************************************************************/
const char *app_mgr_get_dual_stack_mode_desc(void);

/*******************************************************************************
 **
 ** Function         app_mgr_config
 **
 ** Description      Configure the BSA server
 **
 ** Parameters       cmd send callback
 **
 ** Returns          Status of the operation
 **
 *******************************************************************************/
int app_mgr_config(const char *bt_name, const char *bt_addr, app_mgr_callback cb);

/*******************************************************************************
 **
 ** Function         app_mgr_send_pincode
 **
 ** Description      Sends simple pairing passkey to server
 **
 ** Parameters       passkey
 **
 ** Returns          void
 **
 *******************************************************************************/
void app_mgr_send_pincode(tBSA_SEC_PIN_CODE_REPLY pin_code_reply);

/*******************************************************************************
 **
 ** Function         app_mgr_send_passkey
 **
 ** Description      Sends simple pairing passkey to server
 **
 ** Parameters       passkey
 **
 ** Returns          void
 **
 *******************************************************************************/
void app_mgr_send_passkey(UINT32 passkey);

/*******************************************************************************
 **
 ** Function         app_mgr_sec_set_sp_cap
 **
 ** Description      Set simple pairing capabilities
 **
 ** Parameters       sp_cap: simple pairing capabilities
 **
 ** Returns          status
 **
 *******************************************************************************/
int app_mgr_sec_set_sp_cap(tBSA_SEC_IO_CAP sp_cap);

/*******************************************************************************
 **
 ** Function         app_mgr_read_oob
 **
 ** Description      This function is used to read local OOB data from local controller
 **
 ** Parameters
 **
 ** Returns          void
 **
 *******************************************************************************/
void app_mgr_read_oob_data();

/*******************************************************************************
 **
 ** Function         app_mgr_set_remote_oob
 **
 ** Description      This function is used to set OOB data for peer device
 **                  During pairing stack uses this information to pair
 **
 ** Parameters
 **
 ** Returns          void
 **
 *******************************************************************************/
void app_mgr_set_remote_oob();

/*******************************************************************************
 **
 ** Function         app_set_sleepmode_param
 **
 ** Description      This function is used to set sleep wake
 **                  bsa_server must set parameters "-diag=0 -lpm"
 **
 ** Parameters
 **
 ** Returns          void
 **
 *******************************************************************************/
int app_mgr_set_sleep_mode_param(void);

/*******************************************************************************
 **
 ** Function         app_mgr_set_link_policy
 **
 ** Description      Set the device link policy
 **                  This function sets/clears the link policy mask to the given
 **                  bd_addr.
 **                  If clearing the sniff or park mode mask, the link is put
 **                  in active mode.
 **
 ** Parameters
 **
 ** Returns          void
 **
 *******************************************************************************/
void app_mgr_set_link_policy(BD_ADDR bd_addr, tBSA_DM_LP_MASK policy_mask, BOOLEAN set);

/*******************************************************************************
 **
 ** Function         app_mgr_send_hci_cmd
 **
 ** Description      This function is used to send a HCI Command
 **
 ** Parameters
 **
 ** Returns          int
 **
 *******************************************************************************/
extern int app_mgr_send_hci_cmd(char *cmd);

void app_mgr_register_disc_cb(RK_BT_DISCOVERY_CALLBACK cb);
void app_mgr_deregister_disc_cb();
void app_mgr_register_dev_found_cb(RK_BT_DEV_FOUND_CALLBACK cb);
void app_mgr_deregister_dev_found_cb();
int app_mgt_set_cod(int cod);
int app_mgr_get_latest_device(void);
int app_manager_init(const char *bt_name, const char *bt_addr, app_mgr_callback cb);
int app_manager_deinit(void);
int app_mgr_is_reconnect(void);
int app_mgr_set_auto_reconnect(int enable);
UINT8 app_mgr_get_dev_platform(BD_ADDR bd_addr);
int app_mgr_bd2str(BD_ADDR bd_addr, char *address, int addr_len);
int app_mgr_str2bd(char *address, BD_ADDR bd_addr);
int app_mgt_set_device_name(char *name);
int app_mgr_get_scaned_devices(RkBtScanedDevice **dev_list,int *count, bool paired);
int app_mgr_free_scaned_devices(RkBtScanedDevice *dev_list);
int app_mgr_xml_display_devices(void);
void app_mgr_register_name_callback(RK_BT_NAME_CHANGE_CALLBACK cb);
void app_mgr_deregister_name_callback();
int app_mgr_name_change_send(BD_ADDR bd_addr, BD_NAME bd_name);
RK_BT_PLAYROLE_TYPE app_mgr_get_playrole_by_addr(BD_ADDR bd_addr);
int app_mgr_save_visibility(BOOLEAN discoverable, BOOLEAN connectable);

#endif /* __APP_MANAGER_H__ */
