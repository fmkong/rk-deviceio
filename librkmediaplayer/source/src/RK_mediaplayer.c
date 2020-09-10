#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <gst/gst.h>
#include <sys/prctl.h>

#include "RK_mediaplayer.h"
typedef struct _RkPlayerInfo {
	char *url;
	char *title;
	char *singer;
	char *album;
	char *image_url;

	void *next;
	void *prev;
} RkPlayerInfo;

/* Structure to contain all our information, so we can pass it around */
typedef struct _RkMediaPlayer {
	GstElement *playbin;   /* Our one and only element */
	gboolean playing;      /* Are we in the PLAYING state? */
	gboolean stoped;      /* Are we in the STOPED state? */
	gboolean terminate;    /* Should we terminate execution? */
	gboolean seek_enabled; /* Is seeking enabled for this media? */
	gboolean is_buffering; /* Is the player in buffer? */
	gboolean is_live;      /* live stream*/
	pthread_t thread_id;   /* Thread id */
	RK_media_event_callback callback; /* Call back function */
	void *userdata;        /* Callback arg */

	RkPlayerInfo *playlist_start;
	RkPlayerInfo *playlist_end;
	RkPlayerInfo *playlist_current;
	unsigned int playlist_cnt;
	pthread_mutex_t playlist_mutex;
	RK_MIDEA_MODE playlist_mode;

} RkMediaPlayer;

/* Forward definition of the message processing function */
static void handle_message (GstMessage *msg, RkMediaPlayer *c_player)
{
	GError *err;
	gchar *debug_info;

	switch (GST_MESSAGE_TYPE (msg)) {
		case GST_MESSAGE_ERROR:
			gst_message_parse_error (msg, &err, &debug_info);
			g_printerr ("Error received from element %s: %s\n", GST_OBJECT_NAME (msg->src), err->message);
			g_printerr ("Debugging information: %s\n", debug_info ? debug_info : "none");
			g_clear_error (&err);
			g_free (debug_info);
			/* Send MediaEvent by callback */
			if (c_player->callback)
				(*c_player->callback)(c_player->userdata, RK_MediaEvent_Error);

			break;
		case GST_MESSAGE_EOS:
			g_print ("End-Of-Stream reached.\n");
			/* Send MediaEvent by callback */
			if (c_player->callback)
				(*c_player->callback)(c_player->userdata, RK_MediaEvent_End);

			RK_mediaplayer_next((int)c_player);
			break;
		case GST_MESSAGE_DURATION:
			/* Send MediaEvent by callback */
			if (c_player->callback)
				(*c_player->callback)(c_player->userdata, RK_MediaEvent_Duration);

			break;
#if 0
		case GST_MESSAGE_BUFFERING:
		{
			gint percent = 0;
			/* If the stream is live, we do not care about buffering. */
			if (c_player->is_live || c_player->stoped)
				break;
			gst_message_parse_buffering (msg, &percent);
			/* Send MediaEvent by callback */
			if (c_player->callback) {
				if ((percent < 100) && (!c_player->is_buffering)) {
					(*c_player->callback)(c_player->userdata, RK_MediaEvent_BufferStart);
					c_player->is_buffering = TRUE;
					gst_element_set_state (c_player->playbin, GST_STATE_PAUSED);
				} else if ((percent >= 100) && c_player->is_buffering) {
					(*c_player->callback)(c_player->userdata, RK_MediaEvent_BufferEnd);
					c_player->is_buffering = FALSE;
					gst_element_set_state (c_player->playbin, GST_STATE_PLAYING);
				}
			}
			break;
		}
#endif
		case GST_MESSAGE_STATE_CHANGED: {
			GstState old_state, new_state, pending_state;
			gst_message_parse_state_changed (msg, &old_state, &new_state, &pending_state);

			if (GST_MESSAGE_SRC (msg) == GST_OBJECT (c_player->playbin)) {
				switch (new_state) {
					case GST_STATE_PLAYING:
						/* Send MediaEvent by callback */
						if (c_player->callback)
							(*c_player->callback)(c_player->userdata, RK_MediaEvent_Play);
						break;
					case GST_STATE_PAUSED:
						/* Send MediaEvent by callback */
						if (!c_player->is_buffering && c_player->callback)
							(*c_player->callback)(c_player->userdata, RK_MediaEvent_Pause);
						break;
					case GST_STATE_READY:
						/* Send MediaEvent by callback */
						if (c_player->callback)
							(*c_player->callback)(c_player->userdata, RK_MediaEvent_Ready);
						break;
					case GST_STATE_NULL:
						/* Send MediaEvent by callback */
						if (c_player->callback)
							(*c_player->callback)(c_player->userdata, RK_MediaEvent_Pause);
						break;
					default:
						break;
				}

				/* Remember whether we are in the PLAYING state or not */
				c_player->playing = (new_state == GST_STATE_PLAYING);
				if (c_player->playing) {
					/* We just moved to PLAYING. Check if seeking is possible */
					GstQuery *query;
					gint64 start, end;
					query = gst_query_new_seeking (GST_FORMAT_TIME);
					if (gst_element_query (c_player->playbin, query)) {
						gst_query_parse_seeking (query, NULL, &c_player->seek_enabled, &start, &end);
						/* Send MediaEvent by callback */
						if (c_player->seek_enabled && c_player->callback)
							(*c_player->callback)(c_player->userdata, RK_MediaEvent_SeekEnable);
					} else {
						g_printerr ("Seeking query failed.");
					}
					gst_query_unref (query);
				}
			}
			break;
		}
		case GST_MESSAGE_CLOCK_LOST: {
			/* Get a new clock */
			gst_element_set_state (c_player->playbin, GST_STATE_PAUSED);
			gst_element_set_state (c_player->playbin, GST_STATE_PLAYING);
			break;
		}
		default:
			/* We should not reach here */
			//g_printerr ("Unexpected message received.\n");
			break;
	}
	gst_message_unref (msg);
}

