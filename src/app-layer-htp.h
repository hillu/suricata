/* Copyright (C) 2007-2011 Open Information Security Foundation
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
 * \defgroup httplayer HTTP layer support
 *
 * @{
 */

/**
 * \file
 *
 * \author Gurvinder Singh <gurvindersinghdahiya@gmail.com>
 * \author Pablo Rincon <pablo.rincon.crespo@gmail.com>
 *
 * This file provides a HTTP protocol support for the engine using HTP library.
 */

#ifndef __APP_LAYER_HTP_H__
#define __APP_LAYER_HTP_H__

#include "util-radix-tree.h"
#include "util-file.h"

#include <htp/htp.h>

/* default request body limit */
#define HTP_CONFIG_DEFAULT_REQUEST_BODY_LIMIT       4096U
#define HTP_CONFIG_DEFAULT_RESPONSE_BODY_LIMIT      4096U

/** a boundary should be smaller in size */
#define HTP_BOUNDARY_MAX                            200U

#define HTP_FLAG_STATE_OPEN         0x0001    /**< Flag to indicate that HTTP
                                             connection is open */
#define HTP_FLAG_STATE_CLOSED       0x0002    /**< Flag to indicate that HTTP
                                             connection is closed */
#define HTP_FLAG_STATE_DATA         0x0004    /**< Flag to indicate that HTTP
                                             connection needs more data */
#define HTP_FLAG_STATE_ERROR        0x0008    /**< Flag to indicate that an error
                                             has been occured on HTTP
                                             connection */
#define HTP_FLAG_NEW_BODY_SET       0x0010    /**< Flag to indicate that HTTP
                                             has parsed a new body (for
                                             pcre) */
#define HTP_FLAG_STORE_FILES_TS     0x0020
#define HTP_FLAG_STORE_FILES_TC     0x0040
#define HTP_FLAG_STORE_FILES_TX_TS  0x0080
#define HTP_FLAG_STORE_FILES_TX_TC  0x0100
/** flag the state that a new file has been set in this tx */
#define HTP_FLAG_NEW_FILE_TX_TS     0x0200
/** flag the state that a new file has been set in this tx */
#define HTP_FLAG_NEW_FILE_TX_TC     0x0400

enum {
    HTP_BODY_NONE = 0,                  /**< Flag to indicate the current
                                             operation */
    HTP_BODY_REQUEST,                   /**< Flag to indicate that the
                                             current operation is a request */
    HTP_BODY_RESPONSE                   /**< Flag to indicate that the current
                                          * operation is a response */
};

enum {
    HTP_BODY_REQUEST_NONE = 0,
    HTP_BODY_REQUEST_MULTIPART,
    HTP_BODY_REQUEST_PUT,
};

enum {
    HTTP_DECODER_EVENT_UNKNOWN_ERROR,
    HTTP_DECODER_EVENT_GZIP_DECOMPRESSION_FAILED,
    HTTP_DECODER_EVENT_REQUEST_FIELD_MISSING_COLON,
    HTTP_DECODER_EVENT_RESPONSE_FIELD_MISSING_COLON,
    HTTP_DECODER_EVENT_INVALID_REQUEST_CHUNK_LEN,
    HTTP_DECODER_EVENT_INVALID_RESPONSE_CHUNK_LEN,
    HTTP_DECODER_EVENT_INVALID_TRANSFER_ENCODING_VALUE_IN_REQUEST,
    HTTP_DECODER_EVENT_INVALID_TRANSFER_ENCODING_VALUE_IN_RESPONSE,
    HTTP_DECODER_EVENT_INVALID_CONTENT_LENGTH_FIELD_IN_REQUEST,
    HTTP_DECODER_EVENT_INVALID_CONTENT_LENGTH_FIELD_IN_RESPONSE,
    HTTP_DECODER_EVENT_100_CONTINUE_ALREADY_SEEN,
    HTTP_DECODER_EVENT_UNABLE_TO_MATCH_RESPONSE_TO_REQUEST,
    HTTP_DECODER_EVENT_INVALID_SERVER_PORT_IN_REQUEST,
    HTTP_DECODER_EVENT_INVALID_AUTHORITY_PORT,
    HTTP_DECODER_EVENT_REQUEST_HEADER_INVALID,
    HTTP_DECODER_EVENT_RESPONSE_HEADER_INVALID,
    HTTP_DECODER_EVENT_MISSING_HOST_HEADER,
    HTTP_DECODER_EVENT_HOST_HEADER_AMBIGUOUS,
    HTTP_DECODER_EVENT_INVALID_REQUEST_FIELD_FOLDING,
    HTTP_DECODER_EVENT_INVALID_RESPONSE_FIELD_FOLDING,
    HTTP_DECODER_EVENT_REQUEST_FIELD_TOO_LONG,
    HTTP_DECODER_EVENT_RESPONSE_FIELD_TOO_LONG,
};

