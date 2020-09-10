#include <stdlib.h>
#include <string.h>
#include "vp_rscode.h"
#include "vp_common.h"
#include "voice_print.h"

/* must be equal to decoder sync_str */
static unsigned char sync_str[2] = {0x0a, 0x0a};

static int lag_sync_table[3][4] = {
	5500, 4700, 5500, 4700,
	0,0,0,0,
	0,0,0,0,
};

#define INIT_STATE 0
#define FRAME_END_STATE 1
#define PRE_ENCODE_STATE 2
#define ENCODE_STATE 3
#define END_STATE 4

typedef struct
{
	/* Reed Solomon codec */
	RS_INFO_T* rs;
	/* input buffer */
	unsigned char* input;
	/* encode buffer */
	unsigned char* enc_buf;
	/* sample rate */
	int sample_rate;
	/* low, middle, high? */
	int freqrange_select;
	/* max string len of input */
	int max_strlen;
	/* rs coding ? */
	int error_correct;
	/* grouping of symbol number in symbols */
	int grouping_symbol_num;
	/* symbol length in samples */
	int symbol_length;
	/* sync symbol number */
	int sync_symbol_num;
	/* error correct symbol number */
	int check_symbol_num;
	/* total symbol number */
	int internal_symbol_num;
	/* */
	/* encoder index */
	int idx;
	/* encoder bit index */
	int bidx;
	/* input len */
	int input_len;
	/* inpub buffer byte idx */
	int input_idx;
	/*status */
	int state;
	/* lag count */
	int lag_count;
	/* lag interval*/
	int lag_symbol_num_interval;
}ENCODER_INFO_T;

static int setParameters(ENCODER_INFO_T* encoder, ENCOEDR_CONFIG_T* encoder_config)
{
	if(encoder_config != NULL)
	{
		if(( encoder_config->freq_type == LOW_FREQ_TYPE && encoder_config->sample_rate < 11025)
			|| ( encoder_config->freq_type == MIDDLE_FREQ_TYPE && encoder_config->sample_rate < 32000)
			|| ( encoder_config->freq_type == HIGH_FREQ_TYPE && encoder_config->sample_rate < 44100))
		{
			printf("set parameter error type: %d, rate: %d\n", encoder_config->freq_type, encoder_config->sample_rate);
			return -1;
		}
		encoder->freqrange_select = encoder_config->freq_type;
		encoder->sample_rate = encoder_config->sample_rate;
		encoder->max_strlen = encoder_config->max_strlen;
		encoder->error_correct = encoder_config->error_correct;
		encoder->grouping_symbol_num = encoder_config->group_symbol_num+1;
		if(encoder->error_correct)
		{
			encoder->check_symbol_num = encoder_config->error_correct_num*2;
		}else
		{
			encoder->check_symbol_num = 0;
		}

		switch(encoder->sample_rate)
		{
		case 11025:
		case 16000:
			encoder->symbol_length = 2 * 1024 * SYMBOL_LENGTH_FACTOR;
			break;
		case 22050:
		case 24000:
		case 32000:
			encoder->symbol_length = 4 * 1024 * SYMBOL_LENGTH_FACTOR;
			break;
		case 44100:
		case 48000:
			encoder->symbol_length = 8 * 1024 * SYMBOL_LENGTH_FACTOR;
			break;
		default:
			printf("sample rate invaild: %d\n", encoder->sample_rate);
			return -1;
		}
	}else {
		encoder->grouping_symbol_num = 10;
		encoder->check_symbol_num = 8;
		encoder->freqrange_select = MIDDLE_FREQ_TYPE;
		encoder->max_strlen = 256;
		encoder->symbol_length = 8*1024;
		encoder->sample_rate = 48000;
		encoder->error_correct = 1;
	}
	encoder->sync_symbol_num = 2;
	encoder->internal_symbol_num = encoder->grouping_symbol_num
										+ encoder->sync_symbol_num + encoder->check_symbol_num;

	encoder->lag_count = 0;
	encoder->lag_symbol_num_interval = SYNC_TONE_SYMBOL_LENGTH;

	if(encoder->grouping_symbol_num + encoder->check_symbol_num > (255-encoder->sync_symbol_num)) {
		return -1;
	}
	return 0;
}

void* encoderInit(ENCOEDR_CONFIG_T* encoder_config, int flag)
{
	ENCODER_INFO_T* encoder;
#ifdef MEMORY_LEAK_DIAGNOSE
	vp_mem_diagnose_init();
#endif
	encoder = (ENCODER_INFO_T*)vp_alloc(sizeof(ENCODER_INFO_T));
	if(encoder != NULL)
	{
		if(setParameters(encoder, encoder_config) != 0)
		{
			encoderDeinit((void*)encoder, flag);
			return NULL;
		}
		encoder->input = (unsigned char*)vp_alloc(encoder->max_strlen+1);
		if(encoder->error_correct)
		{
			encoder->rs = rsInitChar(8, 285, 1, 1, encoder->check_symbol_num, 255-encoder->internal_symbol_num);
		}

		encoder->enc_buf = (unsigned char*)vp_alloc(encoder->internal_symbol_num);
		if(encoder->input  == NULL || (encoder->error_correct && encoder->rs == NULL) || encoder->enc_buf == NULL)
		{
			encoderDeinit((void*)encoder, flag);
			return NULL;
		}
		encoder->idx = 0;
		encoder->state = INIT_STATE;
	}

	return encoder;
}

