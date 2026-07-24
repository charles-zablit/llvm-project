SWIFT_SOURCES=main.swift
# Use $(BUILDDIR) rather than $(shell pwd): on Windows the latter returns an
# MSYS path (/s/m/...) the Swift frontend/linker can't resolve, so SomeLibrary
# fails to resolve. Makefile.rules normalizes BUILDDIR to a native path there.
SWIFTFLAGS_EXTRAS= -I$(BUILDDIR)
MODULENAME=main
LD_EXTRAS=-L$(BUILDDIR)

all: clean SomeLibrary.swiftmodule a.out

include Makefile.rules

SomeLibrary.swiftmodule: SomeLibrary.swift
	"$(MAKE)" BASENAME=$(shell basename $@ .swiftmodule) \
		SWIFTFLAGS_EXTRAS=$(LIBRARY_SWIFTFLAGS_EXTRAS) \
		VPATH=$(SRCDIR) -f $(SRCDIR)/../dylib.mk all

clean::
	$(call RM_RF,*.o *.dylib *.so *.dll *.dSYM *.swiftdoc *.swiftmodule *.swiftinterface)

