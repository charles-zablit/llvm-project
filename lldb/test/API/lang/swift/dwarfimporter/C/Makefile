SWIFT_ENABLE_EXPLICIT_MODULES := NO
SWIFT_SOURCES := main.swift
# Use $(BUILDDIR) rather than $(shell pwd): on Windows the latter returns an
# MSYS/cygwin path (/s/m/...) that the Swift/clang frontend can't resolve, so
# the CModule clang module isn't found. Makefile.rules normalizes BUILDDIR to a
# native path on Windows and leaves it equal to pwd elsewhere.
SWIFTFLAGS_EXTRAS := -I$(BUILDDIR)/include -Xcc -I$(BUILDDIR)/include

include Makefile.rules
