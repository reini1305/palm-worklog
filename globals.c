#include "globals.h"

DmOpenRef gDB;
UInt16    gDay;
Int16     gHdrRec;
Int16     gBegin, gEnd;
Column    gCols[2];

DmOpenRef gCalDB;
CalEvent  gCal[maxCal];
UInt16    gNumCal;

UInt16    gMonthFirst;
UInt16    gMonthDays;
Int16     gMonthBegin[31], gMonthEnd[31];
UInt16    gMonthTop;

Int16     gEditRec;
UInt8     gEditKind;
Int16     gEditTime;

Char      gDateLabel[dowDateStringLength], gBeginLabel[6], gEndLabel[6];
Char      gMonthLabel[10], gEditTimeLabel[6];

Boolean   gShowFullDescription = true;

const Char *kMonth[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                           "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
