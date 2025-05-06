set(LLVM_NO_RTTI 1)

set(SWIFT_CORE_LIB)

if(WIN32)
  list(APPEND SWIFT_CORE_LIB swiftCore)
endif()

add_lldb_library(lldbPluginSwiftLanguage PLUGIN
  FoundationValueTypes.cpp
  LogChannelSwift.cpp
  ObjCRuntimeSyntheticProvider.cpp
  SwiftArray.cpp
  SwiftBasicTypes.cpp
  SwiftDictionary.cpp
  SwiftFormatters.cpp
  SwiftHashedContainer.cpp
  SwiftLanguage.cpp
  SwiftMetatype.cpp
  SwiftOptionSet.cpp
  SwiftOptional.cpp
  SwiftFrameRecognizers.cpp
  SwiftSet.cpp
  SwiftUnsafeTypes.cpp

  LINK_LIBS
    lldbCore
    lldbDataFormatters
    lldbSymbol
    lldbTarget
    lldbUtility
    lldbPluginObjCLanguage
    lldbPluginTypeSystemSwift
    swiftAST
    swiftClangImporter
    ${SWIFT_CORE_LIB} # Fix for a build error when linking lib/liblldb.lib on Windows: 'unresolved external symbol __imp_swift_addNewDSOImage referenced in function "void __cdecl swift_image_constructor(void)"'
                      # The symbol is present in the file but cannot be resolved.
                      # This is a temporary workaround.
    clangAST

  LINK_COMPONENTS
    Support
)
if(CMAKE_CXX_COMPILER_ID STREQUAL Clang AND NOT SWIFT_COMPILER_MSVC_LIKE)
  target_compile_options(lldbPluginSwiftLanguage PRIVATE
    -Wno-dollar-in-identifier-extension)
endif()
