#ifndef GLOBALS_H
#define GLOBALS_H

#include <PalmOS.h>
#include "constants.h"

/* Defined in globals.c. */
extern DmOpenRef gDB;
extern UInt16    gDay;                  /* day shown in day view */
extern Int16     gHdrRec;               /* index of the day record, -1 if none */
extern Int16     gBegin, gEnd;
extern Column    gCols[2];

extern UInt16    gMonthFirst;           /* first day of shown month */
extern UInt16    gMonthDays;
extern Int16     gMonthBegin[31], gMonthEnd[31];
extern UInt16    gMonthTop;

extern Int16     gEditRec;              /* -1 for a new item */
extern UInt8     gEditKind;
extern Int16     gEditTime;

extern Char      gDateLabel[dowDateStringLength], gBeginLabel[6], gEndLabel[6];
extern Char      gMonthLabel[10], gEditTimeLabel[6];
extern const Char *kMonth[12];

extern Boolean   gShowFullDescription;

#endif //GLOBALS_H
