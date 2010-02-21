/** Copyright (c) 2009 Open Information Security Foundation.
 *  \author Anoop Saldanha <poonaatsoc@gmail.com>
 */

#ifndef __DETECT_DCE_OPNUM_H__
#define __DETECT_DCE_OPNUM_H__

typedef struct DetectDceOpnumRange_ {
    uint32_t range1;
    uint32_t range2;
    struct DetectDceOpnumRange_ *next;
} DetectDceOpnumRange;

typedef struct DetectDceOpnumData_ {
    DetectDceOpnumRange *range;
} DetectDceOpnumData;

void DetectDceOpnumRegister(void);
void DetectDceOpnumRegisterTests(void);

#endif /* __DETECT_DCE_OPNUM_H__ */
