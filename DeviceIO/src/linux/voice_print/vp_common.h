#ifndef _VP_COMMON_H_
#define _VP_COMMON_H_

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifndef LP_64
//typedef long long int64_t;
#else
#include <stdint.h>
#endif

#define SYMBOL_LENGTH_FACTOR    1
#define SYNC_TONE_SYMBOL_LENGTH 2
#define LOW_FREQ_SIMPLE_NOISE_SHAPING 0
#define Q_CONST32(x, bits) ((int)(.5 + (x) * (((int)1) << (bits))))

#define PI Q_CONST32(3.1415926, 15)
#define HAMMING_COEF1 Q_CONST32(0.54, 15)
#define HAMMING_COEF2 Q_CONST32(0.46, 15)

#define BLACKMAN_COEF1 Q_CONST32(0.42,15)
#define BLACKMAN_COEF2 Q_CONST32(0.5,15)
#define BLACKMAN_COEF3 Q_CONST32(0.08,15)

#define HANNING_COEF1 Q_CONST32(0.5,15)
#define HANNING_COEF2 Q_CONST32(0.5,15)

#define SIN_COEF1 Q_CONST32(1,15)
#define SIN_COEF3 Q_CONST32(-1./6,15)
#define SIN_COEF5 Q_CONST32(1./120,15)

#define VPMUL(a, b) (int)(((((int64_t)(a))*((int64_t)(b))) + (1<<14) )>> 15)
#define VPMULT(a, b) (((int64_t)(a))*((int64_t)(b)))
#define VPMULT16(a, b) (((int)(a))*((int)(b)))
#define VPSHR(a, b)	((a)>>(b))

#define VPSAT(a) (((a)>32767)? 32767 :(((a)<-32768)?-32768:(a)))
#define VPABS(a) (((a) > 0) ? a : (-(a)))

#define SYMBOL_BITS 4

#define FREQ_NUM (1 << SYMBOL_BITS)

int vp_sin(int x);

#ifndef MEMORY_LEAK_DIAGNOSE
#define vp_alloc(size) calloc(1, size)
#define vp_free(ptr) free(ptr)
#else
void* vp_alloc(size_t size);
void vp_free(void* ptr);
void vp_mem_diagnose();
void vp_mem_diagnose_init();
#endif
#define vp_memset(ptr, v, size) memset(ptr, v, size)
#define vp_memcpy(dst, src, size) memcpy(dst, src, size)

/* frequency mapping (HZ), log scale linear */
typedef enum
{
	LOW_PASS_TYPE,
	HIGH_PASS_TYPE,
	BAND_PASS_TYPE,
	BAND_STOP_TYPE
} tFILTER_TYPE;

extern int vp_freq_point[3][FREQ_NUM];
extern int vp_freq_delta[3];
extern int vp_freq_lag[3];
extern int vp_freq_lag_top[3];
extern int vp_freq_cutoff[3];
extern tFILTER_TYPE vp_filter_type[3];

#endif
