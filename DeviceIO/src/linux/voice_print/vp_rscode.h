#ifndef _VP_RSCODE_H_
#define _VP_RSCODE_H_

/*
 * General purpose RS_INFO_T codec, 8-bit symbols.
 */
typedef struct _RS_INFO RS_INFO_T;
extern RS_INFO_T *rsInitChar(int symsize, int gfpoly, int fcr, int prim, int nroots, int pad);
extern RS_INFO_T *rsInit(int symsize, int gfpoly, int fcr, int prim, int nroots, int pad);
extern void rsEncodeChar(RS_INFO_T *rs, const unsigned char *data, unsigned char *parity);
extern int  rsDecodeChar(RS_INFO_T *rs, unsigned char *data, int *eras_pos, int no_eras);
extern void rsFreeChar(RS_INFO_T *rs);
extern void rsFreeCache(void);

#endif /* __RSCODE_H__ */
