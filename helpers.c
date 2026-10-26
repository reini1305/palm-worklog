#include "helpers.h"

void *GetObj(FormType *frm, UInt16 id)
{
    return FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, id));
}

void GetBounds(FormType *frm, UInt16 id, RectangleType *r)
{
    FrmGetObjectBounds(frm, FrmGetObjectIndex(frm, id), r);
}

UInt16 Today(void)
{
    DateType d;
    DateSecondsToDate(TimGetSeconds(), &d);
    return (UInt16)DateToDays(d);
}

Int16 NowMinutes(void)
{
    DateTimeType dt;
    TimSecondsToDateTime(TimGetSeconds(), &dt);
    return dt.hour * 60 + dt.minute;
}

DateType ToDate(UInt16 days)
{
    DateType d;
    DateDaysToDate(days, &d);
    return d;
}

Boolean PickTime(Int16 *t, const Char *title)
{
    Int16 cur = *t < 0 ? NowMinutes() : *t;
    Int16 hour = cur / 60, minute = cur % 60;
    if (!SelectOneTime(&hour, &minute, title)) return false;
    *t = hour * 60 + minute;
    return true;
}

/* "7:05", or "--:--" when unset. Also used for durations > 24h. */
void FormatTime(Int16 t, Char *s)
{
    if (t < 0) {
        StrCopy(s, "--:--");
        return;
    }
    StrIToA(s, t / 60);
    s += StrLen(s);
    *s++ = ':';
    *s++ = '0' + (t % 60) / 10;
    *s++ = '0' + t % 10;
    *s = 0;
}

/* Day of week + day/month in the user's preferred format, without the
 * year, e.g. "Fri 10/16" or "Fri 16.10." */
void FormatDay(UInt16 days, Char *s)
{
    DateType d = ToDate(days);
    DateFormatType fmt = (DateFormatType)PrefGetPreference(prefDateFormat);
    Char *date;

    DateToDOWDMFormat(d.month, d.day, d.year + firstYear, fmt, s);
    date = StrChr(s, ' ');
    if (!date || StrLen(date) < 4) return;
    date++;
    switch (fmt) {
    case dfYMDWithSlashes:
    case dfYMDWithDots:
    case dfYMDWithDashes:   /* "Fri 26/10/16" -> "Fri 10/16" */
        MemMove(date, date + 3, StrLen(date + 3) + 1);
        break;
    case dfDMYWithDots:     /* "Fri 16.10.26" -> "Fri 16.10." */
        s[StrLen(s) - 2] = 0;
        break;
    default:                /* "Fri 10/16/26" -> "Fri 10/16" */
        s[StrLen(s) - 3] = 0;
        break;
    }
}

void DrawRightAligned(const Char *s, Coord right, Coord y)
{
    UInt16 len = StrLen(s);
    WinDrawChars(s, len, right - FntCharsWidth(s, len), y);
}