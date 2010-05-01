/**
 * Copyright (c) 2009 Open Information Security Foundation.
 *
 * \author Anoop Saldanha <poonaatsoc@gmail.com>
 */

#ifndef __UTIL_MPM_B2G_CUDA_H__
#define __UTIL_MPM_B2G_CUDA_H__

#ifdef __SC_CUDA_SUPPORT__

#include <cuda.h>
#include "decode.h"
#include "util-mpm.h"
#include "util-bloomfilter.h"

#define B2G_CUDA_HASHSHIFT 4
#define B2G_CUDA_TYPE      uint32_t
#define B2G_CUDA_WORD_SIZE 32
#define B2G_CUDA_Q         2

#define B2G_CUDA_HASH16(a, b) (((a) << B2G_CUDA_HASHSHIFT) | (b))

#define B2G_CUDA_SEARCHFUNC B2gCudaSearchBNDMq
#define B2G_CUDA_SEARCHFUNC_NAME "B2gCudaSearchBNDMq"

typedef struct B2gCudaPattern_ {
    uint8_t flags;
    /** \todo we're limited to 32/64 byte lengths, uint8_t would be fine here */
    uint16_t len;
    /* case sensitive */
    uint8_t *cs;
    /* case INsensitive */
    uint8_t *ci;
    struct B2gCudaPattern_ *next;
    MpmEndMatch *em;
} B2gCudaPattern;

typedef struct B2gCudaHashItem_ {
    uint16_t idx;
    uint8_t flags;
    struct B2gCudaHashItem_ *nxt;
} B2gCudaHashItem;

typedef struct B2gCudaCtx_ {
    /* unique handle given by the cuda-handlers API, which indicates the module
     * in the engine that is holding this B2g_Cuda_Ctx */
    int module_handle;

    CUcontext cuda_context;
    CUmodule cuda_module;

    /* the search kernel */
    CUfunction cuda_search_kernel;

    /* the cuda_search_kernel argument offsets */
    uint8_t cuda_search_kernel_arg0_offset;
    uint8_t cuda_search_kernel_arg1_offset;
    uint8_t cuda_search_kernel_arg2_offset;
    uint8_t cuda_search_kernel_arg3_offset;
    uint8_t cuda_search_kernel_arg4_offset;
    uint8_t cuda_search_kernel_arg5_offset;
    uint8_t cuda_search_kernel_arg_total;

    /* cuda device pointer to thelower case table g_u8_lowercasetable */
    CUdeviceptr cuda_g_u8_lowercasetable;
    /* cuda device pointer to B2gCudaCtx->B2G */
    CUdeviceptr cuda_B2G;

    B2G_CUDA_TYPE *B2G;
    B2G_CUDA_TYPE m;
    BloomFilter **bloom;
    /* array containing the minimal length of the patters in a hash bucket.
     * Used for the BloomFilter. */
    uint8_t *pminlen;
    /* pattern arrays */
    B2gCudaPattern **parray;

    uint16_t pat_1_cnt;
#ifdef B2G_CUDA_SEARCH2
    uint16_t pat_2_cnt;
#endif
    uint16_t pat_x_cnt;

    uint32_t hash_size;
    B2gCudaHashItem **hash;
    B2gCudaHashItem hash1[256];
#ifdef B2G_CUDA_SEARCH2
    B2gCudaHashItem **hash2;
#endif

    /* hash used during ctx initialization */
    B2gCudaPattern **init_hash;

    uint8_t s0;

    /* we store our own multi byte search func ptr here for B2gCudaSearch1 */
    uint32_t (*Search)(struct MpmCtx_ *, struct MpmThreadCtx_ *,
                       PatternMatcherQueue *, uint8_t *, uint16_t);

    /* we store our own multi byte search func ptr here for B2gCudaSearch2 */
    uint32_t (*MBSearch2)(struct MpmCtx_ *, struct MpmThreadCtx_ *,
                          PatternMatcherQueue *, uint8_t *, uint16_t);
    uint32_t (*MBSearch)(struct MpmCtx_ *, struct MpmThreadCtx_ *,
                         PatternMatcherQueue *, uint8_t *, uint16_t);
} B2gCudaCtx;

typedef struct B2gCudaThreadCtx_ {
#ifdef B2G_CUDA_COUNTERS
    uint32_t stat_pminlen_calls;
    uint32_t stat_pminlen_total;
    uint32_t stat_bloom_calls;
    uint32_t stat_bloom_hits;
    uint32_t stat_calls;
    uint32_t stat_m_total;
    uint32_t stat_d0;
    uint32_t stat_d0_hashloop;
    uint32_t stat_loop_match;
    uint32_t stat_loop_no_match;
    uint32_t stat_num_shift;
    uint32_t stat_total_shift;
#endif /* B2G_CUDA_COUNTERS */
} B2gCudaThreadCtx;

void MpmB2gCudaRegister(void);

void TmModuleCudaMpmB2gRegister(void);

int B2gCudaStartDispatcherThreadRC(const char *);
int B2gCudaStartDispatcherThreadAPC(const char *);

void B2gCudaKillDispatcherThreadRC(void);
void B2gCudaKillDispatcherThreadAPC(void);

void B2gCudaPushPacketTo_tv_CMB2_RC(Packet *);
void B2gCudaPushPacketTo_tv_CMB2_APC(Packet *);

#endif /* __SC_CUDA_SUPPORT__ */

#endif /* __UTIL_MPM_B2G_CUDA_H__ */
