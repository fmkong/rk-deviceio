/*****************************************************************************
 **
 **  Name:           app_avk.c
 **
 **  Description:    Bluetooth Manager application
 **
 **  Copyright (c) 2009-2015, Broadcom Corp., All Rights Reserved.
 **  Broadcom Bluetooth Core. Proprietary and confidential.
 **
 *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#ifdef PCM_ALSA_OPEN_BLOCKING
#include <pthread.h>
#endif
#include "buildcfg.h"

#include "bsa_api.h"

#include "gki.h"
#include "uipc.h"

#include "app_xml_utils.h"
#include "app_disc.h"
#include "app_utils.h"
#include "app_wav.h"
#include "app_dm.h"
#include "app_manager.h"

#ifdef PCM_ALSA
#include "alsa/asoundlib.h"
#define APP_AVK_ASLA_DEV "default"
#endif

#include "app_avk.h"
/*
 * Defines
 */

#define APP_XML_REM_DEVICES_FILE_PATH "/data/bsa/config/bt_devices.xml"

#define APP_AVK_SOUND_FILE "/data/bsa/test_avk"

#ifndef BSA_AVK_SECURITY
#define BSA_AVK_SECURITY    BSA_SEC_AUTHORIZATION
#endif
#ifndef BSA_AVK_FEATURES
#define BSA_AVK_FEATURES    (BSA_AVK_FEAT_RCCT|BSA_AVK_FEAT_RCTG|\
                             BSA_AVK_FEAT_VENDOR|BSA_AVK_FEAT_METADATA|\
                             BSA_AVK_FEAT_BROWSE|BSA_AVK_FEAT_DELAY_RPT)
#endif

#ifndef BSA_AVK_DUMP_RX_DATA
#define BSA_AVK_DUMP_RX_DATA FALSE
#endif

#ifndef BSA_AVK_AAC_SUPPORTED
#define BSA_AVK_AAC_SUPPORTED FALSE
#endif

/* bitmask of events that BSA client is interested in registering for notifications */
tBSA_AVK_REG_NOTIFICATIONS reg_notifications =
        (1 << (BSA_AVK_RC_EVT_PLAY_STATUS_CHANGE - 1)) |
        (1 << (BSA_AVK_RC_EVT_TRACK_CHANGE - 1)) |
        (1 << (BSA_AVK_RC_EVT_TRACK_REACHED_END - 1)) |
        (1 << (BSA_AVK_RC_EVT_TRACK_REACHED_START - 1)) |
        (1 << (BSA_AVK_RC_EVT_PLAY_POS_CHANGED - 1)) |
        (1 << (BSA_AVK_RC_EVT_BATTERY_STATUS_CHANGE - 1)) |
        (1 << (BSA_AVK_RC_EVT_SYSTEM_STATUS_CHANGE - 1)) |
        (1 << (BSA_AVK_RC_EVT_APP_SETTING_CHANGE - 1)) |
        (1 << (BSA_AVK_RC_EVT_NOW_PLAYING_CHANGE - 1)) |
        (1 << (BSA_AVK_RC_EVT_AVAL_PLAYERS_CHANGE - 1)) |
        (1 << (BSA_AVK_RC_EVT_ADDR_PLAYER_CHANGE - 1)) |
        (1 << (BSA_AVK_RC_EVT_UIDS_CHANGE - 1)) |
        (1 << (BSA_AVK_RC_EVT_VOLUME_CHANGE - 1));

/*
 * global variable
 */

tAPP_AVK_CB app_avk_cb;

#ifdef PCM_ALSA
//static const char *alsa_device = "default"; /* ALSA playback device */
static char alsa_device[30]; /* ALSA playback device */
#ifdef PCM_ALSA_OPEN_BLOCKING
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
#endif
#endif /* PCM_ALSA */

enum APP_AVK_PLAYSTATE {
    SEND_PLAY = 1,
    SEND_PAUSE,
    SEND_STOP,
    PLAYED,
    STOPPED
};

typedef struct {
    RK_BT_SINK_VOLUME_CALLBACK sink_volume_cb;
    RK_BT_SINK_CALLBACK sink_state_cb;
    RK_BT_AVRCP_TRACK_CHANGE_CB sink_track_cb;
    RK_BT_AVRCP_PLAY_POSITION_CB sink_position_cb;
} tAPP_AVK_CALLBACK;

static enum APP_AVK_PLAYSTATE play_state[APP_AVK_MAX_CONNECTIONS];
static pthread_mutex_t ps_mutex = PTHREAD_MUTEX_INITIALIZER;
static RK_BT_SINK_STATE app_avk_state = RK_BT_SINK_STATE_IDLE;
static int app_avk_duration = 0;
static BOOLEAN app_avk_get_track = FALSE;
static BOOLEAN app_avk_pos_change = FALSE;

/*
 * Local functions
 */
static void app_avk_close_wave_file(tAPP_AVK_CONNECTION *connection);
static void app_avk_create_wave_file(void);
static void app_avk_uipc_cback(BT_HDR *p_msg);

static tAPP_AVK_CALLBACK app_avk_cb_control = {
	NULL, NULL, NULL, NULL,
};

static void app_avk_send_state(RK_BT_SINK_STATE state) {
    if(app_avk_cb_control.sink_state_cb)
        app_avk_cb_control.sink_state_cb(state);
}

static void app_avk_send_volume(int volume) {
    if(app_avk_cb_control.sink_volume_cb)
        app_avk_cb_control.sink_volume_cb(volume);
}

static void app_avk_send_position(BD_ADDR bd_addr, int song_len, int song_pos)
{
    char address[18];

    if(app_avk_cb_control.sink_position_cb) {
        if(app_mgr_bd2str(bd_addr, address, 18) < 0)
            memcpy(address, "unknown", strlen("unknown"));

        app_avk_cb_control.sink_position_cb(address, song_len, song_pos);
    }
}

static void app_avk_track_info_send(BD_ADDR bd_addr, tBSA_AVK_GET_ELEMENT_ATTR_MSG elem_attr)
{
    BtTrackInfo track;
    char address[18];

    if(app_mgr_bd2str(bd_addr, address, 18) < 0)
        memcpy(address, "unknown", strlen("unknown"));

    memset(&track, 0, sizeof(BtTrackInfo));
    memcpy(track.title, elem_attr.attr_entry[0].name.data, strlen(elem_attr.attr_entry[0].name.data));
    memcpy(track.artist, elem_attr.attr_entry[1].name.data, strlen(elem_attr.attr_entry[1].name.data));
    memcpy(track.album, elem_attr.attr_entry[2].name.data, strlen(elem_attr.attr_entry[2].name.data));
    memcpy(track.track_num, elem_attr.attr_entry[3].name.data, strlen(elem_attr.attr_entry[3].name.data));
    memcpy(track.num_tracks, elem_attr.attr_entry[4].name.data, strlen(elem_attr.attr_entry[4].name.data));
    memcpy(track.genre, elem_attr.attr_entry[5].name.data, strlen(elem_attr.attr_entry[5].name.data));
    memcpy(track.playing_time, elem_attr.attr_entry[6].name.data, strlen(elem_attr.attr_entry[6].name.data));

    if(app_avk_cb_control.sink_track_cb)
        app_avk_cb_control.sink_track_cb(address, track);

    app_avk_duration = atoi(track.playing_time);
    app_avk_get_track = FALSE;
}

void app_avk_register_cb(RK_BT_SINK_CALLBACK cb)
{
	app_avk_cb_control.sink_state_cb = cb;
}

void app_avk_register_volume_cb(RK_BT_SINK_VOLUME_CALLBACK cb)
{
	app_avk_cb_control.sink_volume_cb = cb;
}

void app_avk_register_track_cb(RK_BT_AVRCP_TRACK_CHANGE_CB cb)
{
	app_avk_cb_control.sink_track_cb = cb;
}

void app_avk_register_position_cb(RK_BT_AVRCP_PLAY_POSITION_CB cb)
{
	app_avk_cb_control.sink_position_cb = cb;
}

void app_avk_deregister_cb()
{
    app_avk_cb_control.sink_state_cb = NULL;
    app_avk_cb_control.sink_volume_cb = NULL;
    app_avk_cb_control.sink_track_cb = NULL;
    app_avk_cb_control.sink_position_cb = NULL;
}

/*******************************************************************************
 **
 ** Function         app_avk_get_label
 **
 ** Description      get transaction label (used to distinguish transactions)
 **
 ** Returns          UINT8
 **
 *******************************************************************************/
UINT8 app_avk_get_label()
{
    if(app_avk_cb.label >= 15)
        app_avk_cb.label = 0;
    return app_avk_cb.label++;
}

/*******************************************************************************
 **
 ** Function         app_avk_create_wave_file
 **
 ** Description     create a new wave file
 **
 ** Returns          void
 **
 *******************************************************************************/
static void app_avk_create_wave_file(void)
{
    int file_index = 0;
    int fd;
    char filename[200];

#ifdef PCM_ALSA
    return;
#endif

    /* If a wav file is currently open => close it */
    if (app_avk_cb.fd != -1)
    {
        tAPP_AVK_CONNECTION dummy;
        memset (&dummy, 0, sizeof(tAPP_AVK_CONNECTION));
        app_avk_close_wave_file(&dummy);
    }

    do
    {
        snprintf(filename, sizeof(filename), "%s-%d.wav", APP_AVK_SOUND_FILE, file_index);
        filename[sizeof(filename)-1] = '\0';
        fd = app_wav_create_file(filename, O_EXCL);
        file_index++;
    } while (fd < 0);

    app_avk_cb.fd = fd;
}
/*******************************************************************************
 **
 ** Function         app_avk_create_aac_file
 **
 ** Description     create a new wave file
 **
 ** Returns          void
 **
 *******************************************************************************/
static void app_avk_create_aac_file(void)
{
    int file_index = 0;
    int fd;
    char filename[200];

    /* If a wav file is currently open => close it */
    if (app_avk_cb.fd != -1)
    {
//        close(app_avk_cb.fd);
//        app_avk_cb.fd = -1;
        return;
    }

    do
    {
        int flags = O_EXCL | O_RDWR | O_CREAT | O_TRUNC;

        snprintf(filename, sizeof(filename), "%s-%d.aac", APP_AVK_SOUND_FILE, file_index);
        filename[sizeof(filename)-1] = '\0';

        /* Create file in read/write mode, reset the length field */
        fd = open(filename, flags, 0666);
        if (fd < 0)
        {
                APP_ERROR1("open(%s) failed: %d", filename, errno);
            }

        file_index++;
    } while ((fd < 0) && (file_index<100));

    app_avk_cb.fd_codec_type = BSA_AVK_CODEC_M24;
    app_avk_cb.fd = fd;
}

/*******************************************************************************
 **
 ** Function         app_avk_close_wave_file
 **
 ** Description     proper header and close the wave file
 **
 ** Returns          void
 **
 *******************************************************************************/
static void app_avk_close_wave_file(tAPP_AVK_CONNECTION *connection)
{
    tAPP_WAV_FILE_FORMAT format;

#ifdef PCM_ALSA
    return;
#endif

    if (app_avk_cb.fd == -1)
    {
        APP_ERROR0("no file to close");
        return;
    }

    format.bits_per_sample = connection->bit_per_sample;
    format.nb_channels = connection->num_channel;
    format.sample_rate = connection->sample_rate;

    app_wav_close_file(app_avk_cb.fd, &format);

    app_avk_cb.fd = -1;
}

/*******************************************************************************
**
** Function         app_avk_end
**
** Description      This function is used to close AV
**
** Returns          void
**
*******************************************************************************/
void app_avk_end(void)
{
    tBSA_AVK_DISABLE disable_param;
    tBSA_AVK_DEREGISTER deregister_param;

    /* deregister avk */
    BSA_AvkDeregisterInit(&deregister_param);
    BSA_AvkDeregister(&deregister_param);

    /* disable avk */
    BSA_AvkDisableInit(&disable_param);
    BSA_AvkDisable(&disable_param);
}

/*******************************************************************************
**
** Function         app_avk_handle_start
**
** Description      This function is the AVK start handler function
**                  Configures PCM Driver with the stream settings
** Returns          void
**
*******************************************************************************/
static void app_avk_handle_start(tBSA_AVK_MSG *p_data, tAPP_AVK_CONNECTION *connection)
{
    const char *avk_format_display[12] = {"SBC", "M12", "M24", "??", "ATRAC", "PCM", "APT-X", "SEC"};
#ifdef PCM_ALSA
    int status;
    snd_pcm_format_t format;
#endif

    if (p_data->start_streaming.media_receiving.format < (sizeof(avk_format_display)/sizeof(avk_format_display[0])))
    {
        APP_INFO1("AVK start: format %s", avk_format_display[p_data->start_streaming.media_receiving.format]);
    }
    else
    {
        APP_INFO1("AVK start: format code (%u) unknown", p_data->start_streaming.media_receiving.format);
    }

    connection->format = p_data->start_streaming.media_receiving.format;

    if (connection->format == BSA_AVK_CODEC_PCM)
    {
        app_avk_create_wave_file();/* create and open a wave file */
        connection->sample_rate = p_data->start_streaming.media_receiving.cfg.pcm.sampling_freq;
        connection->num_channel = p_data->start_streaming.media_receiving.cfg.pcm.num_channel;
        connection->bit_per_sample = p_data->start_streaming.media_receiving.cfg.pcm.bit_per_sample;
        APP_DEBUG1("Sampling rate:%d, number of channel:%d bit per sample:%d",
            p_data->start_streaming.media_receiving.cfg.pcm.sampling_freq,
            p_data->start_streaming.media_receiving.cfg.pcm.num_channel,
            p_data->start_streaming.media_receiving.cfg.pcm.bit_per_sample);
#ifdef PCM_ALSA
        /* If ALSA PCM driver was already open => close it */
        if (app_avk_cb.alsa_handle != NULL)
        {
#ifdef PCM_ALSA_OPEN_BLOCKING
            pthread_mutex_lock(&mutex);
#endif
            snd_pcm_close(app_avk_cb.alsa_handle);
            app_avk_cb.alsa_handle = NULL;
#ifdef PCM_ALSA_OPEN_BLOCKING
            pthread_mutex_unlock(&mutex);
#endif
        }

        /* Open ALSA driver */
        app_avk_set_alsa_device(NULL);
#ifdef PCM_ALSA_OPEN_BLOCKING
        /* Configure as blocking */
        status = snd_pcm_open(&(app_avk_cb.alsa_handle), alsa_device,
            SND_PCM_STREAM_PLAYBACK, 0);
#else
        status = snd_pcm_open(&(app_avk_cb.alsa_handle), alsa_device,
            SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK);
#endif
        if (status < 0)
        {
            APP_ERROR1("snd_pcm_open failed: %s", snd_strerror(status));

        }
        else
        {
            APP_DEBUG0("ALSA driver opened");
            if (connection->bit_per_sample == 8)
                format = SND_PCM_FORMAT_U8;
            else
                format = SND_PCM_FORMAT_S16_LE;
            /* Configure ALSA driver with PCM parameters */
            status = snd_pcm_set_params(app_avk_cb.alsa_handle, format,
                SND_PCM_ACCESS_RW_INTERLEAVED, connection->num_channel,
                connection->sample_rate, 1, 500000);/* 0.5sec */
            if (status < 0)
            {
                APP_ERROR1("snd_pcm_set_params failed: %s", snd_strerror(status));
                exit(1);
            }
        }
#endif
    }
    else if (connection->format == BSA_AVK_CODEC_M24)
    {
        /* create and open an aac file to dump data to */
        app_avk_create_aac_file();

        /* Initialize the decoder with the format information here */
    }

}

