#include <pthread.h>
#include <stdio.h>
#include "alsa/asoundlib.h"
#include "DeviceIo/Rk_audio.h"

static int m_vol_min = 0;
static int m_vol_max = 100;
#define SOFTVOL_CARD			"default"
#define SOFTVOL_ELEM			"name='Master Playback Volume'"

typedef struct {
	int volume;
	int is_mute;
} user_volume_t;

static user_volume_t user_volume = {0, 0};
static pthread_mutex_t user_volume_mutex = PTHREAD_MUTEX_INITIALIZER;

static int cset(const char *value_string, int roflag)
{
	int err;
	int ret = 0;
	static snd_ctl_t *handle = NULL;
	snd_ctl_elem_info_t *info;
	snd_ctl_elem_id_t *id;
	snd_ctl_elem_value_t *control;
	snd_ctl_elem_info_alloca(&info);
	snd_ctl_elem_id_alloca(&id);
	snd_ctl_elem_value_alloca(&control);
	char card[64] = SOFTVOL_CARD;
	int keep_handle = 0;

	if (snd_ctl_ascii_elem_id_parse(id, SOFTVOL_ELEM)) {
		fprintf(stderr, "Wrong control identifier: %s\n", SOFTVOL_ELEM);
		return -EINVAL;
	}

	if (handle == NULL &&
		(err = snd_ctl_open(&handle, card, 0)) < 0) {
		printf("Control %s open error: %d\n", card, err);
		return err;
	}
	snd_ctl_elem_info_set_id(info, id);
	if ((err = snd_ctl_elem_info(handle, info)) < 0) {
		printf("Cannot find the given element from control %s\n", card);
		if (! keep_handle) {
			snd_ctl_close(handle);
			handle = NULL;
		}
		return err;
	}
	snd_ctl_elem_info_get_id(info, id);     /* FIXME: Remove it when hctl find works ok !!! */
	if (!roflag) {
		snd_ctl_elem_value_set_id(control, id);
		if ((err = snd_ctl_elem_read(handle, control)) < 0) {
			printf("Cannot read the given element from control %s\n", card);
			if (! keep_handle) {
				snd_ctl_close(handle);
				handle = NULL;
			}
			return err;
		}
		err = snd_ctl_ascii_value_parse(handle, control, info, value_string);
		if (err < 0) {
			printf("Control %s parse error: %d\n", card, err);
			if (!keep_handle) {
				snd_ctl_close(handle);
				handle = NULL;
			}
			return  err;
		}
		if ((err = snd_ctl_elem_write(handle, control)) < 0) {
			printf("Control %s element write error: %d; errno: %d\n", card,
					err, errno);
			if (!keep_handle) {
				snd_ctl_close(handle);
				handle = NULL;
			}
			return err;
		} else {
			printf("Control %s element write volume %s successfully\n", card,
					value_string);
		}
		system("alsactl store --file=/data/cfg/asound.state");
	} else {
		int vol_l, vol_r;
		snd_ctl_elem_value_set_id(control, id);
		if ((err = snd_ctl_elem_read(handle, control)) < 0) {
			printf("Cannot read the given element from control %s\n", card);
			if (! keep_handle) {
				snd_ctl_close(handle);
				handle = NULL;
			}
			return err;
		}
		vol_l = snd_ctl_elem_value_get_integer(control, 0);
		vol_r = snd_ctl_elem_value_get_integer(control, 1);
		printf("%s:  cget %d, %d!\n", __func__, vol_l, vol_r);
		ret = (vol_l + vol_r) >> 1;
	}

	if (! keep_handle) {
		snd_ctl_close(handle);
		handle = NULL;
	}
	return ret;
}

void RK_audio_set_volume(int vol)
{
	pthread_mutex_lock(&user_volume_mutex);

	char value[4];

	if (vol < m_vol_min)
		vol = m_vol_min;
	if (vol > m_vol_max)
		vol = m_vol_max;

	if (vol == 0) {
		user_volume.is_mute = 1;
	} else {
		user_volume.is_mute = 0;
	}

	user_volume.volume = vol;

	memset(value, 0, sizeof(value));
	snprintf(value, sizeof(value), "%d%%", vol);

	cset(value, 0);

	pthread_mutex_unlock(&user_volume_mutex);
}

int RK_audio_get_volume(void)
{
	int volume;

	pthread_mutex_lock(&user_volume_mutex);

	user_volume.volume = cset(NULL, 1);

	if (user_volume.is_mute) {
		volume = 0;
	} else {
		volume = user_volume.volume;
	}

	pthread_mutex_unlock(&user_volume_mutex);

	return volume;
}

int RK_audio_limit_max_volume(int vol)
{
	if (vol >= 0 && vol <= 100) {
		m_vol_max = vol;
		if (RK_audio_get_volume() > m_vol_max)
			RK_audio_set_volume(m_vol_max);
		return 0;
	}
	return -1;
}

void RK_audio_mute(void)
{
	pthread_mutex_lock(&user_volume_mutex);

	char value[4];
	memset(value, 0, sizeof(value));
	snprintf(value, sizeof(value), "%d%%", 0);
	user_volume.is_mute = 1;

	cset(value, 0);

	pthread_mutex_unlock(&user_volume_mutex);
}

int RK_audio_unmute(void)
{
	pthread_mutex_lock(&user_volume_mutex);

	char value[4];
	memset(value, 0, sizeof(value));
	snprintf(value, sizeof(value), "%d%%", user_volume.volume);
	user_volume.is_mute = 0;

	cset(value, 0);

	pthread_mutex_unlock(&user_volume_mutex);

	return user_volume.volume;
}
