#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef HAVE_LIB_PTHREAD
#  include <pthread.h>
#endif

#ifndef NULL
# define NULL 			((void *)0)
#endif

#ifndef MIN
# define MIN(a,b)		((a) < (b) ? (a) : (b))
#endif

#include "vp_rscode.h"

/* Reed-Solomon codec control block */
struct _RS_INFO {
	int mm;              /* Bits per symbol */
	int nn;              /* Symbols per block (= (1<<mm)-1) */
	unsigned char *alpha_to;     /* log lookup table */
	unsigned char *index_of;     /* Antilog lookup table */
	unsigned char *genpoly;      /* Generator polynomial */
	int nroots;     /* Number of generator roots = number of parity symbols */
	int fcr;        /* First consecutive root, index form */
	int prim;       /* Primitive element, index form */
	int iprim;      /* prim-th root of 1, index form */
	int pad;        /* Padding bytes in shortened block */
	int gfpoly;
	struct _RS_INFO *next;
};

static RS_INFO_T *rs_list = NULL;
#ifdef HAVE_LIB_PTHREAD
static pthread_mutex_t rs_list_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

static int modnn(RS_INFO_T *rs, int x){
	while (x >= rs->nn) {
		x -= rs->nn;
		x = (x >> rs->mm) + (x & rs->nn);
	}
	return x;
}

#define MODNN(x) modnn(rs,x)

#define MM (rs->mm)
#define NN (rs->nn)
#define ALPHA_TO (rs->alpha_to)
#define INDEX_OF (rs->index_of)
#define GENPOLY (rs->genpoly)
#define NROOTS (rs->nroots)
#define FCR (rs->fcr)
#define PRIM (rs->prim)
#define IPRIM (rs->iprim)
#define PAD (rs->pad)
#define A0 (NN)             /* Special reserved value encoding zero in index form */

/* Initialize a Reed-Solomon codec
 * symsize = symbol size, bits
 * gfpoly = Field generator polynomial coefficients
 * fcr = first root of RS_INFO_T code generator polynomial, index form
 * prim = primitive element to generate polynomial roots
 * nroots = RS_INFO_T code generator polynomial degree (number of roots)
 * pad = padding bytes at front of shortened block
 */
