#ifndef DAY_H
#define DAY_H

#include <PalmOS.h>
#include "constants.h"

void LoadDay(void);
void InitColumns(FormType *frm);
void DrawColumn(FormType *frm, Column *c);
void DrawDay(FormType *frm);
void GoToDay(FormType *frm, UInt16 day);
void SetDayTimes(FormType *frm, Int16 begin, Int16 end);
Boolean DayFormHandleEvent(EventType *e);

#endif //DAY_H