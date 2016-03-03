# User supplied information about this project
PROJECT := openooc
VERSION := 0.0.0-pre


# Modules in the project (you could 'find' these, but stating them explicitly
# allows for subdirectories like 'tmp' or 'doc' without upsetting the build
# process.
MODULES := apps bench src


# List of all non-source files/directories that are part of the distribution
AUXFILES := AUTHORS ChangeLog COPYING doc Makefile NEWS README.md


# Default target
default: all


#-------------------------------------------------------------------------------
# DEFAULT VARIABLES AND PROGRAMS
#-------------------------------------------------------------------------------
#{{{1
# Verbosity level
V := 0

# Program variables
AR := ar crs
RM := rm -rvf
CC := cc
LD := cc

AT_0 = @
AT_1 =
AT   = $(AT_$(V))

AR_0 = @echo "  AR       $$@"; $(AR)
AR_1 = $(AR)
ATAR = $(AR_$(V))

CC_0 = @echo "  CC       $(1)/$$*.o"; $(CC)
CC_1 = $(CC)
ATCC = $(CC_$(V))

LD_0 = @echo "  LD       $$@"; $(LD)
LD_1 = $(LD)
ATLD = $(LD_$(V))

CCLD_0 = @echo "  CCLD     $$@"; $(CC)
CCLD_1 = $(CC)
ATCCLD = $(CCLD_$(V))
#}}}1


#-------------------------------------------------------------------------------
# DEFAULT FLAGS
#-------------------------------------------------------------------------------
#{{{1
WARNING  := -Wall -Wextra -pedantic -Wshadow -Wpointer-arith -Wcast-align \
            -Wwrite-strings -Wmissing-prototypes -Wmissing-declarations \
            -Wredundant-decls -Wnested-externs -Winline -Wno-long-long \
            -Wuninitialized -Wconversion -Wstrict-prototypes
OPTIMIZE := -O0 -g
CFLAGS   := $(OPTIMIZE) $(WARNING) -D_GNU_SOURCE
LDFLAGS  :=
#}}}1


#-------------------------------------------------------------------------------
# COMPILATION INFO
#-------------------------------------------------------------------------------
#{{{1
# Root directory where Makefile is located
ROOTDIR := $(dir $(lastword $(MAKEFILE_LIST)))

# Date of compilation
DATE := $(shell date)

# Git commit of compilation
COMMIT := $(shell cd $(ROOTDIR) && git rev-parse --short HEAD)

# Aggregation of all files
ALLFILES := $(addprefix $(ROOTDIR),$(MODULES) $(AUXFILES))

# Name for distribution archive
DIST := $(PROJECT)-$(VERSION).tar.gz
#}}}1


#-------------------------------------------------------------------------------
# TEMPLATES
#-------------------------------------------------------------------------------
#{{{1
# Including a module's build.mk
define MK_template
#{{{2
include $(ROOTDIR)$(1)/build.mk
#}}}2
endef

 
# Setting a module's build rules for object files in obj/<module>.
# Also links a module's global includes into the global include directory
# (where they will be available as <module>/filename.h).
# TODO Should module specified flags override command-line or vice-versa?
define RULES_template
#{{{2
obj/$(1)/%.o: $(ROOTDIR)$(1)/%.c $(ROOTDIR)Makefile $(ROOTDIR)$(1)/build.mk
	$(AT)(test -d include || mkdir -p include)
	$(AT)(test -h include/$(1) || ln -s ../$(ROOTDIR)$(1)/include include/$(1))
	$(AT)(test -d obj/`dirname $(1)/$$*` || mkdir -p obj/`dirname $(1)/$$*`) &&\
    (test -d dep/`dirname $(1)/$$*` || mkdir -p dep/`dirname $(1)/$$*`)
	$(ATCC) $$($(1)_CFLAGS) $$(CFLAGS) -I include -I $(ROOTDIR)\
    -MMD -MP -MF dep/$(1)/$$*.d -DDATE="$(DATE)" -DCOMMIT="$(COMMIT)"\
    -c $$< -o $$@
#}}}2
endef


# Setting a module's build rules for test files in test/<module>.
# TODO Should module specified flags override command-line or vice-versa?
define TESTS_template
#{{{2
test/$(1)/%: $(ROOTDIR)$(1)/%.c\
             $(addprefix lib/,$($(1)_LIBRARIES))
	$(AT)(test -d test/$(1) || mkdir -p test/$(1)) && \
    (test -d dep/$(1)/test || mkdir -p dep/$(1)/test)
	$(ATCCLD) $$($(1)_CFLAGS) $$(CFLAGS) $$($(1)_LDFLAGS) $$(LDFLAGS)\
    -I include -I $(ROOTDIR) -MMD -MP -MF dep/$(1)/test/$$*.d\
    -DTEST -DDATE="$(DATE)" -DCOMMIT="$(COMMIT)" $$^ $$($(1)_LDLIBS) $$(LDLIBS)\
    -o $$@
