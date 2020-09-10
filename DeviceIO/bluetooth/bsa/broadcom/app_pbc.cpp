/*****************************************************************************
**
**  Name:           app_pbc.c
**
**  Description:    Bluetooth Phonebook client sample application
**
**  Copyright (c) 2013, Broadcom Corp., All Rights Reserved.
**  Broadcom Bluetooth Core. Proprietary and confidential.
**
*****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include "gki.h"
#include "uipc.h"
#include "bsa_api.h"
#include "app_xml_param.h"
#include "app_xml_utils.h"
#include "app_disc.h"
#include "app_mgt.h"
#include "app_utils.h"
#include "app_manager.h"
#include "app_pbc.h"

#define MAX_PATH_LENGTH 512
#define DEFAULT_SEC_MASK (BSA_SEC_AUTHENTICATION | BSA_SEC_ENCRYPTION)

#define APP_PBC_FEATURES    ( BSA_PBC_FEA_DOWNLOADING | BSA_PBC_FEA_BROWSING | \
                    BSA_PBC_FEA_DATABASE_ID | BSA_PBC_FEA_FOLDER_VER_COUNTER | \
                    BSA_PBC_FEA_VCARD_SELECTING | BSA_PBC_FEA_ENH_MISSED_CALLS | \
                    BSA_PBC_FEA_UCI_VCARD_FIELD | BSA_PBC_FEA_UID_VCARD_FIELD | \
                    BSA_PBC_FEA_CONTACT_REF | BSA_PBC_FEA_DEF_CONTACT_IMAGE_FORMAT )

#define APP_PBC_SETPATH_ROOT    "/"
#define APP_PBC_SETPATH_UP      ".."

/*
 * Types
 */
typedef struct
{
    BOOLEAN             is_open;
    tBSA_STATUS         last_open_status;
    tBSA_STATUS         last_start_status;
    volatile BOOLEAN    open_pending; /* Indicate that there is an open pending */
    BD_ADDR             bda_connected;
    BOOLEAN             is_channel_open;
    tUIPC_CH_ID         uipc_pbc_channel;
    BOOLEAN             remove;
    char                save_path[MAX_PATH_LENGTH];
} tAPP_PBC_CB;

/*
 * Global Variables
 */
tAPP_PBC_CB app_pbc_cb;

static RK_BT_OBEX_STATE_CALLBACK app_pbc_state_cb = NULL;

static void app_pbc_send_state(RK_BT_OBEX_STATE state)
{
    char address[20];

    if(!app_pbc_state_cb)
        return;

    if(bdcmp(app_pbc_cb.bda_connected, bd_addr_null))
        app_mgr_bd2str(app_pbc_cb.bda_connected, address, 20);
    else
        memset(address, 0, 20);

    app_pbc_state_cb(address, state);
}

static void app_pbc_uipc_close()
{
    if(app_pbc_cb.is_channel_open) {
        /* close the UIPC channel receiving data */
        UIPC_Close(app_pbc_cb.uipc_pbc_channel);
        app_pbc_cb.uipc_pbc_channel = UIPC_CH_ID_BAD;
        app_pbc_cb.is_channel_open = FALSE;
    }
}

/*******************************************************************************
 **
 ** Function        app_pbc_uipc_cback
 **
 ** Description     uipc pbc call back function.
 **
 ** Parameters      pointer on a buffer containing Phonebook data with a BT_HDR header
 **
 ** Returns          void
 **
 *******************************************************************************/