static void app_avk_set_master_volume(int volume)
{
    char buffer[100];
    memset(buffer, 0 ,100);
    sprintf(buffer, "amixer set Master Playback %d", volume * 100 / BSA_MAX_ABS_VOLUME);
    if (-1 == system(buffer))
        APP_ERROR1("error: %d, volume: %d", errno, volume);
}

/*******************************************************************************
 **
 ** Function         app_avk_cback
 **
 ** Description      This function is the AVK callback function
 **
 ** Returns          void
 **
 *******************************************************************************/
static void app_avk_cback(tBSA_AVK_EVT event, tBSA_AVK_MSG *p_data)
{
    tAPP_AVK_CONNECTION *connection = NULL;
    tBSA_AVK_REM_RSP RemRsp;
    int i;

    switch (event) {
    case BSA_AVK_OPEN_EVT:
        APP_DEBUG1("BSA_AVK_OPEN_EVT status 0x%x", p_data->sig_chnl_open.status);

        if (p_data->sig_chnl_open.status == BSA_SUCCESS) {
            connection = app_avk_add_connection(p_data->sig_chnl_open.bd_addr);

            if(connection == NULL) {
                APP_DEBUG0("BSA_AVK_OPEN_EVT cannot allocate connection cb");
                break;
            }

            /* Read the Remote device xml file to have a fresh view */
            app_read_xml_remote_devices();
            app_xml_update_connected_state_db(app_xml_remote_devices_db,
                                   APP_NUM_ELEMENTS(app_xml_remote_devices_db),
                                   p_data->sig_chnl_open.bd_addr, TRUE);
            app_xml_update_latest_connect_db(app_xml_remote_devices_db,
                                   APP_NUM_ELEMENTS(app_xml_remote_devices_db),
                                   p_data->sig_chnl_open.bd_addr);
            app_write_xml_remote_devices();

            connection->ccb_handle = p_data->sig_chnl_open.ccb_handle;
            connection->is_open = TRUE;
            connection->is_streaming_chl_open = FALSE;

            /* Set visisble and disconnectable */
            app_dm_set_visibility(TRUE, FALSE);
            app_avk_state = RK_BT_SINK_STATE_CONNECT;
            app_avk_send_state(RK_BT_SINK_STATE_CONNECT);

            APP_DEBUG1("AVK connected to device %02X:%02X:%02X:%02X:%02X:%02X\n",
                p_data->sig_chnl_open.bd_addr[0], p_data->sig_chnl_open.bd_addr[1],p_data->sig_chnl_open.bd_addr[2],
                p_data->sig_chnl_open.bd_addr[3], p_data->sig_chnl_open.bd_addr[4],p_data->sig_chnl_open.bd_addr[5]);
        }

        app_avk_cb.open_pending = FALSE;
        memset(app_avk_cb.open_pending_bda, 0, sizeof(BD_ADDR));
        break;

    case BSA_AVK_CLOSE_EVT:
        /* Close event, reason BTA_AVK_CLOSE_STR_CLOSED or BTA_AVK_CLOSE_CONN_LOSS*/
        APP_DEBUG1("BSA_AVK_CLOSE_EVT status 0x%x, handle %d", p_data->sig_chnl_close.status, p_data->sig_chnl_close.ccb_handle);

        connection = app_avk_find_connection_by_bd_addr(p_data->sig_chnl_close.bd_addr);
        if(connection == NULL) {
            APP_DEBUG1("BSA_AVK_CLOSE_EVT unknown handle %d", p_data->sig_chnl_close.ccb_handle);
            break;
        }

        play_state[connection->index] = STOPPED;
        app_avk_duration = 0;
        app_avk_get_track = FALSE;
        app_avk_pos_change = FALSE;

        connection->is_open = FALSE;
        app_avk_cb.open_pending = FALSE;
        memset(app_avk_cb.open_pending_bda, 0, sizeof(BD_ADDR));
        if (connection->is_started_streaming == TRUE) {
            connection->is_started_streaming = FALSE;

            if (connection->format == BSA_AVK_CODEC_M24) {
                close(app_avk_cb.fd);
                app_avk_cb.fd  = -1;
            } else {
                app_avk_close_wave_file(connection);
            }
        }

        if(app_avk_cb.alsa_handle != NULL) {
#ifdef PCM_ALSA_OPEN_BLOCKING
            pthread_mutex_lock(&mutex);
#endif
            snd_pcm_close(app_avk_cb.alsa_handle);
            app_avk_cb.alsa_handle = NULL;
#ifdef PCM_ALSA_OPEN_BLOCKING
            pthread_mutex_unlock(&mutex);
#endif
        }

        app_avk_reset_connection(connection->bda_connected);

        /* Read the Remote device xml file to have a fresh view */
        app_read_xml_remote_devices();
        app_xml_update_connected_state_db(app_xml_remote_devices_db,
                               APP_NUM_ELEMENTS(app_xml_remote_devices_db),
                               p_data->sig_chnl_close.bd_addr, FALSE);
        app_write_xml_remote_devices();

        /* Set visisble and connectable */
        app_dm_set_visibility(TRUE, TRUE);
        app_avk_state = RK_BT_SINK_STATE_DISCONNECT;
        app_avk_send_state(RK_BT_SINK_STATE_DISCONNECT);
        break;

    case BSA_AVK_STR_OPEN_EVT:
        APP_DEBUG1("BSA_AVK_STR_OPEN_EVT status 0x%x", p_data->stream_chnl_open.status);

        connection = app_avk_find_connection_by_av_handle(p_data->stream_chnl_open.ccb_handle);
        if(connection == NULL)
            break;

        connection->is_streaming_chl_open = TRUE;
        break;

    case BSA_AVK_STR_CLOSE_EVT:
        APP_DEBUG1("BSA_AVK_STR_CLOSE_EVT streaming chn closed handle: %d ", p_data->stream_chnl_close.ccb_handle);
        connection = app_avk_find_connection_by_bd_addr(p_data->stream_chnl_close.bd_addr);
        if(connection == NULL)
            break;

        connection->is_streaming_chl_open = FALSE;
        break;

    case BSA_AVK_START_EVT:
        APP_DEBUG1("BSA_AVK_START_EVT status 0x%x", p_data->start_streaming.status);

        connection = app_avk_find_connection_by_bd_addr(p_data->start_streaming.bd_addr);
        if(connection == NULL)
            break;

        /* We got START_EVT for a new device that is streaming but server discards the data
            because another stream is already active */
        if(p_data->start_streaming.discarded) {
            connection->is_started_streaming = TRUE;
            connection->is_streaming_chl_open = TRUE;

            break;
        } else {    /* got Start event and device is streaming */
            app_avk_cb.pStreamingConn = NULL;

            APP_DEBUG1("connection->is_started_streaming: %d", connection->is_started_streaming);
            APP_DEBUG1("connection->is_streaming_chl_open: %d", connection->is_streaming_chl_open);
            if(connection->is_started_streaming) {
                break;
            } else if (connection->is_streaming_chl_open) {
                connection->is_started_streaming = TRUE;
                app_avk_handle_start(p_data, connection);
                app_avk_cb.pStreamingConn = connection;

                app_avk_state = RK_BT_A2DP_SINK_STARTED;
                app_avk_send_state(RK_BT_A2DP_SINK_STARTED);
            }
        }

        break;

    case BSA_AVK_STOP_EVT:
        APP_DEBUG1("BSA_AVK_STOP_EVT handle: %d  Suspended: %d", p_data->stop_streaming.ccb_handle, p_data->stop_streaming.suspended);

        connection = app_avk_find_connection_by_bd_addr(p_data->stop_streaming.bd_addr);
        if(connection == NULL)
            break;

        /* Stream was suspended */
        if(p_data->stop_streaming.suspended) {
            connection->is_started_streaming = FALSE;
            app_avk_state = RK_BT_A2DP_SINK_SUSPENDED;
            app_avk_send_state(RK_BT_A2DP_SINK_SUSPENDED);
        } else {    /* stream was closed */
            connection->is_started_streaming = FALSE;

            if (connection->format == BSA_AVK_CODEC_M24) {
                close(app_avk_cb.fd);
                app_avk_cb.fd  = -1;
            } else {
                app_avk_close_wave_file(connection);
            }
            app_avk_state = RK_BT_A2DP_SINK_STOPPED;
            app_avk_send_state(RK_BT_A2DP_SINK_STOPPED);
        }

        break;

    case BSA_AVK_RC_OPEN_EVT:
        if(p_data->rc_open.status == BSA_SUCCESS) {
            connection = app_avk_add_connection(p_data->rc_open.bd_addr);
            if(connection == NULL) {
                APP_DEBUG0("BSA_AVK_RC_OPEN_EVT could not allocate connection");
                break;
            }

            APP_DEBUG1("BSA_AVK_RC_OPEN_EVT peer_feature=0x%x rc_handle=%d",
                p_data->rc_open.peer_features, p_data->rc_open.rc_handle);
            connection->rc_handle = p_data->rc_open.rc_handle;
            connection->peer_features =  p_data->rc_open.peer_features;
            connection->peer_version = p_data->rc_open.peer_version;
            connection->is_rc_open = TRUE;
        }
        break;

    case BSA_AVK_RC_PEER_OPEN_EVT:
        connection = app_avk_find_connection_by_rc_handle(p_data->rc_open.rc_handle);
        if(connection == NULL) {
            APP_DEBUG1("BSA_AVK_RC_PEER_OPEN_EVT could not find connection handle %d", p_data->rc_open.rc_handle);
            break;
        }

        APP_DEBUG1("BSA_AVK_RC_PEER_OPEN_EVT peer_feature=0x%x rc_handle=%d",
            p_data->rc_open.peer_features, p_data->rc_open.rc_handle);

        connection->peer_features =  p_data->rc_open.peer_features;
        connection->peer_version = p_data->rc_open.peer_version;
        connection->is_rc_open = TRUE;
        break;

    case BSA_AVK_RC_CLOSE_EVT:
        APP_DEBUG0("BSA_AVK_RC_CLOSE_EVT");
        connection = app_avk_find_connection_by_rc_handle(p_data->rc_close.rc_handle);
        if(connection == NULL)
            break;

        connection->is_rc_open = FALSE;
        break;

    case BSA_AVK_REMOTE_RSP_EVT:
        APP_DEBUG0("BSA_AVK_REMOTE_RSP_EVT");
        break;

    case BSA_AVK_REMOTE_CMD_EVT:
        APP_DEBUG0("BSA_AVK_REMOTE_CMD_EVT");
        APP_DEBUG1(" label:0x%x", p_data->remote_cmd.label);
        APP_DEBUG1(" op_id:0x%x", p_data->remote_cmd.op_id);
        APP_DEBUG1(" key_state:0x%x", p_data->remote_cmd.key_state);
        APP_DEBUG0(" avrc header");
        APP_DEBUG1("   ctype:0x%x", p_data->remote_cmd.hdr.ctype);
        APP_DEBUG1("   subunit_type:0x%x", p_data->remote_cmd.hdr.subunit_type);
        APP_DEBUG1("   subunit_id:0x%x", p_data->remote_cmd.hdr.subunit_id);
        APP_DEBUG1("   opcode:0x%x", p_data->remote_cmd.hdr.opcode);
        APP_DEBUG1(" len:0x%x", p_data->remote_cmd.len);
        APP_DUMP("data", p_data->remote_cmd.data, p_data->remote_cmd.len);

        connection = app_avk_find_connection_by_rc_handle(p_data->remote_cmd.rc_handle);
        if(connection == NULL)
            break;

        if(p_data->remote_cmd.key_state == BSA_AVK_STATE_PRESS) {
            if (p_data->remote_cmd.op_id == BSA_AVK_RC_VOL_UP) {
                APP_DEBUG0("BSA_AVK_RC_VOL_UP");
            } else if(p_data->remote_cmd.op_id == BSA_AVK_RC_VOL_DOWN) {
                APP_DEBUG0("BSA_AVK_RC_VOL_DOWN");
            }
        }

        BSA_AvkRemoteRspInit(&RemRsp);
        RemRsp.avrc_rsp = BSA_AVK_RSP_ACCEPT;
        RemRsp.label = p_data->remote_cmd.label;
        RemRsp.op_id = p_data->remote_cmd.op_id;
        RemRsp.key_state = p_data->remote_cmd.key_state;
        RemRsp.handle = p_data->remote_cmd.rc_handle;
        RemRsp.length = 0;
        BSA_AvkRemoteRsp(&RemRsp);
        break;

    case BSA_AVK_VENDOR_CMD_EVT:
        APP_DEBUG0("BSA_AVK_VENDOR_CMD_EVT");
        APP_DEBUG1(" code:0x%x", p_data->vendor_cmd.code);
        APP_DEBUG1(" company_id:0x%x", p_data->vendor_cmd.company_id);
        APP_DEBUG1(" label:0x%x", p_data->vendor_cmd.label);
        APP_DEBUG1(" len:0x%x", p_data->vendor_cmd.len);
#if (defined(BSA_AVK_DUMP_RX_DATA) && (BSA_AVK_DUMP_RX_DATA == TRUE))
        APP_DUMP("A2DP Data", p_data->vendor_cmd.data, p_data->vendor_cmd.len);
#endif
        break;

    case BSA_AVK_VENDOR_RSP_EVT:
        APP_DEBUG0("BSA_AVK_VENDOR_RSP_EVT");
        APP_DEBUG1(" code:0x%x", p_data->vendor_rsp.code);
        APP_DEBUG1(" company_id:0x%x", p_data->vendor_rsp.company_id);
        APP_DEBUG1(" label:0x%x", p_data->vendor_rsp.label);
        APP_DEBUG1(" len:0x%x", p_data->vendor_rsp.len);
#if (defined(BSA_AVK_DUMP_RX_DATA) && (BSA_AVK_DUMP_RX_DATA == TRUE))
        APP_DUMP("A2DP Data", p_data->vendor_rsp.data, p_data->vendor_rsp.len);
#endif
        break;

    case BSA_AVK_CP_INFO_EVT:
        APP_DEBUG0("BSA_AVK_CP_INFO_EVT");
        if(p_data->cp_info.id == BSA_AVK_CP_SCMS_T_ID) {
            switch(p_data->cp_info.info.scmst_flag) {
                case BSA_AVK_CP_SCMS_COPY_NEVER:
                    APP_INFO1(" content protection:0x%x - COPY NEVER", p_data->cp_info.info.scmst_flag);
                    break;
                case BSA_AVK_CP_SCMS_COPY_ONCE:
                    APP_INFO1(" content protection:0x%x - COPY ONCE", p_data->cp_info.info.scmst_flag);
                    break;
                case BSA_AVK_CP_SCMS_COPY_FREE:
                case (BSA_AVK_CP_SCMS_COPY_FREE+1):
                    APP_INFO1(" content protection:0x%x - COPY FREE", p_data->cp_info.info.scmst_flag);
                    break;
                default:
                    APP_ERROR1(" content protection:0x%x - UNKNOWN VALUE", p_data->cp_info.info.scmst_flag);
                    break;
            }
        } else {
            APP_INFO0("No content protection");
        }
        break;

    case BSA_AVK_REGISTER_NOTIFICATION_EVT:
/*
        APP_DEBUG1("BSA_AVK_REGISTER_NOTIFICATION_EVT handle:%d", p_data->reg_notif_rsp.handle);
        APP_DEBUG1("event_id:0x%x, opcode:0x%x, pdu:0x%x\n",
                p_data->reg_notif_rsp.rsp.event_id,
                p_data->reg_notif_rsp.rsp.opcode,
                p_data->reg_notif_rsp.rsp.pdu);
*/
        int index;

        connection = app_avk_find_connection_by_rc_handle(p_data->reg_notif_rsp.handle);
        for (index = 0; index < APP_AVK_MAX_CONNECTIONS; index++) {
            if (app_avk_cb.connections[index].rc_handle == p_data->reg_notif_rsp.handle)
                break;
        }

        if (index == APP_AVK_MAX_CONNECTIONS) {
            APP_INFO0("BSA_AVK_REGISTER_NOTIFICATION_EVT handle can not find connection.");
            break;
        }

        switch(p_data->reg_notif_rsp.rsp.event_id) {
        case AVRC_EVT_TRACK_CHANGE:
            APP_DEBUG0("AVRC_EVT_TRACK_CHANGE");
            app_avk_get_track = TRUE;
            app_avk_rc_get_element_attr_command(connection->rc_handle);

            // get current play status
            //app_avk_rc_get_play_status_command(connection->rc_handle);
            break;

        case AVRC_EVT_PLAY_POS_CHANGED:
            //APP_DEBUG0("AVRC_EVT_PLAY_POS_CHANGED");
            app_avk_pos_change = TRUE;
            if(!app_avk_get_track) {
                //APP_DEBUG1("position: %u", p_data->reg_notif_rsp.rsp.param.play_pos);
                app_avk_send_position(connection->bda_connected, app_avk_duration,
                                        p_data->reg_notif_rsp.rsp.param.play_pos);
            }
            break;

        case AVRC_EVT_VOLUME_CHANGE:
            APP_DEBUG1("Volume changed :0x%x", p_data->reg_notif_rsp.rsp.param.volume);
            break;

        case AVRC_EVT_PLAY_STATUS_CHANGE:
            BOOLEAN do_it = FALSE;

            switch(p_data->reg_notif_rsp.rsp.param.play_status) {
            case AVRC_PLAYSTATE_PLAYING:
                APP_INFO1("Play Status Playing, index: %d", index);
                pthread_mutex_lock(&ps_mutex);
                if (play_state[index] != PLAYED) {
                    do_it = TRUE;
                    play_state[index] = PLAYED;
                }
                pthread_mutex_unlock(&ps_mutex);
                if (do_it) {
                    app_avk_state = RK_BT_SINK_STATE_PLAY;
                    app_avk_send_state(RK_BT_SINK_STATE_PLAY);
                }
                break;

            case AVRC_PLAYSTATE_PAUSED:
            case AVRC_PLAYSTATE_STOPPED:
                //enum APP_AVK_PLAYSTATE previous_state;
                APP_INFO1("Play Status %s, index: %d",
                          p_data->reg_notif_rsp.rsp.param.play_status ==
                            AVRC_PLAYSTATE_PAUSED ? "Paused" : "Stopped",
                          index);
                pthread_mutex_lock(&ps_mutex);
                if (play_state[index] != STOPPED) {
                    do_it = TRUE;
                    //previous_state = play_state[index];
                    play_state[index] = STOPPED;
                }
                pthread_mutex_unlock(&ps_mutex);
#if 0
                if (do_it) {
                    if(previous_state == SEND_STOP) {
                        app_avk_state = RK_BT_SINK_STATE_STOP;
                        app_avk_send_state(RK_BT_SINK_STATE_STOP);
                    } else {
                        app_avk_state = RK_BT_SINK_STATE_PAUSE;
                        app_avk_send_state(RK_BT_SINK_STATE_PAUSE);
                    }
                }
#else
                if (do_it) {
                    if(p_data->reg_notif_rsp.rsp.param.play_status == AVRC_PLAYSTATE_STOPPED) {
                        app_avk_state = RK_BT_SINK_STATE_STOP;
                        app_avk_send_state(RK_BT_SINK_STATE_STOP);
                    } else {
                        app_avk_state = RK_BT_SINK_STATE_PAUSE;
                        app_avk_send_state(RK_BT_SINK_STATE_PAUSE);
                    }
                }
#endif
                break;

            default:
                 APP_DEBUG1("Play Status Playing : %02x",
                     p_data->reg_notif_rsp.rsp.param.play_status);
                 break;
            }

            break;
        }
        break;

    case BSA_AVK_GET_ELEM_ATTR_EVT:
        APP_DEBUG0("BSA_AVK_GET_ELEM_ATTR_EVT");
        APP_INFO1("status:0x%x, num_attr:0x%x, handle:0x%x",
                p_data->elem_attr.status,
                p_data->elem_attr.num_attr,
                p_data->elem_attr.handle);
        if(p_data->elem_attr.num_attr <= BSA_AVK_ELEMENT_ATTR_MAX) {
            //0:title, 1:artist, 2:album, 3:track_num, 4:num_tracks, 5:genre, 6:playing_time
            for(i = 0; i < p_data->elem_attr.num_attr ; i++) {
                //APP_INFO1("attr_id:0x%x", p_data->elem_attr.attr_entry[i].attr_id);
                //APP_INFO1("name:%s", p_data->elem_attr.attr_entry[i].name.data);
            }
        }

        connection = app_avk_find_connection_by_rc_handle(p_data->elem_attr.handle);
        if (connection == NULL)
            break;

        app_avk_track_info_send(connection->bda_connected, p_data->elem_attr);
        break ;

    case BSA_AVK_LIST_PLAYER_APP_ATTR_EVT:
    case BSA_AVK_LIST_PLAYER_APP_VALUES_EVT:
    case BSA_AVK_SET_PLAYER_APP_VALUE_EVT:
    case BSA_AVK_GET_PLAYER_APP_VALUE_EVT:
    case BSA_AVK_SET_ADDRESSED_PLAYER_EVT:
    case BSA_AVK_SET_BROWSED_PLAYER_EVT:
    case BSA_AVK_GET_FOLDER_ITEMS_EVT:
    case BSA_AVK_CHANGE_PATH_EVT:
    case BSA_AVK_GET_ITEM_ATTR_EVT:
    case BSA_AVK_PLAY_ITEM_EVT:
    case BSA_AVK_ADD_TO_NOW_PLAYING_EVT:
        break;

    case BSA_AVK_GET_PLAY_STATUS_EVT:
        APP_DEBUG0("BSA_AVK_GET_PLAY_STATUS_EVT");
        connection = app_avk_find_connection_by_rc_handle(p_data->get_play_status.handle);
        if (connection == NULL)
            break;

        APP_DEBUG1("duration:%u, position:%u, play_status:%u", p_data->get_play_status.rsp.song_len,
                p_data->get_play_status.rsp.song_pos, p_data->get_play_status.rsp.play_status);

        app_avk_send_position(connection->bda_connected,
                                p_data->get_play_status.rsp.song_len,
                                p_data->get_play_status.rsp.song_pos);
        break;

    case BSA_AVK_SET_ABS_VOL_CMD_EVT:
        APP_DEBUG0("BSA_AVK_SET_ABS_VOL_CMD_EVT");

        connection = app_avk_find_connection_by_rc_handle(p_data->abs_volume.handle);
        if((connection == NULL))
            break;

        if(connection->m_bAbsVolumeSupported) {
            /* Peer requested change in volume. Make the change and send response with new system volume. BSA is TG role in AVK */
            if (p_data->abs_volume.abs_volume_cmd.volume <= BSA_MAX_ABS_VOLUME) {
                app_avk_cb.volume = p_data->abs_volume.abs_volume_cmd.volume;
                APP_DEBUG1("app_avk_cb.volume: %d", app_avk_cb.volume);

                /* Change the code below based on which interface audio is going out to. */
                //app_avk_set_master_volume(app_avk_cb.volume);

                app_avk_send_volume((int)app_avk_cb.volume);
                app_avk_set_abs_vol_rsp(app_avk_cb.volume, p_data->abs_volume.handle, p_data->abs_volume.label);
            }
        } else {
            APP_ERROR1("not changing volume m_bAbsVolumeSupported %d", connection->m_bAbsVolumeSupported);
        }
        break;


    case BSA_AVK_REG_NOTIFICATION_CMD_EVT:
        APP_DEBUG1("BSA_AVK_REG_NOTIFICATION_CMD_EVT, event_id: %d", p_data->reg_notif_cmd.reg_notif_cmd.event_id);
        if (p_data->reg_notif_cmd.reg_notif_cmd.event_id == AVRC_EVT_VOLUME_CHANGE) {
            connection = app_avk_find_connection_by_rc_handle(p_data->reg_notif_cmd.handle);
            if(connection != NULL) {
                connection->m_bAbsVolumeSupported = TRUE;
                connection->volChangeLabel = p_data->reg_notif_cmd.label;

                APP_DEBUG1("app_avk_cb.volume: %d", app_avk_cb.volume);
                //app_avk_set_master_volume(app_avk_cb.volume);

                /* Peer requested registration for vol change event. Send response with current system volume. BSA is TG role in AVK */
                app_avk_reg_notfn_rsp(app_avk_cb.volume,
                                  p_data->reg_notif_cmd.handle,
                                  p_data->reg_notif_cmd.label,
                                  p_data->reg_notif_cmd.reg_notif_cmd.event_id,
                                  BSA_AVK_RSP_INTERIM);
            }
        }
        break;

    default:
        APP_ERROR1("Unsupported event %d", event);
        break;
    }

    /* forward the callback to registered applications */
    if(app_avk_cb.p_Callback)
        app_avk_cb.p_Callback(event, p_data);
}

