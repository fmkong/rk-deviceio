#include "vp_common.h"

int vp_freq_point[3][FREQ_NUM]=
{
	{2104, 2213, 2328, 2449, 2577, 2711, 2852, 3000, 3156, 3320, 3493, 3674, 3865, 4066, 4278, 4500},
	{8662, 8827, 8996, 9167, 9342, 9520, 9702, 9887, 10075, 10268, 10463, 10663, 10866, 11074, 11285, 11500,},
	{16673, 16848, 17025, 17204, 17384, 17567, 17751, 17937, 18126, 18316, 18508, 18702, 18899, 19097, 19297, 19500}
};

int vp_freq_delta[3] ={109, 165, 175};
int vp_freq_lag[3] = {4700, 7734, 15336};
int vp_freq_lag_top[3] = {5500, 7734, 15336};
int vp_freq_cutoff[3] = {8000,7000,14000};

tFILTER_TYPE vp_filter_type[3] = {LOW_PASS_TYPE, HIGH_PASS_TYPE, HIGH_PASS_TYPE};

/*
	sinx = x - 1/6*x^3 + 1/120*x^5;
	input Q15, output Q15
*/
int vp_sin(int x)
{
	int x1, x2, x3, x5, s= 0;
	/* we first make sure that x in between [0, 2*pi) */
	x1 = x/(2*PI);
	x1 = x - (2*PI*x1);
	if(x1 < 0)
	{
		x1 += 2*PI;
	}
	if(x1 > PI)
	{
		x1 -= 2*PI;
	}
	if( x1 > PI/2)
	{
		x1 -= PI;
		s = 1;
	}else if (x1 < -PI/2)
	{
		x1 += PI;
		s = 1;
	}

	x2 = VPMUL(x1, x1);
	x3 = VPMUL(x1, x2);
	x5 = VPMUL(x2, x3);

	x2 = (x1 + VPMUL(SIN_COEF3, x3) + VPMUL(SIN_COEF5, x5));
	if(s)
	{
		x2 = - x2;
	}
	x2 = VPSAT(x2);
	return x2;
}

#ifdef  MEMORY_LEAK_DIAGNOSE
#define VP_HEAP_SIZE (8*1024*1024)
#define VP_MAX_ALLOC_ITEM (2*2048)
static unsigned char memory_block[VP_HEAP_SIZE];
static unsigned int memory_table[VP_MAX_ALLOC_ITEM][2];
static unsigned int current_size = 0;
static unsigned int table_idx = 0;
static unsigned int occupy_size = 0;
static unsigned int occupy_size_max = 0;
void* vp_alloc(size_t size)
{
	void* ptr =  (void*)(memory_block+current_size);
	memset(ptr, 0, size);
	memory_table[table_idx][1] = (unsigned int)ptr;
	memory_table[table_idx][2] = size;

	if(current_size + size > VP_HEAP_SIZE)
	{
		printf("overflow: %d is larger than heapsize: %d!\n",current_size, VP_HEAP_SIZE);
		return NULL;
	}

	current_size += size;
	occupy_size += size;
	occupy_size_max = (occupy_size_max < occupy_size) ? occupy_size : occupy_size_max;
	table_idx++;
	return ptr;
}

void vp_free(void* ptr)
{
	unsigned int i;
	for(i = 0; i < table_idx; i++)
	{
		if(memory_table[i][1] == (unsigned int) ptr)
		{
			if(memory_table[i][2] == 0)
			{
				printf("illegal free of %d th alloc, addr: %#X\n",i, memory_table[i][1]);
				return ;
			}
			occupy_size -= memory_table[i][2];
			occupy_size_max = (occupy_size_max < occupy_size) ? occupy_size : occupy_size_max;
			memory_table[i][2] = 0;
			return;
		}
	}
	printf("illegal free %#X\n", (unsigned int)ptr);
}

void vp_mem_diagnose()
{
	unsigned int i;
	printf("total alloc %d times, total alloc size %d bytes, max occupy size %d bytes\n", table_idx, current_size, occupy_size_max);
	for(i = 0; i < table_idx; i++)
	{
		if(memory_table[i][2] != 0)
		{
			printf("memory leak of %d th alloc, size: %d, addr: %#X\n", i, memory_table[i][1],memory_table[i][2]);
		}
	}

}

void vp_mem_diagnose_init()
{
	memset(memory_table, 0, sizeof(unsigned int)*VP_MAX_ALLOC_ITEM*2);
	current_size = 0;
	table_idx = 0;
}

#endif