static void app_pbc_uipc_cback(BT_HDR *p_msg)
{
    UINT8 *p_buffer = NULL;
    int dummy = 0;
    int fd = -1;

    APP_DEBUG1("response received... p_msg = %x", p_msg);
    if (p_msg == NULL)
        return;

    /* Make sure we are only handling UIPC_RX_DATA_EVT and other UIPC events. Ignore UIPC_OPEN_EVT and UIPC_CLOSE_EVT */
    if(p_msg->event != UIPC_RX_DATA_EVT) {
        APP_DEBUG1("response received...event = %d", p_msg->event);
        GKI_freebuf(p_msg);
        return;
    }

    /* APP_DEBUG1("response received...msg len = %d, p_buffer = %s", p_msg->len, p_buffer); */
    APP_DEBUG1("response received...event = %d, msg len = %d", p_msg->event, p_msg->len);

    /* Check msg len */
    if(!(p_msg->len)) {
        APP_DEBUG0("response received. DATA Len = 0");
        GKI_freebuf(p_msg);
        return;
    }

    /* Get to the data buffer */
    p_buffer = ((UINT8 *)(p_msg + 1)) + p_msg->offset;

    /* Write received buffer to file */
     APP_DEBUG1("save file path = %s", app_pbc_cb.save_path);

    /* Delete temporary file */
    if(app_pbc_cb.remove) {
        unlink(app_pbc_cb.save_path);
        app_pbc_cb.remove = FALSE;

        app_pbc_send_state(RK_BT_OBEX_TRANSFER_ACTIVE);
    }

    /* Save incoming data in temporary file */
    fd = open(app_pbc_cb.save_path, O_WRONLY | O_CREAT | O_APPEND, 0666);
    APP_DEBUG1("fd = %d", fd);
    if(fd != -1) {
        dummy = write(fd, p_buffer, p_msg->len);
        APP_DEBUG1("dummy = %d err = %d", dummy, errno);
        close(fd);
    }

    /* APP_DUMP("PBC Data", p_buffer, p_msg->len); */
    GKI_freebuf(p_msg);
}

/*******************************************************************************
**
** Function         app_pbc_handle_list_evt
**
** Description      Example of list event callback function
**
** Parameters
**
** Returns          void
**
*******************************************************************************/
static void app_pbc_handle_list_evt(tBSA_PBC_LIST_MSG *p_data)
{
    UINT8 * p_buf = p_data->data;
    UINT16 Index;

    if(p_data->is_xml) {
        for(Index = 0; Index < p_data->len; Index++) {
            printf("%c", p_buf[Index]);
        }
        printf("\n");
    } else {
        for (Index = 0;Index < p_data->num_entry * 2; Index++) {
            APP_DEBUG1("%s", p_buf);
            p_buf += 1+strlen((char *) p_buf);
        }
    }

    APP_DEBUG1("BSA_PBC_LIST_EVT num entry %d,is final %d, xml %d, len %d",
        p_data->num_entry, p_data->final, p_data->is_xml, p_data->len);
}

/*******************************************************************************
**
** Function         app_pbc_auth
**
** Description      Respond to an Authorization Request
**
** Parameters
**
** Returns          void
**
*******************************************************************************/
static void app_pbc_auth(tBSA_PBC_MSG *p_data)
{
    tBSA_PBC_AUTHRSP auth_cmd;

    BSA_PbcAuthRspInit(&auth_cmd);

    /* Provide userid and password for authorization */
    strcpy(auth_cmd.password, "0000");
    strcpy(auth_cmd.userid, "guest");

    BSA_PbcAuthRsp(&auth_cmd);

    /*
    OR Call BSA_PbcClose() to close the connection
    */
}

/*******************************************************************************
**
** Function         app_pbc_get_evt
**
** Description      Response events received in response to GET command
**
** Parameters
**
** Returns          void
**
*******************************************************************************/
static void app_pbc_get_evt(tBSA_PBC_MSG *p_data)
{
    APP_DEBUG1("GET EVT response received... p_data = %x", p_data);

    if(!p_data)
        return;

    switch(p_data->get.type)
    {
    case BSA_PBC_GET_PARAM_STATUS:          /* status only*/
        APP_DEBUG0("GET EVT BSA_PBC_GET_PARAM_STATUS");
        break;

    case BSA_PBC_GET_PARAM_LIST:            /* List message */
        APP_DEBUG0("GET EVT BSA_PBC_GET_PARAM_LIST");
        app_pbc_handle_list_evt(&(p_data->get.param.list));
        break;

    case BSA_PBC_GET_PARAM_PROGRESS:        /* Progress Message*/
        APP_DEBUG0("GET EVT BSA_PBC_GET_PARAM_PROGRESS");
        break;

    case BSA_PBC_GET_PARAM_PHONEBOOK:       /* Phonebook paramm - Indicates Phonebook transfer complete */
        APP_DEBUG0("GET EVT BSA_PBC_GET_PARAM_PHONEBOOK");
        break;

    case BSA_PBC_GET_PARAM_FILE_TRANSFER_STATUS:
        APP_DEBUG0("GET EVT BSA_PBC_GET_PARAM_FILE_TRANSFER_STATUS");
        sleep(1);
        app_pbc_send_state(RK_BT_OBEX_TRANSFER_COMPLETE);
        break;

    default:
        break;
    }
}