RS_INFO_T *rsInitChar(int symsize, int gfpoly, int fcr, int prim, int nroots, int pad)
{
  RS_INFO_T *rs;

/* Common code for intializing a Reed-Solomon control block (char or int symbols)
 * Copyright 2004 Phil Karn, KA9Q
 * May be used under the terms of the GNU Lesser General Public License (LGPL)
 */
//#undef NULL
//#define NULL ((void *)0)

  int i, j, sr,root,iprim;

  rs = NULL;
  /* Check parameter ranges */
  if(symsize < 0 || symsize > (int)(8*sizeof(unsigned char))){
    goto done;
  }

  if(fcr < 0 || fcr >= (1<<symsize))
    goto done;
  if(prim <= 0 || prim >= (1<<symsize))
    goto done;
  if(nroots < 0 || nroots >= (1<<symsize))
    goto done; /* Can't have more roots than symbol values! */
  if(pad < 0 || pad >= ((1<<symsize) -1 - nroots))
    goto done; /* Too much padding */

  rs = (RS_INFO_T *)calloc(1,sizeof(RS_INFO_T));
  if(rs == NULL)
    goto done;

  rs->mm = symsize;
  rs->nn = (1<<symsize)-1;
  rs->pad = pad;

  rs->alpha_to = (unsigned char *)malloc(sizeof(unsigned char)*(rs->nn+1));
  if(rs->alpha_to == NULL){
    free(rs);
    rs = NULL;
    goto done;
  }
  rs->index_of = (unsigned char *)malloc(sizeof(unsigned char)*(rs->nn+1));
  if(rs->index_of == NULL){
    free(rs->alpha_to);
    free(rs);
    rs = NULL;
    goto done;
  }

  /* Generate Galois field lookup tables */
  rs->index_of[0] = A0; /* log(zero) = -inf */
  rs->alpha_to[A0] = 0; /* alpha**-inf = 0 */
  sr = 1;
  /* index_of is for multiplication 
	alpha_to is for addition
  */
  for(i=0;i<rs->nn;i++){
    rs->index_of[sr] = i;//alpha^i = sr;
    rs->alpha_to[i] = sr;//alpha^i = sr;
    sr <<= 1;
    if(sr & (1<<symsize))
      sr ^= gfpoly;
    sr &= rs->nn;
  }
  if(sr != 1){
    /* field generator polynomial is not primitive! */
    free(rs->alpha_to);
    free(rs->index_of);
    free(rs);
    rs = NULL;
    goto done;
  }

  /* Form RS_INFO_T code generator polynomial from its roots */
  rs->genpoly = (unsigned char *)malloc(sizeof(unsigned char)*(nroots+1));
  if(rs->genpoly == NULL){
    free(rs->alpha_to);
    free(rs->index_of);
    free(rs);
    rs = NULL;
    goto done;
  }
  rs->fcr = fcr;
  rs->prim = prim;
  rs->nroots = nroots;
  rs->gfpoly = gfpoly;

  /* Find prim-th root of 1, used in decoding */
  for(iprim=1;(iprim % prim) != 0;iprim += rs->nn)
    ;
  rs->iprim = iprim / prim;

  rs->genpoly[0] = 1;
  for (i = 0,root=fcr*prim; i < nroots; i++,root += prim) {
    rs->genpoly[i+1] = 1;

    /* Multiply rs->genpoly[] by  @**(root + x) */
    for (j = i; j > 0; j--){
      if (rs->genpoly[j] != 0)
	rs->genpoly[j] = rs->genpoly[j-1] ^ rs->alpha_to[modnn(rs,rs->index_of[rs->genpoly[j]] + root)];
      else
	rs->genpoly[j] = rs->genpoly[j-1];
    }
    /* rs->genpoly[0] can never be zero */
    rs->genpoly[0] = rs->alpha_to[modnn(rs,rs->index_of[rs->genpoly[0]] + root)];
  }
  /* convert rs->genpoly[] to index form for quicker encoding */
  for (i = 0; i <= nroots; i++)
    rs->genpoly[i] = rs->index_of[rs->genpoly[i]];
 done:;

  return rs;
}

RS_INFO_T *rsInit(int symsize, int gfpoly, int fcr, int prim, int nroots, int pad)
{
	RS_INFO_T *rs;

#ifdef HAVE_LIB_PTHREAD
	pthread_mutex_lock(&rs_list_mutex);
#endif
	for(rs = rs_list; rs != NULL; rs = rs->next) {
		if(rs->pad != pad) continue;
		if(rs->nroots != nroots) continue;
		if(rs->mm != symsize) continue;
		if(rs->gfpoly != gfpoly) continue;
		if(rs->fcr != fcr) continue;
		if(rs->prim != prim) continue;

		goto DONE;
	}

	rs = rsInitChar(symsize, gfpoly, fcr, prim, nroots, pad);
	if(rs == NULL) goto DONE;
	rs->next = rs_list;
	rs_list = rs;

DONE:
#ifdef HAVE_LIB_PTHREAD
	pthread_mutex_unlock(&rs_list_mutex);
#endif
	return rs;
}

void rsFreeChar(RS_INFO_T *rs)
{
	free(rs->alpha_to);
	free(rs->index_of);
	free(rs->genpoly);
	free(rs);
}

void rsFreeCache(void)
{
	RS_INFO_T *rs, *next;

#ifdef HAVE_LIB_PTHREAD
	pthread_mutex_lock(&rs_list_mutex);
#endif
	rs = rs_list;
	while(rs != NULL) {
		next = rs->next;
		rsFreeChar(rs);
		rs = next;
	}

	rs_list = NULL;
#ifdef HAVE_LIB_PTHREAD
	pthread_mutex_unlock(&rs_list_mutex);
#endif
}

