#include <stdlib.h>
#include <string.h>
#include "vp_common.h"
#include "vp_kiss_fft.h"
#include "vp_rscode.h"
#include "voice_print.h"

#define Q_PRODUCT 15

#define SYNC1_STATE 0
#define SYNC2_STATE 1
#define DECODE_STATE 2
#define FRAME_END_STATE 3
#define END_STATE   4
#define ERROR_STATE -1

/* 4 for 75% overlap, 2 for 50% overlap*/
#define DEC_OVERLAP_FACTOR 8
#define TRANSMIT_PER_SYM 2
#define RECEIVE_PER_SYM (DEC_OVERLAP_FACTOR * TRANSMIT_PER_SYM)

#define vp_abs(a) ((a) > 0 ? (a) : (-(a)))

static int sync_index[4] ={0xf, 0x0, 0xf, 0x0};
static int sync_lag_index[4] ={0xf, 0x0, 0xf, 0x0};

/*setPrms中freq_idx_high赋值时, vp_freq_point的索引必须和这里相等，即10
  sync_char的值可以是0x00 ~ 0x0f之间的值*/
static unsigned char sync_str[2]={0x0a, 0x0a};

typedef struct
{
	int order;
	short* history_buffer;
	short* coeff;
} FIR_FILTER_INFO_T;

typedef struct
{
	int* candidate_freqidx;
	int  candidate_num;
	int  candidate_idx;
} CANDIDATE_INFO_T;

typedef struct
{
	/* maximum of probable string length in bytes */
	int max_strlen;
	/* error correcting coding flag */
	int error_correct;
	/* grouping of symbol number in symbols */
	int grouping_symbol_num;
	/* symbol length in samples */
	int symbol_length;
	/* sync symbol number */
	int sync_symbol_num;
	/* error correct symbol number */
	int check_symbol_num;
	/* total symbol number per frame */
	int internal_symbol_num;
	/* pcm buffer length */
	int pcmbuf_length;
	/* sample rate of the encoded data*/
	int samplerate;
	/* freqrange select*/
	int freqrange_select;
	/* maximum candidate freqidx in each symbol time */
	int max_candidate_num;
	/* threshold 1 in dB, threshold_1 lower than peak should not be a candidate */
	int threshold_1;
	/* threshold 2 in dB, threshold_2 larger than minmum should not be a candidate */
	int threshold_2;
	/* freqency region low limit idx */
	int freq_idx_low;
	/* freqency region high limit idx */
	int freq_idx_high;
	/* freqency region high limit idx cover lag */
	int freq_idx_high_cover_lag;
	/* freqency region head sync idx */
	int freq_idx_lag_low, freq_idx_lag_high;
	/* total frequency bin number of selected frequency region */
	int freq_bin_num;
	int freq_bin_num_lag;
	/* difference of idx of adjoint frequency bin */
	int delta_freq_idx;
	/* fft size */
	int fft_size;
	/* max allowd candidate combin of freq serials */
	int max_allowed_ombin;
	/* frame count */
	int frame_count;
	/* Reed Solomon decoder */
	RS_INFO_T* rs;
	/* fft handle */
	kiss_fft_cfg fft_table;
	/* window */
	int* time_window;
	/* pcm buffer */
	kiss_fft_cpx* pcm_buf;
	/* temp buffer for fft */
	kiss_fft_cpx* temp_buf;
	/* frequency domain buffer */
	kiss_fft_cpx* fd_buf;
	/* power spectrum density buffer */
	unsigned int* psd_buf;
	/* candidate of all time segment */
	CANDIDATE_INFO_T** candidate_array;
	CANDIDATE_INFO_T** candidate_array_lag;
	/* candidate idx array */
	int** candidate_idx_array;
	/* shift idx */
	int shift_idx;
	/* decoder buffer */
	unsigned char* dec_buf;
	/* byte idx of dec_buf*/
	int idx;
	/* bit idx of dec_buf */
	int bidx;
	/* out char buffer */
	unsigned char* out_buf;
	/* freq idx after sorting */
	/* out buffer byte idx */
	int out_idx;
	/* freq idx after descending order sorting */
	int* sort_idx;
	/* status */
	int state;
	/* error byte count */
	int error_count;
	/* fir filter */
	FIR_FILTER_INFO_T* filter;
	/* sync tone define regions, for sync count and count total count */
    int lag_count;
	/* blablabla */
	int lag_symbol_num_internal;
    /* totally process freq range*/
	int process_freq_num;
	/* totally process freq start idx*/
    int process_start_idx;
	/* for debug, total play time*/
	int process_freq_num_lag;
	/* totally process freq start idx*/
    int process_start_idx_lag;
	int start_tone_combo;
	/* for debug, total play time*/
	unsigned int play_time;
	unsigned int fft_fetch_time;
}DECODER_INFO_T;

static int firfilterCoeffCaculate(FIR_FILTER_INFO_T* filter, int fs, int f0, tFILTER_TYPE type)
{
	int ALPHA = VPMULT(f0, 32768*2)/fs;
	int order = filter->order;
	int N = order/2;
	int i,ret=0;
	int w1, w2, w3;
	short* coeff = filter->coeff;
	/* we only support an even order now
		because odd order of symmetric fir filter can not be an highpass filter
	*/
	if(order & 1)
	{
		printf("only support an even order\n");
		return -1;
	}

	/* we use hamming windowing here to get an linear phase filter
		wn = 0.54-0.46cos(2*pi*n/L) 0<= n <= L
	*/
	switch(type)
	{
	case LOW_PASS_TYPE:
		coeff[N] = ALPHA;
		break;
	case HIGH_PASS_TYPE:
		coeff[N] = 32768-ALPHA;
		break;
	default:
		printf("don't support filter type : %d\n", type);
		return-1;

	}
	for(i = 1; i <= N; i++)
	{
		w1 = PI/2-VPMULT(2*PI,(i+N))/order;
		w1 = vp_sin(w1);
		w1 = HAMMING_COEF1 - VPMUL(w1,HAMMING_COEF2);
		w2 = i*PI;
		w3 = VPMUL(w2,ALPHA);
		w3 = vp_sin(w3);
		w3 = VPMULT(w3,32768)/w2;
		w3 = VPMUL(w3,w1);
		switch(type)
		{
		case LOW_PASS_TYPE:
			coeff[i+N] = coeff[-i+N] = VPMUL(w3,w1);
			break;
		case HIGH_PASS_TYPE:
			coeff[i+N] = coeff[-i+N] = VPMUL(-w3,w1);
			break;
		default:
			ret = -1;
			printf("don't support filter type : %d\n", type);
			break;
		}
	}
#if 0
	for(i = 0; i <= order; i++)
	{
		printf("%d\n", coeff[i]);
	}
#endif
	return ret;
}

