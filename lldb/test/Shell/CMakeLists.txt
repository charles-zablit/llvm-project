add_custom_target(lldb-shell-test-deps)
set_target_properties(lldb-shell-test-deps PROPERTIES FOLDER "LLDB/Tests")
add_dependencies(lldb-shell-test-deps lldb-test-depends)

add_lit_testsuites(LLDB-SHELL
  ${CMAKE_CURRENT_SOURCE_DIR}
  DEPENDS lldb-shell-test-deps)

# On Windows, where there is no rpath, the REPL / expression evaluator and the
# inferiors launched by shell tests need the Swift runtime DLLs (swiftCore,
# Foundation, ...) on PATH at launch. This mirrors the same cache variable in
# test/API/CMakeLists.txt; declaring it here keeps the Shell suite consumable
# even if configured without the API suite. (Idempotent: a shared cache entry.)
set(LLDB_TEST_INFERIOR_RUNTIME_BIN
  ""
  CACHE STRING "Directory containing the target platform's runtime DLLs. \
Used on Windows to populate the test inferior's PATH so dynamically-loaded \
Swift runtime DLLs resolve."
)

configure_lit_site_cfg(
  ${CMAKE_CURRENT_SOURCE_DIR}/lit.site.cfg.py.in
  ${CMAKE_CURRENT_BINARY_DIR}/lit.site.cfg.py
  MAIN_CONFIG
  ${CMAKE_CURRENT_SOURCE_DIR}/lit.cfg.py)
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/lit-lldb-init.in
  ${CMAKE_CURRENT_BINARY_DIR}/lit-lldb-init)

file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/lit-lldb-init-quiet
  "command source -C --silent-run true lit-lldb-init\n")

add_lit_testsuite(check-lldb-shell "Running lldb shell test suite"
  ${CMAKE_CURRENT_BINARY_DIR}
  EXCLUDE_FROM_CHECK_ALL
  DEPENDS lldb-shell-test-deps)
