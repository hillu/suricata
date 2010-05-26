/* Copyright (C) 2007-2010 Open Information Security Foundation
 *
 * You can copy, redistribute or modify this Program under the terms of
 * the GNU General Public License version 2 as published by the Free
 * Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

/**
 * \file
 *
 * \author Victor Julien <victor@inliniac.net>
 */

#ifndef __APP_LAYER_DETECT_PROTO_H__
#define __APP_LAYER_DETECT_PROTO_H__

#include "stream.h"
#include "detect-content.h"

typedef struct AlpProtoDetectDirectionThread_ {
    MpmThreadCtx mpm_ctx;
    PatternMatcherQueue pmq;
} AlpProtoDetectDirectionThread;

typedef struct AlpProtoDetectThreadCtx_ {
    AlpProtoDetectDirectionThread toserver;
    AlpProtoDetectDirectionThread toclient;
} AlpProtoDetectThreadCtx;

/** \brief Signature for proto detection
 *  \todo we might just use SigMatch here
 */
typedef struct AlpProtoSignature_ {
    uint16_t proto;                     /**< protocol */
    DetectContentData *co;              /**< content match that needs to match */
    struct AlpProtoSignature_ *next;    /**< next signature */
    struct AlpProtoSignature_ *map_next;    /**< next signature with same id */
} AlpProtoSignature;

#define ALP_DETECT_MAX 256

typedef struct AlpProtoDetectDirection_ {
    MpmCtx mpm_ctx;
    uint32_t id;
    uint16_t map[ALP_DETECT_MAX];   /**< a mapping between condition id's and
                                         protocol */
    uint16_t max_len;              /**< max length of all patterns, so we can
                                         limit the search */
    uint16_t min_len;              /**< min length of all patterns, so we can
                                         tell the stream engine to feed data
                                         to app layer as soon as it has min
                                         size data */
} AlpProtoDetectDirection;

typedef struct AlpProtoDetectCtx_ {
    AlpProtoDetectDirection toserver;
    AlpProtoDetectDirection toclient;

    MpmPatternIdStore *mpm_pattern_id_store;    /** pattern id store */

    int alp_content_module_handle;

    /** mapping between proto id's and pattern id's: this will
     *  be used to look up a proto by the pattern id. The pattern
     *  id is returned by the mpm */
    //uint16_t *proto_map;

    /** Mapping between pattern id and signature. As each signature has a
     *  unique pattern with a unique id, we can lookup the signature by
     *  the pattern id. */
    AlpProtoSignature **map;

    AlpProtoSignature *head;    /**< list of sigs */
    uint16_t sigs;              /**< number of sigs */
} AlpProtoDetectCtx;

void *AppLayerDetectProtoThread(void *td);

void AppLayerDetectProtoThreadInit(void);

uint16_t AppLayerDetectGetProto(AlpProtoDetectCtx *, AlpProtoDetectThreadCtx *, uint8_t *, uint16_t, uint8_t);

void AppLayerDetectProtoThreadSpawn(void);
void AlpDetectRegisterTests(void);

void AlpProtoFinalize2Thread(AlpProtoDetectThreadCtx *);
void AlpProtoDeFinalize2Thread (AlpProtoDetectThreadCtx *);
void AlpProtoDestroy(void);

#endif /* __APP_LAYER_DETECT_PROTO_H__ */

