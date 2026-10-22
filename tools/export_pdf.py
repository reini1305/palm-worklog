#!/usr/bin/env python3
"""Export a HotSynced WorkLogDB-WkJn.pdb to a PDF with a year summary,
monthly views and weekly views.

    export_pdf.py WorkLogDB-WkJn.pdb --year 2026 -o worklog-2026.pdf
"""

import argparse
import struct
import sys
from collections import defaultdict
from dataclasses import dataclass, field
from datetime import date, timedelta

from reportlab.lib import colors
from reportlab.lib.pagesizes import A4, LETTER
from reportlab.lib.styles import getSampleStyleSheet
from reportlab.lib.units import mm
from reportlab.platypus import (KeepTogether, PageBreak, Paragraph,
                                SimpleDocTemplate, Spacer, Table, TableStyle)
from xml.sax.saxutils import escape

EPOCH = date(1904, 1, 1)
KIND_DAY, KIND_MEETING, KIND_TODO = 0, 1, 2
REC_HDR = struct.Struct(">HBBhh")
REC_DELETED = 0x80
PAGE_SIZES = {"a4": A4, "letter": LETTER}
WEEK_STARTS = {"monday": 0, "sunday": 6}


@dataclass
class Day:
    begin: int = -1
    end: int = -1
    meetings: list = field(default_factory=list)   # (start, text)
    todos: list = field(default_factory=list)      # (done, text)

    def duration(self):
        if self.begin < 0 or self.end < 0 or self.end < self.begin:
            return None
        return self.end - self.begin


# ------------------------------------------------------------- database

def read_pdb(path):
    """Return {date: Day} from a Palm database file."""
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < 78:
        sys.exit(f"{path}: too short for a Palm database")
    num = struct.unpack_from(">H", data, 76)[0]
    entries = [struct.unpack_from(">IB", data, 78 + 8 * i) for i in range(num)]

    days = defaultdict(Day)
    for i, (off, attr) in enumerate(entries):
        end = entries[i + 1][0] if i + 1 < num else len(data)
        rec = data[off:end]
        if attr & REC_DELETED or len(rec) < REC_HDR.size:
            continue
        d, kind, flags, t1, t2 = REC_HDR.unpack_from(rec)
        text = rec[REC_HDR.size:].split(b"\0", 1)[0].decode("cp1252", "replace")
        day = days[EPOCH + timedelta(days=d)]
        if kind == KIND_DAY:
            day.begin, day.end = t1, t2
        elif kind == KIND_MEETING:
            day.meetings.append((t1, text))
        elif kind == KIND_TODO:
            day.todos.append((bool(flags & 1), text))
    return days


# ----------------------------------------------------------- formatting

def fmt_time(t):
    """"7:05", or "--:--" when unset. Also used for durations > 24h."""
    if t is None or t < 0:
        return "--:--"
    return f"{t // 60}:{t % 60:02d}"


def week_start(d, first):
    return d - timedelta(days=(d.weekday() - first) % 7)


def weeks_of_month(year, month, first):
    """Weeks belonging to a month: those whose first day falls in it, plus
    the week containing Jan 1 for January."""
    d = week_start(date(year, month, 1), first)
    if d.month != month and not (month == 1 and d.year < year):
        d += timedelta(days=7)
    while (d.year, d.month) == (year, month) or (month == 1 and d.year < year):
        yield d
        d += timedelta(days=7)


def month_days(year, month):
    d = date(year, month, 1)
    while d.month == month:
        yield d
        d += timedelta(days=1)


# ----------------------------------------------------------------- PDF