#define HTP_PCRE_NONE           0x00    /**< No pcre executed yet */
#define HTP_PCRE_DONE           0x01    /**< Flag to indicate that pcre has
                                             done some inspection in the
                                             chunks */
#define HTP_PCRE_HAS_MATCH      0x02    /**< Flag to indicate that the chunks
                                             matched on some rule */

/** Struct used to hold chunks of a body on a request */
typedef struct HtpBodyChunk_ {
    uint8_t *data;              /**< Pointer to the data of the chunk */
    uint32_t len;               /**< Length of the chunk */
    uint32_t id;                /**< number of chunk of the current body */
    struct HtpBodyChunk_ *next; /**< Pointer to the next chunk */
    uint64_t stream_offset;
} HtpBodyChunk;

/** Struct used to hold all the chunks of a body on a request */
typedef struct HtpBody_ {
    HtpBodyChunk *first; /**< Pointer to the first chunk */
    HtpBodyChunk *last;  /**< Pointer to the last chunk */
    uint32_t nchunks;    /**< Number of chunks in the current operation */
    uint8_t type;

    /* Holds the length of the htp request body */
    uint64_t content_len;
    /* Holds the length of the htp request body seen so far */
    uint64_t content_len_so_far;

    uint64_t body_parsed;

    /* pahole: padding: 3 */
} HtpBody;

#define HTP_BODY_COMPLETE       0x01    /**< body is complete or limit is reached,
                                             either way, this is it. */
#define HTP_CONTENTTYPE_SET     0x02    /**< We have the content type */
#define HTP_BOUNDARY_SET        0x04    /**< We have a boundary string */
#define HTP_BOUNDARY_OPEN       0x08    /**< We have a boundary string */
#define HTP_FILENAME_SET        0x10    /**< filename is registered in the flow */
#define HTP_DONTSTORE           0x20    /**< not storing this file */

#define HTP_TX_HAS_FILE             0x01
#define HTP_TX_HAS_FILENAME         0x02    /**< filename is known at this time */
#define HTP_TX_HAS_TYPE             0x04
#define HTP_TX_HAS_FILECONTENT      0x08    /**< file has content so we can do type detect */

#define HTP_RULE_NEED_FILE          HTP_TX_HAS_FILE
#define HTP_RULE_NEED_FILENAME      HTP_TX_HAS_FILENAME
#define HTP_RULE_NEED_TYPE          HTP_TX_HAS_TYPE
#define HTP_RULE_NEED_FILECONTENT   HTP_TX_HAS_FILECONTENT

/** Now the Body Chunks will be stored per transaction, at
  * the tx user data */
typedef struct HtpTxUserData_ {
    /* Body of the request (if any) */
    HtpBody request_body;
    HtpBody response_body;

    /** Holds the boundary identificator string if any (used on
     *  multipart/form-data only)
     */
    uint8_t *boundary;
    uint8_t boundary_len;

    uint8_t flags;

    int16_t operation;
} HtpTxUserData;

typedef struct HtpState_ {

    htp_connp_t *connp;     /**< Connection parser structure for
                                 each connection */
    Flow *f;                /**< Needed to retrieve the original flow when usin HTPLib callbacks */
    uint16_t flags;
    uint16_t transaction_cnt;
    uint16_t transaction_done;
    uint16_t store_tx_id;
    uint32_t request_body_limit;
    uint32_t response_body_limit;
    FileContainer *files_ts;
    FileContainer *files_tc;
} HtpState;

void RegisterHTPParsers(void);
void HTPParserRegisterTests(void);
void HTPAtExitPrintStats(void);
void HTPFreeConfig(void);

htp_tx_t *HTPTransactionMain(const HtpState *);

int HTPCallbackRequestBodyData(htp_tx_data_t *);
int HtpTransactionGetLoggableId(Flow *);
void HtpBodyPrint(HtpBody *);
void HtpBodyFree(HtpBody *);
void AppLayerHtpRegisterExtraCallbacks(void);
/* To free the state from unittests using app-layer-htp */
void HTPStateFree(void *);
void AppLayerHtpEnableRequestBodyCallback(void);
void AppLayerHtpEnableResponseBodyCallback(void);
void AppLayerHtpNeedFileInspection(void);
void AppLayerHtpPrintStats(void);

#endif	/* __APP_LAYER_HTP_H__ */

/**
 * @}
 */
