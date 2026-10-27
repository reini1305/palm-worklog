#include "database.h"
#include "constants.h"
#include "globals.h"
#include "resources.h"
#include "helpers.h"

void LookFor(FindParamsPtr params)
{
    UInt16 rec = params->recordNum;
    UInt16 n;
    MemHandle h;
    RecHdr *r;
    Boolean found;
    UInt32 pos;
    UInt16 length;
    RectangleType rect;
    Char *text;
    Char buf[dowDateStringLength];
    Err err = OpenDB();
    if (err) return;
    n = DmNumRecords(gDB);

    if (FindDrawHeader(params, "WorkLog"))
        return;

    for (; rec < n; rec++)
	{
		if (											// here, we give a chance to the user to cancel the Find
			((rec%10) == 0) &&							// every 10 records
			EvtSysEventAvail(true)						// if there's a system event to process
		   )
			{
				params->more = true;				    // we confirm there are some more records to check
				break;
			}
        h = DmQueryRecord(gDB, rec);
        if (!h) continue;
        r = (RecHdr *)MemHandleLock(h);
        if (r->kind == kindDay) {
            MemHandleUnlock(h);
            continue;
        }
        text = (Char *)(r+1);
        found = TxtFindString(text, params->strToFind, &pos, &length);
        if (found)
        {
            if(FindSaveMatch(params, rec, pos, 0, 0, r->date, 0))
                return;

            FindGetLineBounds(params, &rect);
            FormatDay(r->date, buf);
            WinDrawTruncChars(buf, StrLen(buf), rect.topLeft.x+1, rect.topLeft.y, 30);
            WinDrawTruncChars(text, StrLen(text), rect.topLeft.x+40, rect.topLeft.y, rect.extent.x-42);
        }
        MemHandleUnlock(h);
    }
    DmCloseDatabase(gDB);
}

UInt16 GoTo(GoToParamsPtr params)
{
    return params->dbCardNo;
}

Int16 CompareRecs(void *a, void *b, Int16 other,
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
UInt16 FindDay(UInt16 day)
{
    RecHdr key;
    key.date = day;
    key.kind = kindDay;
    key.flags = 0;
    key.t1 = -32768;
    key.t2 = 0;
    return DmFindSortPosition(gDB, &key, NULL, CompareRecs, 0);
}

Err OpenDB(void)
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
void SaveRec(Int16 rec, UInt8 kind, UInt8 flags, Int16 t1, Int16 t2,
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

void ToggleDone(UInt16 rec)
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