/*******************************************************************************
 **
 ** Function         app_avk_open
 **
 ** Description      Example of function to open AVK connection
 **
 ** Returns          0 success, 1 already trying to connect, -1 failed
 **
 *******************************************************************************/
int app_avk_open(BD_ADDR bd_addr, BD_NAME name)
{
    tBSA_STATUS status;
    tBSA_AVK_OPEN open_param;
    tAPP_AVK_CONNECTION *connection = NULL;

    if (app_avk_cb.open_pending) {
        APP_ERROR0("already trying to connect");
        return 1;
    }

    /* Open AVK stream */
    APP_DEBUG1("Connecting to AV device:%s", name);
    APP_DEBUG1("bd_addr: %02x:%02x:%02x:%02x:%02x:%02x",
                bd_addr[0], bd_addr[1], bd_addr[2],
                bd_addr[3], bd_addr[4], bd_addr[5]);

    app_avk_cb.open_pending = TRUE;
    memcpy(app_avk_cb.open_pending_bda, bd_addr, sizeof(BD_ADDR));

    BSA_AvkOpenInit(&open_param);
    memcpy((char *) (open_param.bd_addr), bd_addr, sizeof(BD_ADDR));

    open_param.sec_mask = BSA_SEC_NONE;
    status = BSA_AvkOpen(&open_param);
    if (status != BSA_SUCCESS) {
        APP_ERROR1("Unable to connect to device %02X:%02X:%02X:%02X:%02X:%02X with status %d",
                open_param.bd_addr[0], open_param.bd_addr[1], open_param.bd_addr[2],
                open_param.bd_addr[3], open_param.bd_addr[4], open_param.bd_addr[5], status);

        app_avk_cb.open_pending = FALSE;
        memset(app_avk_cb.open_pending_bda, 0, sizeof(BD_ADDR));
        return -1;
    } else {
        /* this is an active wait for demo purpose */
        APP_DEBUG0("waiting for AV connection to open");

#if 0
        while (app_avk_cb.open_pending == TRUE);
#else
        GKI_delay(3000);
        if(app_avk_cb.open_pending == TRUE) {
            APP_ERROR0("after 3 seconds, app_avk_cback not return BSA_AVK_OPEN_EVT");
            app_avk_cb.open_pending = FALSE;
            memset(app_avk_cb.open_pending_bda, 0, sizeof(BD_ADDR));
            return -1;
        }
#endif

        connection = app_avk_find_connection_by_bd_addr(open_param.bd_addr);
        if(connection == NULL || connection->is_open == FALSE) {
            APP_DEBUG0("failure opening AV connection");
            return -1;
        } else {
            /* XML Database update should be done upon reception of AV OPEN event */
            APP_DEBUG1("Connected to AV device:%s", name);
            /* Read the Remote device xml file to have a fresh view */
            app_read_xml_remote_devices();

            /* Add AV service for this devices in XML database */
            app_xml_add_trusted_services_db(app_xml_remote_devices_db,
                APP_NUM_ELEMENTS(app_xml_remote_devices_db), bd_addr,
                BSA_A2DP_SERVICE_MASK | BSA_AVRCP_SERVICE_MASK);

            app_xml_update_name_db(app_xml_remote_devices_db,
                APP_NUM_ELEMENTS(app_xml_remote_devices_db), bd_addr, name);

            /* Update database => write to disk */
            if (app_write_xml_remote_devices() < 0) {
                APP_ERROR0("Failed to store remote devices database");
            }
        }
    }

    return 0;
}