static void firfilterDestroy(FIR_FILTER_INFO_T* filter)
{
	if(filter)
	{
		if(filter->history_buffer)
		{
			vp_free(filter->history_buffer);
		}
		if(filter->coeff)
		{
			vp_free(filter->coeff);
		}
		vp_free(filter);
	}
}

static void firfilterReset(FIR_FILTER_INFO_T* filter)
{
	vp_memset(filter->history_buffer,0,(sizeof(short)*filter->order));
}

static FIR_FILTER_INFO_T* firfilterInit(int order, int fs, int f0, tFILTER_TYPE type)
{
	FIR_FILTER_INFO_T* filter = vp_alloc(sizeof(FIR_FILTER_INFO_T));
	if(filter)
	{
		filter->order = order;
		//filter->coeff = coeff;
		filter->history_buffer = vp_alloc(sizeof(short)*order);
		if(filter->history_buffer == NULL)
		{
			firfilterDestroy(filter);
			filter = NULL;
		}
		filter->coeff = vp_alloc(sizeof(short)*(order+1));
		if(filter->coeff == NULL)
		{
			firfilterDestroy(filter);
			filter = NULL;
		}
		if (firfilterCoeffCaculate(filter, fs, f0, type) != 0)
		{
			firfilterDestroy(filter);
			filter = NULL;
		}
	}
	return filter;
}

static void firfilterProcess(FIR_FILTER_INFO_T* filter, short* in, short* out, int len)
{
	int order = filter->order;
	short* coeff = filter->coeff;
	short* history_buffer = filter->history_buffer;
	int i,j;
	long long result;
	int loop1 = order < len ? order : len;
	for(i = 0; i < loop1; i++)
	{
		result = 0;
		for(j = 0; j < order -i; j++)
		{
			result += VPMULT(coeff[i+1+j],history_buffer[order-1-j]);
		}
		for(j = 0; j <= i; j++)
		{
			result += VPMULT(coeff[j], in[i-j]);
		}
		result = VPSHR(result,15);
		out[i] = VPSAT(result);
	}
	for(i = order; i < len; i++)
	{
		result = 0;
		for(j = 0; j <= order; j++)
		{
			result += VPMULT(coeff[j], in[i-j]);
		}
		result = VPSHR(result,15);
		out[i] = VPSAT(result);
	}

	for(i = 0; i < order-len; i++)
	{
		history_buffer[i] = history_buffer[i+len];
	}
	for(;i < order; i++)
	{
		history_buffer[i] = in[len-order+i];
	}

}

static void decoderBufReset(DECODER_INFO_T* decoder)
{
	vp_memset(decoder->dec_buf, 0, sizeof(char)*decoder->internal_symbol_num);
	decoder->idx = 0;
	decoder->bidx = 0;
}

static void defaultPrmsLoad(DECODER_INFO_T* decoder)
{
	decoder->max_strlen = 256;
	decoder->error_correct = 1;
	decoder->grouping_symbol_num = 10;
	decoder->max_allowed_ombin = 1024;
	decoder->sync_symbol_num = 2;
	decoder->check_symbol_num = 8;
	decoder->symbol_length = 8*1024;
	decoder->samplerate = 48000;
	decoder->freqrange_select = MIDDLE_FREQ_TYPE;
	decoder->fft_size = decoder->symbol_length/TRANSMIT_PER_SYM;

	decoder->max_candidate_num = 2;
	decoder->threshold_1 = 328; /* -26dB(0.01) in Q15 */
	decoder->threshold_2 = 3276800; /* 30dB(100) in Q15 */
	decoder->internal_symbol_num = (decoder->grouping_symbol_num+decoder->sync_symbol_num + decoder->check_symbol_num);
	decoder->pcmbuf_length = decoder->symbol_length/TRANSMIT_PER_SYM/**decoder->internal_symbol_num*/;
}