/*******************************************************************************
**
** Function         app_pbc_set_evt
**
** Description      Response events received in response to SET command
**
** Parameters
**
** Returns          void
**
*******************************************************************************/
static void app_pbc_set_evt(tBSA_PBC_MSG *p_data)
{
    APPL_TRACE_DEBUG0("app_pbc_set_evt SET EVT response received...");
}

/*******************************************************************************
**
** Function         app_pbc_abort_evt
**
** Description      Response events received in response to ABORT command
**
** Parameters
**
** Returns          void
**
*******************************************************************************/
static void app_pbc_abort_evt(tBSA_PBC_MSG *p_data)
{
    APPL_TRACE_DEBUG0("app_pbc_abort_evt received...");
}


/*******************************************************************************
**
** Function         app_pbc_open_evt
**
** Description      Response events received in response to OPEN command
**
** Parameters
**
** Returns          void
**
*******************************************************************************/
static void app_pbc_open_evt(tBSA_PBC_MSG *p_data)
{
    APPL_TRACE_DEBUG0("app_pbc_open_evt received...");

    app_pbc_cb.last_open_status = p_data->open.status;

    if (p_data->open.status == BSA_SUCCESS) {
        app_pbc_cb.is_open = TRUE;
        app_pbc_send_state(RK_BT_OBEX_CONNECTED);
    } else {
        app_pbc_cb.is_open = FALSE;
        app_pbc_send_state(RK_BT_OBEX_CONNECT_FAILED);
    }

    app_pbc_cb.open_pending = FALSE;

    if(p_data->open.status == BSA_SUCCESS && !app_pbc_cb.is_channel_open) {
        /* Once connected to PBAP Server, UIPC channel is returned. */
        /* Open the channel and register a callback to get phone book data. */
        /* Save UIPC channel */
        app_pbc_cb.uipc_pbc_channel = p_data->open.uipc_channel;
        app_pbc_cb.is_channel_open = TRUE;

        /* open the UIPC channel to receive data */
        if (UIPC_Open(p_data->open.uipc_channel, app_pbc_uipc_cback) == FALSE) {
            app_pbc_cb.is_channel_open = FALSE;
            APP_ERROR0("Unable to open UIPC channel");
            return;
        }
    }
}

/*******************************************************************************
**
** Function         app_pbc_close_evt
**
** Description      Response events received in response to CLOSE command
**
** Parameters
**
** Returns          void
**
*******************************************************************************/
static void app_pbc_close_evt(tBSA_PBC_MSG *p_data)
{
    APPL_TRACE_DEBUG1("app_pbc_close_evt received... Reason = %d", p_data->close.status);

    app_pbc_uipc_close();
    app_pbc_send_state(RK_BT_OBEX_DISCONNECTED);

    app_pbc_cb.is_open = FALSE;
    app_pbc_cb.open_pending = FALSE;
    bdcpy(app_pbc_cb.bda_connected, bd_addr_null);
}

/*******************************************************************************
**
** Function         app_pbc_cback
**
** Description      Example of PBC callback function to handle events related to PBC
**
** Parameters
**
** Returns          void
**
*******************************************************************************/
static void app_pbc_cback(tBSA_PBC_EVT event, tBSA_PBC_MSG *p_data)
{
    switch (event)
    {
    case BSA_PBC_OPEN_EVT: /* Connection Open (for info) */
        APP_INFO1("BSA_PBC_OPEN_EVT Status %d", p_data->open.status);
        app_pbc_open_evt(p_data);
        break;

    case BSA_PBC_CLOSE_EVT: /* Connection Closed (for info)*/
        APP_INFO0("BSA_PBC_CLOSE_EVT");
        app_pbc_close_evt(p_data);
        break;

    case BSA_PBC_DISABLE_EVT: /* PBAP Client module disabled*/
        APP_INFO0("BSA_PBC_DISABLE_EVT\n");
        break;

    case BSA_PBC_AUTH_EVT: /* Authorization */
        APP_INFO0("BSA_PBC_AUTH_EVT");
        app_pbc_auth(p_data);
        break;

    case BSA_PBC_SET_EVT:
        APP_INFO1("BSA_PBC_SET_EVT status %d", p_data->set.status);
        app_pbc_set_evt(p_data);
        break;

    case BSA_PBC_GET_EVT:
        APP_INFO1("BSA_PBC_GET_EVT status %d", p_data->get.status);
        app_pbc_get_evt(p_data);
        break;

    case BSA_PBC_ABORT_EVT:
        APP_INFO0("BSA_PBC_ABORT_EVT");
        app_pbc_abort_evt(p_data);
        break;

    default:
        APP_INFO1("unknown event:%d", event);
        break;
    }
}