/*******************************************************************************
 **
 ** Function         app_avk_close
 **
 ** Description      Function to close AVK connection
 **
 ** Returns          void
 **
 *******************************************************************************/
int app_avk_close(BD_ADDR bda)
{
    tBSA_STATUS status;
    tBSA_AVK_CLOSE bsa_avk_close_param;
    tAPP_AVK_CONNECTION *connection = NULL;

    /* Close AVK connection */
    BSA_AvkCloseInit(&bsa_avk_close_param);

    APP_DEBUG1("bd_addr: %02x:%02x:%02x:%02x:%02x:%02x",
                bda[0], bda[1], bda[2],
                bda[3], bda[4], bda[5]);

    connection = app_avk_find_connection_by_bd_addr(bda);
    if(connection == NULL) {
        APP_ERROR0("Unable to Close AVK connection , invalid BDA");
        return -1;
    }

    bsa_avk_close_param.ccb_handle = connection->ccb_handle;
    bsa_avk_close_param.rc_handle = connection->rc_handle;

    APP_DEBUG1("ccb_handle: %d", connection->ccb_handle);
    APP_DEBUG1("rc_handle: %d", connection->rc_handle);

    status = BSA_AvkClose(&bsa_avk_close_param);
    if (status != BSA_SUCCESS) {
        APP_ERROR1("Unable to Close AVK connection with status %d", status);
        return -1;
    }

    return 0;
}

/*******************************************************************************
 **
 ** Function         app_avk_close_all
 **
 ** Description      Function to close all AVK connections
 **
 ** Returns          void
 **
 *******************************************************************************/
void app_avk_close_all()
{
    int index;
    for (index = 0; index < APP_AVK_MAX_CONNECTIONS; index++) {
        if (app_avk_cb.connections[index].in_use == TRUE) {
            app_avk_close(app_avk_cb.connections[index].bda_connected);
        }
    }
}

/*******************************************************************************
**
** Function         app_avk_open_rc
**
** Description      Function to opens avrc controller connection. AVK should be open before opening rc.
**
** Returns          void
**
*******************************************************************************/
void app_avk_open_rc(BD_ADDR bd_addr)
{
    tBSA_STATUS status;
    tBSA_AVK_OPEN bsa_avk_open_param;

    /* open avrc connection */
    BSA_AvkOpenRcInit(&bsa_avk_open_param);
    bdcpy(bsa_avk_open_param.bd_addr, bd_addr);
    status = BSA_AvkOpenRc(&bsa_avk_open_param);
    if (status != BSA_SUCCESS)
    {
        APP_ERROR1("Unable to open arvc connection with status %d", status);
    }

}

/*******************************************************************************
**
** Function         app_avk_close_rc
**
** Description      Function to closes avrc controller connection.
**
** Returns          void
**
*******************************************************************************/
void app_avk_close_rc(UINT8 rc_handle)
{
    tBSA_STATUS status;
    tBSA_AVK_CLOSE bsa_avk_close_param;

    /* close avrc connection */
    BSA_AvkCloseRcInit(&bsa_avk_close_param);

    bsa_avk_close_param.rc_handle = rc_handle;

    status = BSA_AvkCloseRc(&bsa_avk_close_param);
    if (status != BSA_SUCCESS)
    {
        APP_ERROR1("Unable to close arvc connection with status %d", status);
    }

}

/*******************************************************************************
**
** Function         app_avk_close_str
**
** Description      Function to close an A2DP Steam connection
**
** Returns          void
**
*******************************************************************************/
void app_avk_close_str(UINT8 ccb_handle)
{
    tBSA_STATUS status;
    tBSA_AVK_CLOSE_STR bsa_avk_close_str_param;

    /* close avrc connection */
    BSA_AvkCloseStrInit(&bsa_avk_close_str_param);

    bsa_avk_close_str_param.ccb_handle = ccb_handle;

    status = BSA_AvkCloseStr(&bsa_avk_close_str_param);
    if (status != BSA_SUCCESS)
    {
        APP_ERROR1("Unable to close arvc connection with status %d", status);
    }

}

/*******************************************************************************
**
** Function        app_avk_register
**
** Description     Example of function to register an avk endpoint
**
** Returns         void
**
*******************************************************************************/
int app_avk_register(void)
{
    tBSA_STATUS status;
    tBSA_AVK_REGISTER bsa_avk_register_param;

    /* register an audio AVK end point */
    BSA_AvkRegisterInit(&bsa_avk_register_param);

    bsa_avk_register_param.media_sup_format.audio.pcm_supported = TRUE;
    bsa_avk_register_param.media_sup_format.audio.sec_supported = TRUE;
#if (defined(BSA_AVK_AAC_SUPPORTED) && (BSA_AVK_AAC_SUPPORTED==TRUE))
    /* Enable AAC support in the app */
    bsa_avk_register_param.media_sup_format.audio.aac_supported = TRUE;
#endif
    strncpy(bsa_avk_register_param.service_name, "BSA Audio Service",
        BSA_AVK_SERVICE_NAME_LEN_MAX);

    bsa_avk_register_param.reg_notifications = reg_notifications;

    status = BSA_AvkRegister(&bsa_avk_register_param);
    if (status != BSA_SUCCESS)
    {
        APP_ERROR1("Unable to register an AV sink with status %d", status);

        if(BSA_ERROR_SRV_AVK_AV_REGISTERED == status)
        {
            APP_ERROR0("AV was registered before AVK, AVK should be registered before AV");
        }
        return -1;
    }
    /* Save UIPC channel */
    app_avk_cb.uipc_audio_channel = bsa_avk_register_param.uipc_channel;

    /* open the UIPC channel to receive the pcm */
    if (UIPC_Open(bsa_avk_register_param.uipc_channel, app_avk_uipc_cback) == FALSE)
    {
        APP_ERROR0("Unable to open UIPC channel");
        return -1;
    }

    app_avk_cb.fd = -1;

    return 0;

}

/*******************************************************************************
 **
 ** Function        app_avk_deregister
 **
 ** Description     Example of function to deregister an avk endpoint
 **
 ** Returns         void
 **
 *******************************************************************************/
void app_avk_deregister(void)
{
    tBSA_STATUS status;
    tBSA_AVK_DEREGISTER req;

    /* register an audio AVK end point */
    BSA_AvkDeregisterInit(&req);
    status = BSA_AvkDeregister(&req);
    if (status != BSA_SUCCESS)
    {
        APP_ERROR1("Unable to deregister an AV sink with status %d", status);
    }

#ifdef TODO
    if (app_avk_cb.format == BSA_AVK_CODEC_M24)
    {
        close(app_avk_cb.fd);
        app_avk_cb.fd  = -1;
    }
    else
        app_avk_close_wave_file();
#endif

    /* close the UIPC channel receiving the pcm */
    tUIPC_CH_ID chn = app_avk_cb.uipc_audio_channel;
    app_avk_cb.uipc_audio_channel = UIPC_CH_ID_BAD;
    UIPC_Close(chn);
}

/*******************************************************************************
**
** Function        app_avk_uipc_cback
**
** Description     uipc audio call back function.
**
** Parameters      pointer on a buffer containing PCM sample with a BT_HDR header
**
** Returns          void
**
*******************************************************************************/
static void app_avk_uipc_cback(BT_HDR *p_msg)
{
#ifdef PCM_ALSA
    snd_pcm_sframes_t alsa_frames;
    snd_pcm_sframes_t alsa_frames_to_send;
#endif
    UINT8 *p_buffer;
    int dummy;
    tAPP_AVK_CONNECTION *connection = NULL;

    if (p_msg == NULL)
    {
        return;
    }

    p_buffer = ((UINT8 *)(p_msg + 1)) + p_msg->offset;

    connection = app_avk_find_streaming_connection();

    if(!connection)
    {
        if(app_avk_cb.uipc_audio_channel != UIPC_CH_ID_BAD)
        APP_ERROR0("Unable to find connection!!!!!!");
        GKI_freebuf(p_msg);
        return;
    }

    if (connection->format == BSA_AVK_CODEC_M24)
    {
        /* Invoke AAC decoder here for the current buffer.
            decode the AAC file that is created */
        if (app_avk_cb.fd != -1)
        {
            dummy = write(app_avk_cb.fd, p_buffer, p_msg->len);
            (void)dummy;
        }

        GKI_freebuf(p_msg);
        return;
    }

    if (connection->format == BSA_AVK_CODEC_PCM &&
            app_avk_cb.fd_codec_type != BSA_AVK_CODEC_M24)
    {
        /* Invoke AAC decoder here for the current buffer.
            decode the AAC file that is created */
        if (app_avk_cb.fd != -1)
        {
#if (defined(BSA_AVK_DUMP_RX_DATA) && (BSA_AVK_DUMP_RX_DATA == TRUE))
            APP_DEBUG1("app_avk_uipc_cback Writing Data 0x%x, len = %d", p_buffer, p_msg->len);
#endif
            dummy = write(app_avk_cb.fd, p_buffer, p_msg->len);
            (void)dummy;
        }

#if (defined(BSA_AVK_DUMP_RX_DATA) && (BSA_AVK_DUMP_RX_DATA == TRUE))
        APP_DUMP("A2DP Data", p_buffer, p_msg->len);
#endif
    }

    if(connection->num_channel == 0)
    {
        APP_ERROR0("connection->num_channel == 0");
        GKI_freebuf(p_msg);
        return;
    }

#ifdef PCM_ALSA
    if (app_avk_cb.alsa_handle != NULL && p_buffer)
    {
        /* Compute number of PCM samples (contained in p_msg->len bytes) */
        alsa_frames_to_send = p_msg->len / connection->num_channel;
        if (connection->bit_per_sample == 16)
            alsa_frames_to_send /= 2;

#ifdef PCM_ALSA_OPEN_BLOCKING
        while (alsa_frames_to_send > 0)
        {
            pthread_mutex_lock(&mutex);
            if (!app_avk_cb.alsa_handle)
            {
                pthread_mutex_unlock(&mutex);
                break;
            }

            alsa_frames = snd_pcm_writei(app_avk_cb.alsa_handle, p_buffer, alsa_frames_to_send);
            if (alsa_frames < 0)
            {
                if (alsa_frames == -EPIPE)
                {
                    APP_DEBUG0("ALSA: underrun in frame");
                    snd_pcm_prepare(app_avk_cb.alsa_handle);
                    alsa_frames = 0;
                }
                else if (alsa_frames == -EBADFD)
                {
                    APP_DEBUG0("ALSA: retry");
                    alsa_frames = 0;
                }
                else
                {
                    APP_DEBUG1("ALSA: snd_pcm_writei err %d (%s)", (int) alsa_frames, snd_strerror(alsa_frames));
                    pthread_mutex_unlock(&mutex);
                    break;
                }
            }

            pthread_mutex_unlock(&mutex);
            alsa_frames_to_send -= alsa_frames;
        }
        if (alsa_frames_to_send)
            APP_DEBUG1("ALSA: short write (discarded %li)", alsa_frames_to_send);
    }
#else
        alsa_frames = snd_pcm_writei(app_avk_cb.alsa_handle, p_buffer, alsa_frames_to_send);
        if (alsa_frames < 0)
        {
            alsa_frames = snd_pcm_recover(app_avk_cb.alsa_handle, alsa_frames, 0);
        }
        if (alsa_frames < 0)
        {
            APP_DEBUG1("app_avk_uipc_cback snd_pcm_writei failed %s", snd_strerror(alsa_frames));
        }
        if (alsa_frames > 0 && alsa_frames < alsa_frames_to_send)
            APP_DEBUG1("app_avk_uipc_cback Short write (expected %li, wrote %li)",
                (long) alsa_frames_to_send, alsa_frames);
    }
    else
        APP_DEBUG0("app_avk_uipc_cback snd_pcm_writei failed FINALLY !!");
#endif /* PCM_ALSA_OPEN_BLOCKING */
#endif /* PCM_ALSA */
    GKI_freebuf(p_msg);
}

/*******************************************************************************
**
** Function         app_avk_init
**
** Description      Init Manager application
**
** Parameters       Application callback (if null, default will be used)
**
** Returns          0 if successful, error code otherwise
**
*******************************************************************************/
int app_avk_init(tBSA_AVK_CBACK pcb)
{
    tBSA_AVK_ENABLE bsa_avk_enable_param;
    tBSA_STATUS status;
    int i = 0;

    /* Initialize the control structure */
    memset(&app_avk_cb, 0, sizeof(app_avk_cb));
    app_avk_cb.uipc_audio_channel = UIPC_CH_ID_BAD;

    app_avk_cb.fd = -1;

    /* register callback */
    app_avk_cb.p_Callback = pcb;

    /* set sytem vol at 50% */
    app_avk_cb.volume = (UINT8)((BSA_MAX_ABS_VOLUME - BSA_MIN_ABS_VOLUME)>>1);

    for(;i < APP_AVK_MAX_CONNECTIONS; i++)
        play_state[i] = STOPPED;

    /* get hold on the AVK resource, synchronous mode */
    BSA_AvkEnableInit(&bsa_avk_enable_param);

    bsa_avk_enable_param.sec_mask = BSA_AVK_SECURITY;
    bsa_avk_enable_param.features = BSA_AVK_FEATURES;
    bsa_avk_enable_param.p_cback = app_avk_cback;

    status = BSA_AvkEnable(&bsa_avk_enable_param);
    if (status != BSA_SUCCESS)
    {
        APP_ERROR1("Unable to enable an AV sink with status %d", status);

        if(BSA_ERROR_SRV_AVK_AV_REGISTERED == status)
        {
            APP_ERROR0("AV was registered before AVK, AVK should be registered before AV");
        }

        return -1;
    }

    return BSA_SUCCESS;

}

