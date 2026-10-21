/*
 * WorkLog - a daily work journal for Palm OS 3.5+ (targets Palm OS 4).
 *
 * Day view:   begin/end of the work day, meetings (left), to-dos (right).
 * Month view: begin/end/duration per day plus a monthly total.
 *
 * Storage: one database, one record per item, kept sorted by
 * (date, kind, time). Each record is a RecHdr followed by NUL-terminated text.
 */

#include <PalmOS.h>
#include "resources.h"

#define appCreator      'WkJn'
#define dbType          'DATA'
#define dbName          "WorkLogDB-WkJn"

enum { kindDay = 0, kindMeeting = 1, kindTodo = 2 };
#define flagDone        0x01
#define kNoTime          (-1)

typedef struct {
    UInt16 date;        /* days since 1.1.1904 */
    UInt8  kind;
    UInt8  flags;
    Int16  t1;          /* day: begin, meeting: start (minutes after midnight) */
    Int16  t2;          /* day: end */
} RecHdr;               /* followed by NUL-terminated text */

#define lineH           11
#define maxLines        128

typedef struct {
    UInt16 rec;         /* record index */
    UInt16 off;         /* byte offset into the record's text */
    UInt8  len;
    UInt8  first;       /* first line of an item: draw time / checkbox */
} Line;

typedef struct {
    UInt16 gadgetID;
    UInt16 barID;
    UInt8  kind;
    Coord  indent;      /* x offset of the text */
    Coord  textW;       /* wrap width */
    UInt16 numLines;
    UInt16 top;         /* first visible line */
    Line   lines[maxLines];
} Column;

static DmOpenRef gDB;
static UInt16    gDay;                  /* day shown in day view */
static Int16     gHdrRec;               /* index of the day record, -1 if none */
static Int16     gBegin, gEnd;
static Column    gCols[2];

static UInt16    gMonthFirst;           /* first day of shown month */
static UInt16    gMonthDays;
static Int16     gMonthBegin[31], gMonthEnd[31];
static UInt16    gMonthTop;

static Int16     gEditRec;              /* -1 for a new item */
static UInt8     gEditKind;
static Int16     gEditTime;

static Char      gDateLabel[12], gBeginLabel[6], gEndLabel[6];
static Char      gMonthLabel[10], gEditTimeLabel[6];

static const Char *kDow[] = { "Su", "Mo", "Tu", "We", "Th", "Fr", "Sa" };
static const Char *kMonth[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

static Boolean   showFullDescription = true;

/* ------------------------------------------------------------ helpers */

static void *GetObj(FormType *frm, UInt16 id)
{
    return FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, id));
}

static void GetBounds(FormType *frm, UInt16 id, RectangleType *r)
{
    FrmGetObjectBounds(frm, FrmGetObjectIndex(frm, id), r);
}

static UInt16 Today(void)
{
    DateType d;
    DateSecondsToDate(TimGetSeconds(), &d);
    return (UInt16)DateToDays(d);
}

static Int16 NowMinutes(void)
{
    DateTimeType dt;
    TimSecondsToDateTime(TimGetSeconds(), &dt);
    return dt.hour * 60 + dt.minute;
}

static DateType ToDate(UInt16 days)
{
    DateType d;
    DateDaysToDate(days, &d);
    return d;
}

/* "7:05", or "--:--" when unset. Also used for durations > 24h. */
static void FormatTime(Int16 t, Char *s)
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

/* "Fr 16.10." */
static void FormatDay(UInt16 days, Char *s)
{
    DateType d = ToDate(days);
    StrCopy(s, kDow[DayOfWeek(d.month, d.day, d.year + firstYear)]);
    s += StrLen(s);
    *s++ = ' ';
    StrIToA(s, d.day);
    s += StrLen(s);
    *s++ = '.';
    StrIToA(s, d.month);
    s += StrLen(s);
    *s++ = '.';
    *s = 0;
}

static void DrawRightAligned(const Char *s, Coord right, Coord y)
{
    UInt16 len = StrLen(s);
    WinDrawChars(s, len, right - FntCharsWidth(s, len), y);
}

/* ----------------------------------------------------------- database */

