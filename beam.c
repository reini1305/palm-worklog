/*
 * Beaming all entries to another Palm running WorkLog.
 *
 * A beam is a BeamHdr followed, for every record, by a UInt16 size and
 * the record exactly as stored (RecHdr + NUL-terminated text).
 *
 * The receiver merges instead of copying blindly:
 *   day      one per date; a missing begin or end is filled in
 *   meeting  skipped if one with the same time and text exists
 *   to-do    skipped if one with the same text exists; done wins
 *
 * ReceiveBeam may run without application globals (when WorkLog isn't
 * the running app), so it only touches globals if `running` is set.
 */

#include "beam.h"
#include "constants.h"
#include "globals.h"
#include "resources.h"
#include "database.h"
#include "day.h"

#define beamExt         "wkl"
#define beamVersion     1

typedef struct {
    UInt32 creator;     /* appCreator */
    UInt16 version;
    UInt16 count;       /* number of records that follow */
} BeamHdr;

enum { mergeAdded, mergeKnown, mergeFailed };

void RegisterBeam(void)
{
    ExgRegisterData(appCreator, exgRegExtensionID, beamExt);
}

static Err Send(ExgSocketType *s, const void *p, UInt32 len)
{
    Err err = errNone;
    while (len && !err) {
        UInt32 n = ExgSend(s, p, len, &err);
        p = (const UInt8 *)p + n;
        len -= n;
    }
    return err;
}

void BeamAll(void)
{
    ExgSocketType s;
    BeamHdr hdr;
    UInt16 i, n = DmNumRecords(gDB);
    UInt32 total = sizeof(hdr);
    Err err;

    hdr.creator = appCreator;
    hdr.version = beamVersion;
    hdr.count = 0;
    for (i = 0; i < n; i++) {
        MemHandle h = DmQueryRecord(gDB, i);
        if (!h) continue;
        total += sizeof(UInt16) + MemHandleSize(h);
        hdr.count++;
    }

    MemSet(&s, sizeof(s), 0);
    s.target = appCreator;
    s.description = "WorkLog entries";
    s.name = "WorkLog." beamExt;
    s.length = total;
    /* The Exchange Manager reports its own errors to the user. */
    if (ExgPut(&s)) return;

    err = Send(&s, &hdr, sizeof(hdr));
    for (i = 0; i < n && !err; i++) {
        MemHandle h = DmQueryRecord(gDB, i);
        UInt16 size;
        if (!h) continue;
        size = MemHandleSize(h);
        err = Send(&s, &size, sizeof(size));
        if (!err) {
            err = Send(&s, MemHandleLock(h), size);
            MemHandleUnlock(h);
        }
    }
    ExgDisconnect(&s, err);
}

/* Read exactly len bytes; a short read is bad data. */
static Boolean Read(ExgSocketType *s, void *p, UInt32 len, Err *err)
{
    while (len && !*err) {
        UInt32 n = ExgReceive(s, p, len, err);
        if (!n) break;
        p = (UInt8 *)p + n;
        len -= n;
    }
    if (len && !*err) *err = exgErrBadData;
    return !*err;
}

static void WriteHdr(DmOpenRef db, UInt16 i, RecHdr *hdr)
{
    MemHandle h = DmGetRecord(db, i);
    DmWrite(MemHandleLock(h), 0, hdr, sizeof(*hdr));
    MemHandleUnlock(h);
    DmReleaseRecord(db, i, true);
}

