/*
 * Read-only access to the built-in Datebook (DatebookDB, creator 'date').
 *
 * A packed appointment record is an ApptHdr, followed by the optional
 * parts its flags announce, in this order: alarm (2 bytes), repeat info
 * (ApptRepeat), exceptions (UInt16 count + DateType list), description
 * and note (NUL-terminated).
 */

#include "calendar.h"
#include "constants.h"
#include "globals.h"
#include "helpers.h"

#define calType         'DATA'
#define calCreator      'date'

#define apptAlarm       0x4000
#define apptRepeat      0x2000
#define apptExceptions  0x0800
#define apptDescription 0x0400

#define calNoTime       0xFF

enum { repDaily = 1, repWeekly, repMonthlyByDay, repMonthlyByDate, repYearly };

typedef struct {
    UInt8    startHour, startMin;   /* 0xFF if the event has no time */
    UInt8    endHour, endMin;
    DateType date;
    UInt16   flags;
} ApptHdr;

typedef struct {
    UInt8    type;
    UInt8    reserved1;
    DateType end;                   /* 0xFFFF if it repeats forever */
    UInt8    freq;
    UInt8    on;                    /* weekly: day mask, monthly by day: DayOfMonthType */
    UInt8    startOfWeek;
    UInt8    reserved2;
} ApptRepeat;

void OpenCalendar(void)
{
    gCalDB = DmOpenDatabaseByTypeCreator(calType, calCreator, dmModeReadOnly);
}

void CloseCalendar(void)
{
    if (gCalDB) DmCloseDatabase(gCalDB);
    gCalDB = NULL;
}

/* Walk the optional parts; returns the description (never NULL). */
static Char *Unpack(ApptHdr *a, ApptRepeat **rep, DateType **exc, UInt16 *nExc)
{
    UInt8 *p = (UInt8 *)(a + 1);

    *rep = NULL;
    *exc = NULL;
    *nExc = 0;
    if (a->flags & apptAlarm) p += 2;
    if (a->flags & apptRepeat) {
        *rep = (ApptRepeat *)p;
        p += sizeof(ApptRepeat);
    }
    if (a->flags & apptExceptions) {
        *nExc = *(UInt16 *)p;
        *exc = (DateType *)(p + 2);
        p += 2 + *nExc * sizeof(DateType);
    }
    return (a->flags & apptDescription) ? (Char *)p : "";
}

Char *CalText(void *rec)
{
    ApptRepeat *rep;
    DateType *exc;
    UInt16 nExc;
    return Unpack((ApptHdr *)rec, &rep, &exc, &nExc);
}

Int16 CalStart(void *rec)
{
    ApptHdr *a = (ApptHdr *)rec;
    return a->startHour == calNoTime ? kNoTime
                                     : a->startHour * 60 + a->startMin;
}

/* 0 = Sunday; 1 Jan 1904 was a Friday. */
static UInt16 Dow(UInt16 days)
{
    return (days + 5) % 7;
}

static Boolean Occurs(ApptHdr *a, ApptRepeat *rep, DateType *exc,
                      UInt16 nExc, UInt16 day)
{
    UInt16 start = (UInt16)DateToDays(a->date);
    UInt16 i, freq;
    Int16 months;
    DateType d;

    if (day < start) return false;
    if (!rep) return day == start;
    if (*(UInt16 *)&rep->end != 0xFFFF && day > DateToDays(rep->end))
        return false;
    for (i = 0; i < nExc; i++)
        if (DateToDays(exc[i]) == day) return false;

    freq = rep->freq ? rep->freq : 1;
    d = ToDate(day);
    months = (d.year - a->date.year) * 12 + d.month - a->date.month;

    switch (rep->type) {
    case repDaily:
        return (day - start) % freq == 0;
    case repWeekly: {
        UInt16 w0 = start - (Dow(start) + 7 - rep->startOfWeek) % 7;
        UInt16 w1 = day - (Dow(day) + 7 - rep->startOfWeek) % 7;
        return (rep->on & (1 << Dow(day))) && ((w1 - w0) / 7) % freq == 0;
    }
    case repMonthlyByDay:
        if (Dow(day) != rep->on % 7 || months % freq) return false;
        if (rep->on >= domLastSun)
            return d.day + 7 > DaysInMonth(d.month, d.year + firstYear);
        return (d.day - 1) / 7 == rep->on / 7;
    case repMonthlyByDate:
        return d.day == a->date.day && months % freq == 0;
    case repYearly:
        return d.month == a->date.month && d.day == a->date.day
            && (d.year - a->date.year) % freq == 0;
    }
    return false;
}

/* Fill gCal with the day's events, sorted by start time (untimed first).
 * The Datebook can't change while we run, so a day is only scanned once. */
void LoadCalendar(UInt16 day)
{
    static UInt16 loaded = 0xFFFF;
    UInt16 i, n;

    if (day == loaded) return;
    loaded = day;
    gNumCal = 0;
    if (!gCalDB) return;

    n = DmNumRecords(gCalDB);
    for (i = 0; i < n && gNumCal < maxCal; i++) {
        UInt16 attr, nExc, k;
        ApptRepeat *rep;
        DateType *exc;
        ApptHdr *a;
        MemHandle h;

        DmRecordInfo(gCalDB, i, &attr, NULL, NULL);
        if (attr & (dmRecAttrDelete | dmRecAttrSecret)) continue;
        h = DmQueryRecord(gCalDB, i);
        if (!h) continue;
        a = (ApptHdr *)MemHandleLock(h);
        Unpack(a, &rep, &exc, &nExc);
        if (Occurs(a, rep, exc, nExc, day)) {
            Int16 start = CalStart(a);
            for (k = gNumCal; k > 0 && gCal[k - 1].start > start; k--)
                gCal[k] = gCal[k - 1];
            gCal[k].rec = i;
            gCal[k].start = start;
            gNumCal++;
        }
        MemHandleUnlock(h);
    }
}