/* The guts of the Reed-Solomon encoder, meant to be #included
 * into a function body with the following typedefs, macros and variables supplied
 * according to the code parameters:
 * unsigned char - a typedef for the data symbol
 * unsigned char data[] - array of NN-NROOTS-PAD and type unsigned char to be encoded
 * unsigned char parity[] - an array of NROOTS and type unsigned char to be written with parity symbols
 * NROOTS - the number of roots in the RS_INFO_T code generator polynomial,
 *          which is the same as the number of parity symbols in a block.
            Integer variable or literal.
        *
 * NN - the total number of symbols in a RS_INFO_T block. Integer variable or literal.
 * PAD - the number of pad symbols in a block. Integer variable or literal.
 * ALPHA_TO - The address of an array of NN elements to convert Galois field
 *            elements in index (log) form to polynomial form. Read only.
 * INDEX_OF - The address of an array of NN elements to convert Galois field
 *            elements in polynomial form to index (log) form. Read only.
 * MODNN - a function to reduce its argument modulo NN. May be inline or a macro.
 * GENPOLY - an array of NROOTS+1 elements containing the generator polynomial in index form
 * The memset() and memmove() functions are used. The appropriate header
 * file declaring these functions (usually <string.h>) must be included by the calling
 * program.
 * Copyright 2004, Phil Karn, KA9Q
 * May be used under the terms of the GNU Lesser General Public License (LGPL)
 */
void rsEncodeChar(RS_INFO_T *rs, const unsigned char *data, unsigned char *parity)
{
  int i, j;
  unsigned char feedback;

  memset(parity,0,NROOTS*sizeof(unsigned char));

  for(i=0;i<NN-NROOTS-PAD;i++){
    feedback = INDEX_OF[data[i] ^ parity[0]];
    if(feedback != A0){      /* feedback term is non-zero */
#ifdef UN_NORMALIZED
      /* This line is unnecessary when GENPOLY[NROOTS] is unity, as it must
       * always be for the polynomials constructed by rsInit()
       */
      feedback = MODNN(NN - GENPOLY[NROOTS] + feedback);
#endif
      for(j=1;j<NROOTS;j++)
	parity[j] ^= ALPHA_TO[MODNN(feedback + GENPOLY[NROOTS-j])];
    }
    /* Shift */
    memmove(&parity[0],&parity[1],sizeof(unsigned char)*(NROOTS-1));
    if(feedback != A0)
      parity[NROOTS-1] = ALPHA_TO[MODNN(feedback + GENPOLY[0])];
    else
      parity[NROOTS-1] = 0;
  }
}

