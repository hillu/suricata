/* Copyright (c) 2009 Open Information Security Foundation */

/**
 * \author Endace Technology Limited
 */

#ifndef __CONF_H__
#define __CONF_H__

#include "queue.h"

/**
 * Structure of a configuration parameter.
 */
typedef struct ConfNode_ {
    char *name;
    char *val;

    int is_seq;
    int allow_override;

    struct ConfNode_ *parent;
    TAILQ_HEAD(, ConfNode_) head;
    TAILQ_ENTRY(ConfNode_) next;
} ConfNode;


/**
 * The default log directory.
 */
#define DEFAULT_LOG_DIR "/var/log/suricata"

void ConfInit(void);
void ConfDeInit(void);
ConfNode *ConfGetRootNode(void);
int ConfGet(char *name, char **vptr);
int ConfGetInt(char *name, intmax_t *val);
int ConfGetBool(char *name, int *val);
int ConfSet(char *name, char *val, int allow_override);
void ConfDump(void);
void ConfNodeDump(ConfNode *node, const char *prefix);
ConfNode *ConfNodeNew(void);
void ConfNodeFree(ConfNode *);
ConfNode *ConfGetNode(char *key);
void ConfCreateContextBackup(void);
void ConfRestoreContextBackup(void);
ConfNode *ConfNodeLookupChild(ConfNode *node, const char *key);
const char *ConfNodeLookupChildValue(ConfNode *node, const char *key);
void ConfNodeRemove(ConfNode *);
void ConfRegisterTests();

#endif /* ! __CONF_H__ */