/*******************************************************************************
**
** Function         app_avk_deinit
**
** Description      Disable AVK application
**
** Returns          void
**
*******************************************************************************/
void app_avk_deinit()
{
    tBSA_AVK_DISABLE disable_param;
    tBSA_STATUS status;

    /* disable avk */
    BSA_AvkDisableInit(&disable_param);
    status = BSA_AvkDisable(&disable_param);
    if (status != BSA_SUCCESS) {
        APP_ERROR1("Unable to disable an AV sink with status %d", status);
    }
}

/*******************************************************************************
**
** Function         app_avk_rc_send_command
**
** Description      Example of Send a RemoteControl command
**
** Returns          void
**
*******************************************************************************/
void app_avk_rc_send_command(UINT8 key_state, UINT8 command, UINT8 rc_handle)
{
    int status;
    tBSA_AVK_REM_CMD bsa_avk_rc_cmd;

    /* Send remote control command */
    status = BSA_AvkRemoteCmdInit(&bsa_avk_rc_cmd);
    bsa_avk_rc_cmd.rc_handle = rc_handle;
    bsa_avk_rc_cmd.key_state = (tBSA_AVK_STATE)key_state;
    bsa_avk_rc_cmd.rc_id = (tBSA_AVK_RC)command;
    bsa_avk_rc_cmd.label = app_avk_get_label();

    status = BSA_AvkRemoteCmd(&bsa_avk_rc_cmd);
    if (status != BSA_SUCCESS)
    {
        APP_ERROR1("Unable to Send AV RC Command %d", status);
    }
}

/*******************************************************************************
**
** Function         app_avk_rc_send_click
**
** Description      Send press and release states of a command
**
** Returns          void
**
*******************************************************************************/
void app_avk_rc_send_click(UINT8 command, UINT8 rc_handle)
{
    app_avk_rc_send_command(BSA_AV_STATE_PRESS, command, rc_handle);
    GKI_delay(300);
    app_avk_rc_send_command(BSA_AV_STATE_RELEASE, command, rc_handle);
}

/*******************************************************************************
 **
 ** Function         app_avk_rc_send_cmd
 **
 ** Description      Example of send avrcp commands
 **
 ** Returns          void
 **
 *******************************************************************************/
void app_avk_rc_send_cmd(int command)
{
    int index;
    tAPP_AVK_CONNECTION *conn = NULL;
    BOOLEAN do_it = FALSE;

    int num_conn = app_avk_num_connections();

    if(num_conn == 0) {
        APP_INFO0("No connections");
        return;
    }

    for (index = 0; index < APP_AVK_MAX_CONNECTIONS; index++) {
        conn = app_avk_find_connection_by_index(index);
        if(conn->in_use) {
            APP_INFO1("index: %d\n", index);
            APP_INFO1("Connection index %d:%02X:%02X:%02X:%02X:%02X:%02X",
                index,
                conn->bda_connected[0], conn->bda_connected[1],
                conn->bda_connected[2], conn->bda_connected[3],
                conn->bda_connected[4], conn->bda_connected[5]);

            switch (command) {
            case APP_AVK_PLAY_START:
                APP_INFO0("AVRCP PLAY");
                pthread_mutex_lock(&ps_mutex);
                if (play_state[index] != PLAYED && play_state[index] != SEND_PLAY) {
                    do_it = TRUE;
                    play_state[index] = SEND_PLAY;
                }
                pthread_mutex_unlock(&ps_mutex);
                if (do_it)
                    app_avk_play_start(conn->rc_handle);
                break;

            case APP_AVK_PLAY_STOP:
                APP_INFO0("AVRCP STOP");
                pthread_mutex_lock(&ps_mutex);
                if (play_state[index] != STOPPED && play_state[index] != SEND_STOP) {
                    do_it = TRUE;
                    play_state[index] = SEND_STOP;
                }
                pthread_mutex_unlock(&ps_mutex);
                if (do_it)
                    app_avk_play_stop(conn->rc_handle);
                break;

            case APP_AVK_PLAY_PAUSE:
                APP_INFO0("AVRCP PAUSE");
                pthread_mutex_lock(&ps_mutex);
                if (play_state[index] != STOPPED && play_state[index] != SEND_PAUSE) {
                    do_it = TRUE;
                    play_state[index] = SEND_PAUSE;
                }
                pthread_mutex_unlock(&ps_mutex);
                if (do_it)
                    app_avk_play_pause(conn->rc_handle);
                break;

            case APP_AVK_VOLUME_UP:
                APP_INFO0("AVRCP VOLUME UP");
                app_avk_volume_up(conn->rc_handle);
                break;

            case APP_AVK_VOLUME_DOWN:
                APP_INFO0("AVRCP VOLUME DOWN");
                app_avk_volume_down(conn->rc_handle);
                break;

            case APP_AVK_PLAY_NEXT_TRACK:
                APP_INFO0("AVRCP NEXT TRACK");
                app_avk_play_next_track(conn->rc_handle);
                break;

            case APP_AVK_PLAY_PREVIOUS_TRACK:
                APP_INFO0("AVRCP PREVIOUS TRACK");
                app_avk_play_previous_track(conn->rc_handle);
                break;
            }
        }
    }
}

/*******************************************************************************
 **
 ** Function         app_avk_volume_up
 **
 ** Description      Example of set volume
 **
 ** Returns          void
 **
 *******************************************************************************/
int app_avk_set_volume(int volume)
{
    int index;
    tAPP_AVK_CONNECTION *connection = NULL;

    if(volume < 0 || volume > BSA_MAX_ABS_VOLUME) {
        APP_ERROR1("Invalid value(%d), should within [0, 0x7F]", volume);
        return -1;
    }

    for (index = 0; index < APP_AVK_MAX_CONNECTIONS; index++) {
        connection = app_avk_find_connection_by_index(index);
        if(connection->in_use) {
            if (connection->is_rc_open) {
                APP_DEBUG1("connection->m_bAbsVolumeSupported: %d", connection->m_bAbsVolumeSupported);
                if (connection->m_bAbsVolumeSupported) {
                    app_avk_cb.volume = volume;
                    APP_DEBUG1("app_avk_cb.volume: %d", app_avk_cb.volume);

                    /* send abs vol change notification */
                    app_avk_reg_notfn_rsp(app_avk_cb.volume, connection->rc_handle,
                        connection->volChangeLabel, AVRC_EVT_VOLUME_CHANGE, BSA_AVK_RSP_CHANGED);
                }
            } else {
                APP_ERROR1("Unable to send AVRC command, is support RCTG %d, is rc open %d",
                    (connection->peer_features & BSA_AVK_FEAT_RCCT), connection->is_rc_open);
            }
        }
    }

    return 0;
}

/*******************************************************************************
 **
 ** Function         app_avk_volume_up
 **
 ** Description      Example of volume up
 **
 ** Returns          void
 **
 *******************************************************************************/
void app_avk_volume_up(UINT8 rc_handle)
{
    tAPP_AVK_CONNECTION *connection = NULL;
    connection = app_avk_find_connection_by_rc_handle(rc_handle);
    if(connection == NULL)
    {
        APP_DEBUG1("could not find connection handle %d", rc_handle);
        return;
    }

    if (connection->is_rc_open)
    {
        APP_DEBUG1("connection->m_bAbsVolumeSupported: %d", connection->m_bAbsVolumeSupported);
        /* If abs vol is not supported, send vol pass thru command */
        if (connection->m_bAbsVolumeSupported == FALSE) {
            app_avk_rc_send_click(BSA_AVK_RC_VOL_UP, rc_handle);
        } else {
            UINT8 vol = app_avk_cb.volume + (UINT8)(BSA_MAX_ABS_VOLUME/BSA_ABS_VOLUME_STEP);
            app_avk_cb.volume = (vol <= BSA_MAX_ABS_VOLUME) ? vol : BSA_MAX_ABS_VOLUME;
            APP_DEBUG1("app_avk_cb.volume: %d", app_avk_cb.volume);

            /* send abs vol change notification */
            app_avk_reg_notfn_rsp(app_avk_cb.volume, rc_handle,
                connection->volChangeLabel, AVRC_EVT_VOLUME_CHANGE, BSA_AVK_RSP_CHANGED);
        }
    }
    else
    {
        APP_ERROR1("Unable to send AVRC command, is support RCTG %d, is rc open %d",
            (connection->peer_features & BSA_AVK_FEAT_RCCT), connection->is_rc_open);
    }
}

/*******************************************************************************
 **
 ** Function         app_avk_volume_down
 **
 ** Description      Example of volume down
 **
 ** Returns          void
 **
 *******************************************************************************/
void app_avk_volume_down(UINT8 rc_handle)
{
    tAPP_AVK_CONNECTION *connection = NULL;
    connection = app_avk_find_connection_by_rc_handle(rc_handle);
    if(connection == NULL)
    {
        APP_DEBUG1("could not find connection handle %d", rc_handle);
        return;
    }

    if (connection->is_rc_open)
    {
        APP_DEBUG1("connection->m_bAbsVolumeSupported: %d", connection->m_bAbsVolumeSupported);
        /* If abs vol is not supported, send vol pass thru command */
        if (connection->m_bAbsVolumeSupported == FALSE) {
            app_avk_rc_send_click(BSA_AVK_RC_VOL_DOWN, rc_handle);
        } else {
            UINT8 voldown = (UINT8)(BSA_MAX_ABS_VOLUME/BSA_ABS_VOLUME_STEP);
            app_avk_cb.volume = (app_avk_cb.volume <= voldown) ? BSA_MIN_ABS_VOLUME : app_avk_cb.volume-voldown;
            APP_DEBUG1("app_avk_cb.volume: %d", app_avk_cb.volume);

            /* send abs vol change notification */
            app_avk_reg_notfn_rsp(app_avk_cb.volume, rc_handle,
                connection->volChangeLabel, AVRC_EVT_VOLUME_CHANGE, BSA_AVK_RSP_CHANGED);
        }
    }
    else
    {
        APP_ERROR1("Unable to send AVRC command, is support RCTG %d, is rc open %d",
            (connection->peer_features & BSA_AVK_FEAT_RCCT), connection->is_rc_open);
    }
}

/*******************************************************************************
**
** Function         app_avk_play_start
**
** Description      Example of start steam play
**
** Returns          void
**
*******************************************************************************/
void app_avk_play_start(UINT8 rc_handle)
{
    tAPP_AVK_CONNECTION *connection = NULL;
    connection = app_avk_find_connection_by_rc_handle(rc_handle);
    if(connection == NULL)
    {
        APP_DEBUG1("app_avk_play_start could not find connection handle %d", rc_handle);
        return;
    }

    if ((connection->peer_features & BSA_AVK_FEAT_RCCT) && connection->is_rc_open)
    {
        app_avk_rc_send_click(BSA_AVK_RC_PLAY, rc_handle);
    }
    else
    {
        APP_ERROR1("Unable to send AVRC command, is support RCTG %d, is rc open %d",
            (connection->peer_features & BSA_AVK_FEAT_RCCT), connection->is_rc_open);
    }
}

/*******************************************************************************
 **
 ** Function         app_avk_play_stop
 **
 ** Description      Example of stop steam play
 **
 ** Returns          void
 **
 *******************************************************************************/
void app_avk_play_stop(UINT8 rc_handle)
{
    tAPP_AVK_CONNECTION *connection = NULL;
    connection = app_avk_find_connection_by_rc_handle(rc_handle);
    if(connection == NULL)
    {
        APP_DEBUG1("app_avk_play_stop could not find connection handle %d", rc_handle);
        return;
    }

    if ((connection->peer_features & BSA_AVK_FEAT_RCCT) && connection->is_rc_open)
    {
        app_avk_rc_send_click(BSA_AVK_RC_STOP, rc_handle);
    }
    else
    {
        APP_ERROR1("Unable to send AVRC command, is support RCTG %d, is rc open %d",
            (connection->peer_features & BSA_AVK_FEAT_RCCT), connection->is_rc_open);
    }
}


/*******************************************************************************
 **
 ** Function         app_avk_play_pause
 **
 ** Description      Example of pause steam pause
 **
 ** Returns          void
 **
 *******************************************************************************/
void app_avk_play_pause(UINT8 rc_handle)
{
    tAPP_AVK_CONNECTION *connection = NULL;
    connection = app_avk_find_connection_by_rc_handle(rc_handle);
    if(connection == NULL)
    {
        APP_DEBUG1("app_avk_play_pause could not find connection handle %d", rc_handle);
        return;
    }

    if ((connection->peer_features & BSA_AVK_FEAT_RCCT) && connection->is_rc_open)
    {
        app_avk_rc_send_click(BSA_AVK_RC_PAUSE, rc_handle);
    }
    else
    {
        APP_ERROR1("Unable to send AVRC command, is support RCTG %d, is rc open %d",
                    (connection->peer_features & BSA_AVK_FEAT_RCCT), connection->is_rc_open);
    }
}


/*******************************************************************************
 **
 ** Function         app_avk_play_next_track
 **
 ** Description      Example of play next track
 **
 ** Returns          void
 **
 *******************************************************************************/
void app_avk_play_next_track(UINT8 rc_handle)
{
    tAPP_AVK_CONNECTION *connection = NULL;
    connection = app_avk_find_connection_by_rc_handle(rc_handle);
    if(connection == NULL)
    {
        APP_DEBUG1("app_avk_play_pause could not find connection handle %d", rc_handle);
        return;
    }

    if ((connection->peer_features & BSA_AVK_FEAT_RCCT) && connection->is_rc_open)
    {
        app_avk_rc_send_click(BSA_AVK_RC_FORWARD, rc_handle);
    }
    else
    {
        APP_ERROR1("Unable to send AVRC command, is support RCTG %d, is rc open %d",
                    (connection->peer_features & BSA_AVK_FEAT_RCCT), connection->is_rc_open);
    }
}