int rsDecodeChar(RS_INFO_T *rs, unsigned char *data, int *eras_pos, int no_eras) {

    int retval;
#if 1

	unsigned char *lambda, *s, *b, *t, *omega, *root, *reg, *loc;
#endif

    {
        int deg_lambda, el, deg_omega;
        int i, j, r,k;
        unsigned char u,q,tmp,num1,num2,den,discr_r;

#if 0
		unsigned char lambda[NROOTS+1], s[NROOTS];	/* Err+Eras Locator poly
                                             * and syndrome poly */
        unsigned char b[NROOTS+1], t[NROOTS+1], omega[NROOTS+1];
        unsigned char root[NROOTS], reg[NROOTS+1], loc[NROOTS];
#endif
		int syn_error, count, count_validloc=0;

#if 1
		lambda = (unsigned char*)calloc(NROOTS+1, sizeof(unsigned char));
		s = (unsigned char*)calloc(NROOTS, sizeof(unsigned char));
		b = (unsigned char*)calloc(NROOTS+1, sizeof(unsigned char));
		t = (unsigned char*)calloc(NROOTS+1, sizeof(unsigned char));
		omega = (unsigned char*)calloc(NROOTS+1, sizeof(unsigned char));
		root = (unsigned char*)calloc(NROOTS, sizeof(unsigned char));
		reg = (unsigned char*)calloc(NROOTS+1, sizeof(unsigned char));
		loc = (unsigned char*)calloc(NROOTS, sizeof(unsigned char));
		if(lambda == NULL || s== NULL || b == NULL || t == NULL
			|| omega == NULL || root == NULL || reg == NULL || loc == NULL)
		{
			retval = -1;
			goto destroy_internal_memory;
		}
#endif
        /* form the syndromes; i.e., evaluate data(x) at roots of g(x) */
        for(i=0;i<NROOTS;i++)
            s[i] = data[0];
        
        for(j=1;j<NN-PAD;j++){
            
            for(i=0;i<NROOTS;i++){
            
                if(s[i] == 0){
                
                    s[i] = data[j];
                
                } else {
                    
                    s[i] = data[j] ^ ALPHA_TO[MODNN(INDEX_OF[s[i]] + (FCR+i)*PRIM)];
                }
            }
        }
        
        /* Convert syndromes to index form, checking for nonzero condition */
        syn_error = 0;
        for(i=0;i<NROOTS;i++){
            syn_error |= s[i];
            s[i] = INDEX_OF[s[i]];
        }
        
        if (!syn_error) {
            /* if syndrome is zero, data[] is a codeword and there are no
             * errors to correct. So return data[] unmodified
             */
            count = 0;
            goto finish;
        }
        
        memset(&lambda[1],0,NROOTS*sizeof(lambda[0]));
        lambda[0] = 1;
        
        if (no_eras > 0) {
            /* Init lambda to be the erasure locator polynomial */
            lambda[1] = ALPHA_TO[MODNN(PRIM*(NN-1-eras_pos[0]))];

            for (i = 1; i < no_eras; i++) {
                u = MODNN(PRIM*(NN-1-eras_pos[i]));
                for (j = i+1; j > 0; j--) {
                    tmp = INDEX_OF[lambda[j - 1]];
                    
                    if(tmp != A0)
                        lambda[j] ^= ALPHA_TO[MODNN(u + tmp)];
                }
            }

#if 0//DEBUG >= 1
            /* Test code that verifies the erasure locator polynomial just constructed
             Needed only for decoder debugging. */

            /* find roots of the erasure location polynomial */
            for(i=1;i<=no_eras;i++)
                reg[i] = INDEX_OF[lambda[i]];

            count = 0;
            for (i = 1,k=IPRIM-1; i <= NN; i++,k = MODNN(k+IPRIM)) {
                q = 1;
                for (j = 1; j <= no_eras; j++)
                    if (reg[j] != A0) {
                        reg[j] = MODNN(reg[j] + j);
                        q ^= ALPHA_TO[reg[j]];
                    }
                if (q != 0)
                    continue;
                /* store root and error location number indices */
                root[count] = i;
                loc[count] = k;
                count++;
            }
            if (count != no_eras) {
                printf("lambda(x) error, count: %d, no_eras: %d\n", count, no_eras);
                count = -1;
                goto finish;
            }
#if DEBUG >= 2
            printf("Erasure positions as determined by roots of Eras Loc Poly:");
            for (i = 0; i < count; i++)
                printf("%d ", loc[i]);
            printf("\n");
#endif
#endif
        }
        for(i=0;i<NROOTS+1;i++)
            b[i] = INDEX_OF[lambda[i]];
        
        /*
         * Begin Berlekamp-Massey algorithm to determine error+erasure
         * locator polynomial
         */
        r = no_eras;
        el = no_eras;
        while (++r <= NROOTS) {	/* r is the step number */
            /* Compute discrepancy at the r-th step in poly-form */
            discr_r = 0;
            for (i = 0; i < r; i++){
                if ((lambda[i] != 0) && (s[r-i-1] != A0)) {
                    discr_r ^= ALPHA_TO[MODNN(INDEX_OF[lambda[i]] + s[r-i-1])];
                }
            }
            discr_r = INDEX_OF[discr_r];	/* Index form */
            if (discr_r == A0) {
                /* 2 lines below: B(x) <-- x*B(x) */
                memmove(&b[1],b,NROOTS*sizeof(b[0]));
                b[0] = A0;
            } else {
                /* 7 lines below: T(x) <-- lambda(x) - discr_r*x*b(x) */
                t[0] = lambda[0];
                for (i = 0 ; i < NROOTS; i++) {
                    if(b[i] != A0)
                        t[i+1] = lambda[i+1] ^ ALPHA_TO[MODNN(discr_r + b[i])];
                    else
                        t[i+1] = lambda[i+1];
                }
                if (2 * el <= r + no_eras - 1) {
                    el = r + no_eras - el;
                    /*
                     * 2 lines below: B(x) <-- inv(discr_r) *
                     * lambda(x)
                     */
                    for (i = 0; i <= NROOTS; i++)
                        b[i] = (lambda[i] == 0) ? A0 : MODNN(INDEX_OF[lambda[i]] - discr_r + NN);
                } else {
                    /* 2 lines below: B(x) <-- x*B(x) */
                    memmove(&b[1],b,NROOTS*sizeof(b[0]));
                    b[0] = A0;
                }
                memcpy(lambda,t,(NROOTS+1)*sizeof(t[0]));
            }
        }
        
        /* Convert lambda to index form and compute deg(lambda(x)) */
        deg_lambda = 0;
        for(i=0;i<NROOTS+1;i++){
            lambda[i] = INDEX_OF[lambda[i]];
            if(lambda[i] != A0)
                deg_lambda = i;
        }
        /* Find roots of the error+erasure locator polynomial by Chien search */
        memcpy(&reg[1],&lambda[1],NROOTS*sizeof(reg[0]));
        count = 0;		/* Number of roots of lambda(x) */
        for (i = 1,k=IPRIM-1; i <= NN; i++,k = MODNN(k+IPRIM)) {
            q = 1; /* lambda[0] is always 0 */
            for (j = deg_lambda; j > 0; j--){
                if (reg[j] != A0) {
                    reg[j] = MODNN(reg[j] + j);
                    q ^= ALPHA_TO[reg[j]];
                }
            }
            if (q != 0)
                continue; /* Not a root */
            /* store root (index-form) and error location number */
#if 0//DEBUG>=2
            printf("count %d root %d loc %d\n",count,i,k);
#endif
            root[count] = i;
            loc[count] = k;
            /* If we've already found max possible roots,
             * abort the search to save time
             */
            if(++count == deg_lambda)
                break;
        }
        if (deg_lambda != count) {
            /*
             * deg(lambda) unequal to number of roots => uncorrectable
             * error detected
             */
            count = -1;
            goto finish;
        }
        /*
         * Compute err+eras evaluator poly omega(x) = s(x)*lambda(x) (modulo
         * x**NROOTS). in index form. Also find deg(omega).
         */
        deg_omega = deg_lambda-1;
        for (i = 0; i <= deg_omega;i++){
            tmp = 0;
            for(j=i;j >= 0; j--){
                if ((s[i - j] != A0) && (lambda[j] != A0))
                    tmp ^= ALPHA_TO[MODNN(s[i - j] + lambda[j])];
            }
            omega[i] = INDEX_OF[tmp];
        }
        
        /*
         * Compute error values in poly-form. num1 = omega(inv(X(l))), num2 =
         * inv(X(l))**(FCR-1) and den = lambda_pr(inv(X(l))) all in poly-form
         */
        for (j = count-1; j >=0; j--) {
            num1 = 0;
            for (i = deg_omega; i >= 0; i--) {
                if (omega[i] != A0)
                    num1  ^= ALPHA_TO[MODNN(omega[i] + i * root[j])];
            }
            num2 = ALPHA_TO[MODNN(root[j] * (FCR - 1) + NN)];
            den = 0;

            /* lambda[i+1] for i even is the formal derivative lambda_pr of lambda[i] */
            for (i = MIN(deg_lambda,NROOTS-1) & ~1; i >= 0; i -=2) {
                if(lambda[i+1] != A0)
                    den ^= ALPHA_TO[MODNN(lambda[i+1] + i * root[j])];
            }
#if 0//DEBUG >= 1
            if (den == 0) {
                printf("\n ERROR: denominator = 0\n");
                count = -1;
                goto finish;
            }
#endif
            /* Apply error to data */
            if (num1 != 0 && loc[j] >= PAD) {
                data[loc[j]-PAD] ^= ALPHA_TO[MODNN(INDEX_OF[num1] + INDEX_OF[num2] + NN - INDEX_OF[den])];
            }
        }
    finish:
        if(eras_pos != NULL){
            for(i=0;i<count;i++)
                eras_pos[i] = loc[i];
        }
		for(i = 0; i < count; i++)
		{
			if(loc[i] < PAD)
			{
				count_validloc++;
			}
			if(count_validloc > 0 )
			{
				count = -1;
			}
		}
        retval = count;
    }
    
#if 1
destroy_internal_memory:
	if(lambda != NULL)
	{
		free(lambda);
	}
	if(s != NULL)
	{
		free(s);
	}
	if(b != NULL)
	{
		free(b);
	}
	if(t != NULL)
	{
		free(t);
	}
	if(omega != NULL)
	{
		free(omega);
	}
	if(root != NULL)
	{
		free(root); 
	}
	if(reg != NULL)
	{
		free(reg);
	}
	if(loc != NULL)
	{
		free(loc);
	}

#endif
    return retval;

}
