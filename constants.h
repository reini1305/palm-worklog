#ifndef CONSTANTS_H
#define CONSTANTS_H

#include <PalmOS.h>

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
    UInt8  cal;         /* rec indexes the Datebook, not our database */
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

#define maxCal          16

typedef struct {
    UInt16 rec;         /* Datebook record index */
    Int16  start;       /* minutes after midnight, kNoTime if untimed */
} CalEvent;

#define appPrefID       0
#define appPrefVersion  1

typedef struct {
    Boolean showFullDescription;
} AppPrefs;

#endif //CONSTANTS_H