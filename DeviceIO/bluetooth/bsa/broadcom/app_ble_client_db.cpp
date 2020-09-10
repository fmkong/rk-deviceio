/*****************************************************************************
 **
 **  Name:           app_ble_client_db.c
 **
 **  Description:    Bluetooth BLE Database utility functions
 **
 **  Copyright (c) 2014, Broadcom Corp., All Rights Reserved.
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

#include "bsa_api.h"

#include "bsa_trace.h"
#include "app_utils.h"
#include "app_ble.h"
#include "app_ble_client_db.h"
#include "app_ble_client_xml.h"

#define UUID16_STRING_LEN 4 //eg: 180A
#define UUID32_STRING_LEN 8 //eg: 180A
#define UUID128_STRING_LEN 36 //eg: 0000180A-0000-1000-8000-00805F9B34FB

/* Structure to hold Database data */
typedef struct
{
    tAPP_BLE_CLIENT_DB_ELEMENT *first;
} tAPP_BLE_CLIENT_DB;


tAPP_BLE_CLIENT_DB app_ble_client_db;

/*
 * Local functions
 */

/*******************************************************************************
 **
 ** Function        app_ble_client_db_init
 **
 ** Description     Initialize database system
 **
 ** Parameters      None
 **
 ** Returns         0 if successful, otherwise the error number
 **
 *******************************************************************************/
int app_ble_client_db_init(void)
{
    /* By default, clear the entire structure */
    memset(&app_ble_client_db, 0, sizeof(app_ble_client_db));

    /* Reload and save the database to clean it at startup */
    app_ble_client_db_reload();
    app_ble_client_db_save();

    return 0;
}

/*******************************************************************************
 **
 ** Function        app_ble_client_db_reload
 **
 ** Description     Reload the database
 **
 ** Parameters      None
 **
 ** Returns         0 if successful, otherwise the error number
 **
 *******************************************************************************/
int app_ble_client_db_reload(void)
{
    /* Free up the current database */
    app_ble_client_db_clear();

    /* Reload the XML */
    return app_ble_client_xml_read(app_ble_client_db.first);
}

/*******************************************************************************
 **
 ** Function        app_ble_client_db_save
 **
 ** Description     Save the database
 **
 ** Parameters      None
 **
 ** Returns         0 if successful, otherwise the error number
 **
 *******************************************************************************/
int app_ble_client_db_save(void)
{
    /* Save the XML */
    return app_ble_client_xml_write(app_ble_client_db.first);
}

/*******************************************************************************
 **
 ** Function        app_ble_db_clear
 **
 ** Description     Clear the database
 **
 ** Parameters      None
 **
 ** Returns         None
 **
 *******************************************************************************/
void app_ble_client_db_clear(void)
{
    tAPP_BLE_CLIENT_DB_ELEMENT *p_next, *p_element = app_ble_client_db.first;

    app_ble_client_db.first = NULL;

    while (p_element != NULL)
    {
        p_next = p_element->next;
        app_ble_client_db_free_element(p_element);
        p_element = p_next;
    }
}

/*******************************************************************************
 **
 ** Function        app_ble_client_db_clear_by_bda
 **
 ** Description    Finds an element in the database and clears
 **
 ** Parameters      bda: the BD address of the element to clear from db
 **
 ** Returns         None
 **
 *******************************************************************************/
void app_ble_client_db_clear_by_bda(BD_ADDR bda)
{
    tAPP_BLE_CLIENT_DB_ELEMENT *p_element = app_ble_client_db.first;
    tAPP_BLE_CLIENT_DB_ELEMENT *p_last = NULL;

    p_element = app_ble_client_db.first;

    APP_INFO0("app_ble_client_db_clear_by_bda p_element");
    while(p_element != NULL)
    {
        if (bdcmp(bda, p_element->bd_addr) == 0)
        {
            break;
        }
        p_last = p_element;
        p_element = p_element->next;
    }

    if(p_element == NULL)
    {
        APP_INFO0("Device not found");
        return;
    }
    if (p_last == NULL)
    {
        APP_INFO0("Need to remove first item in DB");
        app_ble_client_db.first = p_element->next;
    }
    else
    {
        APP_INFO0("Delete the element");
        p_last->next = p_element->next;
    }

    app_ble_client_db_free_element(p_element);
}

/*******************************************************************************
 **
 ** Function        app_ble_client_db_alloc_element
 **
 ** Description     Allocate a new DB element
 **
 ** Parameters      None
 **
 ** Returns         Pointer to the new element found, NULL if error
 **
 *******************************************************************************/