/*******************************************************************************
**
** Function         app_pbc_enable
**
** Description      Example of function to enable PBC functionality
**
** Parameters
**
** Returns          void
**
*******************************************************************************/
int app_pbc_enable()
{
    tBSA_STATUS status = BSA_SUCCESS;
    tBSA_PBC_ENABLE enable_param;

    /* Initialize the control structure */
    memset(&app_pbc_cb, 0, sizeof(app_pbc_cb));
    app_pbc_cb.uipc_pbc_channel = UIPC_CH_ID_BAD;

    status = BSA_PbcEnableInit(&enable_param);

    enable_param.p_cback = app_pbc_cback;
    enable_param.local_features = APP_PBC_FEATURES;

    status = BSA_PbcEnable(&enable_param);
    if (status != BSA_SUCCESS) {
        APP_ERROR1("Unable to enable PBC status:%d", status);
        return -1;
    }

    return 0;
}

/*******************************************************************************
**
** Function         app_pbc_disable
**
** Description      Example of function to stop PBC functionality
**
** Parameters
**
** Returns          void
**
*******************************************************************************/
int app_pbc_disable()
{
    tBSA_STATUS status;
    tBSA_PBC_DISABLE disable_param;

    app_pbc_uipc_close();

    status = BSA_PbcDisableInit(&disable_param);
    status = BSA_PbcDisable(&disable_param);
    if (status != BSA_SUCCESS) {
        APP_ERROR1("app_stop_pbc: Error: %d", status);
        return -1;
    }

    return 0;
}

/*******************************************************************************
**
** Function         app_pbc_open
**
** Description      Example of function to open up a connection to peer
**
** Parameters
**
** Returns          void
**
*******************************************************************************/
int app_pbc_open(BD_ADDR bda)
{
    tBSA_STATUS status = BSA_SUCCESS;
    tBSA_PBC_OPEN param;

    if(app_pbc_cb.open_pending || app_pbc_cb.is_open) {
        APP_DEBUG1("pbc connecting or connected: open_pending = %d, is_open: %d",
            app_pbc_cb.open_pending, app_pbc_cb.is_open);
        return -1;
    }

    app_pbc_cb.open_pending = TRUE;
    bdcpy(app_pbc_cb.bda_connected, bda);

    BSA_PbcOpenInit(&param);

    bdcpy(param.bd_addr, bda);
    param.sec_mask = DEFAULT_SEC_MASK;
    status = BSA_PbcOpen(&param);

    if (status != BSA_SUCCESS) {
        APP_ERROR1("BSA_PbcOpen Error:%d", status);
        bdcpy(app_pbc_cb.bda_connected, bd_addr_null);
        app_pbc_cb.open_pending = FALSE;
        return -1;
    }

    return 0;
}

/*******************************************************************************
**
** Function         app_pbc_close
**
** Description      Example of function to disconnect current connection
**
** Parameters
**
** Returns          void
**
*******************************************************************************/
int app_pbc_close(BD_ADDR bd_addr)
{
    tBSA_STATUS status;

    tBSA_PBC_CLOSE close_param;

    if(bdcmp(app_pbc_cb.bda_connected, bd_addr)) {
        APP_ERROR1("not find connected device(%02X:%02X:%02X:%02X:%02X:%02X)",
            bd_addr[0], bd_addr[1], bd_addr[2],
            bd_addr[3], bd_addr[4], bd_addr[5]);
        return -1;
    }

    app_pbc_uipc_close();

    status = BSA_PbcCloseInit(&close_param);
    status = BSA_PbcClose(&close_param);
    if (status != BSA_SUCCESS) {
        APP_ERROR1("BSA_PbcClose Error: %d", status);
        return -1;
    }

    return 0;
}