static Int16 CompareRecs(void *a, void *b, Int16 other,
                         SortRecordInfoPtr ia, SortRecordInfoPtr ib,
                         MemHandle appInfoH)
{
    RecHdr *x = (RecHdr *)a, *y = (RecHdr *)b;
    if (x->date != y->date) return x->date < y->date ? -1 : 1;
    if (x->kind != y->kind) return x->kind < y->kind ? -1 : 1;
    if (x->t1 != y->t1)     return x->t1 < y->t1 ? -1 : 1;
    return 0;
}

/* Index of the first record on or after the given day. */
static UInt16 FindDay(UInt16 day)
{
    RecHdr key;
    key.date = day;
    key.kind = kindDay;
    key.flags = 0;
    key.t1 = -32768;
    key.t2 = 0;
    return DmFindSortPosition(gDB, &key, NULL, CompareRecs, 0);
}

static Err OpenDB(void)
{
    gDB = DmOpenDatabaseByTypeCreator(dbType, appCreator, dmModeReadWrite);
    if (!gDB) {
        LocalID id;
        UInt16 attr;
        Err err = DmCreateDatabase(0, dbName, appCreator, dbType, false);
        if (err) return err;
        id = DmFindDatabase(0, dbName);
        DmDatabaseInfo(0, id, NULL, &attr, NULL, NULL, NULL, NULL, NULL,
                       NULL, NULL, NULL, NULL);
        attr |= dmHdrAttrBackup;
        DmSetDatabaseInfo(0, id, NULL, &attr, NULL, NULL, NULL, NULL, NULL,
                          NULL, NULL, NULL, NULL);
        gDB = DmOpenDatabase(0, id, dmModeReadWrite);
        if (!gDB) return DmGetLastErr();
    }
    return errNone;
}

/* Write a record (rec < 0 creates one) and keep the database sorted. */
static void SaveRec(Int16 rec, UInt8 kind, UInt8 flags, Int16 t1, Int16 t2,
                    const Char *text)
{
    RecHdr hdr;
    UInt16 len = text ? StrLen(text) : 0;
    UInt16 idx;
    MemHandle h;
    void *p;

    hdr.date = gDay;
    hdr.kind = kind;
    hdr.flags = flags;
    hdr.t1 = t1;
    hdr.t2 = t2;

    if (rec < 0) {
        idx = dmMaxRecordIndex;
        h = DmNewRecord(gDB, &idx, sizeof(hdr) + len + 1);
    } else {
        idx = rec;
        h = DmResizeRecord(gDB, idx, sizeof(hdr) + len + 1);
        if (h) h = DmGetRecord(gDB, idx);
    }
    if (!h) {
        FrmAlert(MemoryAlert);
        return;
    }
    p = MemHandleLock(h);
    DmWrite(p, 0, &hdr, sizeof(hdr));
    DmWrite(p, sizeof(hdr), text ? text : "", len + 1);
    MemHandleUnlock(h);
    DmReleaseRecord(gDB, idx, true);
    DmInsertionSort(gDB, CompareRecs, 0);
}

static void ToggleDone(UInt16 rec)
{
    MemHandle h = DmGetRecord(gDB, rec);
    RecHdr *r = (RecHdr *)MemHandleLock(h);
    UInt8 f = r->flags ^ flagDone;
    Int16 t = f;
    DmWrite(r, OffsetOf(RecHdr, t1), &t, sizeof(t));
    DmWrite(r, OffsetOf(RecHdr, flags), &f, 1);
    MemHandleUnlock(h);
    DmReleaseRecord(gDB, rec, true);
    DmInsertionSort(gDB, CompareRecs, 0);
}

/* ----------------------------------------------------------- day view */

static void AddLines(Column *c, UInt16 rec, RecHdr *r)
{
    Char *text = (Char *)(r + 1), *p = text;
    Boolean first = true;
    UInt8 lines = 0;
    const UInt8 max_lines = showFullDescription ? 128 : 2;

    do {
        UInt16 len = FntWordWrap(p, c->textW);
        if (c->numLines == maxLines) break;
        if (len == 0 && *p) len = 1;
        c->lines[c->numLines].rec = rec;
        c->lines[c->numLines].off = p - text;
        c->lines[c->numLines].len = len;
        c->lines[c->numLines].first = first;
        c->numLines++;
        lines++;
        p += len;
        first = false;
    } while (*p && lines < max_lines);
}

static void LoadDay(void)
{
    UInt16 i, n = DmNumRecords(gDB);
    FontID old = FntSetFont(stdFont);

    gHdrRec = -1;
    gBegin = gEnd = kNoTime;
    gCols[0].numLines = gCols[1].numLines = 0;

    for (i = FindDay(gDay); i < n; i++) {
        MemHandle h = DmQueryRecord(gDB, i);
        RecHdr *r;
        if (!h) continue;
        r = (RecHdr *)MemHandleLock(h);
        if (r->date != gDay) {
            MemHandleUnlock(h);
            break;
        }
        if (r->kind == kindDay) {
            gHdrRec = i;
            gBegin = r->t1;
            gEnd = r->t2;
        } else {
            AddLines(&gCols[r->kind - 1], i, r);
        }
        MemHandleUnlock(h);
    }
    FntSetFont(old);
}

static void InitColumns(FormType *frm)
{
    RectangleType r;
    Column *m = &gCols[0], *t = &gCols[1];

    m->gadgetID = DayMeetings;
    m->barID = DayMeetingsBar;
    m->kind = kindMeeting;
    m->indent = 30;
    GetBounds(frm, DayMeetings, &r);
    m->textW = r.extent.x - m->indent;

    t->gadgetID = DayTodos;
    t->barID = DayTodosBar;
    t->kind = kindTodo;
    t->indent = 12;
    GetBounds(frm, DayTodos, &r);
    t->textW = r.extent.x - t->indent;
}

static void DrawColumn(FormType *frm, Column *c)
{
    RectangleType r;
    UInt16 i, rows;
    Int16 maxTop;
    FontID old;

    GetBounds(frm, c->gadgetID, &r);
    rows = r.extent.y / lineH;
    maxTop = c->numLines > rows ? c->numLines - rows : 0;
    if (c->top > maxTop) c->top = maxTop;

    WinEraseRectangle(&r, 0);
    old = FntGetFont();
    for (i = 0; i < rows && c->top + i < c->numLines; i++) {
        Line *ln = &c->lines[c->top + i];
        MemHandle h = DmQueryRecord(gDB, ln->rec);
        RecHdr *rec = (RecHdr *)MemHandleLock(h);
        Char *s = (Char *)(rec + 1) + ln->off;
        UInt16 len = ln->len;
        Coord y = r.topLeft.y + i * lineH;

        while (len && (s[len - 1] == '\n' || s[len - 1] == ' ')) len--;

        if (ln->first) {
            if (c->kind == kindMeeting) {
                Char t[8];
                FormatTime(rec->t1, t);
                FntSetFont(boldFont);
                WinDrawChars(t, StrLen(t), r.topLeft.x + 1, y);
            } else {
                Char box = (rec->flags & flagDone) ? symbolCheckboxOn
                                                   : symbolCheckboxOff;
                FntSetFont(symbol11Font);
                WinDrawChars(&box, 1, r.topLeft.x, y);
            }
        }
        FntSetFont(stdFont);
        WinDrawChars(s, len, r.topLeft.x + c->indent, y);
        MemHandleUnlock(h);
    }
    FntSetFont(old);

    SclSetScrollBar((ScrollBarType *)GetObj(frm, c->barID),
                    c->top, 0, maxTop, rows - 1);
}

static void DrawDay(FormType *frm)
{
    FormatDay(gDay, gDateLabel);
    CtlSetLabel((ControlType *)GetObj(frm, DayDate), gDateLabel);
    FormatTime(gBegin, gBeginLabel);
    CtlSetLabel((ControlType *)GetObj(frm, DayBegin), gBeginLabel);
    FormatTime(gEnd, gEndLabel);
    CtlSetLabel((ControlType *)GetObj(frm, DayEnd), gEndLabel);
    CtlSetValue((ControlType *)GetObj(frm, DayShowFull), showFullDescription);

    DrawColumn(frm, &gCols[0]);
    DrawColumn(frm, &gCols[1]);
    WinDrawLine(83, 33, 83, 142);
}

