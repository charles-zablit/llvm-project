// clang-format off
// REQUIRES: lld, x86

// Test that a member without a size at the end of a record is kept.
// RUN: %clang_cl --target=x86_64-windows-msvc -Od -Z7 -c /Fo%t.obj -- %s
// RUN: lld-link -debug:full -nodefaultlib -entry:main %t.obj -out:%t.exe -pdb:%t.pdb
// RUN: %lldb -f %t.exe -o "type lookup Flexible" -b 2>&1 | FileCheck %s

struct Flexible {
  int size;
  char data[0];
};

Flexible f;

int main() { return f.size; }

// CHECK:      struct Flexible {
// CHECK-NEXT:   int size;
// CHECK-NEXT:   char data[0];
// CHECK-NEXT: }
