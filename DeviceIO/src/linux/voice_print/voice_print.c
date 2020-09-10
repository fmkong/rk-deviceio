#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/time.h>
#include <sys/prctl.h>
#include <alsa/asoundlib.h>
#include "voice_print.h"
#include "DeviceIo/VoicePrint.h"

//#define INTERACTIVE_3_TIMES

#define VP_SSID_LEN    32
#define VP_PSK_LEN     64

#define VP_CHANNEL_NUM      1
#define VP_READ_FRAME       (1024 * 2)
#define VP_PERIOD_SIZE      VP_READ_FRAME
#define VP_PERIOD_COUNT     2
#define VP_BUFFER_SIZE      (VP_PERIOD_SIZE * VP_PERIOD_COUNT)
#define VP_PCM_BUF_SIZE     (VP_PERIOD_SIZE * 2 * VP_CHANNEL_NUM) /* 16bit */

#define VP_TIME_OUT_MS 120000

typedef enum {
    VP_STATUS_NORMAL = 0,
    VP_STATUS_NOTREADY,
    VP_STATUS_DEC_ERROR,
    VP_STATUS_COMPLETE,
} vp_status_t;

typedef struct {
    uint8_t ssid[VP_SSID_LEN];
    uint8_t passphrase[VP_PSK_LEN];
} vp_result_t;

typedef struct {
    unsigned int channels;
    unsigned int sample_rate;
    snd_pcm_uframes_t period_size;
    snd_pcm_uframes_t buffer_size;
    snd_pcm_stream_t stream;
    snd_pcm_access_t access;
    snd_pcm_format_t format;
} vp_alsa_config_t;

typedef struct {
    vp_result_t result;
    vp_status_t status;
    uint32_t decoder_bitsize;
    uint8_t ref;
    void *handle;
    vp_alsa_config_t config;
    uint8_t pcm_data_buf[VP_PCM_BUF_SIZE];
#ifdef INTERACTIVE_3_TIMES
    uint8_t result_str[3][200];
#else
    uint8_t result_str[200];
#endif
} vp_info_t;

static pthread_t vp_thread = 0;
static int vp_thread_done = 0;
static snd_pcm_t *vp_capture_handle = NULL;
static const char *vp_capture_device = "2mic_loopback"; /* ALSA capture device */
static vp_info_t *voice_print = NULL;
static VP_SSID_PSK_CALLBACK vp_ssid_psk_cb = NULL;

#ifdef SAVE_FILE
FILE *file_fd = NULL;
#endif

static int pcm_device_open(snd_pcm_t **handle, const char *device_name, vp_alsa_config_t *config)
{
    int err;
    snd_pcm_hw_params_t *hw_params;

    err = snd_pcm_open(handle, device_name, config->stream, 0);
    if (err) {
        printf( "Unable to open PCM device: %s\n", device_name);
        return -1;
    }

    err = snd_pcm_hw_params_malloc(&hw_params);
    if (err) {
        printf("Can't malloc hardware parameter structure (%s)\n", snd_strerror(err));
        return -1;
    }

    err = snd_pcm_hw_params_any(*handle, hw_params);
    if (err) {
        printf("Can't initialize hardware parameter structure (%s)\n", snd_strerror(err));
        return -1;
    }

    err = snd_pcm_hw_params_set_access(*handle, hw_params, config->access);
    if (err) {
        printf("Error setting access: %s\n", snd_strerror(err));
        return -1;
    }

    err = snd_pcm_hw_params_set_format(*handle, hw_params, config->format);
    if (err) {
        printf("Error setting format: %s\n", snd_strerror(err));
        return -1;
    }

    err = snd_pcm_hw_params_set_channels(*handle, hw_params, config->channels);
    if (err) {
        printf( "Error setting channels (%d), %s\n", config->channels, snd_strerror(err));
        return -1;
    }

    err = snd_pcm_hw_params_set_rate_near(*handle, hw_params, &config->sample_rate, 0);
    if (err) {
        printf("Error setting sampling rate (%d): %s\n", config->sample_rate, snd_strerror(err));
        return -1;
    }
    printf("setting sample_rate = %d\n", config->sample_rate);

    err = snd_pcm_hw_params_set_period_size_near(*handle, hw_params, &config->period_size, 0);
    if (err) {
        printf("Error setting period size (%d): %s\n", (int)config->period_size, snd_strerror(err));
        return -1;
    }
    printf("period_size = %d\n", (int)config->period_size);

    err = snd_pcm_hw_params_set_buffer_size_near(*handle, hw_params, &config->buffer_size);
    if (err) {
        printf("Error setting buffer size (%d): %s\n", (int)config->buffer_size, snd_strerror(err));
        return -1;
    }
    printf("buffer_size = %d\n", (int)config->buffer_size);

    /* Write the parameters to the driver */
     err = snd_pcm_hw_params(*handle, hw_params);
     if (err < 0) {
         printf( "Unable to set HW parameters: %s\n", snd_strerror(err));
         return -1;
     }

     printf("Open pcm device is successful: %s\n", device_name);
     if (hw_params)
        snd_pcm_hw_params_free(hw_params);

     return 0;
}