#}}}2
endef
 
 
# Setting a module's build rules for executable targets.
# (Depending on its sources' object files and any libraries.)
# Also adds a module's object and dependency files to the global lists.
# TODO Should module specified flags override command-line or vice-versa?
define PROGRAM_template
#{{{2
OBJFILES += $(patsubst %.c,obj/$(1)/%.o,$($(2)_SOURCES))
DEPFILES += $(patsubst %.c,dep/$(1)/%.d,$($(2)_SOURCES))

bin/$(2): $(patsubst %.c,obj/$(1)/%.o,$($(2)_SOURCES))\
          $(addprefix lib/,$($(2)_LDADD) $($(1)_LDADD))
	$(AT)(test -d bin || mkdir -p bin)
	$(ATLD) $$($(2)_LDFLAGS) $$($(1)_LDFLAGS) $$(LDFLAGS)\
    $$^ $$($(2)_LDLIBS) $$($(1)_LDLIBS) $$(LDLIBS) -o $$@
#}}}2
endef

 
# Setting a module's build rules for library targets.
# (Depending on its sources' object files.)
# Also adds a module's object and dependency files to the global lists.
define LIBRARY_template
#{{{2
OBJFILES += $(patsubst %.c,obj/$(1)/%.o,$($(2)_SOURCES))
DEPFILES += $(patsubst %.c,dep/$(1)/%.d,$($(2)_SOURCES))
DEPFILES += $(patsubst %.c,dep/$(1)/test/%.d,$($(2)_SOURCES))

lib/$(2): $(patsubst %.c,obj/$(1)/%.o,$($(2)_SOURCES))
	$(AT)(test -d lib || mkdir -p lib)
	$(ATAR) $$@ $$?
#}}}2
endef
#}}}1

 
#-------------------------------------------------------------------------------
# INSTANTIATE TEMPLATES
#-------------------------------------------------------------------------------
#{{{1
# Now, instantiating the templates for each module.
$(foreach mod,$(MODULES),$(eval $(call MK_template,$(mod))))
$(foreach mod,$(MODULES),$(eval $(call RULES_template,$(mod))))
$(foreach mod,$(MODULES),$(foreach bin,$($(mod)_PROGRAMS),\
  $(eval $(call PROGRAM_template,$(mod),$(bin)))))
$(foreach mod,$(MODULES),$(foreach lib,$($(mod)_LIBRARIES),\
  $(eval $(call LIBRARY_template,$(mod),$(lib)))))
$(foreach mod,$(MODULES),$(eval $(call INCLUDE_template,$(mod))))
$(foreach mod,$(MODULES),$(eval $(call TESTS_template,$(mod))))

# Include the dependency files (generated by GCC's -MMD option)
-include $(sort $(DEPFILES))
#}}}1


#-------------------------------------------------------------------------------
# PHONY TARGETS
#-------------------------------------------------------------------------------
#{{{1
# Make all modules programs and libraries
all: \
  $(foreach mod,$(MODULES),$(addprefix lib/,$($(mod)_LIBRARIES)))\
  $(foreach mod,$(MODULES),$(addprefix bin/,$($(mod)_PROGRAMS)))


# Check build
check: \
	$(foreach mod,$(MODULES),$(foreach lib,$($(mod)_LIBRARIES), \
    $(patsubst %.c,test/$(mod)/%,$($(lib)_SOURCES))))
	-@rc=0; count=0; \
    for file in $^; do \
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
 

# Remove object (*.o) and dependency (*.d) files
clean:
	$(AT)$(RM) $(OBJFILES) $(DEPFILES)


# Create archive for distribution
dist:
	-$(AT)tar czf $(DIST) $(ALLFILES)


# Make clean and remove executables and libraries 
realclean: clean
	$(AT)$(RM) bin/* include/* lib/* test/* $(DIST)


# Make realclean and remove distribution archive
distclean: realclean
	$(AT)$(RM) $(DIST)


# Print out any TODO or FIXME notations
todolist:
	-@for file in $(ALLFILES:$(ROOTDIR)Makefile=); do \
    fgrep -H -e TODO -e FIXME $$file; \
  done; \
  true
#}}}1


#-------------------------------------------------------------------------------
# SPECIAL TARGETS
#-------------------------------------------------------------------------------
#{{{1
.PHONY: all check clean dist distclean realclean todolist
#}}}1


# vim: set foldmethod=marker:
