#include "month.h"
#include "helpers.h"
#include "globals.h"
#include "resources.h"
#include "database.h"
#include "beam.h"

enum { colDay, colBegin, colEnd, colDuration, numMonthCols };
static const Char *kMonthCols[numMonthCols] = { "Day", "Begin", "End", "Time" };
static const Coord kMonthColW[numMonthCols] = { 56, 32, 32, 32 };


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
/* Table callback; the row ID is the day of month (0-based). Each cell
 * draws its own right and bottom grid line. */
static void DrawMonthCell(void *table, Int16 row, Int16 col, RectangleType *b)
{
    UInt16 day = TblGetRowID((TableType *)table, row);
    Coord right = b->topLeft.x + b->extent.x - 2;
    Coord bottom = b->topLeft.y + b->extent.y - 1;
    Char buf[dowDateStringLength];
    FontID old = FntSetFont(gMonthFirst + day == Today() ? boldFont : stdFont);

    if (col == colDay) {
        FormatDay(gMonthFirst + day, buf);
        WinDrawChars(buf, StrLen(buf), b->topLeft.x + 1, b->topLeft.y);
    } else if (gMonthBegin[day] >= 0 || gMonthEnd[day] >= 0) {
        FormatTime(col == colBegin ? gMonthBegin[day] :
                   col == colEnd   ? gMonthEnd[day] : Duration(day), buf);
        DrawRightAligned(buf, right, b->topLeft.y);
    }
    FntSetFont(old);

    if (col < numMonthCols - 1)
        WinDrawGrayLine(right + 1, b->topLeft.y, right + 1, bottom);
    WinDrawGrayLine(b->topLeft.x, bottom, right + 1, bottom);
}

static void InitMonthTable(FormType *frm)
{
    TableType *t = (TableType *)GetObj(frm, MonthTable);
    Int16 row, col, rows = TblGetNumberOfRows(t);

    for (row = 0; row < rows; row++) {
        TblSetRowHeight(t, row, lineH);
        TblSetRowUsable(t, row, false);     /* until DrawMonth fills it */
        for (col = 0; col < numMonthCols; col++)
            TblSetItemStyle(t, row, col, customTableItem);
    }
    for (col = 0; col < numMonthCols; col++) {
        TblSetColumnUsable(t, col, true);
        TblSetColumnWidth(t, col, kMonthColW[col]);
        TblSetColumnSpacing(t, col, 0);
        TblSetCustomDrawProcedure(t, col, DrawMonthCell);
    }
}

/* Column titles above the table, aligned like the cells below them. */
static void DrawMonthHeader(FormType *frm)
{
    TableType *t = (TableType *)GetObj(frm, MonthTable);
    RectangleType r;
    Coord x, y;
    Int16 col;
    FontID old = FntSetFont(boldFont);

    GetBounds(frm, MonthTable, &r);
    x = r.topLeft.x;
    y = r.topLeft.y - lineH - 2;
    for (col = 0; col < numMonthCols; col++) {
        Coord w = TblGetColumnWidth(t, col);
        if (col == colDay)
            WinDrawChars(kMonthCols[col], StrLen(kMonthCols[col]), x + 1, y);
        else
            DrawRightAligned(kMonthCols[col], x + w - 2, y);
        if (col < numMonthCols - 1)
            WinDrawGrayLine(x + w - 1, y, x + w - 1, r.topLeft.y - 3);
        x += w + TblGetColumnSpacing(t, col);
    }
    WinDrawLine(r.topLeft.x, r.topLeft.y - 2,
                r.topLeft.x + r.extent.x - 1, r.topLeft.y - 2);
    FntSetFont(old);
}

static void DrawMonth(FormType *frm)
{
    TableType *t = (TableType *)GetObj(frm, MonthTable);
    RectangleType r, totalR;
    UInt16 i, rows = TblGetNumberOfRows(t);
    Int16 maxTop;
    Int32 total = 0;
    Char buf[dowDateStringLength];
    FontID old = FntGetFont();

    CtlSetLabel((ControlType *)GetObj(frm, MonthLabel), gMonthLabel);

    maxTop = gMonthDays > rows ? gMonthDays - rows : 0;
    if (gMonthTop > maxTop) gMonthTop = maxTop;

    for (i = 0; i < rows; i++) {
        TblSetRowUsable(t, i, gMonthTop + i < gMonthDays);
        TblSetRowID(t, i, gMonthTop + i);
    }
    GetBounds(frm, MonthTable, &r);
    WinEraseRectangle(&r, 0);
    TblMarkTableInvalid(t);
    TblRedrawTable(t);

    SclSetScrollBar((ScrollBarType *)GetObj(frm, MonthTableBar),
                    gMonthTop, 0, maxTop, rows - 1);

    for (i = 0; i < gMonthDays; i++)
        if (Duration(i) > 0) total += Duration(i);
    StrCopy(buf, " ");
    FormatTime((Int16)total, buf + StrLen(buf));
    RctSetRectangle(&totalR, 96, 147, 84, 12);
    WinEraseRectangle(&totalR, 0);
    FntSetFont(boldFont);
    DrawRightAligned(buf, r.topLeft.x + r.extent.x - 2, 148);
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

Boolean MonthFormHandleEvent(EventType *e)
{
    FormType *frm = FrmGetActiveForm();

    switch (e->eType) {
    case frmOpenEvent:
        InitMonthTable(frm);
        FrmDrawForm(frm);
        DrawMonthHeader(frm);
        ShowMonth(frm, true);
        return true;

    case frmUpdateEvent:
        FrmDrawForm(frm);
        DrawMonthHeader(frm);
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
            FrmGotoForm(DayForm);
            return true;
        }
        break;

    case sclRepeatEvent:
        gMonthTop = e->data.sclRepeat.newValue;
        DrawMonth(frm);
        return false;

    case tblSelectEvent:
        gDay = gMonthFirst + TblGetRowID(e->data.tblSelect.pTable,
                                         e->data.tblSelect.row);
        FrmGotoForm(DayForm);
        return true;

    case keyDownEvent:
        if (EvtKeydownIsVirtual(e)) {
            UInt16 rows = TblGetNumberOfRows(
                (TableType *)GetObj(frm, MonthTable));
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
        case MonthMenuBeam:
            BeamAll();
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