static void pcm_device_close()
{
    if(vp_capture_handle) {
        snd_pcm_close(vp_capture_handle);
        vp_capture_handle = NULL;
    }
}

static long get_current_time()
{
    struct timeval tv;

    gettimeofday(&tv , NULL);
    return tv.tv_sec * 1000 + tv.tv_usec / 1000; //ms
}

static int voice_print_get_samplerate(FREQ_TYPE_T type)
{
    int sample_rate;

    switch(type) {
        case LOW_FREQ_TYPE:
            sample_rate = 16000;
            break;
        case MIDDLE_FREQ_TYPE:
            sample_rate = 32000;
            break;
        case HIGH_FREQ_TYPE:
            sample_rate = 44100;
            break;
    }

    return sample_rate;
}

static int voice_print_get_result(vp_result_t *result)
{
    vp_info_t *vp = voice_print;

    if (!vp) {
        printf("%s vp don't init!\n", __func__);
        return -1;
    }

    if (vp->result_str[0] == 0) {
        printf("invalid result\n");
        return -1;
    }

    if (sscanf((char *)vp->result_str, "%[^:]:%[^:]",
            (char *)result->ssid, (char *)result->passphrase) != 2) {
        printf("don't find ssid or psk\n");
        return -1;
    }

    printf("ssid: %s, psk: %s\n", result->ssid, result->passphrase);
    return 0;
}