static int setParameters(DECODER_INFO_T* decoder, DECODER_CONFIG_T* decoder_config)
{
	if(decoder_config != NULL)
	{
		if(( decoder_config->freq_type == LOW_FREQ_TYPE && decoder_config->sample_rate < 11025)
		   || ( decoder_config->freq_type == MIDDLE_FREQ_TYPE && decoder_config->sample_rate < 32000)
		   || ( decoder_config->freq_type == HIGH_FREQ_TYPE && decoder_config->sample_rate < 44100))
		{
			return -1;
		}
		decoder->max_strlen = decoder_config->max_strlen;
		decoder->freqrange_select =  decoder_config->freq_type;
		decoder->samplerate = decoder_config->sample_rate;
		decoder->error_correct = decoder_config->error_correct;
		switch(decoder->samplerate)
		{
		case 11025:
		case 16000:
            decoder->symbol_length = 2 * 1024 * SYMBOL_LENGTH_FACTOR;
			break;
		case 22050:
		case 24000:
		case 32000:
			decoder->symbol_length = 4 * 1024 * SYMBOL_LENGTH_FACTOR;
			break;
		case 44100:
		case 48000:
			decoder->symbol_length = 8 * 1024 * SYMBOL_LENGTH_FACTOR;
			break;
		default:
			return -1;
		}

		if(decoder->error_correct)
		{
			decoder->check_symbol_num = decoder_config->error_correct_num*2;
			decoder->max_candidate_num = 2;
			decoder->max_allowed_ombin = 1024;
			decoder->threshold_1 = 1024; /* -26dB(0.01) in Q15 */
			decoder->threshold_2 = 3276800; /* 30dB(100) in Q15 */
		} else {
			decoder->max_candidate_num = 2;
			decoder->check_symbol_num = 0;
			decoder->max_allowed_ombin = 48;
			decoder->threshold_1 = 4096; /* -26dB(0.01) in Q15 */
			decoder->threshold_2 = 3276800; /* 30dB(100) in Q15 */
		}

		decoder->grouping_symbol_num = decoder_config->group_symbol_num + 1;//10;
		//decoder->max_allowed_ombin = 1024;
		decoder->sync_symbol_num = 2;
		if(decoder->grouping_symbol_num+decoder->check_symbol_num > (255 - decoder->sync_symbol_num))
		{
			return -1;
		}

		decoder->fft_size = decoder->symbol_length/TRANSMIT_PER_SYM;
		decoder->internal_symbol_num = (decoder->grouping_symbol_num+decoder->sync_symbol_num + decoder->check_symbol_num);
		decoder->pcmbuf_length = decoder->symbol_length/TRANSMIT_PER_SYM/**decoder->internal_symbol_num*/;
	}else {
		defaultPrmsLoad(decoder);
	}
#if 0
	printf("decoder->freqrange_select: %d\n", decoder->freqrange_select);
	printf("decoder->fft_size: %d\n", decoder->fft_size);
	printf("decoder->samplerate: %d\n", decoder->samplerate);
	printf("vp_freq_point[decoder->freqrange_select][0]: %d\n", vp_freq_point[decoder->freqrange_select][0]);
	printf("vp_freq_point[decoder->freqrange_select][15]: %d\n", vp_freq_point[decoder->freqrange_select][15]);
#endif
	decoder->freq_idx_low = vp_freq_point[decoder->freqrange_select][0]*decoder->fft_size/decoder->samplerate;
	decoder->freq_idx_high = (vp_freq_point[decoder->freqrange_select][10]*decoder->fft_size+decoder->samplerate/2)/decoder->samplerate;
	decoder->freq_idx_high_cover_lag = (vp_freq_lag_top[decoder->freqrange_select]*decoder->fft_size+decoder->samplerate/2)/decoder->samplerate;
	decoder->delta_freq_idx = vp_freq_delta[decoder->freqrange_select]*decoder->fft_size/decoder->samplerate;
	decoder->freq_bin_num = decoder->freq_idx_high - decoder->freq_idx_low + 1;
	decoder->freq_bin_num_lag = decoder->freq_idx_high_cover_lag - decoder->freq_idx_high;
#if 0
	printf("decoder->freq_idx_high: %d\n", decoder->freq_idx_high);
	printf("decoder->freq_idx_low: %d\n", decoder->freq_idx_low);
	printf("decoder->freq_idx_high_cover_lag: %d\n", decoder->freq_idx_high_cover_lag);
	printf("decoder->freq_idx_high: %d\n", decoder->freq_idx_high);
	printf("decoder->freq_bin_num: %d\n", decoder->freq_bin_num);
	printf("decoder->freq_bin_num_lag: %d\n", decoder->freq_bin_num_lag);
#endif
	decoder->freq_idx_lag_low = vp_freq_lag[decoder->freqrange_select]*decoder->fft_size/decoder->samplerate;
	decoder->freq_idx_lag_high = (vp_freq_lag[decoder->freqrange_select] + 800)*decoder->fft_size/decoder->samplerate;
	decoder->lag_count = 0;
	decoder->lag_symbol_num_internal = SYNC_TONE_SYMBOL_LENGTH;
	return 0;
}

static void sortIdxReset(DECODER_INFO_T* decoder, int start, int range)
{
	int i;
	for(i = 0; i < range; i++)
	{
		decoder->sort_idx[i + start - decoder->process_start_idx] = i+start;
	}
}

static void candidateReset(DECODER_INFO_T* decoder)
{
	int i,k;
	for(k = 0; k < DEC_OVERLAP_FACTOR; k++)
	{
		for(i = 0; i < decoder->internal_symbol_num*TRANSMIT_PER_SYM; i++)
		{
			vp_memset(decoder->candidate_array[k][i].candidate_freqidx, 0, sizeof(int)*decoder->max_candidate_num);
			decoder->candidate_array[k][i].candidate_num = 0;
			decoder->candidate_array[k][i].candidate_idx = 0;
		}
		for(i = 0; i < decoder->lag_symbol_num_internal*TRANSMIT_PER_SYM; i++)
		{
			vp_memset(decoder->candidate_array_lag[k][i].candidate_freqidx, 0, sizeof(int)*decoder->max_candidate_num);
			decoder->candidate_array_lag[k][i].candidate_num = 0;
			decoder->candidate_array_lag[k][i].candidate_idx = 0;
		}
	}
	sortIdxReset(decoder, decoder->process_start_idx, (decoder->process_freq_num + decoder->process_freq_num_lag));
}

static void frameReset(DECODER_INFO_T* decoder)
{
		decoder->error_count = 0;
		decoderBufReset(decoder);
		decoder->frame_count = 0;
		decoder->state = SYNC2_STATE;
		candidateReset(decoder);
}

static void blackmanWindowInit(int* window, int L)
{
	/* 0.42-0.5*cos(2*pi*n/(L-1))+0.08*cos(4*pi*n/(L-1))*/
	int w1, w2, w3,i;
	for(i = 0; i < L; i++)
	{
		w1 = PI/2-VPMULT(2*PI,i)/(L-1);
		w2 = PI/2-2*w1;
		if(w2 < (-2*PI))
		{
			w2 += 2*PI;
		}
		w1 = vp_sin(w1);
		w2 = vp_sin(w2);
		w3 = BLACKMAN_COEF1 - VPMUL(BLACKMAN_COEF2, w1) + VPMUL(BLACKMAN_COEF3, w2);
		window[i] = w3;
	}
}

