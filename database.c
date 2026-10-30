#include "database.h"
#include "constants.h"
#include "globals.h"
#include "resources.h"
#include "helpers.h"

void LookFor(FindParamsPtr params)
{
    UInt16 rec, n, cardNo;
    MemHandle h;
    RecHdr *r;
    Boolean found;
    UInt32 pos;
    UInt16 length;
    RectangleType rect;
    Char *text;
    Char buf[dowDateStringLength];
    LocalID dbID;
    UInt8 pad = 35;
    DmOpenRef db = DmOpenDatabaseByTypeCreator(dbType, appCreator, dmModeReadOnly);
    if(!db) return;
    DmOpenDatabaseInfo(db, &dbID, NULL, NULL, &cardNo, NULL);
    n = DmNumRecords(db);

    if (FindDrawHeader(params, "WorkLog"))
        return;

    for (rec = params->recordNum; rec < n; rec++)
	{
		if (											// here, we give a chance to the user to cancel the Find
			((rec%10) == 0) &&							// every 10 records
			EvtSysEventAvail(true)						// if there's a system event to process
		   )
			{
				params->more = true;				    // we confirm there are some more records to check
				break;
			}
        h = DmQueryRecord(db, rec);
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
            if(FindSaveMatch(params, rec, (UInt16)pos, 0, r->date, cardNo, dbID))
            {
                MemHandleUnlock(h);
                goto done;
            }

            FindGetLineBounds(params, &rect);
            FormatDay(r->date, buf);
            WinDrawTruncChars(buf+4, StrLen(buf) - 4, rect.topLeft.x+1, rect.topLeft.y, 25);
            if (r->kind == kindTodo) {
                Char box = (r->flags & flagDone) ? symbolCheckboxOn
                                                 : symbolCheckboxOff;
                pad = 45;
                FntSetFont(symbol11Font);
                WinDrawChars(&box, 1, rect.topLeft.x+pad-12, rect.topLeft.y);
                FntSetFont(stdFont);
            }
            WinDrawTruncChars(
                text,
                StrLen(text),
                rect.topLeft.x+pad,
                rect.topLeft.y,
                rect.extent.x-pad-2);
            params->lineNumber++;
        }
        MemHandleUnlock(h);
    }
done:
    DmCloseDatabase(db);
}

UInt16 GoTo(GoToParamsPtr params)
{
    return (UInt16)params->matchCustom;
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

/* Open the database, creating it if needed; NULL on failure. */
DmOpenRef OpenWorkLogDB(void)
{
    DmOpenRef db = DmOpenDatabaseByTypeCreator(dbType, appCreator,
                                               dmModeReadWrite);
    if (!db) {
        LocalID id;
        UInt16 attr;
        if (DmCreateDatabase(0, dbName, appCreator, dbType, false))
            return NULL;
        id = DmFindDatabase(0, dbName);
        DmDatabaseInfo(0, id, NULL, &attr, NULL, NULL, NULL, NULL, NULL,
                       NULL, NULL, NULL, NULL);
        attr |= dmHdrAttrBackup;
        DmSetDatabaseInfo(0, id, NULL, &attr, NULL, NULL, NULL, NULL, NULL,
                          NULL, NULL, NULL, NULL);
        db = DmOpenDatabase(0, id, dmModeReadWrite);
    }
    return db;
}

Err OpenDB(void)
{
    gDB = OpenWorkLogDB();
    return gDB ? errNone : DmGetLastErr();
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
