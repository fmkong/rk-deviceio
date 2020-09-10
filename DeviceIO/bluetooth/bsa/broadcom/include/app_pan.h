/*****************************************************************************
 **
 **  Name:           app_pan.h
 **
 **  Description:    Bluetooth PAN application
 **
 **  Copyright (c) 2011-2014, Broadcom Corp., All Rights Reserved.
 **  Broadcom Bluetooth Core. Proprietary and confidential.
 **
 *****************************************************************************/

#ifndef APP_PAN_H
#define APP_PAN_H

#include "bluetooth_bsa.h"

/******************************************************************************
 **
 ** Function         app_pan_init
 **
 ** Description      Init PAN application
 **
 ** Returns          status
 **
 ******************************************************************************/
extern int app_pan_init(void);

/******************************************************************************
 **
 ** Function         app_pan_start
 **
 ** Description      Start PAN application
 **
 ** Returns          status
 **
 ******************************************************************************/
extern int app_pan_start(void);

/******************************************************************************
 **
 ** Function         app_pan_stop
 **
 ** Description      Stop PAN application
 **
 ** Returns          status
 **
 ******************************************************************************/
extern int app_pan_stop(void);

/******************************************************************************
 **
 ** Function         app_pan_set_role
 **
 ** Description      Example of function to set role
 **
 ** Returns          status
 **
 ******************************************************************************/
extern int app_pan_set_role(tBSA_PAN_ROLE role);

/******************************************************************************
 **
 ** Function         app_pan_open
 **
 ** Description      Example of function to open a PAN connection
 **
 ** Returns          int
 **
 ******************************************************************************/
extern int app_pan_open(BD_ADDR bd_addr);

/******************************************************************************
 **
 ** Function         app_pan_close
 **
 ** Description      Example of function to close a PAN connection
 **
 ** Returns          int
 **
 ******************************************************************************/
extern int app_pan_close(BD_ADDR bd_addr);

/******************************************************************************
 **
 ** Function         app_pan_set_pfilter
 **
 ** Description      Example of function to set a Network Protocol Type filter
 **
 ** Returns          int
 **
 ******************************************************************************/
extern int app_pan_set_pfilter(void);

/******************************************************************************
 **
 ** Function         app_pan_set_mfilter
 **
 ** Description      Example of function to set Multicast Address Type filter
 **
 ** Returns          int
 **
 ******************************************************************************/
extern int app_pan_set_mfilter(void);

/******************************************************************************
 **
 ** Function         app_pan_set_mfilter
 **
 ** Description      Example of function to set Multicast Address Type filter
 **
 ** Returns          int
 **
 ******************************************************************************/
extern void app_pan_con_display(void);

void app_pan_register_event_cb(RK_BT_PAN_EVENT_CALLBACK cb);

#endif /* APP_PAN_H */