static int voice_print_handle_once()
{
    int i, ret, size, flag = 0;
    int finish = 0;
    static int decode_step = 0;
    vp_status_t status;
    int buf_size;
    vp_info_t *vp = voice_print;

    if (!vp) {
        printf("%s: vp don't init!\n", __func__);
        return -1;
    }

    buf_size = vp->decoder_bitsize * 2; //16bit

repeat:
    if (!vp->ref) {
        printf("Start the recording\n");
        ret = pcm_device_open(&vp_capture_handle, vp_capture_device, &vp->config);
        if (ret != 0) {
            printf("%s: open pcm capture device failed\n", __func__);
            return -1;
        }
        vp->ref++;
    }

    ret = snd_pcm_readi(vp_capture_handle, vp->pcm_data_buf, VP_READ_FRAME);
    if (ret != VP_READ_FRAME)
        printf("==== read frame error = %d ===\n",ret);

    if (ret < 0) {
        if (ret == -EPIPE)
            printf("Overrun occurred: %d\n", ret);

        ret = snd_pcm_recover(vp_capture_handle, ret, 0);
        // Still an error, need to exit.
        if (ret < 0) {
            printf( "Error occured while recording: %s\n", snd_strerror(ret));
            usleep(200 * 1000);
            pcm_device_close();

            vp->ref--;
            goto repeat;
        }
    }

#ifdef SAVE_FILE
    if(file_fd) {
         size = fwrite(vp->pcm_data_buf, VP_PCM_BUF_SIZE, 1, file_fd);
         //printf("size: %d\n",size);
    }
#endif

    for (i = 0; i < (VP_PCM_BUF_SIZE / buf_size); i++) {
        ret = decoderPcmData(vp->handle, (short *)&vp->pcm_data_buf[i * buf_size]);

        switch (ret) {
            case DEC_NORMAL:
                status = VP_STATUS_NORMAL;
                break;

            case DEC_NOTREADY:
                //printf("%s: wait to decoder\n", __func__);
                status = VP_STATUS_NOTREADY;
                break;

            case DEC_END:
                status = VP_STATUS_COMPLETE;
#ifdef INTERACTIVE_3_TIMES
                ret = decoderGetResult(vp->handle, vp->result_str[decode_step]);
#else
                ret = decoderGetResult(vp->handle, vp->result_str);
#endif
                if (ret == DEC_NOTREADY) {
                    printf("get decoder result error\n");
                    status = VP_STATUS_NOTREADY;
                    break;
                } else {
                    /*  It's better transfer ssid and psk
                     *  like "ssid:psk" in 3 times, and modify this decode code here.
                     */
                    printf("result: %d, %s\n", strlen((char *)vp->result_str), vp->result_str);

#ifdef INTERACTIVE_3_TIMES
                    decode_step++;
#else
                    decode_step = 3;
#endif
                    decoderReset(vp->handle, flag);
                }

                if (decode_step < 3) {
                    /* save ssid psk and check by index decode_times */
                } else {
                    decode_step = 0;
                    finish = 1;
                    goto out;
                }
                break;

            default:
                printf("The return value is invalid: %d\n", ret);
                break;
        }
    }

out:
    if (finish && vp->ref) {
        pcm_device_close();
        vp->ref--;
    }

    vp->status = status;
    return status;
}

static int voice_print_handle(int timeout_ms)
{
    long end_time;
    vp_status_t status;
    vp_info_t *vp = voice_print;

    if (!vp) {
        printf("%s vp don't init!\n", __func__);
        return -1;
    }

#ifdef SAVE_FILE
    file_fd = fopen("/tmp/vp.pcm", "w+");
    if (NULL == file_fd)
        printf("open pcm file failed\n");
#endif

    end_time = get_current_time() + timeout_ms;
    while(vp_thread_done && (end_time > get_current_time())) {
        status = voice_print_handle_once();
        if (status == VP_STATUS_DEC_ERROR || status == -1)
            goto err_out;
        else if (status == VP_STATUS_COMPLETE)
            break;
    }

    if (vp->result_str[0] != 0) {
        printf("voiceprint handle success\n");
        return 0;
    }

err_out:
    printf("voiceprint handle failed\n");

#ifdef SAVE_FILE
    if(file_fd) {
        fclose(file_fd);
        file_fd = NULL;
    }
#endif

    return -1;
}

static void *voice_print_handle_thread(void *arg)
{
    vp_result_t result;

    prctl(PR_SET_NAME,"voice_print_handle_thread");

    if(voice_print_handle(VP_TIME_OUT_MS) < 0) {
        printf("%s: voiceprint handle failed\n", __func__);
        return NULL;
    }

    if(!voice_print_get_result(&result)) {
        if(vp_ssid_psk_cb)
            vp_ssid_psk_cb(result.ssid, result.passphrase);
    }

    return NULL;
}

