#ifndef __DETECT_RPC_H__
#define __DETECT_RPC_H__

/* At least we check the program, the version is optional,
 * and the procedure is optional if we are checking the version.
 * If we parse the wildcard "*" we will allow any value (no check) */
#define    DETECT_RPC_CHECK_PROGRAM   0x01
#define    DETECT_RPC_CHECK_VERSION   0x02
#define    DETECT_RPC_CHECK_PROCEDURE 0x04

/** Simple struct for a rpc msg call */
typedef struct RpcMsg_ {
     uint32_t xid;
     uint32_t type;     /**< CALL = 0 (We only search for CALLS */
     uint32_t rpcvers;  /**< must be equal to two (2) */
     uint32_t prog;
     uint32_t vers;
     uint32_t proc;
} RpcMsg;

/* Extract uint32_t */
#define EXT_GET_UINT32T(buf)      ((long)ntohl((long)*(buf)++))

typedef struct DetectRpcData_ {
    uint32_t program;
    uint32_t program_version;
    uint32_t procedure;
    uint8_t flags;
} DetectRpcData;

/* prototypes */
void DetectRpcRegister (void);

#endif /* __DETECT_RPC_H__ */