static void *listen_playbin_bus(void *arg)
{
	GstBus *bus;
	GstMessage *msg;
	GstMessageType listen_flag;
	RkMediaPlayer *c_player = (RkMediaPlayer *)arg;

	prctl(PR_SET_NAME,"listen_playbin_bus");

	listen_flag = (GstMessageType)(GST_MESSAGE_STATE_CHANGED |
								   GST_MESSAGE_ERROR | GST_MESSAGE_EOS |
								   GST_MESSAGE_DURATION | GST_MESSAGE_BUFFERING);
	/* Listen to the bus */
	bus = gst_element_get_bus (c_player->playbin);
	do {
		msg = gst_bus_timed_pop_filtered (bus, 100 * GST_MSECOND, listen_flag);
		/* Parse message */
		if (msg != NULL)
			handle_message (msg, c_player);

	} while (!c_player->terminate);

	/* Free resources */
	gst_object_unref (bus);
	gst_element_set_state (c_player->playbin, GST_STATE_NULL);
	gst_object_unref (c_player->playbin);
	c_player->playbin = NULL;
}

int RK_mediaplayer_create(int *pHandle)
{
	RkMediaPlayer *c_player;

	c_player = (RkMediaPlayer *)malloc(sizeof(RkMediaPlayer));
	if (c_player == NULL)
		return -ENOSPC;

	memset(c_player, 0, sizeof(RkMediaPlayer));
	c_player->playing = FALSE;
	c_player->terminate = FALSE;
	c_player->seek_enabled = FALSE;
	c_player->is_buffering = FALSE;
	c_player->callback = NULL;
	c_player->userdata = NULL;
	c_player->thread_id = 0;

	/* Initialize GStreamer */
	if (!gst_is_initialized())
		gst_init (NULL, NULL);

	/* Create the elements */
	c_player->playbin = gst_element_factory_make ("playbin", "playbin");
	if (!c_player->playbin) {
		g_printerr ("Not all elements could be created.\n");
		return -1;
	}
	/* Init mutex */
	pthread_mutex_init(&c_player->playlist_mutex, NULL);

	*pHandle = (int)c_player;
	return 0;
}

int RK_mediaplayer_destroy(int iHandle)
{
	void *status;
	int ret;
	RkMediaPlayer *c_player = (RkMediaPlayer *)iHandle;

	if (!c_player)
		return 0;

	/* Wait for thread exit */
	c_player->terminate = TRUE;
	ret = pthread_join(c_player->thread_id, &status);
	if (ret)
		usleep(200000); /* 200ms */

	if (c_player->playbin) {
		gst_element_set_state (c_player->playbin, GST_STATE_NULL);
		gst_object_unref (c_player->playbin);
		c_player->playbin = NULL;
	}

	pthread_mutex_destroy(&c_player->playlist_mutex);
	free(c_player);
	return 0;
}

