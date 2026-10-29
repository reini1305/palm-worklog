#include "day.h"
#include "globals.h"
#include "database.h"
#include "resources.h"
#include "helpers.h"
#include "calendar.h"

static void AddLines(Column *c, UInt16 rec, Char *text, UInt8 cal)
{
    Char *p = text;
    Boolean first = true;
    UInt8 lines = 0;
    const UInt8 max_lines = gShowFullDescription ? 128 : 2;

    do {
        UInt16 len = FntWordWrap(p, c->textW);
        if (c->numLines == maxLines) break;
        if (len == 0 && *p) len = 1;
        c->lines[c->numLines].rec = rec;
        c->lines[c->numLines].off = p - text;
        c->lines[c->numLines].len = len;
        c->lines[c->numLines].first = first;
        c->lines[c->numLines].cal = cal;
        c->numLines++;
        lines++;
        p += len;
        first = false;
    } while (*p && lines < max_lines);
}

/* Add the calendar events from index k on that start no later than
 * the given time; returns the index of the first one not added. */
static UInt16 AddCalendar(Column *c, UInt16 k, Int16 before)
{
    for (; k < gNumCal && gCal[k].start <= before; k++) {
        MemHandle h = DmQueryRecord(gCalDB, gCal[k].rec);
        AddLines(c, gCal[k].rec, CalText(MemHandleLock(h)), true);
        MemHandleUnlock(h);
    }
    return k;
}

void LoadDay(void)
{
    UInt16 i, k = 0, n = DmNumRecords(gDB);
    FontID old = FntSetFont(stdFont);

    gHdrRec = -1;
    gBegin = gEnd = kNoTime;
    gCols[0].numLines = gCols[1].numLines = 0;
    LoadCalendar(gDay);

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
            if (r->kind == kindMeeting)
                k = AddCalendar(&gCols[0], k, r->t1);
            AddLines(&gCols[r->kind - 1], i, (Char *)(r + 1), false);
        }
        MemHandleUnlock(h);
    }
    AddCalendar(&gCols[0], k, 32767);
    FntSetFont(old);
}

void InitColumns(FormType *frm)
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

void DrawColumn(FormType *frm, Column *c)
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
        MemHandle h = DmQueryRecord(ln->cal ? gCalDB : gDB, ln->rec);
        void *p = MemHandleLock(h);
        RecHdr *rec = (RecHdr *)p;
        Char *s = (ln->cal ? CalText(p) : (Char *)(rec + 1)) + ln->off;
        UInt16 len = ln->len;
        Coord y = r.topLeft.y + i * lineH;

        while (len && (s[len - 1] == '\n' || s[len - 1] == ' ')) len--;

        if (ln->cal) {
            /* Datebook event: plain time and a gray bar, read-only */
            WinDrawGrayLine(r.topLeft.x + c->indent - 3, y,
                            r.topLeft.x + c->indent - 3, y + lineH - 1);
            if (ln->first && CalStart(p) != kNoTime) {
                Char t[8];
                FormatTime(CalStart(p), t);
                FntSetFont(stdFont);
                WinDrawChars(t, StrLen(t), r.topLeft.x + 1, y);
            }
        } else if (ln->first) {
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

void DrawDay(FormType *frm)
{
    FormatDay(gDay, gDateLabel);
    CtlSetLabel((ControlType *)GetObj(frm, DayDate), gDateLabel);
    FormatTime(gBegin, gBeginLabel);
    CtlSetLabel((ControlType *)GetObj(frm, DayBegin), gBeginLabel);
    FormatTime(gEnd, gEndLabel);
    CtlSetLabel((ControlType *)GetObj(frm, DayEnd), gEndLabel);
    CtlSetValue((ControlType *)GetObj(frm, DayShowFull), gShowFullDescription);

    DrawColumn(frm, &gCols[0]);
    DrawColumn(frm, &gCols[1]);
    WinDrawLine(83, 33, 83, 142);
}

void GoToDay(FormType *frm, UInt16 day)
{
    gDay = day;
    gCols[0].top = gCols[1].top = 0;
    LoadDay();
    DrawDay(frm);
}

void SetDayTimes(FormType *frm, Int16 begin, Int16 end)
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
    } else if (c->lines[line].cal) {
        /* Datebook events are read-only */
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

Boolean DayFormHandleEvent(EventType *e)
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
            gShowFullDescription = !gShowFullDescription;
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
