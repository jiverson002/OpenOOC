# Name of project
PROJECT := OpenOOC


# Name of library
LIBRARY := openooc


# Directory which holds $(LIBRARY).h
#   $(LIBRARY).h MUST contain the version information for the project in the
#   following macros: $(PROJECT)_MAJOR, $(PROJECT)_MINOR, $(PROJECT)_PATCH, and
#   optionally $(PROJECT)_RCAND.
INCLUDE := src/include


# Sub-directories holding lib sources
PROJDIRS := src


# Sub-directories holding app sources
APPSDIRS := apps


# List of all non-source files/directories that are part of the distribution
AUXFILES := AUTHORS ChangeLog COPYING Makefile NEWS README.md


#-------------------------------------------------------------------------------
# PROGRAMS
#-------------------------------------------------------------------------------
#{{{1
# Program variables
AR := ar
RM := rm -f
ECHO := echo
CC := cc
LD := cc
#}}}1


#-------------------------------------------------------------------------------
# FLAGS
#-------------------------------------------------------------------------------
#{{{1
ARFLAGS  := crsP
WARNING  := -Wall -Wextra -pedantic -Wshadow -Wpointer-arith -Wcast-align \
            -Wwrite-strings -Wmissing-prototypes -Wmissing-declarations \
            -Wredundant-decls -Wnested-externs -Winline -Wno-long-long \
            -Wuninitialized -Wconversion -Wstrict-prototypes
OPTIMIZE := -O0 -g
CFLAGS   := -std=c99 $(OPTIMIZE) $(WARNING)
LDFLAGS  :=
#}}}1


#===============================================================================
# MUST NOT CHANGE ANYTHING BELOW HERE
#===============================================================================


#-------------------------------------------------------------------------------
# INTERNAL VARIABLES
#-------------------------------------------------------------------------------
#{{{1
# Version information
DATE    := $(shell date)
COMMIT  := $(shell git rev-parse --short HEAD)
VERSION := $(shell grep -e '^\#define $(PROJECT)_MAJOR' \
                        -e '^\#define $(PROJECT)_MINOR' \
                        -e '^\#define $(PROJECT)_PATCH' \
                        -e '^\#define $(PROJECT)_RCAND' \
                        $(INCLUDE)/$(LIBRARY).h \
                 | awk '{print $$3}' \
                 | paste -d ' ' - - - - \
                 | awk '{printf "%d.%d.%d%s", $$1,$$2,$$3,$$4}')
#}}}1


#-------------------------------------------------------------------------------
# FILES
#-------------------------------------------------------------------------------
#{{{1
SRCFILES    := $(shell find $(PROJDIRS) -type f -name "*.c")
HDRFILES    := $(shell find $(PROJDIRS) -type f -name "*.h")
APPFILES    := $(shell find $(APPSDIRS) -type f -name "*.c")

OBJFILES    := $(patsubst %.c,%.o,$(SRCFILES))
BINFILES    := $(patsubst %.c,%,$(APPFILES))
TSTFILES    := $(patsubst %.c,%_t,$(SRCFILES))

DEPFILES    := $(patsubst %.c,%.d,$(SRCFILES))
BINDEPFILES := $(patsubst %.c,%.d,$(APPFILES))
TSTDEPFILES := $(patsubst %,%.d,$(TSTFILES))

ALLFILES    := $(SRCFILES) $(HDRFILES) $(APPFILES) $(AUXFILES)

LIB         := lib$(LIBRARY).a
DIST        := $(LIBRARY)-$(VERSION).tar.gz
#}}}1


#-------------------------------------------------------------------------------
# COMPILE TARGETS
#-------------------------------------------------------------------------------
#{{{1
all: $(LIB) $(BINFILES)

-include $(DEPFILES) $(BINDEPFILES) $(TSTDEPFILES)

$(LIB): $(OBJFILES)
	@echo "  AR       $@"
	@$(AR) $(ARFLAGS) $@ $?

%.o: %.c Makefile
	@echo "  CC       $@"
	@$(CC) $(CFLAGS) -MMD -MP -I$(INCLUDE) \
         -DVERSION="$(VERSION)" -DDATE="$(DATE)" -DCOMMIT="$(COMMIT)" \
         -c $< -o $@

%: %.c Makefile $(LIB)
	@echo "  CC       $@"
	@$(CC) $(CFLAGS) -MMD -MP -I$(INCLUDE) $(LDFLAGS) \
         -DVERSION="$(VERSION)" -DDATE="$(DATE)" -DCOMMIT="$(COMMIT)" \
         $< $(LIB) -o $@

%_t: %.c Makefile $(LIB)
	@echo "  CC       $@"
	@$(CC) $(CFLAGS) -MMD -MP -I$(INCLUDE) $(LDFLAGS) -DTEST \
         -DVERSION="$(VERSION)" -DDATE="$(DATE)" -DCOMMIT="$(COMMIT)" \
         $< $(LIB) -o $@

# FIXME This will not update distribution if a file in a directory included in
# ALLFILES is updated/added.
$(DIST): $(ALLFILES)
	-tar czf $(DIST) $(ALLFILES)
#}}}1


#-------------------------------------------------------------------------------
# PHONY TARGETS
#-------------------------------------------------------------------------------
#{{{1
check: $(TSTFILES)
	-@rc=0; count=0; \
    for file in $(TSTFILES); do \
      ./$$file; \
      ret=$$?; \
      rc=`expr $$rc + $$ret`; count=`expr $$count + 1`; \
      if [ $$ret -eq 0 ] ; then \
        echo -n "  PASS"; \
      else \
        echo -n "  FAIL"; \
      fi; \
      echo "     $$file"; ./$$file; \
    done; \
    echo; \
    echo "Tests executed: $$count  Tests failed: $$rc"

clean:
	-@$(RM) $(wildcard $(OBJFILES) $(BINFILES) $(TSTFILES) \
    $(DEPFILES) $(BINDEPFILES) $(TSTDEPFILES) $(LIB))

dist: $(DIST)

distclean: clean
	-@$(RM) $(wildcard $(DIST))

todolist:
	-@for file in $(ALLFILES:Makefile=); do \
    fgrep -H -e TODO -e FIXME $$file; \
    done; \
    true
#}}}1


#-------------------------------------------------------------------------------
# SPECIAL TARGETS
#-------------------------------------------------------------------------------
#{{{1
.PHONY: check clean dist distclean todolist
#}}}1


# vim: set foldmethod=marker:
