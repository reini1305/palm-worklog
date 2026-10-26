#include "editor.h"
#include "helpers.h"
#include "resources.h"
#include "globals.h"
#include "database.h"

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

Boolean EditFormHandleEvent(EventType *e)
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