tAPP_BLE_CLIENT_DB_ELEMENT *app_ble_client_db_alloc_element(void)
{
    tAPP_BLE_CLIENT_DB_ELEMENT *p_element;

    p_element = calloc(1, sizeof(*p_element));

    /* Initialize element with default values */
    p_element->p_attr = NULL;
    p_element->app_handle = 0xffff;
    p_element->next = NULL;

    return p_element;
}


/*******************************************************************************
 **
 ** Function        app_ble_client_db_free_element
 **
 ** Description     Free an element
 **
 ** Parameters      p_element: Element to free
 **
 ** Returns         None
 **
 *******************************************************************************/
void app_ble_client_db_free_element(tAPP_BLE_CLIENT_DB_ELEMENT *p_element)
{
    tAPP_BLE_CLIENT_DB_ATTR *p_current, *p_next;

    if (p_element->p_attr != NULL)
    {
        p_current = p_element->p_attr;
        while (p_current != NULL)
        {
            p_next = p_current->next;
            app_ble_client_db_free_attribute(p_current);
            p_current = p_next;
        }
    }
    free(p_element);
}

/*******************************************************************************
 **
 ** Function        app_ble_client_db_free_attribute
 **
 ** Description     Free an element
 **
 ** Parameters      p_element: Element to free
 **
 ** Returns         None
 **
 *******************************************************************************/
void app_ble_client_db_free_attribute(tAPP_BLE_CLIENT_DB_ATTR *p_attribute)
{
    free(p_attribute);
}

/*******************************************************************************
 **
 ** Function        app_ble_client_db_add_element
 **
 ** Description     Add a database element to the database
 **
 ** Parameters      p_element: Element to add (allocated by app_hh_db_alloc_element)
 **
 ** Returns         None
 **
 *******************************************************************************/
void app_ble_client_db_add_element(tAPP_BLE_CLIENT_DB_ELEMENT *p_element)
{
    p_element->next = app_ble_client_db.first;
    app_ble_client_db.first = p_element;

    APP_INFO1("added ele on db BDA:%02X:%02X:%02X:%02X:%02X:%02X, client_if= %d ",
        app_ble_client_db.first->bd_addr[0], app_ble_client_db.first->bd_addr[1],
        app_ble_client_db.first->bd_addr[2], app_ble_client_db.first->bd_addr[3],
        app_ble_client_db.first->bd_addr[4], app_ble_client_db.first->bd_addr[5],
        app_ble_client_db.first->app_handle);
}

/*******************************************************************************
 **
 ** Function        app_ble_db_find_by_bda
 **
 ** Description     Find an element in the database if it exists
 **
 ** Parameters      bda: the BD address of the element to look for
 **
 ** Returns         Pointer to the element found, NULL if not found
 **
 *******************************************************************************/
tAPP_BLE_CLIENT_DB_ELEMENT *app_ble_client_db_find_by_bda(BD_ADDR bda)
{
    tAPP_BLE_CLIENT_DB_ELEMENT *p_element;

    p_element = app_ble_client_db.first;

    while (p_element != NULL)
    {
        if (bdcmp(bda, p_element->bd_addr) == 0)
        {
            return p_element;
        }
        p_element = p_element->next;
    }

    return NULL;
}

/*******************************************************************************
 **
 ** Function        app_ble_client_db_find_by_handle
 **
 ** Description     Find an element in the database if it exists
 **
 ** Parameters      bda: the BD address of the element to look for
 **
 ** Returns         Pointer to the element found, NULL if not found
 **
 *******************************************************************************/
tAPP_BLE_CLIENT_DB_ATTR *app_ble_client_db_find_by_handle(tAPP_BLE_CLIENT_DB_ELEMENT *p_element,
     UINT16 handle)
{
    tAPP_BLE_CLIENT_DB_ATTR *p_attribute;

    p_attribute = p_element->p_attr;
    while (p_attribute != NULL)
    {
        if (p_attribute->handle == handle)
        {
            return p_attribute;
        }
        p_attribute = p_attribute->next;
    }
    return NULL;
}

/*******************************************************************************
 **
 ** Function        app_ble_client_db_alloc_attr
 **
 ** Description     Find an element in the database if it exists
 **
 ** Parameters      bda: the BD address of the element to look for
 **
 ** Returns         Pointer to the element found, NULL if not found
 **
 *******************************************************************************/