static void GoToDay(FormType *frm, UInt16 day)
{
    gDay = day;
    gCols[0].top = gCols[1].top = 0;
    LoadDay();
    DrawDay(frm);
}

static void SetDayTimes(FormType *frm, Int16 begin, Int16 end)
{
    SaveRec(gHdrRec, kindDay, 0, begin, end, NULL);
    LoadDay();
    DrawDay(frm);
}

/* True if the shown day already has a to-do with this text. */
static Boolean HasTodo(const Char *text)
{
    UInt16 i, n = DmNumRecords(gDB);
    Boolean found = false;

    for (i = FindDay(gDay); i < n && !found; i++) {
        MemHandle h = DmQueryRecord(gDB, i);
        RecHdr *r;
        if (!h) continue;
        r = (RecHdr *)MemHandleLock(h);
        if (r->date != gDay) {
            MemHandleUnlock(h);
            break;
        }
        found = r->kind == kindTodo && StrCompare((Char *)(r + 1), text) == 0;
        MemHandleUnlock(h);
    }
    return found;
}

/*
 * Copy the open to-dos of the last earlier day that has any to-dos
 * (skipping empty days such as weekends) to the shown day.
 */
static void CopyOpenTodos(FormType *frm)
{
    UInt16 i = FindDay(gDay), n, src = 0;
    Boolean found = false;

    /* Records are sorted by (date, kind), so walking backwards the first
       to-do belongs to the latest earlier day with to-dos. */
    while (i > 0 && !found) {
        MemHandle h = DmQueryRecord(gDB, --i);
        RecHdr *r;
        if (!h) continue;
        r = (RecHdr *)MemHandleLock(h);
        if (r->kind == kindTodo) {
            src = r->date;
            found = true;
        }
        MemHandleUnlock(h);
    }
    if (!found) return;

    /* New records sort after the source day, so its indices stay valid. */
    n = DmNumRecords(gDB);
    for (i = FindDay(src); i < n; i++) {
        MemHandle h = DmQueryRecord(gDB, i);
        RecHdr *r;
        Char *text = NULL;
        if (!h) continue;
        r = (RecHdr *)MemHandleLock(h);
        if (r->date != src) {
            MemHandleUnlock(h);
            break;
        }
        if (r->kind == kindTodo && !(r->flags & flagDone)) {
            text = (Char *)MemPtrNew(StrLen((Char *)(r + 1)) + 1);
            if (text) StrCopy(text, (Char *)(r + 1));
        }
        MemHandleUnlock(h);
        if (text) {
            if (!HasTodo(text)) SaveRec(-1, kindTodo, 0, 0, 0, text);
            MemPtrFree(text);
        }
    }
    LoadDay();
    DrawDay(frm);
}

static Boolean PickTime(Int16 *t, const Char *title)
{
    Int16 cur = *t < 0 ? NowMinutes() : *t;
    Int16 hour = cur / 60, minute = cur % 60;
    if (!SelectOneTime(&hour, &minute, title)) return false;
    *t = hour * 60 + minute;
    return true;
}

static void OpenEditor(Int16 rec, UInt8 kind)
{
    gEditRec = rec;
    gEditKind = kind;
    if (rec >= 0) {
        MemHandle h = DmQueryRecord(gDB, rec);
        RecHdr *r = (RecHdr *)MemHandleLock(h);
        gEditTime = r->t1;
        MemHandleUnlock(h);
    } else {
        UInt16 n, i;
        // take time of latest meeting today
        gEditTime = (NowMinutes() / 60) * 60;
        n = DmNumRecords(gDB);
        for (i = FindDay(gDay); i < n; i++) {
            MemHandle h = DmQueryRecord(gDB, i);
            RecHdr *r;
            if (!h) continue;
            r = (RecHdr *)MemHandleLock(h);
            if (r->date != gDay) {
                MemHandleUnlock(h);
                break;
            }
            if (r->kind == kindMeeting) {
                gEditTime = r->t1;
            }
            MemHandleUnlock(h);
        }
    }
    FrmPopupForm(EditForm);
}