int RK_mediaplayer_play(int iHandle, const char *uri)
{
	GstStateChangeReturn ret;
	GstState cur_state, pending_state;
	pthread_t gst_player_thread;
	RkMediaPlayer *c_player = (RkMediaPlayer *)iHandle;
	int err = 0;
	pthread_attr_t attr;
	gchar *cur_uri = NULL;

	g_printerr ("#### %s, %x uil: %s\n", __func__, iHandle, uri);

	if (!c_player || !c_player->playbin || !uri)
		return -EINVAL;

	/* Get current status */
	while (1) {
		ret = gst_element_get_state (c_player->playbin, &cur_state, &pending_state, NULL);
		if (ret == GST_STATE_CHANGE_FAILURE) {
			g_print("Unable to get player status before playing operation\n");
			return -1;
		} else if (ret ==  GST_STATE_CHANGE_ASYNC)
			usleep(500000);//500ms
		else //GST_STATE_CHANGE_SUCCESS
			break;
	}

	/* Stop playing */
	if (cur_state == GST_STATE_PLAYING) {
		g_print("%s reset pipeline frome playing state to ready state...\n", __func__);
		ret = gst_element_set_state (c_player->playbin, GST_STATE_PAUSED);
		if (ret == GST_STATE_CHANGE_FAILURE) {
			g_printerr ("Unable to set the pipeline frome playing to the pause state.\n");
			return -1;
		}
		ret = gst_element_set_state (c_player->playbin, GST_STATE_READY);
		if (ret == GST_STATE_CHANGE_FAILURE) {
			g_printerr ("Unable to set the pipeline frome pause to the ready state.\n");
			return -1;
		}
	} else {
		g_print("%s reset pipeline to null state...\n", __func__);
		ret = gst_element_set_state (c_player->playbin, GST_STATE_NULL);
		if (ret == GST_STATE_CHANGE_FAILURE) {
			g_printerr ("Unable to set the pipeline to the null state.\n");
			return -1;
		}
	}

	/* Set the URI to play */
	g_object_set (c_player->playbin, "uri", uri, NULL);
	g_object_get (c_player->playbin, "uri", &cur_uri, NULL);
	printf(">>>>> Set Uri:%s\n", uri);
	printf(">>>>> Aft Uri:%s\n", cur_uri);
	g_free(cur_uri);

	/* Start playing */
	ret = gst_element_set_state (c_player->playbin, GST_STATE_PLAYING);
	if (ret == GST_STATE_CHANGE_FAILURE) {
		g_printerr ("Unable to set the pipeline to the playing state.\n");
		if (c_player->callback)
			(*c_player->callback)(c_player->userdata, RK_MediaEvent_URLInvalid);

		return -1;
	} else if (ret == GST_STATE_CHANGE_NO_PREROLL) {
		c_player->is_live = TRUE;
	} else {
		c_player->is_live = FALSE;
	}
	c_player->stoped = FALSE;

	g_object_get (c_player->playbin, "current-uri", &cur_uri, NULL);
	printf(">>>>> Cur Uri0:%s\n", cur_uri);
	g_free(cur_uri);

	if (c_player->thread_id == 0) {
		/* Set thread joineable. */
		pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
		err = pthread_create(&c_player->thread_id, NULL, listen_playbin_bus, c_player);
		if (err) {
			g_printerr("Unable to create thread to listen player status, error:%s\n",
					   strerror(err));
			pthread_attr_destroy(&attr);
			return -1;
		}
		pthread_attr_destroy(&attr);
	}

	return 0;
}

int RK_mediaplayer_pause(int iHandle)
{
	GstStateChangeReturn ret;
	RkMediaPlayer *c_player = (RkMediaPlayer *)iHandle;
	GstState cur_state, pending_state;

	g_printerr ("#### %s, %x\n",__func__, iHandle);

	if (!c_player || !c_player->playbin)
		return -EINVAL;

	/* Stop buffering status. */
	c_player->is_buffering = FALSE;

	/* Check current status */
	ret = gst_element_get_state (c_player->playbin, &cur_state, &pending_state, NULL);
	if (ret != GST_STATE_CHANGE_SUCCESS) {
		g_print("Unable to get player status before performing a pause operation\n");
		return -1;
	} else if (cur_state != GST_STATE_PLAYING)
		return 0;

	ret = gst_element_set_state (c_player->playbin, GST_STATE_PAUSED);
	if (ret == GST_STATE_CHANGE_FAILURE) {
		g_printerr ("Unable to set the player to the pause state.\n");
		return -1;
	}

	return 0;
}

