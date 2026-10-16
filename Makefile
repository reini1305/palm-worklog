# Uses the native toolchain under $(PALM) if present, else tools on PATH
# (e.g. inside the Docker image from ./build.sh).
PALM    ?= $(HOME)/opt/palm
BIN     := $(if $(wildcard $(PALM)/bin/m68k-palmos-gcc),$(PALM)/bin/,)

CC      = $(BIN)m68k-palmos-gcc
PILRC   = $(BIN)pilrc
BUILDPRC = $(BIN)build-prc
CFLAGS  = -palmos4 -O2 -Wall
APP     = WorkLog
CREATOR = WkJn

all: $(APP).prc

worklog: worklog.o
	$(CC) $(CFLAGS) -o $@ $^

worklog.o: worklog.c resources.h

res/stamp: worklog.rcp resources.h
	mkdir -p res
	$(PILRC) -q worklog.rcp res
	touch $@

$(APP).prc: worklog res/stamp
	$(BUILDPRC) -o $@ -n $(APP) -c $(CREATOR) worklog res/*.bin

clean:
	rm -rf *.o worklog res $(APP).prc

.PHONY: all clean