static Boolean ColumnTap(FormType *frm, Column *c, Coord x, Coord y)
{
    RectangleType r;
    UInt16 line;

    GetBounds(frm, c->gadgetID, &r);
    if (!RctPtInRectangle(x, y, &r)) return false;

    line = c->top + (y - r.topLeft.y) / lineH;
    if (line >= c->numLines) {
        OpenEditor(-1, c->kind);
    } else if (c->kind == kindTodo && c->lines[line].first
               && x < r.topLeft.x + c->indent) {
        ToggleDone(c->lines[line].rec);
        LoadDay();
        DrawColumn(frm, c);
    } else {
        OpenEditor(c->lines[line].rec, c->kind);
    }
    return true;
}

static Boolean DayFormHandleEvent(EventType *e)
{
    FormType *frm = FrmGetActiveForm();
    Int16 t;

    switch (e->eType) {
    case frmOpenEvent:
        InitColumns(frm);
        LoadDay();
        FrmDrawForm(frm);
        DrawDay(frm);
        return true;

    case frmUpdateEvent:
        LoadDay();
        FrmDrawForm(frm);
        DrawDay(frm);
        return true;

    case ctlRepeatEvent:
        if (e->data.ctlRepeat.controlID == DayPrev)
            GoToDay(frm, gDay - 1);
        else if (e->data.ctlRepeat.controlID == DayNext)
            GoToDay(frm, gDay + 1);
        return false;   /* keep repeating */

    case ctlSelectEvent:
        switch (e->data.ctlSelect.controlID) {
        case DayShowFull:
            showFullDescription = !showFullDescription;
            LoadDay();
            DrawDay(frm);
            return true;
        case DayDate: {
            DateType d = ToDate(gDay);
            Int16 month = d.month, day = d.day, year = d.year + firstYear;
            if (SelectDay(selectDayByDay, &month, &day, &year, "Go to Day")) {
                d.year = year - firstYear;
                d.month = month;
                d.day = day;
                GoToDay(frm, (UInt16)DateToDays(d));
            }
            return true;
        }
        case DayBegin:
            t = gBegin;
            if (PickTime(&t, "Begin of Work")) SetDayTimes(frm, t, gEnd);
            return true;
        case DayEnd:
            t = gEnd;
            if (PickTime(&t, "End of Work")) SetDayTimes(frm, gBegin, t);
            return true;
        case DayNewMeeting:
            OpenEditor(-1, kindMeeting);
            return true;
        case DayNewTodo:
            OpenEditor(-1, kindTodo);
            return true;
        case DayMonth:
            FrmGotoForm(MonthForm);
            return true;
        }
        break;

    case sclRepeatEvent: {
        Column *c = e->data.sclRepeat.scrollBarID == DayMeetingsBar
                    ? &gCols[0] : &gCols[1];
        c->top = e->data.sclRepeat.newValue;
        DrawColumn(frm, c);
        return false;   /* let the scroll bar keep tracking */
    }

    case penDownEvent:
        return ColumnTap(frm, &gCols[0], e->screenX, e->screenY)
            || ColumnTap(frm, &gCols[1], e->screenX, e->screenY);

    case keyDownEvent:
        if (EvtKeydownIsVirtual(e)) {
            if (e->data.keyDown.chr == vchrPageUp) {
                GoToDay(frm, gDay - 1);
                return true;
            }
            if (e->data.keyDown.chr == vchrPageDown) {
                GoToDay(frm, gDay + 1);
                return true;
            }
        }
        break;

    case menuEvent:
        switch (e->data.menu.itemID) {
        case DayMenuBeginNow:
            SetDayTimes(frm, NowMinutes(), gEnd);
            break;
        case DayMenuEndNow:
            SetDayTimes(frm, gBegin, NowMinutes());
            break;
        case DayMenuNewMeeting:
            OpenEditor(-1, kindMeeting);
            break;
        case DayMenuNewTodo:
            OpenEditor(-1, kindTodo);
            break;
        case DayMenuCopyTodos:
            CopyOpenTodos(frm);
            break;
        case DayMenuAbout:
            FrmAlert(AboutAlert);
            break;
        }
        return true;

    default:
        break;
    }
    return false;
}

/* ---------------------------------------------------------- edit form */