int RK_mediaplayer_resume(int iHandle)
{
	GstStateChangeReturn ret;
	RkMediaPlayer *c_player = (RkMediaPlayer *)iHandle;
	GstState cur_state, pending_state;
	int retry_cnt = 10;

	g_printerr ("#### %s, %x\n",__func__, iHandle);

	if (!c_player || !c_player->playbin)
		return -EINVAL;

RETRY_CHECK:
	/* Check current status */
	ret = gst_element_get_state (c_player->playbin, &cur_state, &pending_state, NULL);
	if (ret != GST_STATE_CHANGE_SUCCESS) {
		if (retry_cnt-- > 0) {
			usleep(200000);//200ms
			goto RETRY_CHECK;
		} else {
			g_print("Unable to get player status before performing a resume operation\n");
			return -1;
		}
	} else if (cur_state == GST_STATE_PLAYING)
		return 0;

	ret = gst_element_set_state (c_player->playbin, GST_STATE_PLAYING);
	if (ret == GST_STATE_CHANGE_FAILURE) {
		g_printerr ("Unable to set the player to the playing state.\n");
		return -1;
	}

	return 0;
}

int RK_mediaplayer_get_position(int iHandle)
{
	gint64 current = 0;
	RkMediaPlayer *c_player = (RkMediaPlayer *)iHandle;

	if (!c_player || !c_player->playbin)
		return -EINVAL;

	if (!c_player->playing)
		return -EPERM;

	/* Query the current position of the stream */
	if (!gst_element_query_position (c_player->playbin, GST_FORMAT_TIME, &current))
		return -EAGAIN;

	return (int)(current / 1000000); // ms
}

int RK_mediaplayer_get_duration(int iHandle)
{
	RkMediaPlayer *c_player = (RkMediaPlayer *)iHandle;
	gint64 duration = 0;

	if (!c_player || !c_player->playbin)
		return -EINVAL;

	if (!c_player->playing)
		return -EPERM;

	/* query the stream duration */
	if (!gst_element_query_duration (c_player->playbin, GST_FORMAT_TIME, &duration))
		return -EAGAIN;

	return (int)(duration / 1000000); // ms
}

int RK_mediaplayer_seek(int iHandle, int iMs)
{
	int ret;
	RkMediaPlayer *c_player = (RkMediaPlayer *)iHandle;

	g_printerr ("#### %s, %x ms:%d\n", __func__, iHandle, iMs);

	if (!c_player || !c_player->playbin)
		return -EINVAL;

	/* If seeking is enabled, we have not done it yet, and the time is right, seek */
	if (c_player->seek_enabled) {
		g_print ("\nSeek is enabled, performing seek...\n");
		gst_element_seek_simple (c_player->playbin, GST_FORMAT_TIME,
								 (GstSeekFlags)(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT),
								 iMs * GST_MSECOND);
		ret = 0;
	} else {
		g_print ("\nSeek is not enabled!\n");
		ret = -EAGAIN;
	}

	return ret;
}

int RK_mediaplayer_stop(int iHandle)
{
	g_printerr ("#### %s, %x\n" ,__func__, iHandle);
	GstStateChangeReturn ret;
	RkMediaPlayer *c_player = (RkMediaPlayer *)iHandle;

	if (!c_player || !c_player->playbin)
		return -EINVAL;

	c_player->stoped = TRUE;

	ret = gst_element_set_state (c_player->playbin, GST_STATE_NULL);
	if (ret == GST_STATE_CHANGE_FAILURE) {
		g_printerr ("Unable to set the player to the null state.\n");
		return -1;
	}

	if (c_player->callback)
		(*c_player->callback)(c_player->userdata, RK_MediaEvent_Stop);

	return 0;
}

int RK_mediaplayer_register_callback(int iHandle, void *userdata, RK_media_event_callback cb)
{
	RkMediaPlayer *c_player = (RkMediaPlayer *)iHandle;

	if (!c_player)
		return -EINVAL;

	c_player->callback = cb;
	c_player->userdata = userdata;

	return 0;
}

