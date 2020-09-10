#ifndef _VOICE_PRINT_H_
#define _VOICE_PRINT_H_

/* frequency type selector */
typedef enum
{
    /*低频 2K~5K */
    LOW_FREQ_TYPE =0,
    /*中频, 8K~12K*/
    MIDDLE_FREQ_TYPE,
    /*高频 16K~20K*/
    HIGH_FREQ_TYPE
} FREQ_TYPE_T;

/*macros of return valule of decoder*/
/* 解码正常返回 */
#define DEC_NORMAL 0
/* 解码还未结束,       不能获取到解码结果 */
#define DEC_NOTREADY 1
/* 解码结束 */
#define DEC_END 2

/* definition of decoder config paramters */
typedef struct
{
    /* 支持的最大字符串长度(字节数) */
    int max_strlen;
    /* 采样率 */
    int sample_rate;
    /* 频率范围选择 */
    FREQ_TYPE_T freq_type;
    /* 每个分组传输的字节数 */
    int group_symbol_num;
    /* 是否采用纠错码 */
    int error_correct;
    /* 纠错码的纠错能力(字节数) */
    int error_correct_num;
} DECODER_CONFIG_T;

/*
    描述：创建解码器
    参数：decode_config: 参数结构体(指针)
          flag: this is a flag, but it's a reserved argument now
    返回值：解码器句柄, NULL表示创建失败
*/
void* decoderInit(DECODER_CONFIG_T* decode_config, int flag);

/*
    描述：复位解码器
    参数：handle：解码器句柄
          flag: this is a flag, but it's a reserved argument now
    返回值：无
*/
void decoderReset(void* handle, int flag);

/*
    描述：获取每帧数据量(样本数)
    参数：handle：解码器句柄
          flag: this is a flag, but it's a reserved argument now
    返回值：每帧数据量(样本数, 每个样本为16bit)
*/
int decoderGetSize(void* handle, int flag);

/*
    描述：解码pcm数据
    参数：handle：解码器句柄
          pcm：数据buffer, 需保证含有的样本数等于decoderGetSize的返回值
    返回值：同解码返回值的宏定义
*/
int decoderPcmData(void* handle, short* pcm);

/*
    描述：获取解码结果
    参数：handle：解码器句柄
          str：解码结果buffer
    返回值：同解码返回值的宏定义
*/
int decoderGetResult(void* handle, unsigned char* str);

/*
    描述：释放解码器句柄
    参数：handle：解码器句柄
          flag: this is a flag, but it's a reserved argument now
    返回值：无
*/
void decoderDeinit(void* handle, int flag);

/* definition of encoder config paramters */
typedef struct
{
    /* 支持的最大字符串长度(字节数) */
    int max_strlen;
    /* 采样率 */
    int sample_rate;
    /* 频率范围选择 */
    FREQ_TYPE_T freq_type;
    /* 每个分组传输的字节数 */
    int group_symbol_num;
    /* 是否采用纠错码 */
    int error_correct;
    /* 纠错码的纠错能力(字节数) */
    int error_correct_num;
} ENCOEDR_CONFIG_T;

/* macros of return value of encoder */
/* 编码正常返回 */
#define		ENC_NORMAL 0
/* 编码结束 */
#define 	ENC_END 1
/* 编码出错 */
#define		ENC_ERROR -1

/*
    描述：释放编码器句柄
    参数：handle：编码器句柄
          flag: this is a flag, but it's a reserved argument now
    返回值：无

*/
void encoderDeinit(void* handle, int flag);

/*
    描述：创建编码器
    参数：config: 参数结构体(指针)
          flag: this is a flag, but it's a reserved argument now
    返回值：编码器句柄, NULL表示创建失败
*/
void* encoderInit(ENCOEDR_CONFIG_T* config, int flag);

/*
    描述：复位编码器
    参数：handle：编码器句柄
          flag: this is a flag, but it's a reserved argument now
    返回值：无
*/
void encoderReset(void* handle, int flag);

/*
    描述：获取帧缓存大小(字节单位), 用于外部分配帧缓存buffer
    参数：handle：编码器句柄
    返回值：帧缓存大小(字节单位)
*/
int encoderGetsize(void* handle);

/*
    描述：对字符串进行编码
    参数：handle：编码器句柄
          outpcm：帧数据buffer(外部分配)
    返回值：同编码器返回值定义
*/
int encoderStrData(void* handle, short* outpcm);

/*
    描述：设置待编码的字符串, 字符串需以 '\0' 结尾
    参数：handle：编码器句柄
          input：以 '\0' 结尾的字符串
    返回值：同编码器返回值定义
*/
int encoderSetStr(void* handle, unsigned char* input);

#endif