static int voice_print_init(FREQ_TYPE_T type)
{
    int flag = 0;
    vp_info_t *vp = voice_print;
    DECODER_CONFIG_T decode_config;

    if (vp) {
        printf("%s vp has already inited!\n", __func__);
        return -1;
    }

    vp = (vp_info_t *)malloc(sizeof(vp_info_t));
    if (!vp) {
        printf("%s malloc failed!\n", __func__);
        return -1;
    }
    memset(vp, 0, sizeof(vp_info_t));
    voice_print = vp;

    decode_config.error_correct = 0;
    decode_config.error_correct_num = 0;
    decode_config.freq_type = type;
    decode_config.group_symbol_num = 10;
    decode_config.max_strlen = 200; //256;
    decode_config.sample_rate = voice_print_get_samplerate(type);

    vp->handle = decoderInit(&decode_config, flag);
    if (vp->handle == NULL) {
        printf("%s: voiceprint decoder init error\n", __func__);
        return -1;
    }

    vp->decoder_bitsize = decoderGetSize(vp->handle, flag);
    if (VP_PCM_BUF_SIZE % (vp->decoder_bitsize * 2)) {
        printf("alsa read buffer size(%d) is not aligned to %d\n",
               VP_PCM_BUF_SIZE, vp->decoder_bitsize);
    }

    vp->config.channels = VP_CHANNEL_NUM;
    vp->config.sample_rate = voice_print_get_samplerate(type);
    vp->config.format = SND_PCM_FORMAT_S16_LE;
    vp->config.access = SND_PCM_ACCESS_RW_INTERLEAVED;
    vp->config.stream = SND_PCM_STREAM_CAPTURE;
    vp->config.period_size = VP_PERIOD_SIZE;
    vp->config.buffer_size = VP_BUFFER_SIZE;

    return 0;
}

int voice_print_start()
{
    int ret;

    printf("%s\n", __func__);

    if(voice_print_init(LOW_FREQ_TYPE) < 0) {
        printf("%s: vp init failed\n", __func__);
        return -1;
    }

    vp_thread_done = 1;
    ret = pthread_create(&vp_thread, NULL, voice_print_handle_thread, NULL);
    if (0 != ret) {
        printf("Create vp handle thread failed, return code: %d\n", ret);
        vp_thread = 0;
        return -1;
    }

    return 0;
}

int voice_print_stop()
{
    int flag = 0;
    vp_info_t *vp = voice_print;

    if (!vp) {
        printf("%s vp has already stoped!\n", __func__);
        return -1;
    }

    vp_ssid_psk_cb = NULL;
    pcm_device_close();

    vp_thread_done = 0;
    if (0 != pthread_join(vp_thread, NULL))
        printf("%s pthread_join failed!\n", __func__);
    vp_thread = 0;

    decoderDeinit(vp->handle, flag);
    free(vp);
    voice_print = NULL;

    printf("%s\n", __func__);
    return 0;
}

void voice_print_register_callback(VP_SSID_PSK_CALLBACK cb)
{
    vp_ssid_psk_cb = cb;
}

int vp_encode(char *file_path, unsigned char *str)
{
    int ret, size, outsize, flag = 0;
    uint8_t *buffer = NULL;
    FILE *fd = NULL;
    ENCOEDR_CONFIG_T config;
    void *handle;

    if(file_path == NULL || str == NULL) {
        printf("input string or file path error\n");
        return -1;
    }

    fd = fopen(file_path, "w+");
    if (fd == NULL) {
        printf("open pcm file failed\n");
        return -1;
    }

    config.error_correct = 0;
    config.error_correct_num = 0;
    config.freq_type = LOW_FREQ_TYPE;
    config.group_symbol_num = 10;
    config.max_strlen = 200;
    config.sample_rate = voice_print_get_samplerate(config.freq_type);
    handle = encoderInit(&config, flag);
    if (handle == NULL) {
        printf("voiceprint encoder init error\n");
        return -1;
    }

    outsize = encoderGetsize(handle);
    //printf("outsize: %d\n", outsize);
    buffer = (uint8_t*)malloc(outsize);
    if(buffer == NULL) {
        printf("malloc buffer failed\n");
        return -1;
    }

    ret = encoderSetStr(handle, str);
    if(ret == ENC_ERROR) {
        printf("encoder set input string failed\n");
        free(buffer);
        return -1;
    }

    while(1) {
        memset(buffer, 0, outsize);
        ret = encoderStrData(handle, (short*)buffer);
        if(ret == ENC_ERROR) {
            printf("encoder get pcm data failed\n");
            break;
        }

         size = fwrite(buffer, outsize, 1, fd);

        if(ret == ENC_END) {
            printf("encoder get pcm data success\n");
            break;
        }
    }

    fclose(fd);
    free(buffer);
    encoderDeinit(handle, flag);
    return 0;
}