int RK_mediaplayer_add_music(int iHandle, char *title, char *url)
{
	RkMediaPlayer *c_player = (RkMediaPlayer *)iHandle;
	RkPlayerInfo *new_music = NULL;
	int mem_size = 0;

	g_printerr ("#### %s, %x\n",__func__, iHandle);

	if (!c_player || !url || !strlen(url))
		return -EINVAL;

	new_music = (RkPlayerInfo *) malloc(sizeof(RkPlayerInfo));
	if (!new_music)
		return -ENOSPC;
	/* Reset value */
	memset(new_music, 0, sizeof(RkPlayerInfo));
	/* save url */
	mem_size = strlen(url) + 1;
	new_music->url = (char *) malloc(mem_size);
	memset(new_music->url, 0, mem_size);
	memcpy(new_music->url, url, strlen(url));
	/* save title */
	if (title && strlen(title)) {
		mem_size = strlen(title) + 1;
		new_music->title = (char *) malloc(mem_size);
		memset(new_music->title, 0, mem_size);
		memcpy(new_music->title, title, strlen(title));
	}

	/* Insert into playlist */
	pthread_mutex_lock(&c_player->playlist_mutex);
	if (c_player->playlist_end) {
		new_music->prev = c_player->playlist_end;
		c_player->playlist_end->next = new_music;
		c_player->playlist_end = new_music;
	} else {
		c_player->playlist_start = new_music;
		c_player->playlist_end = new_music;
	}

	c_player->playlist_cnt++;
	pthread_mutex_unlock(&c_player->playlist_mutex);

	return 0;
}

int RK_mediaplayer_show_list(int iHandle)
{
	RkMediaPlayer *c_player = (RkMediaPlayer *)iHandle;
	RkPlayerInfo *tmp_music = NULL;
	int id = 0;

	if (!c_player)
		return -EINVAL;

	pthread_mutex_lock(&c_player->playlist_mutex);
	tmp_music = c_player->playlist_start;

	if (tmp_music == NULL)
		printf("%s PlayList is NULL!\n", __func__);

	while(tmp_music) {
		printf("#%02d Title:%s, URL:%s\n", id++, tmp_music->title, tmp_music->url);
		tmp_music = tmp_music->next;
	}

	if (c_player->playlist_current)
		printf("Current Music Title:%s, URL:%s\n",
			c_player->playlist_current->title,
			c_player->playlist_current->url);

	pthread_mutex_unlock(&c_player->playlist_mutex);

	return 0;
}

int RK_mediaplayer_clear_playlist(int iHandle)
{
	RkMediaPlayer *c_player = (RkMediaPlayer *)iHandle;
	RkPlayerInfo *tmp_music = NULL;

	g_printerr ("#### %s, %x\n",__func__, iHandle);

	if (!c_player)
		return -EINVAL;

	RK_mediaplayer_stop(iHandle);

	pthread_mutex_lock(&c_player->playlist_mutex);
	while (c_player->playlist_start) {
		tmp_music = c_player->playlist_start;
		c_player->playlist_start = tmp_music->next;
		if (tmp_music->title)
			free(tmp_music->title);
		if (tmp_music->url)
			free(tmp_music->url);
		if (tmp_music->singer)
			free(tmp_music->singer);
		if (tmp_music->album)
			free(tmp_music->album);
		if (tmp_music->image_url)
			free(tmp_music->image_url);
		free(tmp_music);
	}

	c_player->playlist_start = NULL;
	c_player->playlist_end = NULL;
	c_player->playlist_current = NULL;
	c_player->playlist_cnt = 0;
	pthread_mutex_unlock(&c_player->playlist_mutex);

	return 0;
}