static void EditFormInit(FormType *frm)
{
    FieldType *fld = (FieldType *)GetObj(frm, EditField);

    if (gEditKind == kindMeeting) {
        FrmSetTitle(frm, "Meeting");
        FormatTime(gEditTime, gEditTimeLabel);
        CtlSetLabel((ControlType *)GetObj(frm, EditTime), gEditTimeLabel);
    } else {
        FrmSetTitle(frm, "To Do");
        FrmHideObject(frm, FrmGetObjectIndex(frm, EditTimeLabel));
        FrmHideObject(frm, FrmGetObjectIndex(frm, EditTime));
    }
    if (gEditRec < 0)
        FrmHideObject(frm, FrmGetObjectIndex(frm, EditDelete));

    if (gEditRec >= 0) {
        MemHandle rh = DmQueryRecord(gDB, gEditRec);
        Char *text = (Char *)((RecHdr *)MemHandleLock(rh) + 1);
        MemHandle th = MemHandleNew(StrLen(text) + 1);
        if (th) {
            StrCopy((Char *)MemHandleLock(th), text);
            MemHandleUnlock(th);
            FldSetTextHandle(fld, th);
        }
        MemHandleUnlock(rh);
    }
}

static void EditFormSave(FormType *frm)
{
    FieldType *fld = (FieldType *)GetObj(frm, EditField);
    Char *text = FldGetTextPtr(fld);
    UInt8 flags = 0;

    if (gEditRec < 0 && (!text || !*text)) return;  /* nothing entered */

    if (gEditRec >= 0) {
        MemHandle h = DmQueryRecord(gDB, gEditRec);
        flags = ((RecHdr *)MemHandleLock(h))->flags;
        MemHandleUnlock(h);
    }
    SaveRec(gEditRec, gEditKind, flags,
            gEditKind == kindMeeting ? gEditTime : 0, 0, text);
}

static void EditFormClose(void)
{
    FrmReturnToForm(0);
    FrmUpdateForm(DayForm, frmRedrawUpdateCode);
}

static Boolean EditFormHandleEvent(EventType *e)
{
    FormType *frm = FrmGetActiveForm();

    switch (e->eType) {
    case frmOpenEvent:
        EditFormInit(frm);
        FrmDrawForm(frm);
        FrmSetFocus(frm, FrmGetObjectIndex(frm, EditField));
        return true;

    case ctlSelectEvent:
        switch (e->data.ctlSelect.controlID) {
        case EditTime:
            if (PickTime(&gEditTime, "Meeting Time")) {
                FormatTime(gEditTime, gEditTimeLabel);
                CtlSetLabel((ControlType *)GetObj(frm, EditTime),
                            gEditTimeLabel);
            }
            return true;
        case EditOK:
            EditFormSave(frm);
            EditFormClose();
            return true;
        case EditCancel:
            EditFormClose();
            return true;
        case EditDelete:
            if (FrmAlert(DeleteAlert) == 0) {
                DmRemoveRecord(gDB, gEditRec);
                EditFormClose();
            }
            return true;
        }
        break;

    default:
        break;
    }
    return false;
}

/* --------------------------------------------------------- month view */

static void LoadMonth(void)
{
    DateType d = ToDate(gDay);
    UInt16 i, n = DmNumRecords(gDB);

    d.day = 1;
    gMonthFirst = (UInt16)DateToDays(d);
    gMonthDays = DaysInMonth(d.month, d.year + firstYear);

    StrCopy(gMonthLabel, kMonth[d.month - 1]);
    StrCat(gMonthLabel, " ");
    StrIToA(gMonthLabel + StrLen(gMonthLabel), d.year + firstYear);

    for (i = 0; i < gMonthDays; i++) {
        UInt16 idx = FindDay(gMonthFirst + i);
        gMonthBegin[i] = gMonthEnd[i] = kNoTime;
        if (idx < n) {
            MemHandle h = DmQueryRecord(gDB, idx);
            RecHdr *r = (RecHdr *)MemHandleLock(h);
            if (r->date == gMonthFirst + i && r->kind == kindDay) {
                gMonthBegin[i] = r->t1;
                gMonthEnd[i] = r->t2;
            }
            MemHandleUnlock(h);
        }
    }
}

