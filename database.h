#ifndef DATABASE_H
#define DATABASE_H

#include <PalmOS.h>

void LookFor(FindParamsPtr params);
UInt16 GoTo(GoToParamsPtr params);
Int16 CompareRecs(void *a, void *b, Int16 other,
                         SortRecordInfoPtr ia, SortRecordInfoPtr ib,
                         MemHandle appInfoH);
UInt16 FindDay(UInt16 day);
DmOpenRef OpenWorkLogDB(void);
Err OpenDB(void);
void SaveRec(Int16 rec, UInt8 kind, UInt8 flags, Int16 t1, Int16 t2,
                    const Char *text);
void ToggleDone(UInt16 rec);

#endif //DATABASE_H