/*******************************************************************************
**
** Function         app_pbc_abort
**
** Description      Example of function to abort current actions
**
** Parameters
**
** Returns          void
**
*******************************************************************************/
tBSA_STATUS app_pbc_abort(void)
{
    tBSA_STATUS status;

    tBSA_PBC_ABORT abort_param;

    printf("app_pbc_abort\n");

    status = BSA_PbcAbortInit(&abort_param);

    status = BSA_PbcAbort(&abort_param);

    if (status != BSA_SUCCESS)
    {
        fprintf(stderr, "app_pbc_abort: Error:%d\n", status);
    }
    return status;
}

/*******************************************************************************
**
** Function         app_pbc_cancel
**
** Description      cancel connections
**
** Returns          0 if success -1 if failure
*******************************************************************************/
tBSA_STATUS app_pbc_cancel(void)
{
    tBSA_PBC_CANCEL param;
    tBSA_STATUS status;

    /* Prepare parameters */
    BSA_PbcCancelInit(&param);

    status = BSA_PbcCancel(&param);
    if (status != BSA_SUCCESS)
    {
        APP_ERROR1("app_pbc_cancel - failed with status : %d", status);
        return -1;
    }

    return 0;
}

/*******************************************************************************
**
** Function         app_pbc_get_vcard
**
** Description      Example of function to perform a get VCard operation
**
** Parameters
**
** Returns          void
**
*******************************************************************************/
tBSA_STATUS app_pbc_get_vcard(const char* file_name)
{
    tBSA_STATUS status = 0;
    tBSA_PBC_GET pbcGet;

    tBSA_PBC_FILTER_MASK filter = BSA_PBC_FILTER_ALL;
    tBSA_PBC_FORMAT format =  BSA_PBC_FORMAT_CARD_21;

    UINT16   len = strlen(file_name) + 1;
    char    *p_path;
    if(  (p_path = (char *) GKI_getbuf(MAX_PATH_LENGTH + 1)) == NULL)
    {
        return FALSE;
    }

    memset(p_path, 0, MAX_PATH_LENGTH + 1);
    sprintf(p_path, "/%s", file_name); /* Choosing the root folder */
    APPL_TRACE_DEBUG3("GET vCard Entry p_path = %s,  file name = %s, len = %d",
        p_path, file_name, len);

    app_pbc_cb.remove = TRUE;

    status = BSA_PbcGetInit(&pbcGet);

    pbcGet.type = BSA_PBC_GET_CARD;
    strncpy(pbcGet.param.get_card.remote_name, file_name, BSA_PBC_FILENAME_MAX - 1);
    pbcGet.param.get_card.filter = filter;
    pbcGet.param.get_card.format = format;

    status = BSA_PbcGet(&pbcGet);

    if (status != BSA_SUCCESS)
    {
        fprintf(stderr, "app_pbc_get_vcard: Error:%d\n", status);
    }

    GKI_freebuf(p_path);

    return status;
}


/*******************************************************************************
**
** Function         app_pbc_get_list_vcards
**
** Description      Example of function to perform a GET - List VCards operation
**
** Parameters
**
** Returns          void
**
*******************************************************************************/
tBSA_STATUS app_pbc_get_list_vcards(const char* string_dir, tBSA_PBC_ORDER  order,
    tBSA_PBC_ATTR  attr, const char* searchvalue, UINT16 max_list_count,
    BOOLEAN is_reset_missed_calls, tBSA_PBC_FILTER_MASK selector, tBSA_PBC_OP selector_op)
{
    UINT16 list_start_offset = 0;

    tBSA_STATUS status = 0;
    tBSA_PBC_GET pbcGet;
    int is_xml = 1;

    APP_DEBUG0("app_pbc_get_list_vcards");

    status = BSA_PbcGetInit(&pbcGet);

    pbcGet.type = BSA_PBC_GET_LIST_CARDS;
    pbcGet.is_xml = is_xml;

    strncpy( pbcGet.param.list_cards.dir, string_dir, BSA_PBC_ROOT_PATH_LEN_MAX - 1);
    pbcGet.param.list_cards.order = order;
    strncpy(pbcGet.param.list_cards.value, searchvalue, BSA_PBC_MAX_VALUE_LEN - 1);
    pbcGet.param.list_cards.attribute =  attr;
    pbcGet.param.list_cards.max_list_count = max_list_count;
    pbcGet.param.list_cards.list_start_offset = list_start_offset;
    pbcGet.param.list_cards.is_reset_miss_calls = is_reset_missed_calls;
    pbcGet.param.list_cards.selector = selector;
    pbcGet.param.list_cards.selector_op = selector_op;

    status = BSA_PbcGet(&pbcGet);

    if (status != BSA_SUCCESS)
    {
        fprintf(stderr, "app_pbc_get_list_vcards: Error:%d\n", status);
    }

    return status;
}

