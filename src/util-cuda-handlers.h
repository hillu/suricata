/**
 * Copyright (c) 2010 Open Information Security Foundation.
 *
 * \author Anoop Saldanha <poonaatsoc@gmail.com>
 */

/* macros decides if cuda is enabled for the platform or not */
#ifdef __SC_CUDA_SUPPORT__

#include <cuda.h>

#ifndef __UTIL_MPM_CUDA_HANDLERS_H__
#define __UTIL_MPM_CUDA_HANDLERS_H__

typedef enum {
    SC_CUDA_HL_MTYPE_RULE_NONE = -1,
    SC_CUDA_HL_MTYPE_RULE_CONTENTS = 0,
    SC_CUDA_HL_MTYPE_RULE_URICONTENTS,
    SC_CUDA_HL_MTYPE_APP_LAYER,
    SC_CUDA_HL_MTYPE_RULE_CUSTOM,
    SC_CUDA_HL_MTYPE_MAX,
} SCCudaHlModuleType;

typedef struct SCCudaHlModuleDevicePointer_ {
    /* device pointer name.  This is a primary key.  For the same module you
     * can't register different device pointers */
    char *name;
    CUdeviceptr d_ptr;

    struct SCCudaHlModuleDevicePointer_ *next;
} SCCudaHlModuleDevicePointer;

typedef struct SCCudaHlModuleData_ {
    /* The unique module handle.  This has to be first obtained from the
     * call to SCCudaHlGetUniqueHandle() */
    const char *name;
    int handle;

    CUcontext cuda_context;
    CUmodule cuda_module;
    void *(*SCCudaHlDispFunc)(void *);
    SCCudaHlModuleDevicePointer *device_ptrs;

    struct SCCudaHlModuleData_ *next;
} SCCudaHlModuleData;

int SCCudaHlGetCudaContext(CUcontext *, int);
int SCCudaHlGetCudaModule(CUmodule *, const char *, int);
int SCCudaHlGetCudaDevicePtr(CUdeviceptr *, const char *, size_t, void *, int);
int SCCudaHlRegisterDispatcherFunc(void *(*SCCudaHlDispFunc)(void *), int);

SCCudaHlModuleData *SCCudaHlGetModuleData(uint8_t);
const char *SCCudaHlGetModuleName(int);
int SCCudaHlGetModuleHandle(const char *);

int SCCudaHlRegisterModule(const char *);
int SCCudaHlDeRegisterModule(const char *);
void SCCudaHlDeRegisterAllRegisteredModules(void);

int SCCudaHlPushCudaContextFromModule(const char *);

int SCCudaHlTestEnvCudaContextInit(void);
int SCCudaHlTestEnvCudaContextDeInit(void);

void SCCudaHlProcessPacketWithDispatcher(Packet *, DetectEngineThreadCtx *,
                                         void *);
void SCCudaHlProcessUriWithDispatcher(uint8_t *, uint16_t, DetectEngineThreadCtx *,
                                      void *);

#endif /* __UTIL_CUDA_HANDLERS__ */

#endif /* __SC_CUDA_SUPPORT__ */