void encoderReset(void* handle, int flag)
{
	ENCODER_INFO_T* encoder = (ENCODER_INFO_T*)handle;
	encoder->bidx = 0;
	encoder->idx = 0;
	encoder->input_idx  = 0;
	encoder->state = INIT_STATE;
	encoder->lag_count = 0;
}

int encoderSetStr(void* handle,  unsigned char* input)
{
	int len = strlen((const char *)input);
	ENCODER_INFO_T* encoder = (ENCODER_INFO_T*) handle;
	if(len > encoder->max_strlen)
	{
		return -1;
	}
	strcpy(encoder->input, input);
	encoder->input_len = len+1;

	//encoder->input[encoder->input_len+8] = '\0';
	return 0;
}

int encoderGetsize(void* handle)
{
	ENCODER_INFO_T* encoder = (ENCODER_INFO_T*)handle;
	return encoder->symbol_length/2*sizeof(short);
}

static void genTone(ENCODER_INFO_T* encoder, short* outpcm, int freq, int len, int fs)
{
	int i;
	int w1, w2;
	for(i = 0; i < len; i++)
	{
		w1 = VPMULT(2*PI,freq*i)/fs;
		w2 = PI/2 - VPMULT(2*PI,i)/(len-1);
		/* haming(N) = 0.54 - 0.46*cos(2*pi*n/(N-1)), n = 0, 1, 2, ... N-1 */
		w1 = vp_sin(w1);
		w2 = vp_sin(w2);
		w2 = HAMMING_COEF1 - VPMUL(HAMMING_COEF2, w2);
		w1 = VPMUL(w1, w2);
		outpcm[i] = w1;
	}
}

int encoderStrData(void* handle, short* outpcm)
{
	int i, freqidx;
	ENCODER_INFO_T* encoder = (ENCODER_INFO_T*)handle;
#ifdef HAVE_SYNC_TONE
    if(encoder->state == INIT_STATE)
	{
		genTone(encoder, outpcm, lag_sync_table[encoder->freqrange_select][encoder->lag_count], encoder->symbol_length/2, encoder->sample_rate);
		encoder->lag_count++;
		if(encoder->lag_count == encoder->lag_symbol_num_interval*2)
		{
			encoder->state = PRE_ENCODE_STATE;
			encoder->lag_count = 0;
		}
		return ENC_NORMAL;
	}
	if(encoder->state == PRE_ENCODE_STATE)
	{
		genTone(encoder, outpcm, 0, encoder->symbol_length/2, encoder->sample_rate);
		encoder->lag_count++;
		if(encoder->lag_count == 2)
			encoder->state = ENCODE_STATE;
		return ENC_NORMAL;
	}
#endif

	if(encoder->idx == 0 && encoder->bidx == 0)
	{
		for(i = 0; i < encoder->sync_symbol_num; i++)
		{
			encoder->enc_buf[i] = sync_str[i];
		}
		for(i = 0; encoder->input_idx < encoder->input_len && i < encoder->grouping_symbol_num; i++)
		{
			encoder->enc_buf[i+encoder->sync_symbol_num] = encoder->input[encoder->input_idx++];
		}
		/* tail with zeros to force the input of rs encoder be BSYMNUM */
		for(;i < encoder->grouping_symbol_num; i++)
		{
			encoder->enc_buf[i+encoder->sync_symbol_num] = 0;
		}
		if(encoder->error_correct)
		{
			rsEncodeChar(encoder->rs, encoder->enc_buf, encoder->enc_buf+encoder->sync_symbol_num+encoder->grouping_symbol_num);
		}
	}

	if((8 - encoder->bidx) > SYMBOL_BITS)
	{
		freqidx = encoder->enc_buf[encoder->idx] >> encoder->bidx;
		encoder->bidx += SYMBOL_BITS;
	}else
	{
		freqidx = encoder->enc_buf[encoder->idx] >> encoder->bidx;
		encoder->idx++;
		if(encoder->idx < encoder->internal_symbol_num)
		{
			freqidx |= encoder->enc_buf[encoder->idx] << (8-encoder->bidx);
			encoder->bidx = SYMBOL_BITS-(8-encoder->bidx);
		}

	}
	freqidx &= ((1<<SYMBOL_BITS) - 1);

	genTone(encoder, outpcm, vp_freq_point[encoder->freqrange_select][freqidx], encoder->symbol_length/2, encoder->sample_rate);

	if(encoder->idx >= encoder->internal_symbol_num)
	{
		if(encoder->input_idx >= encoder->input_len)
		{
			encoder->state = END_STATE;
			return ENC_END;
		}
		encoder->idx = encoder->bidx = 0;
		encoder->lag_count = 0;
		encoder->state = FRAME_END_STATE;
	}
	
	return ENC_NORMAL;
}

void encoderDeinit(void* handle ,int flag)
{
	ENCODER_INFO_T* encoder  = (ENCODER_INFO_T*)handle;
	if(encoder != NULL)
	{
		if(encoder->input != NULL)
		{
			vp_free(encoder->input);
			encoder->input = NULL;
		}
		if(encoder->error_correct && encoder->rs != NULL)
		{
			rsFreeChar(encoder->rs);
			encoder->rs = NULL;
		}
		if(encoder->enc_buf != NULL)
		{
			vp_free(encoder->enc_buf);
			encoder->enc_buf = NULL;
		}

		vp_free(encoder);
		encoder = NULL;
	}
#ifdef MEMORY_LEAK_DIAGNOSE
	vp_mem_diagnose();
#endif
}