/*******************************************************************************
 **
 ** Function         app_avk_play_previous_track
 **
 ** Description      Example of play previous track
 **
 ** Returns          void
 **
 *******************************************************************************/
void app_avk_play_previous_track(UINT8 rc_handle)
{
    tAPP_AVK_CONNECTION *connection = NULL;
    connection = app_avk_find_connection_by_rc_handle(rc_handle);
    if(connection == NULL)
    {
        APP_DEBUG1("app_avk_play_pause could not find connection handle %d", rc_handle);
        return;
    }

    if ((connection->peer_features & BSA_AVK_FEAT_RCCT) && connection->is_rc_open)
    {
        app_avk_rc_send_click(BSA_AVK_RC_BACKWARD, rc_handle);
    }
    else
    {
        APP_ERROR1("Unable to send AVRC command, is support RCTG %d, is rc open %d",
                    (connection->peer_features & BSA_AVK_FEAT_RCCT), connection->is_rc_open);
    }
}


/*******************************************************************************
 **
 ** Function         app_avk_rc_cmd
 **
 ** Description      Example of function to open AVK connection
 **
 ** Returns          void
 **
 *******************************************************************************/
void app_avk_rc_cmd(UINT8 rc_handle)
{
    int choice;
    int value;

    do
    {
        APP_DEBUG0("Bluetooth AVK AVRC CMD menu:");
        APP_DEBUG0("    0 play");
        APP_DEBUG0("    1 stop");
        APP_DEBUG0("    2 pause");
        APP_DEBUG0("    3 forward");
        APP_DEBUG0("    4 backward");
        APP_DEBUG0("    5 angle");
        APP_DEBUG0("    6 rewind key_press");
        APP_DEBUG0("    7 rewind key_release");
        APP_DEBUG0("    8 fast forward key_press");
        APP_DEBUG0("    9 fast forward key_release");
        APP_DEBUG0("    10 eject key press");
        APP_DEBUG0("    11 eject key release");
        APP_DEBUG0("    12 subpicture");
        APP_DEBUG0("    13 0 key");
        APP_DEBUG0("    14 1 key");
        APP_DEBUG0("    15 2 key");
        APP_DEBUG0("    16 3 key");
        APP_DEBUG0("    17 4 key");
        APP_DEBUG0("    18 5 key");
        APP_DEBUG0("    19 mute key");
        APP_DEBUG0("    20 raw value");
        APP_DEBUG0("    99 exit");

        choice = app_get_choice("Select source");

        switch(choice)
        {
            case 0:
                app_avk_rc_send_click(BSA_AVK_RC_PLAY, rc_handle);
                break;

            case 1:
                app_avk_rc_send_click(BSA_AVK_RC_STOP, rc_handle);
                break;

            case 2:
                app_avk_rc_send_click(BSA_AVK_RC_PAUSE, rc_handle);
                break;

            case 3:
                app_avk_rc_send_click(BSA_AVK_RC_FORWARD, rc_handle);
                break;

            case 4:
                app_avk_rc_send_click(BSA_AVK_RC_BACKWARD, rc_handle);
                break;

            case 5:
                app_avk_rc_send_click(BSA_AVK_RC_ANGLE, rc_handle);
                break;

            case 6:
                app_avk_rc_send_command(BSA_AV_STATE_PRESS, BSA_AVK_RC_REWIND, rc_handle);
                break;

            case 7:
                app_avk_rc_send_command(BSA_AV_STATE_RELEASE, BSA_AVK_RC_REWIND, rc_handle);
                break;

            case 8:
                app_avk_rc_send_command(BSA_AV_STATE_PRESS, BSA_AVK_RC_FAST_FOR, rc_handle);
                break;

            case 9:
                app_avk_rc_send_command(BSA_AV_STATE_RELEASE, BSA_AVK_RC_FAST_FOR, rc_handle);
                break;

            case 10:
                app_avk_rc_send_command(BSA_AV_STATE_PRESS, BSA_AVK_RC_EJECT, rc_handle);
                break;

            case 11:
                app_avk_rc_send_command(BSA_AV_STATE_RELEASE, BSA_AVK_RC_EJECT, rc_handle);
                break;

            case 12:
                app_avk_rc_send_click(BSA_AVK_RC_SUBPICT, rc_handle);
                break;

            case 13:
                app_avk_rc_send_click(BSA_AVK_RC_0, rc_handle);
                break;

            case 14:
                app_avk_rc_send_click(BSA_AVK_RC_1, rc_handle);
                break;

            case 15:
                app_avk_rc_send_click(BSA_AVK_RC_2, rc_handle);
                break;

            case 16:
                app_avk_rc_send_click(BSA_AVK_RC_3, rc_handle);
                break;

            case 17:
                app_avk_rc_send_click(BSA_AVK_RC_4, rc_handle);
                break;

            case 18:
                app_avk_rc_send_click(BSA_AVK_RC_5, rc_handle);
                break;

            case 19:
                app_avk_rc_send_click(BSA_AVK_RC_MUTE, rc_handle);
                break;

            case 20:
                value = app_get_choice("value to press");
                app_avk_rc_send_click(value, rc_handle);
                break;

            default:
                APP_ERROR0("Unsupported choice");
            break;
        }
    } while (choice != 99);
}


/*******************************************************************************
 **
 ** Function         app_avk_send_get_element_attributes_cmd
 **
 ** Description      Example of function to send Vendor Dependent Command
 **
 ** Returns          void
 **
 *******************************************************************************/
void app_avk_send_get_element_attributes_cmd(UINT8 rc_handle)
{
    int status;
    tBSA_AVK_VEN_CMD bsa_avk_vendor_cmd;

    /* Send remote control command */
    status = BSA_AvkVendorCmdInit(&bsa_avk_vendor_cmd);

    bsa_avk_vendor_cmd.rc_handle = rc_handle;
    bsa_avk_vendor_cmd.ctype = BSA_AVK_CMD_STATUS;
    bsa_avk_vendor_cmd.data[0] = BSA_AVK_RC_VD_GET_ELEMENT_ATTR;
    bsa_avk_vendor_cmd.data[1] = 0; /* reserved & packet type */
    bsa_avk_vendor_cmd.data[2] = 0; /* length high*/
    bsa_avk_vendor_cmd.data[3] = 0x11; /* length low*/

    /* data 4 ~ 11 are 0, which means "identifier 0x0 PLAYING" */

    bsa_avk_vendor_cmd.data[12] = 2; /* num of attributes */

    /* data 13 ~ 16 are 0x1, which means "attribute ID 1 : Title of media" */
    bsa_avk_vendor_cmd.data[16] = 0x1;

    /* data 17 ~ 20 are 0x7, which means "attribute ID 2 : Playing Time" */
    bsa_avk_vendor_cmd.data[20] = 0x7;

    bsa_avk_vendor_cmd.length = 21;
    bsa_avk_vendor_cmd.label = app_avk_get_label(); /* Just used to distinguish commands */

    status = BSA_AvkVendorCmd(&bsa_avk_vendor_cmd);
    if (status != BSA_SUCCESS)
    {
        APP_ERROR1("Unable to Send AV Vendor Command %d", status);
    }
}

/*******************************************************************************
**
** Function         avk_is_open_pending
**
** Description      Check if AVK Open is pending
**
** Parameters
**
** Returns          TRUE if open is pending, FALSE otherwise
**
*******************************************************************************/
BOOLEAN avk_is_open_pending()
{
    return app_avk_cb.open_pending;
}

/*******************************************************************************
**
** Function         avk_set_open_pending
**
** Description      Set AVK open pending
**
** Parameters
**
** Returns          void
**
*******************************************************************************/
void avk_set_open_pending(BOOLEAN bopenpend)
{
    app_avk_cb.open_pending = bopenpend;
}

/*******************************************************************************
**
** Function         avk_is_open
**
** Description      Check if AVK is open
**
** Parameters       BDA of device to check
**
** Returns          TRUE if AVK is open, FALSE otherwise.
**
*******************************************************************************/
BOOLEAN avk_is_open(BD_ADDR bda)
{
    int index;
    for (index = 0; index < APP_AVK_MAX_CONNECTIONS; index++)
    {
        if ((app_avk_cb.connections[index].in_use == TRUE) &&
                (app_avk_cb.connections[index].is_open == TRUE))
    {
            if(bdcmp(bda, app_avk_cb.connections[index].bda_connected) == 0)
                return TRUE;
        }
    }

    return FALSE;
}

/*******************************************************************************
**
** Function         avk_is_any_open
**
** Description      Check if any AVK is open
**
** Parameters
**
** Returns          TRUE if AVK is open, FALSE otherwise. Return BDA of the connected device
**
*******************************************************************************/
BOOLEAN avk_is_any_open(BD_ADDR bda /* out */)
{
    int index;
    for (index = 0; index < APP_AVK_MAX_CONNECTIONS; index++)
    {
        if ((app_avk_cb.connections[index].in_use == TRUE) &&
                (app_avk_cb.connections[index].is_open == TRUE))
    {
            bdcpy(bda, app_avk_cb.connections[index].bda_connected);
            return TRUE;
        }
    }

    return FALSE;
}

/*******************************************************************************
**
** Function         avk_is_rc_open
**
** Description      Check if AVRC is open
**
** Parameters
**
** Returns          TRUE if AVRC is open, FALSE otherwise.
**
*******************************************************************************/
BOOLEAN avk_is_rc_open(BD_ADDR bda)
{
    tAPP_AVK_CONNECTION *connection = app_avk_find_connection_by_bd_addr(bda);

    if(connection == NULL)
        return FALSE;

    return connection->is_rc_open;
}

/*******************************************************************************
**
** Function         app_avk_cancel
**
** Description      Example of cancel connection command
**
** Returns          void
**
*******************************************************************************/
void app_avk_cancel(BD_ADDR bda)
{
    tBSA_STATUS status;
    tBSA_AVK_CANCEL_CMD bsa_avk_cancel_cmd;

    /* Send remote control command */
    status = BSA_AvkCancelCmdInit(&bsa_avk_cancel_cmd);

    bdcpy(bsa_avk_cancel_cmd.bda, bda);

    status = BSA_AvkCancelCmd(&bsa_avk_cancel_cmd);
    if (status != BSA_SUCCESS)
    {
        APP_ERROR1("Unable to Send app_avk_cancel_command %d", status);
    }
}


/*******************************************************************************
 **
 ** Function         app_avk_rc_get_element_attr_command
 **
 ** Description      Example of get_element_attr_command command
 **
 ** Returns          void
 **
 *******************************************************************************/
void app_avk_rc_get_element_attr_command(UINT8 rc_handle)
{
    int status;
    tBSA_AVK_GET_ELEMENT_ATTR bsa_avk_get_elem_attr_cmd;

    UINT8 attrs[]  = {AVRC_MEDIA_ATTR_ID_TITLE,
                      AVRC_MEDIA_ATTR_ID_ARTIST,
                      AVRC_MEDIA_ATTR_ID_ALBUM,
                      AVRC_MEDIA_ATTR_ID_TRACK_NUM,
                      AVRC_MEDIA_ATTR_ID_NUM_TRACKS,
                      AVRC_MEDIA_ATTR_ID_GENRE,
                      AVRC_MEDIA_ATTR_ID_PLAYING_TIME};
    UINT8 num_attr  = 7;

    /* Send command */
    status = BSA_AvkGetElementAttrCmdInit(&bsa_avk_get_elem_attr_cmd);
    bsa_avk_get_elem_attr_cmd.rc_handle = rc_handle;
    bsa_avk_get_elem_attr_cmd.label = app_avk_get_label(); /* Just used to distinguish commands */

    bsa_avk_get_elem_attr_cmd.num_attr = num_attr;
    memcpy(bsa_avk_get_elem_attr_cmd.attrs, attrs, sizeof(attrs));

    status = BSA_AvkGetElementAttrCmd(&bsa_avk_get_elem_attr_cmd);
    if (status != BSA_SUCCESS)
    {
        APP_ERROR1("Unable to Send app_avk_rc_get_element_attr_command %d", status);
    }
}

/*******************************************************************************
 **
 ** Function         app_avk_rc_list_player_attr_command
 **
 ** Description      Example of app_avk_rc_list_player_attr_command command
 **
 ** Returns          void
 **
 *******************************************************************************/
void app_avk_rc_list_player_attr_command(UINT8 rc_handle)
{
    int status;
    tBSA_AVK_LIST_PLAYER_ATTR bsa_avk_list_player_attr_cmd;

    /* Send command */
    status = BSA_AvkListPlayerAttrCmdInit(&bsa_avk_list_player_attr_cmd);
    bsa_avk_list_player_attr_cmd.rc_handle = rc_handle;
    bsa_avk_list_player_attr_cmd.label = app_avk_get_label(); /* Just used to distinguish commands */

    status = BSA_AvkListPlayerAttrCmd(&bsa_avk_list_player_attr_cmd);
    if (status != BSA_SUCCESS)
    {
        APP_ERROR1("Unable to Send app_avk_rc_list_player_attr_command %d", status);
    }
}

/*******************************************************************************
 **
 ** Function         app_avk_rc_list_player_attr_value_command
 **
 ** Description      Example of app_avk_rc_list_player_attr_value_command command
 **
 ** Returns          void
 **
 *******************************************************************************/
void app_avk_rc_list_player_attr_value_command(UINT8 attr, UINT8 rc_handle)
{
    int status;
    tBSA_AVK_LIST_PLAYER_VALUES bsa_avk_list_player_attr_cmd;

    /* Send command */
    status = BSA_AvkListPlayerValuesCmdInit(&bsa_avk_list_player_attr_cmd);
    bsa_avk_list_player_attr_cmd.rc_handle = rc_handle;
    bsa_avk_list_player_attr_cmd.label = app_avk_get_label(); /* Just used to distinguish commands */
    bsa_avk_list_player_attr_cmd.attr = attr;

    status = BSA_AvkListPlayerValuesCmd(&bsa_avk_list_player_attr_cmd);
    if (status != BSA_SUCCESS)
    {
        APP_ERROR1("Unable to Send app_avk_rc_list_player_attr_command %d", status);
    }
}