static Int16 Duration(UInt16 i)
{
    if (gMonthBegin[i] < 0 || gMonthEnd[i] < 0 || gMonthEnd[i] < gMonthBegin[i])
        return kNoTime;
    return gMonthEnd[i] - gMonthBegin[i];
}

static void DrawMonth(FormType *frm)
{
    RectangleType r, totalR;
    UInt16 i, rows, today = Today();
    Int16 maxTop;
    Int32 total = 0;
    Char buf[12];
    FontID old = FntGetFont();

    CtlSetLabel((ControlType *)GetObj(frm, MonthLabel), gMonthLabel);

    GetBounds(frm, MonthList, &r);
    rows = r.extent.y / lineH;
    maxTop = gMonthDays > rows ? gMonthDays - rows : 0;
    if (gMonthTop > maxTop) gMonthTop = maxTop;

    WinEraseRectangle(&r, 0);
    for (i = 0; i < rows && gMonthTop + i < gMonthDays; i++) {
        UInt16 day = gMonthTop + i;
        Coord y = r.topLeft.y + i * lineH;

        FntSetFont(gMonthFirst + day == today ? boldFont : stdFont);
        FormatDay(gMonthFirst + day, buf);
        WinDrawChars(buf, StrLen(buf), r.topLeft.x + 1, y);

        if (gMonthBegin[day] >= 0 || gMonthEnd[day] >= 0) {
            FormatTime(gMonthBegin[day], buf);
            DrawRightAligned(buf, 76, y);
            WinDrawChars("-", 1, 80, y);
            FormatTime(gMonthEnd[day], buf);
            DrawRightAligned(buf, 110, y);
            FormatTime(Duration(day), buf);
            DrawRightAligned(buf, r.topLeft.x + r.extent.x - 2, y);
        }
    }

    SclSetScrollBar((ScrollBarType *)GetObj(frm, MonthListBar),
                    gMonthTop, 0, maxTop, rows - 1);

    for (i = 0; i < gMonthDays; i++)
        if (Duration(i) > 0) total += Duration(i);
    StrCopy(buf, "Total ");
    FormatTime((Int16)total, buf + StrLen(buf));
    RctSetRectangle(&totalR, 96, 147, 84, 12);
    WinEraseRectangle(&totalR, 0);
    FntSetFont(boldFont);
    DrawRightAligned(buf, 159, 148);
    FntSetFont(old);
}

static void ShowMonth(FormType *frm, Boolean centerOnDay)
{
    LoadMonth();
    if (centerOnDay) {
        UInt16 day = gDay - gMonthFirst;
        gMonthTop = day > 4 ? day - 4 : 0;
    }
    DrawMonth(frm);
}

/* Move gDay by whole months, clamping the day of month. */
static void ShiftMonth(Int16 delta)
{
    DateType d = ToDate(gDay);
    Int16 month = d.month + delta, year = d.year + firstYear;
    Int16 dim;

    if (month < 1)  { month = 12; year--; }
    if (month > 12) { month = 1;  year++; }
    if (year < firstYear || year > lastYear) return;

    dim = DaysInMonth(month, year);
    d.year = year - firstYear;
    d.month = month;
    if (d.day > dim) d.day = dim;
    gDay = (UInt16)DateToDays(d);
}