class Builder:
    def __init__(self, days, year, first, date_fmt):
        self.days, self.year, self.first, self.date_fmt = days, year, first, date_fmt
        ss = getSampleStyleSheet()
        self.h1, self.h2 = ss["Heading1"], ss["Heading2"]
        self.body = ss["BodyText"].clone("cell", fontSize=8.5, leading=10.5,
                                        spaceBefore=0, spaceAfter=2)
        self.muted = self.body.clone("muted", textColor=colors.grey)

    def day(self, d):
        return self.days.get(d) or Day()

    def month_total(self, month):
        return sum(self.day(d).duration() or 0 for d in month_days(self.year, month))

    @staticmethod
    def grid(data, widths, header_rows=1, total_row=False):
        t = Table(data, colWidths=widths, repeatRows=header_rows)
        style = [
            ("FONT", (0, 0), (-1, header_rows - 1), "Helvetica-Bold", 9),
            ("FONT", (0, header_rows), (-1, -1), "Helvetica", 9),
            ("LINEBELOW", (0, header_rows - 1), (-1, header_rows - 1), 0.8, colors.black),
            ("ROWBACKGROUNDS", (0, header_rows), (-1, -1), [colors.white, colors.HexColor("#f2f2f2")]),
            ("VALIGN", (0, 0), (-1, -1), "TOP"),
            ("TOPPADDING", (0, 0), (-1, -1), 2),
            ("BOTTOMPADDING", (0, 0), (-1, -1), 2),
        ]
        if total_row:
            style += [("LINEABOVE", (0, -1), (-1, -1), 0.8, colors.black),
                      ("FONT", (0, -1), (-1, -1), "Helvetica-Bold", 9),
                      ("BACKGROUND", (0, -1), (-1, -1), colors.white)]
        t.setStyle(TableStyle(style))
        return t

    def year_page(self, width):
        rows = [["Month", "Days worked", "Total"]]
        total = worked = 0
        for m in range(1, 13):
            mins = self.month_total(m)
            n = sum(1 for d in month_days(self.year, m) if self.day(d).duration())
            total, worked = total + mins, worked + n
            rows.append([date(self.year, m, 1).strftime("%B"), str(n), fmt_time(mins)])
        rows.append(["Total", str(worked), fmt_time(total)])
        return [Paragraph(f"WorkLog {self.year}", self.h1),
                self.grid(rows, [width * 0.5, width * 0.25, width * 0.25], total_row=True),
                PageBreak()]

    def month_page(self, month, width):
        rows = [["Day", "Begin", "End", "Duration", "Meetings", "To-dos"]]
        for d in month_days(self.year, month):
            day = self.day(d)
            done = sum(1 for ok, _ in day.todos if ok)
            rows.append([d.strftime(self.date_fmt), fmt_time(day.begin), fmt_time(day.end),
                         fmt_time(day.duration()), str(len(day.meetings) or ""),
                         f"{done}/{len(day.todos)}" if day.todos else ""])
        rows.append(["Total", "", "", fmt_time(self.month_total(month)), "", ""])
        w = width / 6
        t = self.grid(rows, [w * 1.5, w * 0.9, w * 0.9, w, w * 0.85, w * 0.85], total_row=True)
        # Shade weekends (Sat/Sun) so the weeks stand out.
        for i, d in enumerate(month_days(self.year, month), start=1):
            if d.weekday() >= 5:
                t.setStyle(TableStyle([("TEXTCOLOR", (0, i), (0, i), colors.HexColor("#a03030"))]))
        return [Paragraph(date(self.year, month, 1).strftime("%B %Y"), self.h1), t, PageBreak()]

    def week_page(self, start, width):
        dates = [start + timedelta(days=i) for i in range(7)]
        end = dates[-1]
        # Number Sunday-first weeks after their Monday, so they match ISO weeks.
        week = (start + timedelta(days=(7 - self.first) % 7)).isocalendar()[1]
        title = (f"Week {week}: "
                 f"{start.strftime(self.date_fmt)} – {end.strftime(self.date_fmt)} {end.year}")
        rows = [["Day", "Meetings", "To-dos"]]
        total = 0
        for d in dates:
            day = self.day(d)
            total += day.duration() or 0
            style = self.body if d.year == self.year else self.muted
            head = f"<b>{escape(d.strftime(self.date_fmt))}</b>"
            if day.begin >= 0 or day.end >= 0:
                head += (f"<br/>{fmt_time(day.begin)} – {fmt_time(day.end)}"
                         f"<br/><i>{fmt_time(day.duration())}</i>")
            meetings = [Paragraph(f"<b>{fmt_time(t)}</b> {escape(txt)}", style)
                        for t, txt in sorted(day.meetings)]
            todos = [Paragraph(("[x] " if ok else "[  ] ") + escape(txt),
                               self.muted if ok else style)
                     for ok, txt in day.todos]
            rows.append([Paragraph(head, style), meetings, todos])
        rows.append(["Total " + fmt_time(total), "", ""])
        t = self.grid(rows, [width * 0.2, width * 0.45, width * 0.35], total_row=True)
        return [KeepTogether([Paragraph(title, self.h2), Spacer(0, 2 * mm)]), t, PageBreak()]

    def story(self, width):
        out = self.year_page(width)
        for m in range(1, 13):
            out += self.month_page(m, width)
            for w in weeks_of_month(self.year, m, self.first):
                out += self.week_page(w, width)
        return out[:-1]   # no trailing blank page


def footer(canvas, doc):
    canvas.saveState()
    canvas.setFont("Helvetica", 8)
    canvas.setFillColor(colors.grey)
    canvas.drawRightString(doc.pagesize[0] - doc.rightMargin, 10 * mm, str(doc.page))
    canvas.restoreState()


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("pdb", help="WorkLogDB-WkJn.pdb from the HotSync backup folder")
    p.add_argument("--year", type=int, default=date.today().year)
    p.add_argument("-o", "--output", help="default: worklog-<year>.pdf")
    p.add_argument("--first-day", choices=WEEK_STARTS, default="monday",
                   help="first day of the week (default: monday)")
    p.add_argument("--page-size", choices=PAGE_SIZES, default="a4",
                   help="paper size (default: a4)")
    p.add_argument("--date-format", default="%a %d.%m.",
                   help="strftime format for days (default: '%%a %%d.%%m.')")
    args = p.parse_args()

    out = args.output or f"worklog-{args.year}.pdf"
    doc = SimpleDocTemplate(out, pagesize=PAGE_SIZES[args.page_size],
                            leftMargin=15 * mm, rightMargin=15 * mm,
                            topMargin=15 * mm, bottomMargin=18 * mm,
                            title=f"WorkLog {args.year}")
    b = Builder(read_pdb(args.pdb), args.year, WEEK_STARTS[args.first_day], args.date_format)
    doc.build(b.story(doc.width), onFirstPage=footer, onLaterPages=footer)
    print(out)


if __name__ == "__main__":
    main()