/*******************************************************************************
 **
 ** Function         app_avk_rc_get_player_value_command
 **
 ** Description      Example of get_player_value_command command
 **
 ** Returns          void
 **
 *******************************************************************************/
void app_avk_rc_get_player_value_command(UINT8 *attrs, UINT8 num_attr, UINT8 rc_handle)
{
    int status;
    tBSA_AVK_GET_PLAYER_VALUE bsa_avk_get_player_val_cmd;

    /* Send command */
    status = BSA_AvkGetPlayerValueCmdInit(&bsa_avk_get_player_val_cmd);
    bsa_avk_get_player_val_cmd.rc_handle = rc_handle;
    bsa_avk_get_player_val_cmd.label = app_avk_get_label(); /* Just used to distinguish commands */
    bsa_avk_get_player_val_cmd.num_attr = num_attr;

    memcpy(bsa_avk_get_player_val_cmd.attrs, attrs, sizeof(UINT8) * num_attr);

    status = BSA_AvkGetPlayerValueCmd(&bsa_avk_get_player_val_cmd);
    if (status != BSA_SUCCESS)
    {
        APP_ERROR1("Unable to Send app_avk_rc_get_player_value_command %d", status);
    }
}



/*******************************************************************************
 **
 ** Function         app_avk_rc_set_player_value_command
 **
 ** Description      Example of set_player_value_command command
 **
 ** Returns          void
 **
 *******************************************************************************/
void app_avk_rc_set_player_value_command(UINT8 num_attr, UINT8 *attr_ids, UINT8 * attr_val, UINT8 rc_handle)
{
    int status;
    tBSA_AVK_SET_PLAYER_VALUE bsa_avk_set_player_val_cmd;

    /* Send command */
    status = BSA_AvkSetPlayerValueCmdInit(&bsa_avk_set_player_val_cmd);
    bsa_avk_set_player_val_cmd.rc_handle = rc_handle;
    bsa_avk_set_player_val_cmd.label = app_avk_get_label(); /* Just used to distinguish commands */
    bsa_avk_set_player_val_cmd.num_attr = num_attr;

    memcpy(bsa_avk_set_player_val_cmd.player_attr, attr_ids, sizeof(UINT8) * num_attr);
    memcpy(bsa_avk_set_player_val_cmd.player_value, attr_val, sizeof(UINT8) * num_attr);

    status = BSA_AvkSetPlayerValueCmd(&bsa_avk_set_player_val_cmd);
    if (status != BSA_SUCCESS)
    {
        APP_ERROR1("Unable to Send app_avk_rc_set_player_value_command %d", status);
    }
}


/*******************************************************************************
 **
 ** Function         app_avk_rc_get_play_status_command
 **
 ** Description      Example of get_play_status
 **
 ** Returns          void
 **
 *******************************************************************************/
int app_avk_rc_get_play_status_command(UINT8 rc_handle)
{
    int status;
    tBSA_AVK_GET_PLAY_STATUS bsa_avk_play_status_cmd;

    /* Send command */
    status = BSA_AvkGetPlayStatusCmdInit(&bsa_avk_play_status_cmd);
    bsa_avk_play_status_cmd.rc_handle = rc_handle;
    bsa_avk_play_status_cmd.label = app_avk_get_label(); /* Just used to distinguish commands */

    status = BSA_AvkGetPlayStatusCmd(&bsa_avk_play_status_cmd);
    if (status != BSA_SUCCESS) {
        APP_ERROR1("Unable to Send app_avk_rc_get_play_status_command %d", status);
        return -1;
    }

    return 0;
}


/*******************************************************************************
 **
 ** Function         app_avk_rc_set_browsed_player_command
 **
 ** Description      Example of set_browsed_player
 **
 ** Returns          void
 **
 *******************************************************************************/
void app_avk_rc_set_browsed_player_command(UINT16  player_id, UINT8 rc_handle)
{
    int status;
    tBSA_AVK_SET_BROWSED_PLAYER bsa_avk_set_browsed_player;

    /* Send command */
    status = BSA_AvkSetBrowsedPlayerCmdInit(&bsa_avk_set_browsed_player);
    bsa_avk_set_browsed_player.player_id = player_id;
    bsa_avk_set_browsed_player.rc_handle = rc_handle;
    bsa_avk_set_browsed_player.label = app_avk_get_label(); /* Just used to distinguish commands */

    status = BSA_AvkSetBrowsedPlayerCmd(&bsa_avk_set_browsed_player);
    if (status != BSA_SUCCESS)
    {
        APP_ERROR1("Unable to Send app_avk_rc_set_browsed_player_command %d", status);
    }
}


/*******************************************************************************
 **
 ** Function         app_avk_rc_set_addressed_player_command
 **
 ** Description      Example of set_addressed_player
 **
 ** Returns          void
 **
 *******************************************************************************/
void app_avk_rc_set_addressed_player_command(UINT16  player_id, UINT8 rc_handle)
{
    int status;
    tBSA_AVK_SET_ADDR_PLAYER bsa_avk_set_player;

    /* Send command */
    status = BSA_AvkSetAddressedPlayerCmdInit(&bsa_avk_set_player);
    bsa_avk_set_player.player_id = player_id;
    bsa_avk_set_player.rc_handle = rc_handle;
    bsa_avk_set_player.label = app_avk_get_label(); /* Just used to distinguish commands */

    status = BSA_AvkSetAddressedPlayerCmd(&bsa_avk_set_player);
    if (status != BSA_SUCCESS)
    {
        APP_ERROR1("Unable to Send app_avk_rc_set_addressed_player_command %d", status);
    }
}

/*******************************************************************************
 **
 ** Function         app_avk_rc_change_path_command
 **
 ** Description      Example of change_path
 **
 ** Returns          void
 **
 *******************************************************************************/
void app_avk_rc_change_path_command(UINT16   uid_counter, UINT8  direction, tAVRC_UID folder_uid, UINT8 rc_handle)
{
    int status;
    tBSA_AVK_CHG_PATH bsa_change_path;

    /* Send command */
    status = BSA_AvkChangePathCmdInit(&bsa_change_path);

    bsa_change_path.rc_handle = rc_handle;
    bsa_change_path.label = app_avk_get_label(); /* Just used to distinguish commands */

    bsa_change_path.uid_counter = uid_counter;
    bsa_change_path.direction = direction;
    memcpy(bsa_change_path.folder_uid, folder_uid, sizeof(tAVRC_UID));

    status = BSA_AvkChangePathCmd(&bsa_change_path);
    if (status != BSA_SUCCESS)
    {
        APP_ERROR1("Unable to Send app_avk_rc_change_path_command %d", status);
    }
}


/*******************************************************************************
 **
 ** Function         app_avk_rc_get_folder_items
 **
 ** Description      Example of get_folder_items
 **
 ** Returns          void
 **
 *******************************************************************************/
void app_avk_rc_get_folder_items(UINT8  scope, UINT32  start_item, UINT32  end_item, UINT8 rc_handle)
{
    int status;
    tBSA_AVK_GET_FOLDER_ITEMS bsa_get_folder_items;

    UINT32 attrs[]  = {AVRC_MEDIA_ATTR_ID_TITLE,
                      AVRC_MEDIA_ATTR_ID_ARTIST,
                      AVRC_MEDIA_ATTR_ID_ALBUM,
                      AVRC_MEDIA_ATTR_ID_TRACK_NUM,
                      AVRC_MEDIA_ATTR_ID_NUM_TRACKS,
                      AVRC_MEDIA_ATTR_ID_GENRE,
                      AVRC_MEDIA_ATTR_ID_PLAYING_TIME};
    UINT8 num_attr  = 7;

    /* Send command */
    status = BSA_AvkGetFolderItemsCmdInit(&bsa_get_folder_items);

    bsa_get_folder_items.rc_handle = rc_handle;
    bsa_get_folder_items.label = app_avk_get_label(); /* Just used to distinguish commands */

    bsa_get_folder_items.scope = scope;
    bsa_get_folder_items.start_item = start_item;
    bsa_get_folder_items.end_item = end_item;

    if(AVRC_SCOPE_PLAYER_LIST != scope)
    {
        bsa_get_folder_items.attr_count = num_attr;
        memcpy(bsa_get_folder_items.attrs, attrs, sizeof(attrs));
    }


    status = BSA_AvkGetFolderItemsCmd(&bsa_get_folder_items);
    if (status != BSA_SUCCESS)
    {
        APP_ERROR1("Unable to Send app_avk_rc_get_folder_items %d", status);
    }
}

/*******************************************************************************
 **
 ** Function         app_avk_rc_get_items_attr
 **
 ** Description      Example of get_items_attr
 **
 ** Returns          void
 **
 *******************************************************************************/
void app_avk_rc_get_items_attr(UINT8  scope, tAVRC_UID  uid, UINT16  uid_counter, UINT8 rc_handle)
{
    int status;
    tBSA_AVK_GET_ITEMS_ATTR bsa_get_items_attr;

    UINT8 attrs[]  = {AVRC_MEDIA_ATTR_ID_TITLE,
                      AVRC_MEDIA_ATTR_ID_ARTIST,
                      AVRC_MEDIA_ATTR_ID_ALBUM,
                      AVRC_MEDIA_ATTR_ID_TRACK_NUM,
                      AVRC_MEDIA_ATTR_ID_NUM_TRACKS,
                      AVRC_MEDIA_ATTR_ID_GENRE,
                      AVRC_MEDIA_ATTR_ID_PLAYING_TIME};
    UINT8 num_attr  = 7;

    /* Send command */
    status = BSA_AvkGetItemsAttrCmdInit(&bsa_get_items_attr);

    bsa_get_items_attr.rc_handle = rc_handle;
    bsa_get_items_attr.label = app_avk_get_label(); /* Just used to distinguish commands */

    bsa_get_items_attr.scope = scope;
    memcpy(bsa_get_items_attr.uid, uid, sizeof(tAVRC_UID));
    bsa_get_items_attr.uid_counter = uid_counter;
    bsa_get_items_attr.attr_count = num_attr;
    memcpy(bsa_get_items_attr.attrs, attrs, sizeof(attrs));

    status = BSA_AvkGetItemsAttrCmd(&bsa_get_items_attr);
    if (status != BSA_SUCCESS)
    {
        APP_ERROR1("Unable to Send app_avk_rc_get_items_attr %d", status);
    }
}


/*******************************************************************************
 **
 ** Function         app_avk_rc_play_item
 **
 ** Description      Example of play_item
 **
 ** Returns          void
 **
 *******************************************************************************/
void app_avk_rc_play_item(UINT8  scope, tAVRC_UID  uid, UINT16  uid_counter, UINT8 rc_handle)
{
    int status;
    tBSA_AVK_PLAY_ITEM bsa_play_item;


    /* Send command */
    status = BSA_AvkPlayItemCmdInit(&bsa_play_item);

    bsa_play_item.rc_handle = rc_handle;
    bsa_play_item.label = app_avk_get_label(); /* Just used to distinguish commands */

    bsa_play_item.scope = scope;
    memcpy(bsa_play_item.uid, uid, sizeof(tAVRC_UID));
    bsa_play_item.uid_counter = uid_counter;

    status = BSA_AvkPlayItemCmd(&bsa_play_item);
    if (status != BSA_SUCCESS)
    {
        APP_ERROR1("Unable to Send app_avk_rc_play_item %d", status);
    }
}


/*******************************************************************************
 **
 ** Function         app_avk_rc_add_to_play
 **
 ** Description      Example of add_to_play
 **
 ** Returns          void
 **
 *******************************************************************************/
void app_avk_rc_add_to_play(UINT8  scope, tAVRC_UID  uid, UINT16  uid_counter, UINT8 rc_handle)
{
    int status;
    tBSA_AVK_ADD_TO_PLAY bsa_add_play;


    /* Send command */
    status = BSA_AvkAddToPlayCmdInit(&bsa_add_play);

    bsa_add_play.rc_handle = rc_handle;
    bsa_add_play.label = app_avk_get_label(); /* Just used to distinguish commands */

    bsa_add_play.scope = scope;
    memcpy(bsa_add_play.uid, uid, sizeof(tAVRC_UID));
    bsa_add_play.uid_counter = uid_counter;

    status = BSA_AvkAddToPlayCmd(&bsa_add_play);
    if (status != BSA_SUCCESS)
    {
        APP_ERROR1("Unable to Send app_avk_rc_add_to_play %d", status);
    }
}

/*******************************************************************************
 **
 ** Function         app_avk_set_abs_vol_rsp
 **
 ** Description      This function sends abs vol response
 **
 ** Returns          None
 **
 *******************************************************************************/
void app_avk_set_abs_vol_rsp(UINT8 volume, UINT8 rc_handle, UINT8 label)
{
    int status;
    tBSA_AVK_SET_ABS_VOLUME_RSP abs_vol;
    BSA_AvkSetAbsVolumeRspInit(&abs_vol);

    abs_vol.label = label;
    abs_vol.rc_handle = rc_handle;
    abs_vol.volume = volume;
    status = BSA_AvkSetAbsVolumeRsp(&abs_vol);
    if (status != BSA_SUCCESS)
    {
        APP_ERROR1("Unable to send app_avk_set_abs_vol_rsp %d", status);
    }
}

/*******************************************************************************
 **
 ** Function         app_avk_reg_notfn_rsp
 **
 ** Description      This function sends reg notfn response (BSA as TG role in AVK)
 **
 ** Returns          none
 **
 *******************************************************************************/
void app_avk_reg_notfn_rsp(UINT8 volume, UINT8 rc_handle, UINT8 label, UINT8 event, tBSA_AVK_CODE code)
{
    int status;
    tBSA_AVK_REG_NOTIF_RSP reg_notf;
    BSA_AvkRegNotifRspInit(&reg_notf);

    reg_notf.reg_notf.param.volume  = volume;
    reg_notf.reg_notf.event_id      = event;
    reg_notf.rc_handle              = rc_handle;
    reg_notf.label                  = label;
    reg_notf.code                   = code;

    status = BSA_AvkRegNotifRsp(&reg_notf);
    if (status != BSA_SUCCESS)
    {
        APP_ERROR1("Unable to send app_avk_reg_notfn_rsp %d", status);
    }
}


