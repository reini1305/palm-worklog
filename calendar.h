#ifndef CALENDAR_H
#define CALENDAR_H

#include <PalmOS.h>

void OpenCalendar(void);
void CloseCalendar(void);
void LoadCalendar(UInt16 day);
Char *CalText(void *rec);
Int16 CalStart(void *rec);

#endif //CALENDAR_H