void* decoderInit(DECODER_CONFIG_T *config, int flag)
{
	int i,k/*, symsize, gfpoly, fcr, prim, nroots, pad*/;
	DECODER_INFO_T* decoder;
#ifdef MEMORY_LEAK_DIAGNOSE
	vp_mem_diagnose_init();
#endif

	decoder = vp_alloc(sizeof(DECODER_INFO_T));
	if(decoder)
	{
		/*defaultPrmsLoad(decoder);
		decoder->max_strlen = config->max_strlen;
		decoder->freqrange_select = config->freq_type;
		decoder->freq_idx_low = vp_freq_point[decoder->freqrange_select][0]*decoder->fft_size/decoder->samplerate;
		decoder->freq_idx_high = (vp_freq_point[decoder->freqrange_select][15]*decoder->fft_size+decoder->samplerate/2)/decoder->samplerate;
		decoder->delta_freq_idx = vp_freq_delta[decoder->freqrange_select]*decoder->fft_size/decoder->samplerate;
		decoder->freq_bin_num = decoder->freq_idx_high - decoder->freq_idx_low + 1;
		*/
		if(setParameters(decoder, config) != 0)
		{
			decoderDeinit(decoder, flag);
			return NULL;
		}

		decoder->process_freq_num  = decoder->freq_bin_num; //(decoder->freq_idx_low > decoder->freq_bin_num ? 2*decoder->freq_bin_num : (decoder->freq_bin_num + decoder->freq_idx_low));
		decoder->process_start_idx = decoder->freq_idx_low; //(decoder->freq_bin_num > 0)?(decoder->freq_idx_low - decoder->freq_bin_num):0;
		decoder->process_freq_num_lag  = decoder->freq_bin_num_lag;
		decoder->process_start_idx_lag = decoder->freq_idx_high + 1;

		decoder->fft_fetch_time    = decoder->fft_size*1000/(DEC_OVERLAP_FACTOR*decoder->samplerate);
		decoder->time_window = vp_alloc(sizeof(int)*decoder->fft_size);
		decoder->pcm_buf = vp_alloc(sizeof(kiss_fft_cpx)*decoder->pcmbuf_length);
		decoder->temp_buf = vp_alloc(sizeof(kiss_fft_cpx)*decoder->fft_size);
		decoder->fd_buf = vp_alloc(sizeof(kiss_fft_cpx)*decoder->fft_size);
		//printf("decoder->process_freq_num: %d\n", decoder->process_freq_num);
		//printf("decoder->process_freq_num_lag: %d\n", decoder->process_freq_num_lag);
		decoder->psd_buf = vp_alloc(sizeof(unsigned int)*(decoder->process_freq_num + decoder->process_freq_num_lag));
		decoder->candidate_array = vp_alloc(sizeof(CANDIDATE_INFO_T*)*DEC_OVERLAP_FACTOR);
		decoder->candidate_array_lag = vp_alloc(sizeof(CANDIDATE_INFO_T*)*DEC_OVERLAP_FACTOR);
		if(decoder->error_correct)
		{
			decoder->rs = rsInitChar(8, 285, 1, 1, decoder->check_symbol_num, 255-decoder->internal_symbol_num/*symsize, gfpoly, fcr, prim, nroots, pad*/);
		}
		decoder->fft_table = kissFftAlloc(decoder->fft_size, 0, NULL, NULL);
		decoder->sort_idx = vp_alloc(sizeof(int)*(decoder->process_freq_num + decoder->process_freq_num_lag));
		decoder->candidate_idx_array = vp_alloc(sizeof(int*)*decoder->max_allowed_ombin);
		decoder->dec_buf = vp_alloc(sizeof(unsigned char)*decoder->internal_symbol_num);
		decoder->out_buf = vp_alloc(sizeof(unsigned char)*(decoder->max_strlen+1));

		if(!decoder->pcm_buf || !decoder->fd_buf || !decoder->psd_buf || !decoder->sort_idx
			|| !decoder->candidate_array || !decoder->candidate_array_lag
			||(decoder->error_correct && !decoder->rs) || !decoder->fft_table
			|| !decoder->candidate_idx_array || !decoder->dec_buf
			|| !decoder->out_buf || !decoder->time_window || !decoder->temp_buf)
		{
			decoderDeinit((void*) decoder, flag);
			return NULL;
		}

		blackmanWindowInit(decoder->time_window, decoder->fft_size);
		for(k = 0; k < DEC_OVERLAP_FACTOR; k++)
		{
			decoder->candidate_array[k] = vp_alloc(decoder->internal_symbol_num*TRANSMIT_PER_SYM*sizeof(CANDIDATE_INFO_T));
			if(decoder->candidate_array[k] == NULL)
			{
				decoderDeinit((void*)decoder, flag);
				return NULL;
			}
			vp_memset(decoder->candidate_array[k], 0, decoder->internal_symbol_num*TRANSMIT_PER_SYM*sizeof(CANDIDATE_INFO_T));

			for(i = 0; i < decoder->internal_symbol_num*TRANSMIT_PER_SYM; i++)
			{
				decoder->candidate_array[k][i].candidate_freqidx = vp_alloc(sizeof(int)*decoder->max_candidate_num);
				if(!decoder->candidate_array[k][i].candidate_freqidx)
				{
					decoderDeinit((void*) decoder, flag);
					return NULL;
				}
			}
		}
		for(k = 0; k < DEC_OVERLAP_FACTOR; k++)
		{
			decoder->candidate_array_lag[k] = vp_alloc(decoder->lag_symbol_num_internal*TRANSMIT_PER_SYM*sizeof(CANDIDATE_INFO_T));
			if(decoder->candidate_array_lag[k] == NULL)
			{
				decoderDeinit((void*)decoder, flag);
				return NULL;
			}
			vp_memset(decoder->candidate_array_lag[k], 0, decoder->lag_symbol_num_internal*TRANSMIT_PER_SYM*sizeof(CANDIDATE_INFO_T));

			for(i = 0; i < decoder->lag_symbol_num_internal*TRANSMIT_PER_SYM; i++)
			{
				decoder->candidate_array_lag[k][i].candidate_freqidx = vp_alloc(sizeof(int)*decoder->max_candidate_num);
				if(!decoder->candidate_array_lag[k][i].candidate_freqidx)
				{
					decoderDeinit((void*) decoder, flag);
					return NULL;
				}
			}
		}
		decoder->shift_idx = 0;

		for(i = 0; i < decoder->max_allowed_ombin; i++)
		{
			decoder->candidate_idx_array[i] = vp_alloc(sizeof(int)*TRANSMIT_PER_SYM*decoder->internal_symbol_num);
			if(!decoder->candidate_idx_array[i])
			{
				decoderDeinit((void*) decoder, flag);
				return NULL;
			}
		}

		decoder->filter = firfilterInit(32, decoder->samplerate, vp_freq_cutoff[decoder->freqrange_select], vp_filter_type[decoder->freqrange_select]);//firfilterInit(32, coefftable);
		if(decoder->filter == NULL)
		{
			decoderDeinit((void*) decoder, flag);
			return NULL;
		}

		decoder->out_idx = 0;
		decoder->error_count = 0;
		decoderBufReset(decoder);
		decoder->frame_count = 0;
		sync_index[0] = decoder->freq_idx_high;
		sync_index[1] = decoder->freq_idx_low;
		sync_index[2] = decoder->freq_idx_high;
		sync_index[3] = decoder->freq_idx_low;
		sync_lag_index[0] = decoder->freq_idx_lag_high;
		sync_lag_index[1] = decoder->freq_idx_lag_low;
		sync_lag_index[2] = decoder->freq_idx_lag_high;
		sync_lag_index[3] = decoder->freq_idx_lag_low;
		decoder->state = SYNC1_STATE;
	}
	return (void*)decoder;
}

