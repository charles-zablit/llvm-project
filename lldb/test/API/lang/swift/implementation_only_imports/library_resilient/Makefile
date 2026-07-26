SWIFT_SOURCES=main.swift
SWIFTFLAGS_EXTRAS= -I.
LD_EXTRAS=-L.

all: clean SomeLibraryCore.swiftmodule SomeLibrary.swiftmodule a.out

include Makefile.rules

# -rpath is meaningless on Windows and the MSVC linker rejects it (it treats the
# following path as an input file -> LNK1104), so omit it there; on Windows the
# dependent DLLs are located via PATH (extra_images) instead.
ifeq "$(OS)" "Windows_NT"
SOMELIBRARY_LD_EXTRAS = -L$(BUILDDIR)
else
SOMELIBRARY_LD_EXTRAS = -L$(BUILDDIR) -Xlinker -rpath -Xlinker $(BUILDDIR)
endif

SomeLibraryCore.swiftmodule: SomeLibraryCore.swift
	"$(MAKE)" BASENAME=$(shell basename $@ .swiftmodule) \
		SWIFTFLAGS_EXTRAS=-enable-library-evolution \
		VPATH=$(SRCDIR) -f $(SRCDIR)/../dylib.mk all

SomeLibrary.swiftmodule: SomeLibrary.swift SomeLibraryCore.swiftmodule
	"$(MAKE)" BASENAME=$(shell basename $@ .swiftmodule) \
		SWIFTFLAGS_EXTRAS=-enable-library-evolution \
		VPATH=$(SRCDIR) LD_EXTRAS="$(SOMELIBRARY_LD_EXTRAS)" -f $(SRCDIR)/../dylib.mk all

clean::
	$(call RM_RF,*.o *.dylib *.so *.dll *.dSYM *.swiftdoc *.swiftmodule *.swiftinterface)