int RK_mediaplayer_next(int iHandle)
{
	RkMediaPlayer *c_player = (RkMediaPlayer *)iHandle;
	RkPlayerInfo *cur_music = NULL;
	int rand_step = 0;

	g_printerr ("#### %s, %x\n",__func__, iHandle);

	if (!c_player)
		return -EINVAL;

	pthread_mutex_lock(&c_player->playlist_mutex);
	if (!c_player->playlist_current) {
		if (c_player->playlist_start)
			c_player->playlist_current = c_player->playlist_start;
	} else {
		switch (c_player->playlist_mode) {
			case RK_MIDEA_MODE_SEQ:
				c_player->playlist_current = c_player->playlist_current->next;
				break;
			case RK_MIDEA_MODE_RAND:
				if (c_player->playlist_cnt <= 1)
					break;
				srand((unsigned)time(NULL));
				rand_step = rand() % c_player->playlist_cnt;
				if (!rand_step)
					rand_step++;

				while(rand_step) {
					c_player->playlist_current = c_player->playlist_current->next;
					if (!c_player->playlist_current)
						c_player->playlist_current = c_player->playlist_start;
					rand_step--;
				}
				break;
			case RK_MIDEA_MODE_SINGLE:
				/* Do nothing */
				break;
			case RK_MIDEA_MODE_LOOP:
				c_player->playlist_current = c_player->playlist_current->next;
				if (!c_player->playlist_current)
					c_player->playlist_current = c_player->playlist_start;
				break;
		}
	}
	cur_music = c_player->playlist_current;
	pthread_mutex_unlock(&c_player->playlist_mutex);

	if (!cur_music) {
		printf(">>>>> %s PlayList is null, Stop player! <<<<<\n", __func__);
		if (c_player->callback)
			(*c_player->callback)(c_player->userdata, RK_MediaEvent_Stop);

		return -1;
	}

	printf(">>>>> %s cur_music title:%s, url:%s <<<<<\n",
			__func__, cur_music->title, cur_music->url);
	return RK_mediaplayer_play(iHandle, cur_music->url);
}

int RK_mediaplayer_prev(int iHandle)
{
	RkMediaPlayer *c_player = (RkMediaPlayer *)iHandle;
	RkPlayerInfo *cur_music = NULL;
	int rand_step = 0;

	if (!c_player)
		return -EINVAL;

	pthread_mutex_lock(&c_player->playlist_mutex);
	if (!c_player->playlist_current) {
		if (c_player->playlist_end)
			c_player->playlist_current = c_player->playlist_end;
	} else {
		switch (c_player->playlist_mode) {
			case RK_MIDEA_MODE_SEQ:
				if (c_player->playlist_current->prev)
					c_player->playlist_current = c_player->playlist_current->prev;
				break;
			case RK_MIDEA_MODE_RAND:
				if (c_player->playlist_cnt <= 1)
					break;
				srand((unsigned)time(NULL));
				rand_step = rand() % c_player->playlist_cnt;
				if (!rand_step)
					rand_step++;

				while(rand_step) {
					c_player->playlist_current = c_player->playlist_current->next;
					if (!c_player->playlist_current)
						c_player->playlist_current = c_player->playlist_start;
					rand_step--;
				}
				break;
			case RK_MIDEA_MODE_SINGLE:
				/* Do nothing */
				break;
			case RK_MIDEA_MODE_LOOP:
				c_player->playlist_current = c_player->playlist_current->prev;
				if (!c_player->playlist_current)
					c_player->playlist_current = c_player->playlist_end;
				break;
		}
	}
	cur_music = c_player->playlist_current;
	pthread_mutex_unlock(&c_player->playlist_mutex);

	if (!cur_music)
		return -1;

	printf(">>>>> %s cur_music title:%s, url:%s <<<<<\n",
			__func__, cur_music->title, cur_music->url);
	return RK_mediaplayer_play(iHandle, cur_music->url);
}

int RK_mediaplayer_set_mode(int iHandle, RK_MIDEA_MODE mode)
{
	RkMediaPlayer *c_player = (RkMediaPlayer *)iHandle;

	g_printerr ("#### %s, %x, mode = %d\n", __func__, iHandle, mode);

	if (!c_player)
		return -EINVAL;

	c_player->playlist_mode = mode;
	return 0;
}

char* RK_mediaplayer_get_title(int iHandle)
{
	RkMediaPlayer *c_player = (RkMediaPlayer *)iHandle;
	if (!c_player || !c_player->playlist_current)
		return NULL;

	return c_player->playlist_current->title;
}

int RK_mediaplayer_start_playlist(int iHandle)
{
	RkMediaPlayer *c_player = (RkMediaPlayer *)iHandle;
	RkPlayerInfo *cur_music = NULL;
	int rand_step = 0;

	g_printerr ("#### %s, %x\n",__func__, iHandle);

	if (!c_player)
		return -EINVAL;

	pthread_mutex_lock(&c_player->playlist_mutex);
	if (!c_player->playlist_current) {
		c_player->playlist_current = c_player->playlist_end;
		cur_music = c_player->playlist_current;
	}
	pthread_mutex_unlock(&c_player->playlist_mutex);

	if (!cur_music)
		return 0;

	printf(">>>>> %s cur_music title:%s, url:%s <<<<<\n",
			__func__, cur_music->title, cur_music->url);
	return RK_mediaplayer_play(iHandle, cur_music->url);
}