void decoderReset(void* handle, int flag)
{
	DECODER_INFO_T* decoder = (DECODER_INFO_T*)handle;
	vp_memset(decoder->pcm_buf,0,sizeof(kiss_fft_cpx)*decoder->pcmbuf_length);
	decoder->shift_idx = 0;
	decoder->out_idx = 0;
	decoder->error_count = 0;
	decoderBufReset(decoder);
	decoder->frame_count = 0;
	decoder->lag_count   = 0;
	decoder->state = SYNC1_STATE;
	firfilterReset(decoder->filter);

}

int decoderGetSize(void* handle, int flag)
{
	DECODER_INFO_T* decoder = (DECODER_INFO_T*)handle;
	return decoder->fft_size/DEC_OVERLAP_FACTOR;
}

static void psdSorting(DECODER_INFO_T* decoder, int start, int range)
{
	int i, j, temp;
	register int* psd = (int*)(&decoder->psd_buf[start - decoder->process_start_idx]);
	register int* sort_idx = (int*)(&decoder->sort_idx[start - decoder->process_start_idx]);
#if LOW_FREQ_SIMPLE_NOISE_SHAPING
	if(start != decoder->process_start_idx)
	{
		for(i = 0; i < range - 1; i++)
		{
			if(i < range/2)
				psd[i] = vp_abs(psd[i] - psd[range - 1 -i]);
			else
				psd[i] = 0;
		}
	}
#endif
	for(i = 0; i < range - 1; i++)
	{
		for(j = i+1; j < range; j++)
		{
			if(psd[j] > psd[i])
			{
				temp = psd[j];
				psd[j] = psd[i];
				psd[i] = temp;
				temp = sort_idx[j];
				sort_idx[j] = sort_idx[i];
				sort_idx[i] = temp;

			}
		}

	}
    //printf("shift_idx : %d, sort_idx : %d", decoder->shift_idx, sort_idx[0]);
}

static int isValidIdx(DECODER_INFO_T* decoder, int idx, int symbol_i, int len)
{
	int i, v;
	for(i = 0; i < len; i++)
	{
		v = decoder->candidate_array[decoder->shift_idx][symbol_i].candidate_freqidx[i];
		if( vp_abs(idx-v) <= decoder->delta_freq_idx)
		{
			return 0;
		}
	}
	return 1;
}

static void showCandidate(DECODER_INFO_T* decoder)
{
	int symbol_i = decoder->internal_symbol_num*TRANSMIT_PER_SYM;

	int j, idx, shift_idx, jdx;
	FILE * text = NULL;
	char name[128] = {0};
	char *prefix = "/tmp/print_result";
	char *postfix ="txt";

	sprintf(name, "%s_%dms-%dms.%s", prefix, decoder->play_time - decoder->fft_fetch_time, decoder->play_time, postfix);
	text = fopen(name, "wb");

	fprintf(text,"---------- print start -----------\n");

	{
		int* psd  = &decoder->psd_buf[decoder->process_start_idx_lag - decoder->process_start_idx];
		int* sort_idx = &decoder->sort_idx[decoder->process_start_idx_lag - decoder->process_start_idx];
		for(j = 0; j < decoder->process_freq_num_lag; j++)
		{
			fprintf(text, "psd[%d] : %d, sort_idx : %d\n", j, psd[j], sort_idx[j]);
		}
	}
	fprintf(text,"----------- print end ------------\n");
	fclose(text);
}

static int candidateUpdate(DECODER_INFO_T* decoder, int range, int symbol_i)
{
	int i, j;

	int* psd  = &decoder->psd_buf[0];
	int* sort_idx = &decoder->sort_idx[0];

	int low_limit2 = (int)(((long long)decoder->threshold_2*psd[range-1]) >> Q_PRODUCT);
	int low_limit1 = (int)(((long long)decoder->threshold_1*psd[0]) >> Q_PRODUCT);

	/* shift */

	for(j = 0; j < symbol_i; j++)
	{
		vp_memcpy(decoder->candidate_array[decoder->shift_idx][j].candidate_freqidx,decoder->candidate_array[decoder->shift_idx][j+1].candidate_freqidx,sizeof(int)*decoder->candidate_array[decoder->shift_idx][j+1].candidate_num);
		decoder->candidate_array[decoder->shift_idx][j].candidate_num = decoder->candidate_array[decoder->shift_idx][j+1].candidate_num;
		decoder->candidate_array[decoder->shift_idx][j].candidate_idx = decoder->candidate_array[decoder->shift_idx][j+1].candidate_idx;
	}

	decoder->candidate_array[decoder->shift_idx][symbol_i].candidate_idx = 0;
	decoder->candidate_array[decoder->shift_idx][symbol_i].candidate_num = 0;

	if(psd[0] > low_limit2)
	{
		decoder->candidate_array[decoder->shift_idx][symbol_i].candidate_freqidx[decoder->candidate_array[decoder->shift_idx][symbol_i].candidate_num++]
									= sort_idx[0];

		for(j = 1; j < range&& decoder->candidate_array[decoder->shift_idx][symbol_i].candidate_num < decoder->max_candidate_num; j++)
		{
			if(psd[j] > low_limit1 && psd[j] > low_limit2 )
			{
				if(isValidIdx(decoder, sort_idx[j], symbol_i, decoder->candidate_array[decoder->shift_idx][symbol_i].candidate_num))
				{
					decoder->candidate_array[decoder->shift_idx][symbol_i].candidate_freqidx[decoder->candidate_array[decoder->shift_idx][symbol_i].candidate_num++]
									= sort_idx[j];
				}
			}else
			{
				break;
			}
		}
		return 0;
	}

	return -1;
}