tAPP_BLE_CLIENT_DB_ATTR *app_ble_client_db_alloc_attr(tAPP_BLE_CLIENT_DB_ELEMENT *p_element)
{
    tAPP_BLE_CLIENT_DB_ATTR *p_attribute, *n_attribute, *prev_attr;

    p_attribute = p_element->p_attr;
    if(p_attribute == NULL)
    {
         p_attribute = malloc(sizeof(tAPP_BLE_CLIENT_DB_ATTR));
         p_attribute->next = NULL;
         p_element->p_attr = p_attribute;
         return p_attribute;
    }
    else
    {
        n_attribute = p_attribute->next;
        prev_attr = p_attribute;
        while (n_attribute != NULL)
        {
            prev_attr = prev_attr->next;
            n_attribute = n_attribute->next;
        }
        n_attribute = malloc(sizeof(tAPP_BLE_CLIENT_DB_ATTR));
        prev_attr->next = n_attribute;
        n_attribute->next = NULL;
        return n_attribute;
    }

    return NULL;
}

void app_ble_client_db_get_uuid(tBT_UUID attr_uuid, char *uuid, int uuid_len)
{
    if (attr_uuid.len == LEN_UUID_16) {
        sprintf(uuid, "%04X"/*"0x%04X"*/, (int)attr_uuid.uu.uuid16);
    } else if (attr_uuid.len == LEN_UUID_32) {
        sprintf(uuid, "%08X"/*"0x%08X"*/, (int)attr_uuid.uu.uuid32);
    } else if (attr_uuid.len == LEN_UUID_128) {
        sprintf(uuid, "%02hhx%02hhx%02hhx%02hhx-%02hhx%02hhx-%02hhx%02hhx-%02hhx%02hhx-%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx",
                attr_uuid.uu.uuid128[LEN_UUID_128 - 1], attr_uuid.uu.uuid128[LEN_UUID_128 - 2],
                attr_uuid.uu.uuid128[LEN_UUID_128 - 3], attr_uuid.uu.uuid128[LEN_UUID_128 - 4],
                attr_uuid.uu.uuid128[LEN_UUID_128 - 5], attr_uuid.uu.uuid128[LEN_UUID_128 - 6],
                attr_uuid.uu.uuid128[LEN_UUID_128 - 7], attr_uuid.uu.uuid128[LEN_UUID_128 - 8],
                attr_uuid.uu.uuid128[LEN_UUID_128 - 9], attr_uuid.uu.uuid128[LEN_UUID_128 - 10],
                attr_uuid.uu.uuid128[LEN_UUID_128 - 11], attr_uuid.uu.uuid128[LEN_UUID_128 - 12],
                attr_uuid.uu.uuid128[LEN_UUID_128 - 13], attr_uuid.uu.uuid128[LEN_UUID_128 - 14],
                attr_uuid.uu.uuid128[LEN_UUID_128 - 15], attr_uuid.uu.uuid128[LEN_UUID_128 - 16]);
    }
}

int app_ble_client_db_get_service_info(BD_ADDR bd_addr, RK_BLE_CLIENT_SERVICE_INFO *info)
{
    tAPP_BLE_CLIENT_DB_ELEMENT *element;
    tAPP_BLE_CLIENT_DB_ATTR *attr;
    RK_BLE_CLIENT_SERVICE *service;
    RK_BLE_CLIENT_CHRC *chrc;
    RK_BLE_CLIENT_DESC *desc;

    memset(info, 0, sizeof(RK_BLE_CLIENT_SERVICE_INFO));
    element = app_ble_client_db.first;

    while (element != NULL) {
        if(!bdcmp(element->bd_addr, bd_addr)) {
            attr = element->p_attr;
            while(attr != NULL) {
                if(attr->attr_type == 3) { //service
                    app_ble_client_db_get_uuid(attr->attr_UUID, info->service[info->service_cnt].uuid, UUID_BUF_LEN);
                    info->service_cnt++;
                } else if (attr->attr_type == 1) { //characteristics
                    service = &(info->service[info->service_cnt - 1]);
                    chrc = &(service->chrc[service->chrc_cnt]);

                    app_ble_client_db_get_uuid(attr->attr_UUID, chrc->uuid, UUID_BUF_LEN);
                    chrc->props = (int)attr->prop;
                    service->chrc_cnt++;
                } else if (attr->attr_type == 2) { //descriptor
                    service = &(info->service[info->service_cnt - 1]);
                    chrc = &(service->chrc[service->chrc_cnt - 1]);
                    desc = &(chrc->desc[chrc->desc_cnt]);

                    app_ble_client_db_get_uuid(attr->attr_UUID, desc->uuid, UUID_BUF_LEN);
                    chrc->desc_cnt++;
                }

                attr = attr->next;
            }
        }

        /* Move to next DB element */
        element = element->next;
    }
}

static int app_ble_client_db_set_uuid(tBT_UUID *attr_uuid, char *uuid)
{
    int len, ret = -1;

    if(!uuid) {
        APP_ERROR0("uuid == NULL");
        return -1;
    }

    memset(attr_uuid, 0, sizeof(tBT_UUID));

    len = strlen(uuid);
    if(len == UUID16_STRING_LEN) {
        app_ble_string_to_uuid16(&attr_uuid->uu.uuid16, uuid);
        ret = LEN_UUID_16;
        APP_DEBUG1("uu.uuid16: 0x%x", attr_uuid->uu.uuid16);
    } else if (len == UUID32_STRING_LEN) {
        app_ble_string_to_uuid32(&attr_uuid->uu.uuid32, uuid);
        ret = LEN_UUID_32;
        APP_DEBUG1("uu.uuid32: 0x%x", attr_uuid->uu.uuid32);
    } else if (len == UUID128_STRING_LEN) {
        app_ble_string_to_uuid128(attr_uuid->uu.uuid128, uuid, FALSE);
        ret = LEN_UUID_128;

        printf("-----uu.uuid128: ");
        for (int i = 0; i < MAX_UUID_SIZE; i++)
            printf("0x%x ", attr_uuid->uu.uuid128[i]);
        printf("\n");

    } else {
        APP_ERROR1("not support, uuid(%s) len = %d\n", uuid, len);
    }

    return ret;
}

static BOOLEAN app_ble_client_db_arraycmp(UINT8 *a, UINT8 *b, int len)
{
    BOOLEAN equal = true;

    for (int i = 0; i < len; i++) {
        if (a[i] != b[i]) {
            //printf("len(%d), a[%d]: 0x%x, b[%d]: 0x%x\n", len, i, a[i], i, b[i]);
            equal = false;
            break;
        }
    }

    return equal;
}

int app_ble_client_db_find_attr(tAPP_BLE_CLIENT_DB_CHAR *char_id, BD_ADDR bd_addr, char *uuid)
{
    tAPP_BLE_CLIENT_DB_ELEMENT *element;
    tAPP_BLE_CLIENT_DB_ATTR *attr;
    tBT_UUID char_uuid;
    int uuid_len;
    BOOLEAN is_find = FALSE;

    uuid_len = app_ble_client_db_set_uuid(&char_uuid, uuid);
    if(uuid_len < 0)
        return -1;

    memset(char_id, 0, sizeof(tAPP_BLE_CLIENT_DB_CHAR));
    element = app_ble_client_db.first;

    while (element != NULL) {
        if(!bdcmp(element->bd_addr, bd_addr)) {
            attr = element->p_attr;
            while(attr != NULL) {
                if(attr->attr_type == 3) { //service
                    memset(&char_id->service_uuid, 0, sizeof(tBT_UUID));
                    memcpy(&char_id->service_uuid, &attr->attr_UUID, sizeof(tBT_UUID));
                    char_id->is_primary = attr->is_primary;
                } else if (attr->attr_type == 1) { //characteristics
                    if(attr->attr_UUID.len == uuid_len) {
                        switch(uuid_len) {
                        case LEN_UUID_16:
                            //APP_DEBUG1("attr uu.uuid16: 0x%x", attr->attr_UUID.uu.uuid16);
                            if(char_uuid.uu.uuid16 == attr->attr_UUID.uu.uuid16)
                                is_find = TRUE;
                            break;

                        case LEN_UUID_32:
                            //APP_DEBUG1("attr uu.uuid32: 0x%x", attr->attr_UUID.uu.uuid32);
                            if(char_uuid.uu.uuid32 == attr->attr_UUID.uu.uuid32)
                                is_find = TRUE;
                            break;

                        case LEN_UUID_128:
                            //printf("attr uu.uuid128: ");
                            //for (int i = 0; i < MAX_UUID_SIZE; i++)
                            //    printf("0x%x ", attr->attr_UUID.uu.uuid128[i]);
                            //printf("\n");

                            if(app_ble_client_db_arraycmp(char_uuid.uu.uuid128, attr->attr_UUID.uu.uuid128, MAX_UUID_SIZE))
                                is_find = TRUE;
                            break;
                        }

                        if(is_find) {
                            memcpy(&char_id->char_uuid, &attr->attr_UUID, sizeof(tBT_UUID));
                            char_id->char_prop = attr->prop;
                            return 0;
                        }
                    }
                }

                attr = attr->next;
            }
        }

        /* Move to next DB element */
        element = element->next;
    }

    APP_DEBUG0("No match characteristics was found");
    return -1;
}