static Boolean MonthFormHandleEvent(EventType *e)
{
    FormType *frm = FrmGetActiveForm();
    RectangleType r;

    switch (e->eType) {
    case frmOpenEvent:
        FrmDrawForm(frm);
        ShowMonth(frm, true);
        return true;

    case frmUpdateEvent:
        FrmDrawForm(frm);
        ShowMonth(frm, false);
        return true;

    case ctlRepeatEvent:
        if (e->data.ctlRepeat.controlID == MonthPrev) {
            ShiftMonth(-1);
            gMonthTop = 0;
            ShowMonth(frm, false);
        } else if (e->data.ctlRepeat.controlID == MonthNext) {
            ShiftMonth(1);
            gMonthTop = 0;
            ShowMonth(frm, false);
        }
        return false;

    case ctlSelectEvent:
        switch (e->data.ctlSelect.controlID) {
        case MonthLabel:
        case MonthToday:
            gDay = Today();
            ShowMonth(frm, true);
            return true;
        case MonthDay:
            // showFullDescription = true;
            FrmGotoForm(DayForm);
            return true;
        }
        break;

    case sclRepeatEvent:
        gMonthTop = e->data.sclRepeat.newValue;
        DrawMonth(frm);
        return false;

    case penDownEvent:
        GetBounds(frm, MonthList, &r);
        if (RctPtInRectangle(e->screenX, e->screenY, &r)) {
            UInt16 day = gMonthTop + (e->screenY - r.topLeft.y) / lineH;
            if (day < gMonthDays) {
                gDay = gMonthFirst + day;
                // showFullDescription = true;
                FrmGotoForm(DayForm);
            }
            return true;
        }
        break;

    case keyDownEvent:
        if (EvtKeydownIsVirtual(e)) {
            UInt16 rows;
            GetBounds(frm, MonthList, &r);
            rows = r.extent.y / lineH;
            if (e->data.keyDown.chr == vchrPageUp) {
                gMonthTop = gMonthTop > rows - 1 ? gMonthTop - (rows - 1) : 0;
                DrawMonth(frm);
                return true;
            }
            if (e->data.keyDown.chr == vchrPageDown) {
                gMonthTop += rows - 1;   /* clamped in DrawMonth */
                DrawMonth(frm);
                return true;
            }
        }
        break;

    case menuEvent:
        switch (e->data.menu.itemID) {
        case MonthMenuToday:
            gDay = Today();
            ShowMonth(frm, true);
            break;
        case MonthMenuAbout:
            FrmAlert(AboutAlert);
            break;
        }
        return true;

    default:
        break;
    }
    return false;
}

/* ---------------------------------------------------------- app plumbing */

static Boolean AppHandleEvent(EventType *e)
{
    FormType *frm;

    if (e->eType != frmLoadEvent) return false;

    frm = FrmInitForm(e->data.frmLoad.formID);
    FrmSetActiveForm(frm);
    switch (e->data.frmLoad.formID) {
    case DayForm:   FrmSetEventHandler(frm, DayFormHandleEvent);   break;
    case MonthForm: FrmSetEventHandler(frm, MonthFormHandleEvent); break;
    case EditForm:  FrmSetEventHandler(frm, EditFormHandleEvent);  break;
    }
    return true;
}

static void EventLoop(void)
{
    EventType e;
    Err err;

    do {
        EvtGetEvent(&e, evtWaitForever);
        if (SysHandleEvent(&e)) continue;
        if (MenuHandleEvent(NULL, &e, &err)) continue;
        if (AppHandleEvent(&e)) continue;
        FrmDispatchEvent(&e);
    } while (e.eType != appStopEvent);
}

static Err RomVersionCompatible(UInt16 launchFlags)
{
    UInt32 rom = 0;
    FtrGet(sysFtrCreator, sysFtrNumROMVersion, &rom);
    if (rom >= sysMakeROMVersion(3, 1, 0, sysROMStageRelease, 0))
        return errNone;

    if ((launchFlags & (sysAppLaunchFlagNewGlobals | sysAppLaunchFlagUIApp))
        == (sysAppLaunchFlagNewGlobals | sysAppLaunchFlagUIApp)) {
        FrmAlert(RomIncompatibleAlert);
        /* Palm OS 1.0 would loop forever without this. */
        if (rom < sysMakeROMVersion(2, 0, 0, sysROMStageRelease, 0))
            AppLaunchWithCommand(sysFileCDefaultApp,
                                 sysAppLaunchCmdNormalLaunch, NULL);
    }
    return sysErrRomIncompatible;
}

UInt32 PilotMain(UInt16 cmd, MemPtr cmdPBP, UInt16 launchFlags)
{
    Err err;

    if (cmd != sysAppLaunchCmdNormalLaunch) return errNone;

    err = RomVersionCompatible(launchFlags);
    if (err) return err;

    err = OpenDB();
    if (err) return err;

    gDay = Today();
    FrmGotoForm(DayForm);
    EventLoop();
    FrmCloseAllForms();
    DmCloseDatabase(gDB);
    return errNone;
}