static int lagCandidateUpdate(DECODER_INFO_T* decoder, int range, int symbol_i)
{
	int i, j;

	int* psd  = &decoder->psd_buf[0];
	int* sort_idx = &decoder->sort_idx[0];

	int low_limit2 = (int)(((long long)decoder->threshold_2*psd[range-1]) >> Q_PRODUCT);
	int low_limit1 = (int)(((long long)decoder->threshold_1*psd[0]) >> Q_PRODUCT);

	/* shift */

	for(j = 0; j < symbol_i; j++)
	{
		vp_memcpy(decoder->candidate_array_lag[decoder->shift_idx][j].candidate_freqidx,decoder->candidate_array_lag[decoder->shift_idx][j+1].candidate_freqidx,sizeof(int)*decoder->candidate_array_lag[decoder->shift_idx][j+1].candidate_num);
		decoder->candidate_array_lag[decoder->shift_idx][j].candidate_num = decoder->candidate_array_lag[decoder->shift_idx][j+1].candidate_num;
		decoder->candidate_array_lag[decoder->shift_idx][j].candidate_idx = decoder->candidate_array_lag[decoder->shift_idx][j+1].candidate_idx;
	}

	decoder->candidate_array_lag[decoder->shift_idx][symbol_i].candidate_idx = 0;
	decoder->candidate_array_lag[decoder->shift_idx][symbol_i].candidate_num = 0;

	if(psd[0] > low_limit2)
	{
		decoder->candidate_array_lag[decoder->shift_idx][symbol_i].candidate_freqidx[decoder->candidate_array_lag[decoder->shift_idx][symbol_i].candidate_num++]
									= sort_idx[0];

		for(j = 1; j < range&& decoder->candidate_array_lag[decoder->shift_idx][symbol_i].candidate_num < decoder->max_candidate_num; j++)
		{
			if(psd[j] > low_limit1 && psd[j] > low_limit2 )
			{
				if(isValidIdx(decoder, sort_idx[j], symbol_i, decoder->candidate_array_lag[decoder->shift_idx][symbol_i].candidate_num))
				{
					decoder->candidate_array_lag[decoder->shift_idx][symbol_i].candidate_freqidx[decoder->candidate_array_lag[decoder->shift_idx][symbol_i].candidate_num++]
									= sort_idx[j];
				}
			}else
			{
				break;
			}
		}
		return 0;
	}

	return -1;
}

static int storeBits(DECODER_INFO_T* decoder, unsigned char c, int bits)
{
	int bidx, idx;
	bidx = decoder->bidx;
	idx = decoder->idx;

	while(bits > 0)
	{
		if(idx >= decoder->internal_symbol_num)
		{
			return -1;
		}
		if(8-bidx > bits)
		{
			decoder->dec_buf[idx] |= c<<bidx;
			bidx += bits;
			bits = 0;
		}else
		{
			decoder->dec_buf[idx++] |= c<<bidx;
			
			bits -= (8-bidx);
			c >>= (8-bidx);
			bidx = 0;
		}
	}
	decoder->bidx = bidx;
	decoder->idx = idx;
	return 0;
}

static int findNull(char* str, int len)
{
	int i;
	for(i = len-1; i >= 0; i--)
	{
		if(str[i] == '\0')
		{
			return 1;
		}
	}
	return 0;
}

static int findSync(DECODER_INFO_T* decoder)
{
	int i, j, total_num = 1;
	CANDIDATE_INFO_T* candidate_array = decoder->candidate_array[decoder->shift_idx];
	for(i = 0; i < decoder->sync_symbol_num*TRANSMIT_PER_SYM; i++)
	{
		for(j = 0; j < candidate_array[i].candidate_num; j++)
		{
			if(vp_abs(candidate_array[i].candidate_freqidx[j] - sync_index[i]) < decoder->delta_freq_idx)
			{
				candidate_array[i].candidate_idx = j;
				break;
			}
		}
		if(j == candidate_array[i].candidate_num)
		{
			return -1;
		}

	}
	return 0;
}

static int findSyncLag(DECODER_INFO_T* decoder)
{
	int i, j, total_num = 1;
	{
		CANDIDATE_INFO_T* candidate_array = decoder->candidate_array_lag[decoder->shift_idx];
		for(i = 0; i < decoder->lag_symbol_num_internal*TRANSMIT_PER_SYM; i++)
		{
			for(j = 0; j < candidate_array[i].candidate_num; j++)
			{
				if(vp_abs(candidate_array[i].candidate_freqidx[j] - sync_lag_index[i]) < decoder->delta_freq_idx)
				{
					candidate_array[i].candidate_idx = j;
					break;
				}
			}
			if(j == candidate_array[i].candidate_num)
			{
				return -1;
			}
		}
	}
	return 0;
}

static unsigned char freqIdx2Char(DECODER_INFO_T* decoder, int freq_idx)
{
	int freq = freq_idx*decoder->samplerate/decoder->fft_size;
	int i;
	for(i = 0; i < 15; i++)
	{
		if(vp_abs(freq - vp_freq_point[decoder->freqrange_select][i]) < vp_abs(freq - vp_freq_point[decoder->freqrange_select][i+1]))
		{
			return i;
		}
	}
	return 15;
}

static void caculatePsd(kiss_fft_cpx* Xf, unsigned int* power, int length, int idx_start, int base_start)
{
	int i;

	for(i = 0; i < length ; i++)
	{
		power[i+idx_start - base_start] = VPMULT16(Xf[i+idx_start].r, Xf[i+idx_start].r) + VPMULT16(Xf[i+idx_start].i, Xf[i+idx_start].i);
	}
}

static void windowingPcm(kiss_fft_cpx* pcm, kiss_fft_cpx* out, int* window, int L)
{
	int i;
	for(i = 0; i < L; i++)
	{
		out[i].r = VPMUL(window[i], pcm[i].r);
		out[i].i = VPMUL(window[i], pcm[i].i);
	}
}