/*******************************************************************************
**
** Function         app_pbc_get_phonebook
**
** Description      Example of function to perform a get phonebook operation
**
** Parameters
**
** Returns          void
**
*******************************************************************************/
tBSA_STATUS app_pbc_get_phonebook(const char* file_name,
    BOOLEAN is_reset_missed_calls, tBSA_PBC_FILTER_MASK selector, tBSA_PBC_OP selector_op)
{
    tBSA_STATUS status = 0;
    tBSA_PBC_GET pbcGet;

    tBSA_PBC_FILTER_MASK filter = BSA_PBC_FILTER_ALL;
    tBSA_PBC_FORMAT format =  BSA_PBC_FORMAT_CARD_30;

    UINT16 max_list_count = 65535;
    UINT16 list_start_offset = 0;

    UINT16 len = strlen(file_name) + 1;
    char *p_path;
    char *p = file_name;
    char *p_name = NULL;

    do {
        /* double check the specified phonebook is correct */
        if(strncmp(p, "SIM1/", 5) == 0)
            p += 5;
        if(strncmp(p, "telecom/", 8) != 0)
            break;
        p += 8;
        p_name = p;
        if(strcmp(p, "pb.vcf") && strcmp(p, "fav.vcf") && strcmp(p, "spd.vcf")) {
            p++;
            if(strcmp(p, "ch.vcf") != 0) /* this covers rest of the ich, och,mch,cch */
                break;
        }

        if ((p_path = (char *) GKI_getbuf(MAX_PATH_LENGTH + 1)) == NULL) {
            break;
        }

        memset(p_path, 0, MAX_PATH_LENGTH + 1);
        sprintf(p_path, "/%s", p_name); /* Choosing the root folder */
        APPL_TRACE_DEBUG1("GET PhoneBook p_path = %s", p_path);
        APPL_TRACE_DEBUG2("file name = %s, len = %d", file_name, len);

        app_pbc_cb.remove = TRUE;

        status = BSA_PbcGetInit(&pbcGet);
        pbcGet.type = BSA_PBC_GET_PHONEBOOK;
        strncpy(pbcGet.param.get_phonebook.remote_name, file_name, BSA_PBC_MAX_REALM_LEN - 1);
        pbcGet.param.get_phonebook.filter = filter;
        pbcGet.param.get_phonebook.format = format;
        pbcGet.param.get_phonebook.max_list_count = max_list_count;
        pbcGet.param.get_phonebook.list_start_offset = list_start_offset;
        pbcGet.param.get_phonebook.is_reset_miss_calls = is_reset_missed_calls;
        pbcGet.param.get_phonebook.selector = selector;
        pbcGet.param.get_phonebook.selector_op = selector_op;

        status = BSA_PbcGet(&pbcGet);
        if (status != BSA_SUCCESS)
            APP_ERROR1("BSA_PbcGet Error:%d", status);

        GKI_freebuf(p_path);
    } while(0);

    return status;
}

/*******************************************************************************
**
** Function         app_pbc_set_chdir
**
** Description      Example of function to perform a set current folder operation
**
** Parameters
**
** Returns          void
**
*******************************************************************************/
tBSA_STATUS app_pbc_set_chdir(const char *string_dir)
{
   tBSA_STATUS status = BSA_SUCCESS;
   tBSA_PBC_SET set_param;

    APPL_TRACE_EVENT1("app_pbc_set_chdir: path= %s", string_dir);
    status = BSA_PbcSetInit(&set_param);
    if (strcmp(string_dir, APP_PBC_SETPATH_ROOT) == 0)
    {
        set_param.param.ch_dir.flag = BSA_PBC_FLAG_NONE;
    }
    else if (strcmp(string_dir, APP_PBC_SETPATH_UP) == 0)
    {
        set_param.param.ch_dir.flag = BSA_PBC_FLAG_BACKUP;
    }
    else
    {
        strncpy(set_param.param.ch_dir.dir, string_dir, BSA_PBC_ROOT_PATH_LEN_MAX - 1);
        set_param.param.ch_dir.flag = BSA_PBC_FLAG_NONE;
    }
    status = BSA_PbcSet(&set_param);

    if (status != BSA_SUCCESS)
    {
        fprintf(stderr, "app_pbc_set_chdir: Error:%d\n", status);
    }
    return status;
}

