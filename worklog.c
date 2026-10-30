/*
 * WorkLog - a daily work journal for Palm OS 3.1+
 *
 * Day view:   begin/end of the work day, meetings (left), to-dos (right).
 * Month view: begin/end/duration per day plus a monthly total.
 *
 * Storage: one database, one record per item, kept sorted by
 * (date, kind, time). Each record is a RecHdr followed by NUL-terminated text.
 */

#include "resources.h"
#include "constants.h"
#include "globals.h"
#include "helpers.h"
#include "database.h"
#include "day.h"
#include "editor.h"
#include "month.h"
#include "calendar.h"
#include "beam.h"

static void LoadPrefs(void)
{
    AppPrefs p;
    UInt16 size = sizeof(p);
    Int16 ver = PrefGetAppPreferences(appCreator, appPrefID, &p, &size, true);
    if (ver == appPrefVersion && size == sizeof(p))
        gShowFullDescription = p.showFullDescription;
}

static void SavePrefs(void)
{
    AppPrefs p;
    p.showFullDescription = gShowFullDescription;
    PrefSetAppPreferences(appCreator, appPrefID, appPrefVersion,
                          &p, sizeof(p), true);
}

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
    err = RomVersionCompatible(launchFlags);
    if (err) return err;

    switch (cmd)
	{
		case sysAppLaunchCmdNormalLaunch:		// Normal launch: we have a UI, handlers and all the stuff
			err = OpenDB();
            if (err) return err;
            OpenCalendar();
            gDay = Today();
            LoadPrefs();
            RegisterBeam();
            FrmGotoForm(DayForm);
            EventLoop();
            FrmCloseAllForms();
            SavePrefs();
            DmCloseDatabase(gDB);
            CloseCalendar();
			break;
			
		case sysAppLaunchCmdFind:				// STEP 2: here we are... system requests us to search for data that user is looking for
            LookFor((FindParamsPtr)cmdPBP);
			break;
			
		case sysAppLaunchCmdGoTo:				// STEP 3: there we go... system requests us to SHOW the data we just told him we had found!
			if (launchFlags & sysAppLaunchFlagNewGlobals)		// We're being launched... so we have more stuff to do than just display something
			{
				err = OpenDB();
                if (err) return err;
                OpenCalendar();
                gDay = GoTo((GoToParamsPtr)cmdPBP);
                LoadPrefs();
                FrmGotoForm(DayForm);
                EventLoop();
                FrmCloseAllForms();
                SavePrefs();
                DmCloseDatabase(gDB);
                CloseCalendar();
			}
			else												// We're ALREADY running... so just display the data we found previously
			{
				gDay = GoTo((GoToParamsPtr)cmdPBP);
				FrmGotoForm(DayForm);
			}
			break;
			
		case sysAppLaunchCmdSyncNotify:			// installed or HotSynced: claim .wkl beams
			RegisterBeam();
			break;

		case sysAppLaunchCmdExgReceiveData:		// another Palm beamed its entries
			err = ReceiveBeam((ExgSocketType *)cmdPBP,
			                  (launchFlags & sysAppLaunchFlagSubCall) != 0);
			break;

		default:
			break;
	}
    
    return err;
}