int decoderPcmData(void* handle, short* pcm)
{
	int i, j, k, idx_total, idx_total1;
	int ret = 0, ret_lag = 0;
	int sync_score = 0;
	DECODER_INFO_T* decoder = (DECODER_INFO_T*)handle;
	int max_amp, count_leadingzeros;

	/* filter input to keep the precision of the fixed point number
	   we use the temp_buf, because it can be used uninitialized later
	*/
	short* filt_tmpbuf = (short*)decoder->temp_buf;
	firfilterProcess(decoder->filter, pcm, filt_tmpbuf,decoder->fft_size/DEC_OVERLAP_FACTOR);

	decoder->play_time += decoder->fft_fetch_time;//milisec

	/*shift buffer */
	for(i = 0; i < decoder->pcmbuf_length - decoder->fft_size/DEC_OVERLAP_FACTOR; i++)
	{
		decoder->pcm_buf[i].r = decoder->pcm_buf[i+decoder->fft_size/DEC_OVERLAP_FACTOR].r;
		decoder->pcm_buf[i].i = decoder->pcm_buf[i+decoder->fft_size/DEC_OVERLAP_FACTOR].i;
	}

	for(i = 0; i < decoder->fft_size/DEC_OVERLAP_FACTOR; i++)
	{
		decoder->pcm_buf[i+decoder->pcmbuf_length - decoder->fft_size/DEC_OVERLAP_FACTOR].r
																= filt_tmpbuf[i]/*pcm[i]*/;
		decoder->pcm_buf[i+decoder->pcmbuf_length - decoder->fft_size/DEC_OVERLAP_FACTOR].i
																= 0;
	}

	/* normalize to avoid precision dropping */
	max_amp = 0;
	for(i = 0; i < decoder->fft_size; i++)
	{
		int abs_amp = VPABS(decoder->pcm_buf[i].r);
		max_amp = (max_amp > abs_amp) ? max_amp : abs_amp;
	}
	/*count leading zeros of max_amp */
	count_leadingzeros = 0;
	while((max_amp & 0x8000 ) == 0 && count_leadingzeros <= 75)
	{
		count_leadingzeros++;
		max_amp <<= 1;
	}
	if(count_leadingzeros > 1)
	{
		count_leadingzeros -= 1;
		for(i = 0; i < decoder->fft_size; i++)
		{
			decoder->pcm_buf[i].r <<= count_leadingzeros;
		}
	}

    if(decoder->state == SYNC1_STATE)
    {
#ifdef HAVE_SYNC_TONE
        sortIdxReset(decoder, decoder->process_start_idx, decoder->process_freq_num_lag + decoder->process_freq_num);
        /* windowing */
        windowingPcm(decoder->pcm_buf, decoder->temp_buf, decoder->time_window, decoder->fft_size);
        /* fft */
        kissFft(decoder->fft_table, decoder->temp_buf, decoder->fd_buf);
        /* caculate the psd */
        caculatePsd(decoder->fd_buf, decoder->psd_buf, decoder->process_freq_num_lag + decoder->process_freq_num, decoder->process_start_idx, decoder->process_start_idx);
        /* sorting psd, TODO: init sort_idx */
        psdSorting(decoder, decoder->process_start_idx, decoder->process_freq_num_lag + decoder->process_freq_num);
        decoder->lag_count++;
        decoder->shift_idx = (decoder->shift_idx + 1) % (DEC_OVERLAP_FACTOR);
		ret = lagCandidateUpdate(decoder, decoder->process_freq_num_lag + decoder->process_freq_num, decoder->lag_symbol_num_internal*TRANSMIT_PER_SYM - 1);
        if(ret < 0)
        {
            /* no candidate in current sync tone symbol interval, so we just return to get more data */
            return DEC_NORMAL;
        }
        if(decoder->lag_count < decoder->lag_symbol_num_internal*RECEIVE_PER_SYM)
        {
            return DEC_NORMAL;
        }
        if(findSyncLag(decoder) < 0)
        {
            return DEC_NORMAL;
        }
        candidateReset(decoder);
        decoder->state = SYNC2_STATE;
        //printf("find sync tone... decoding start!! lag_count : %d, sync_score : %d, during (%d ms - %d ms)", decoder->lag_count, sync_score, decoder->play_time - decoder->fft_fetch_time, decoder->play_time);
        return DEC_NORMAL;
#else
        decoder->state = SYNC2_STATE;
#endif
    }

	sortIdxReset(decoder, decoder->process_start_idx, (decoder->process_freq_num + decoder->process_freq_num_lag));
	/* windowing */
	windowingPcm(decoder->pcm_buf, decoder->temp_buf, decoder->time_window, decoder->fft_size);
	/* fft */
	kissFft(decoder->fft_table, decoder->temp_buf, decoder->fd_buf);
	/* caculate the psd */
	caculatePsd(decoder->fd_buf, decoder->psd_buf, (decoder->process_freq_num + decoder->process_freq_num_lag), decoder->process_start_idx , decoder->process_start_idx);
	/* sorting psd, TODO: init sort_idx */
	psdSorting(decoder, decoder->process_start_idx, decoder->process_freq_num_lag + decoder->process_freq_num);

	decoder->frame_count++;
	decoder->shift_idx = (decoder->shift_idx + 1) % DEC_OVERLAP_FACTOR;

	ret = candidateUpdate(decoder, decoder->process_freq_num_lag + decoder->process_freq_num, decoder->internal_symbol_num*TRANSMIT_PER_SYM - 1);
	lagCandidateUpdate(decoder, decoder->process_freq_num_lag + decoder->process_freq_num, decoder->lag_symbol_num_internal*TRANSMIT_PER_SYM - 1);
	if(!findSyncLag(decoder))
	{
		decoder->out_idx = 0;
		decoder->error_count = 0;
		decoderBufReset(decoder);
		candidateReset(decoder);
		decoder->state = SYNC2_STATE;
		vp_memset(decoder->out_buf,0x00, (sizeof(unsigned char)*(decoder->max_strlen+1)));
		//printf("find some start tone during decoding..., sync_score : %d, during (%d ms - %d ms)", sync_score, decoder->play_time - decoder->fft_fetch_time, decoder->play_time);
		return DEC_NORMAL;
	}

	if(ret < 0)
	{
		/* no candidate in current sync tone symbol interval, so we just return to get more data */
		return DEC_NORMAL;
	}

	if(decoder->frame_count < decoder->internal_symbol_num*RECEIVE_PER_SYM)
	{
		return DEC_NORMAL;
	}
	//showCandidate(decoder);
	if(decoder->state == SYNC2_STATE && findSync(decoder) != 0)
	{
		return DEC_NORMAL;
	}
	decoder->state = DECODE_STATE;
	/* TODO init candidate_idx_array*/
	idx_total = 1;
	for(i  = decoder->sync_symbol_num*TRANSMIT_PER_SYM; i < decoder->internal_symbol_num*TRANSMIT_PER_SYM; i++)
	{
		idx_total1 =  idx_total* decoder->candidate_array[decoder->shift_idx][i].candidate_num;
		if(idx_total1  > decoder->max_allowed_ombin)
		{
			break;
		}
		for(k = 0; k < idx_total; k++)
		{
			decoder->candidate_idx_array[k][i] = decoder->candidate_array[decoder->shift_idx][i].candidate_freqidx[0];
		}
		for(j = 1; j < decoder->candidate_array[decoder->shift_idx][i].candidate_num; j++)
		{
			for(k = 0; k < idx_total; k++)
			{
				memcpy(decoder->candidate_idx_array[k+j*idx_total],decoder->candidate_idx_array[k], sizeof(int)*(i-1));
				decoder->candidate_idx_array[k+j*idx_total][i] = decoder->candidate_array[decoder->shift_idx][i].candidate_freqidx[j];
			}

		}
		idx_total = idx_total1;

	}

	if(idx_total1 > decoder->max_allowed_ombin)
	{
		idx_total = decoder->error_correct ? 1:0;
		for(i  = decoder->sync_symbol_num*TRANSMIT_PER_SYM; i < decoder->internal_symbol_num*TRANSMIT_PER_SYM; i++)
		{
			decoder->candidate_idx_array[0][i] = decoder->candidate_array[decoder->shift_idx][i].candidate_freqidx[0];
		}

	}
	for(k = 0; k < idx_total; k++)
	{
		for(i = 0; i < decoder->sync_symbol_num*TRANSMIT_PER_SYM; i++)
		{
			decoder->candidate_idx_array[k][i] = sync_index[i];
		}
	}

	for(k = 0; k < idx_total; k++)
	{
		int ret1;
		decoderBufReset(decoder);
		for(i = 0; i < decoder->internal_symbol_num*TRANSMIT_PER_SYM; i++)
		{
			unsigned char c_value = freqIdx2Char(decoder, decoder->candidate_idx_array[k][i]);
			storeBits(decoder, c_value, 4);
		}
		if(decoder->error_correct)
		{
			ret1 = rsDecodeChar(decoder->rs, decoder->dec_buf, NULL, 0);
		}else
		{
			ret1 = 0;
		}
		if(ret1 >= 0 && ret1 <= decoder->check_symbol_num/TRANSMIT_PER_SYM && memcmp(decoder->dec_buf,sync_str,sizeof(sync_str))==0)
		{
			memcpy(&decoder->out_buf[decoder->out_idx], &decoder->dec_buf[decoder->sync_symbol_num], decoder->grouping_symbol_num*sizeof(unsigned char));
			decoder->error_count += ret1;
			if(findNull(&decoder->dec_buf[decoder->sync_symbol_num], decoder->grouping_symbol_num) != 0)
			{
				decoder->state = END_STATE;
				return DEC_END;
			}else
			{
				decoder->state = FRAME_END_STATE;
				decoder->out_idx += decoder->grouping_symbol_num;
				frameReset(decoder);
			}
			return DEC_NORMAL;
		}
	}

	//decoder->state = ERROR_STATE
	return DEC_NORMAL;
}