/* Merge a received record into db. Sets *resort if a sort key changed. */
static UInt8 Merge(DmOpenRef db, RecHdr *in, UInt16 size, Boolean *resort)
{
    Char *text = (Char *)(in + 1);
    UInt16 i, n = DmNumRecords(db);
    RecHdr key, cur;
    MemHandle h;

    key.date = in->date;
    key.kind = kindDay;
    key.flags = 0;
    key.t1 = -32768;
    key.t2 = 0;
    for (i = DmFindSortPosition(db, &key, NULL, CompareRecs, 0); i < n; i++) {
        RecHdr *r;
        Boolean same = false;
        h = DmQueryRecord(db, i);
        if (!h) continue;
        r = (RecHdr *)MemHandleLock(h);
        if (r->date != in->date) {
            MemHandleUnlock(h);
            break;
        }
        if (r->kind == in->kind) {
            switch (in->kind) {
            case kindDay:
                same = true;
                break;
            case kindMeeting:
                same = r->t1 == in->t1
                    && StrCompare((Char *)(r + 1), text) == 0;
                break;
            case kindTodo:
                same = StrCompare((Char *)(r + 1), text) == 0;
                break;
            }
        }
        cur = *r;
        MemHandleUnlock(h);
        if (!same) continue;

        if (in->kind == kindDay
            && ((cur.t1 == kNoTime && in->t1 != kNoTime)
                || (cur.t2 == kNoTime && in->t2 != kNoTime))) {
            if (cur.t1 == kNoTime) cur.t1 = in->t1;
            if (cur.t2 == kNoTime) cur.t2 = in->t2;
            WriteHdr(db, i, &cur);
        } else if (in->kind == kindTodo && (in->flags & flagDone)
                   && !(cur.flags & flagDone)) {
            cur.flags |= flagDone;
            cur.t1 = cur.flags;     /* to-dos sort by done, see ToggleDone */
            WriteHdr(db, i, &cur);
            *resort = true;
        }
        return mergeKnown;
    }

    i = DmFindSortPosition(db, in, NULL, CompareRecs, 0);
    h = DmNewRecord(db, &i, size);
    if (!h) return mergeFailed;
    DmWrite(MemHandleLock(h), 0, in, size);
    MemHandleUnlock(h);
    DmReleaseRecord(db, i, true);
    return mergeAdded;
}

Err ReceiveBeam(ExgSocketType *s, Boolean running)
{
    DmOpenRef db = running ? gDB : OpenWorkLogDB();
    BeamHdr hdr;
    UInt16 i, added = 0, known = 0;
    UInt32 editID = 0;
    Boolean resort = false;
    Err err;

    if (!db) return DmGetLastErr();
    /* Inserting shifts record indices; remember the one being edited. */
    if (running && gEditRec >= 0)
        DmRecordInfo(db, gEditRec, NULL, &editID, NULL);

    err = ExgAccept(s);
    if (!err) {
        if (Read(s, &hdr, sizeof(hdr), &err)
            && (hdr.creator != appCreator || hdr.version != beamVersion))
            err = exgErrBadData;
        for (i = 0; !err && i < hdr.count; i++) {
            UInt16 size;
            RecHdr *r;
            if (!Read(s, &size, sizeof(size), &err)) break;
            if (size <= sizeof(RecHdr)) {
                err = exgErrBadData;
                break;
            }
            r = (RecHdr *)MemPtrNew(size);
            if (!r) {
                err = exgMemError;
                break;
            }
            if (Read(s, r, size, &err)) {
                if (((Char *)r)[size - 1] != '\0' || r->kind > kindTodo)
                    err = exgErrBadData;
                else switch (Merge(db, r, size, &resort)) {
                case mergeAdded: added++; break;
                case mergeKnown: known++; break;
                default:         err = exgErrDeviceFull; break;
                }
            }
            MemPtrFree(r);
        }
        ExgDisconnect(s, err);
    }
    if (resort) DmInsertionSort(db, CompareRecs, 0);

    if (running) {
        UInt16 form = FrmGetActiveFormID();
        if (gEditRec >= 0) {
            UInt16 idx;
            if (!DmFindRecordByID(db, editID, &idx)) gEditRec = idx;
        }
        LoadDay();
        if (form == DayForm || form == MonthForm)
            FrmUpdateForm(form, frmRedrawUpdateCode);
    } else {
        DmCloseDatabase(db);
    }

    if (!err) {
        Char a[maxStrIToALen], k[maxStrIToALen];
        StrIToA(a, added);
        StrIToA(k, known);
        FrmCustomAlert(BeamResultAlert, a, k, NULL);
    }
    return err;
}