/*******************************************************************************
**
** Function         pbc_is_open_pending
**
** Description      Check if pbc Open is pending
**
** Parameters
**
** Returns          TRUE if open is pending, FALSE otherwise
**
*******************************************************************************/
BOOLEAN pbc_is_open_pending()
{
    return app_pbc_cb.open_pending;
}

/*******************************************************************************
**
** Function         pbc_set_open_pending
**
** Description      Set pbc open pending
**
** Parameters
**
** Returns          void
**
*******************************************************************************/
void pbc_set_open_pending(BOOLEAN bopenpend)
{
    app_pbc_cb.open_pending = bopenpend;
}

/*******************************************************************************
**
** Function         pbc_is_open
**
** Description      Check if pbc is open
**
** Parameters
**
** Returns          TRUE if pbc is open, FALSE otherwise. Return BDA of the connected device
**
*******************************************************************************/
BOOLEAN pbc_is_open(BD_ADDR *bda)
{
    BOOLEAN bopen = app_pbc_cb.is_open;
    if(bopen)
    {
        bdcpy(*bda, app_pbc_cb.bda_connected);
    }

    return bopen;
}

/*******************************************************************************
**
** Function         pbc_last_open_status
**
** Description      Get the last pbc open status
**
** Parameters
**
** Returns          open status
**
*******************************************************************************/
tBSA_STATUS pbc_last_open_status()
{
    return app_pbc_cb.last_open_status;
}

void app_pbc_register_status_cb(RK_BT_OBEX_STATE_CALLBACK cb)
{
    app_pbc_state_cb = cb;
}

void app_pbc_deregister_status_cb()
{
    app_pbc_state_cb = NULL;
}

int app_pbc_connect(char *address)
{
    BD_ADDR bd_addr;

    if(address == NULL) {
        APP_ERROR0("address is null");
        return -1;
    }

    if(app_mgr_str2bd(address, bd_addr) < 0)
        return -1;

    APP_ERROR1("connect bd_addr: %02X:%02X:%02X:%02X:%02X:%02X",
        bd_addr[0], bd_addr[1], bd_addr[2],
        bd_addr[3], bd_addr[4], bd_addr[5]);

    return app_pbc_open(bd_addr);
}

int app_pbc_disconnect(char *address)
{
    BD_ADDR bd_addr;

    if(!address) {
        APP_ERROR0("address is null");
        return -1;
    }

    if(app_mgr_str2bd(address, bd_addr) < 0)
        return -1;

    APP_ERROR1("connect bd_addr: %02X:%02X:%02X:%02X:%02X:%02X",
        bd_addr[0], bd_addr[1], bd_addr[2],
        bd_addr[3], bd_addr[4], bd_addr[5]);

    return app_pbc_close(bd_addr);
}

int app_pbc_get_vcf(char *dir_name, char *dir_file)
{
    char file_name[MAX_PATH_LENGTH];
    int len;
    tBSA_STATUS status;

    if (!dir_name || !dir_file) {
        APP_ERROR1("Invalid dir_name or dir_file");
        return -1;
    }

    memset(app_pbc_cb.save_path, 0, MAX_PATH_LENGTH);
    len = strlen(dir_file) > MAX_PATH_LENGTH - 1 ? MAX_PATH_LENGTH -1 : strlen(dir_file);
    memcpy(app_pbc_cb.save_path, dir_file, len);

    memset(file_name, 0, MAX_PATH_LENGTH);
    sprintf(file_name, "%s%s%s", "telecom/", dir_name, ".vcf");
    //sprintf(file_name, "%s%s%s", "SIM1/telecom/", dir_name, ".vcf");
    APP_DEBUG1("file_name: %s", file_name);
    status = app_pbc_get_phonebook(file_name, 1, BSA_PBC_FILTER_ALL, BSA_PBC_OP_OR);
    if(status != BSA_SUCCESS)
        return -1;

    return 0;
}