int decoderGetResult(void* handle, unsigned char* str)
{
	DECODER_INFO_T* decoder = (DECODER_INFO_T*)handle;
	if(decoder->state != END_STATE) {
		return DEC_NOTREADY;
	}
	strcpy(str, decoder->out_buf);
	return DEC_NORMAL;

}
void decoderDeinit(void *handle, int flag)
{
	DECODER_INFO_T* decoder = (DECODER_INFO_T*)handle;

	if(decoder)
	{
		int i,k;
		if(decoder->pcm_buf)
			vp_free(decoder->pcm_buf);
		if(decoder->fd_buf)
			vp_free(decoder->fd_buf);
		if(decoder->psd_buf)
			vp_free(decoder->psd_buf);
		if(decoder->candidate_array)
		{
			for(k = 0; k < DEC_OVERLAP_FACTOR; k++)
			{
				if(decoder->candidate_array[k])
				{
					for(i = 0; i < decoder->internal_symbol_num*TRANSMIT_PER_SYM; i++)
					{
						if(decoder->candidate_array[k][i].candidate_freqidx)
							vp_free(decoder->candidate_array[k][i].candidate_freqidx);
					}
					vp_free(decoder->candidate_array[k]);
				}
			}
			vp_free(decoder->candidate_array);
		}
		if(decoder->candidate_array_lag)
		{
			for(k = 0; k < DEC_OVERLAP_FACTOR; k++)
			{
				if(decoder->candidate_array_lag[k])
				{
					for(i = 0; i < decoder->lag_symbol_num_internal*TRANSMIT_PER_SYM; i++)
					{
						if(decoder->candidate_array_lag[k][i].candidate_freqidx)
							vp_free(decoder->candidate_array_lag[k][i].candidate_freqidx);
					}
					vp_free(decoder->candidate_array_lag[k]);
				}
			}
			vp_free(decoder->candidate_array_lag);
		}
		if(decoder->error_correct&&decoder->rs)
			rsFreeChar(decoder->rs);
		if(decoder->fft_table)
			kissFftFree(decoder->fft_table);
		if(decoder->sort_idx)
			vp_free(decoder->sort_idx);
		if(decoder->candidate_idx_array)
		{
			for(i = 0; i < decoder->max_allowed_ombin; i++)
			{
				if(decoder->candidate_idx_array[i])
				{
					vp_free(decoder->candidate_idx_array[i]);
				}
			}
			vp_free(decoder->candidate_idx_array);
		}
		if(decoder->dec_buf)
			vp_free(decoder->dec_buf);
		if(decoder->out_buf)
			vp_free(decoder->out_buf);
		if(decoder->time_window)
			vp_free(decoder->time_window);
		if(decoder->temp_buf)
			vp_free(decoder->temp_buf);

		if(decoder->filter)
			firfilterDestroy(decoder->filter);
		vp_free(decoder);
	}
#ifdef MEMORY_LEAK_DIAGNOSE
	vp_mem_diagnose();
#endif
}