/*******************************************************************************
 **
 ** Function         app_avk_send_get_capabilities
 **
 ** Description      Sample code for attaining capability for events
 **
 ** Returns          void
 **
 *******************************************************************************/
void app_avk_send_get_capabilities(UINT8 rc_handle)
{
    int status;
    tBSA_AVK_VEN_CMD bsa_avk_vendor_cmd;

    /* Send remote control command */
    status = BSA_AvkVendorCmdInit(&bsa_avk_vendor_cmd);

    bsa_avk_vendor_cmd.rc_handle = rc_handle;
    bsa_avk_vendor_cmd.ctype = BSA_AVK_CMD_STATUS;
    bsa_avk_vendor_cmd.subunit_type = AVRC_SUB_PANEL;
    bsa_avk_vendor_cmd.subunit_id = 0;
    bsa_avk_vendor_cmd.data[0] = BSA_AVK_RC_VD_GET_CAPABILITIES;
    bsa_avk_vendor_cmd.data[1] = 0; /* reserved & packet type */
    bsa_avk_vendor_cmd.data[2] = 0; /* length high*/
    bsa_avk_vendor_cmd.data[3] = 1; /* length low*/
    bsa_avk_vendor_cmd.data[4] = 0x03; /* for event*/

    bsa_avk_vendor_cmd.length = 5;
    bsa_avk_vendor_cmd.label = app_avk_get_label(); /* Just used to distinguish commands */

    status = BSA_AvkVendorCmd(&bsa_avk_vendor_cmd);
    if (status != BSA_SUCCESS)
    {
        APP_ERROR1("Unable to Send AV Vendor Command %d", status);
    }
}

/*******************************************************************************
 **
 ** Function         app_avk_send_register_notification
 **
 ** Description      Sample code for attaining capability for events
 **
 ** Returns          void
 **
 *******************************************************************************/
void app_avk_send_register_notification(int evt, UINT8 rc_handle)
{
    int status;
    tBSA_AVK_VEN_CMD bsa_avk_vendor_cmd;

    /* Send remote control command */
    status = BSA_AvkVendorCmdInit(&bsa_avk_vendor_cmd);

    bsa_avk_vendor_cmd.rc_handle = rc_handle;
    bsa_avk_vendor_cmd.ctype = BSA_AVK_CMD_NOTIF;
    bsa_avk_vendor_cmd.subunit_type = AVRC_SUB_PANEL;
    bsa_avk_vendor_cmd.subunit_id = 0;
    bsa_avk_vendor_cmd.data[0] = BSA_AVK_RC_VD_REGISTER_NOTIFICATION;
    bsa_avk_vendor_cmd.data[1] = 0; /* reserved & packet type */
    bsa_avk_vendor_cmd.data[2] = 0; /* length high*/
    bsa_avk_vendor_cmd.data[3] = 5; /* length low*/
    bsa_avk_vendor_cmd.data[4] = evt; /* length low*/

    bsa_avk_vendor_cmd.length = 9;
    bsa_avk_vendor_cmd.label = app_avk_get_label(); /* Just used to distinguish commands */

    status = BSA_AvkVendorCmd(&bsa_avk_vendor_cmd);
    if (status != BSA_SUCCESS)
    {
        APP_ERROR1("Unable to Send AV Vendor Command %d", status);
    }
}

/*******************************************************************************
 **
 ** Function         app_avk_add_connection
 **
 ** Description      This function adds a connection
 **
 ** Returns          Pointer to the found structure or NULL
 **
 *******************************************************************************/
tAPP_AVK_CONNECTION *app_avk_add_connection(BD_ADDR bd_addr)
{
    int index;
    for (index = 0; index < APP_AVK_MAX_CONNECTIONS; index++)
    {
        if (bdcmp(app_avk_cb.connections[index].bda_connected, bd_addr) == 0)
        {
            app_avk_cb.connections[index].in_use = TRUE;
            return &app_avk_cb.connections[index];
        }

        if (app_avk_cb.connections[index].in_use == FALSE)
        {
            bdcpy(app_avk_cb.connections[index].bda_connected, bd_addr);
            app_avk_cb.connections[index].in_use = TRUE;
            app_avk_cb.connections[index].index = index;
            return &app_avk_cb.connections[index];
        }
    }
    return NULL;
}

/*******************************************************************************
 **
 ** Function         app_avk_reset_connection
 **
 ** Description      This function resets a connection
 **
 **
 *******************************************************************************/
void app_avk_reset_connection(BD_ADDR bd_addr)
{
    int index;
    for (index = 0; index < APP_AVK_MAX_CONNECTIONS; index++)
    {
        if (bdcmp(app_avk_cb.connections[index].bda_connected, bd_addr) == 0)
        {
            app_avk_cb.connections[index].in_use = FALSE;

            app_avk_cb.connections[index].m_uiAddressedPlayer = 0;
            app_avk_cb.connections[index].m_uidCounterAddrPlayer = 0;
            app_avk_cb.connections[index].m_uid_counter = 0;
            app_avk_cb.connections[index].m_bDeviceSupportBrowse = FALSE;
            app_avk_cb.connections[index].m_uiBrowsedPlayer = 0;
            app_avk_cb.connections[index].m_iCurrentFolderItemCount = 0;
            app_avk_cb.connections[index].m_bAbsVolumeSupported = FALSE;
            app_avk_cb.connections[index].is_streaming_chl_open = FALSE;
            app_avk_cb.connections[index].is_started_streaming = FALSE;
            break;
        }
    }
}


/*******************************************************************************
 **
 ** Function         app_avk_find_connection_by_av_handle
 **
 ** Description      This function finds the connection structure by its handle
 **
 ** Returns          Pointer to the found structure or NULL
 **
 *******************************************************************************/
tAPP_AVK_CONNECTION *app_avk_find_connection_by_av_handle(UINT8 handle)
{
    int index;
    for (index = 0; index < APP_AVK_MAX_CONNECTIONS; index++)
    {
        if (app_avk_cb.connections[index].ccb_handle == handle)
            return &app_avk_cb.connections[index];
    }
    return NULL;
}


/*******************************************************************************
 **
 ** Function         app_avk_find_connection_by_rc_handle
 **
 ** Description      This function finds the connection structure by its handle
 **
 ** Returns          Pointer to the found structure or NULL
 **
 *******************************************************************************/
tAPP_AVK_CONNECTION *app_avk_find_connection_by_rc_handle(UINT8 handle)
{
    int index;
    for (index = 0; index < APP_AVK_MAX_CONNECTIONS; index++)
    {
        if (app_avk_cb.connections[index].rc_handle == handle)
            return &app_avk_cb.connections[index];
    }
    return NULL;
}


/*******************************************************************************
 **
 ** Function         app_avk_find_connection_by_bd_addr
 **
 ** Description      This function finds the connection structure by its handle
 **
 ** Returns          Pointer to the found structure or NULL
 **
 *******************************************************************************/
tAPP_AVK_CONNECTION *app_avk_find_connection_by_bd_addr(BD_ADDR bd_addr)
{
    int index;
    for (index = 0; index < APP_AVK_MAX_CONNECTIONS; index++)
    {
        if ((bdcmp(app_avk_cb.connections[index].bda_connected, bd_addr) == 0) &&
                (app_avk_cb.connections[index].in_use == TRUE))
            return &app_avk_cb.connections[index];
    }
    return NULL;
}


/*******************************************************************************
 **
 ** Function         app_avk_find_streaming_connection
 **
 ** Description      This function finds the connection structure that is streaming
 **
 ** Returns          Pointer to the found structure or NULL
 **
 *******************************************************************************/
tAPP_AVK_CONNECTION *app_avk_find_streaming_connection()
{
    int index;

    if(app_avk_cb.pStreamingConn &&
            app_avk_cb.pStreamingConn->is_streaming_chl_open &&
            app_avk_cb.pStreamingConn->is_started_streaming)
    {
        return app_avk_cb.pStreamingConn;
    }

    for (index = 0; index < APP_AVK_MAX_CONNECTIONS; index++)
    {
        if (app_avk_cb.connections[index].is_streaming_chl_open == TRUE &&
                app_avk_cb.connections[index].is_started_streaming == TRUE )
            return &app_avk_cb.connections[index];
    }
    return NULL;
}

/*******************************************************************************
 **
 ** Function         app_avk_num_connections
 **
 ** Description      This function number of connections
 **
 ** Returns          number of connections
 **
 *******************************************************************************/
int app_avk_num_connections()
{
    int iCount = 0;
    int index;
    for (index = 0; index < APP_AVK_MAX_CONNECTIONS; index++)
    {
        if (app_avk_cb.connections[index].in_use == TRUE)
            iCount++;
    }
    return iCount;
}

/*******************************************************************************
 **
 ** Function         app_avk_display_connections
 **
 ** Description      This function displays the connections
 **
 ** Returns          status
 **
 *******************************************************************************/
void app_avk_display_connections(void)
{
    int index;
    tAPP_AVK_CONNECTION *conn = NULL;

    int num_conn = app_avk_num_connections();

    if(num_conn == 0)
    {
        APP_INFO0("    No connections");
        return;
    }

    for (index = 0; index < APP_AVK_MAX_CONNECTIONS; index++)
    {
        conn = app_avk_find_connection_by_index(index);
        if(conn->in_use)
        {
            APP_INFO1("Connection index %d:-%02X:%02X:%02X:%02X:%02X:%02X",
                index,
                conn->bda_connected[0], conn->bda_connected[1],
                conn->bda_connected[2], conn->bda_connected[3],
                conn->bda_connected[4], conn->bda_connected[5]);
        }
    }
}


/*******************************************************************************
 **
 ** Function         app_avk_find_connection_by_index
 **
 ** Description      This function finds the connection structure by its index
 **
 ** Returns          Pointer to the found structure or NULL
 **
 *******************************************************************************/
tAPP_AVK_CONNECTION *app_avk_find_connection_by_index(int index)
{
    if(index < APP_AVK_MAX_CONNECTIONS)
        return &app_avk_cb.connections[index];

    return NULL;
}


/*******************************************************************************
 **
 ** Function         app_avk_pause_other_streams
 **
 ** Description      This function pauses other streams when a new stream start
 **                  is received from device identified by bd_addr
 **
 ** Returns          None
 **
 *******************************************************************************/
void app_avk_pause_other_streams(BD_ADDR bd_addr)
{
    int index;
    for (index = 0; index < APP_AVK_MAX_CONNECTIONS; index++)
    {
        if ((bdcmp(app_avk_cb.connections[index].bda_connected, bd_addr) != 0) &&
                app_avk_cb.connections[index].in_use  &&
                app_avk_cb.connections[index].is_streaming_chl_open)
        {
            app_avk_cb.connections[index].is_started_streaming = FALSE;
        }
    }
}


/*******************************************************************************
 **
 ** Function         app_avk_send_delay_report
 **
 ** Description      Sample code to send delay report
 **
 ** Returns          void
 **
 *******************************************************************************/
void app_avk_send_delay_report(UINT16 delay)
{
    tBSA_AVK_DELAY_REPORT report;
    int status;

    APP_DEBUG1("send delay report:%d", delay);

    BSA_AvkDelayReportInit(&report);
    report.delay = delay;
    status = BSA_AvkDelayReport(&report);
    if (status != BSA_SUCCESS)
    {
        APP_ERROR1("Unable to Send delay report %d", status);
    }
}

int app_avk_get_state(RK_BT_SINK_STATE *pState)
{
    if (!pState)
        return -1;

    *pState =  app_avk_state;
    return 0;
}

static int app_avk_latest_connect()
{
    int index;

    index = app_mgr_get_latest_device();
    if(index < 0 || index >= APP_MAX_NB_REMOTE_STORED_DEVICES) {
        APP_DEBUG0("can't find latest connected device");
        return -1;
    }

    return app_avk_open(app_xml_remote_devices_db[index].bd_addr,
                        app_xml_remote_devices_db[index].name);
}

int app_avk_start()
{
    int connect_cnt = 2;

    app_avk_send_state(RK_BT_SINK_STATE_IDLE);

    memset(alsa_device, 0, sizeof(alsa_device));

    if (app_avk_init(NULL) < 0) {
        APP_DEBUG0("app_avk_init failed");
        return -1;
    }

    /* Example to register AVK connection end point*/
    if (app_avk_register() < 0) {
        APP_DEBUG0("app_avk_register failed");
        return -1;
    }

    if(app_mgr_is_reconnect()) {
        while(connect_cnt--) {
            if(app_avk_latest_connect() == 0)
                break;
        }
    }

    //app_dm_set_visibility(TRUE, TRUE);

    return 0;
}

void app_avk_stop()
{
    app_avk_close_all();
    GKI_delay(1000);

    app_avk_deregister();
    app_avk_deinit();

    app_avk_state = RK_BT_SINK_STATE_IDLE;
    app_avk_send_state(RK_BT_SINK_STATE_IDLE);
    app_avk_deregister_cb();
}

int app_avk_get_play_status()
{
    int ret = -1;
    int index;

    for (index = 0; index < APP_AVK_MAX_CONNECTIONS; index++) {
        if (app_avk_cb.connections[index].in_use == TRUE) {
            ret = app_avk_rc_get_play_status_command(app_avk_cb.connections[index].rc_handle);
        }
    }

    return ret;
}

BOOLEAN app_avk_get_pos_change()
{
    return app_avk_pos_change;
}

void app_avk_set_alsa_device(char *alsa_dev)
{
    int len = 0;

    if(alsa_dev == NULL) {
        if(alsa_device[0] == 0) {
            len = sizeof(alsa_device) > sizeof(APP_AVK_ASLA_DEV) ? sizeof(APP_AVK_ASLA_DEV) : sizeof(alsa_device);
            strncpy(alsa_device, APP_AVK_ASLA_DEV, len);
        }
    } else {
        memset(alsa_device, 0, sizeof(alsa_device));
        len = sizeof(alsa_device) > sizeof(alsa_dev) ? sizeof(alsa_dev) : sizeof(alsa_device);
        strncpy(alsa_device, alsa_dev, len);
    }

    APP_DEBUG1("sink alsa device: %s", alsa_device);
}

