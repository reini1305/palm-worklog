#ifndef HELPERS_H
#define HELPERS_H

#include <PalmOS.h>

void *GetObj(FormType *frm, UInt16 id);
void GetBounds(FormType *frm, UInt16 id, RectangleType *r);
UInt16 Today(void);
Int16 NowMinutes(void);
DateType ToDate(UInt16 days);
void FormatTime(Int16 t, Char *s);
void FormatDay(UInt16 days, Char *s);
void DrawRightAligned(const Char *s, Coord right, Coord y);
Boolean PickTime(Int16 *t, const Char *title);

#endif //HELPERS_H