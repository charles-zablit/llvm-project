// RUN: %clang_cc1 -triple x86_64-apple-darwin10.0.0 -fsyntax-only -verify %s -Winvalid-offsetof -std=c++98
// RUN: %clang_cc1 -triple x86_64-apple-darwin10.0.0 -fsyntax-only -verify %s -Winvalid-offsetof -std=c++98 -fexperimental-new-constant-interpreter

struct NonPOD {
  virtual void f();
  int m;
};

struct P {
  NonPOD fieldThatPointsToANonPODType;
};

void f() {
  int i = __builtin_offsetof(P, fieldThatPointsToANonPODType.m); // expected-warning{{'offsetof' on non-POD type 'P'}}
}

struct Base { int x; };
struct Derived : Base { int y; };
int o = __builtin_offsetof(Derived, x); // expected-warning{{'offsetof' on non-POD type}}

const int o2 = sizeof(__builtin_offsetof(Derived, x));

struct HasArray {
  int array[17];
};

// Constant and non-constant offsetof expressions
void test_ice(int i) {
  int array0[__builtin_offsetof(HasArray, array[5])];
  int array1[__builtin_offsetof(HasArray, array[i])]; // expected-warning {{variable length arrays in C++ are a Clang extension}}
}

// Bitfields
struct has_bitfields {
  int i : 7;
  int j : 12; // expected-note{{bit-field is declared here}}
};

int test3 = __builtin_offsetof(struct has_bitfields, j); // expected-error{{cannot compute offset of bit-field 'j'}}

// offsetof referring to members of a base class.
struct Base1 {
  int x;
};

struct Base2 {
  int y;
};

struct Derived2 : public Base1, public Base2 {
  int z;
};

int derived1[__builtin_offsetof(Derived2, x) == 0? 1 : -1]; // expected-warning{{'offsetof' on non-POD type 'Derived2'}}
int derived2[__builtin_offsetof(Derived2, y)  == 4? 1 : -1]; // expected-warning{{'offsetof' on non-POD type 'Derived2'}}
int derived3[__builtin_offsetof(Derived2, z)  == 8? 1 : -1]; // expected-warning{{'offsetof' on non-POD type 'Derived2'}}

// offsetof referring to anonymous struct in base.
// PR7769
struct foo {
    struct {
        int x;
    };
};

struct bar : public foo  {
};

int anonstruct[__builtin_offsetof(bar, x) == 0 ? 1 : -1]; // expected-warning{{'offsetof' on non-POD type 'bar'}}


struct LtoRCheck {
  int a[10];
  int f();
};
int ltor = __builtin_offsetof(struct LtoRCheck, a[LtoRCheck().f]); // \
  expected-error {{reference to non-static member function must be called}}

namespace PR17578 {
struct Base {
  int Field;
};
struct Derived : virtual Base {
  void Fun() { (void)__builtin_offsetof(Derived, Field); } // expected-warning {{'offsetof' on non-POD type}} \
                                                              expected-error {{invalid application of 'offsetof' to a field of a virtual base}}
};
}

int test_definition(void) {
  return __builtin_offsetof(struct A // expected-error {{'A' cannot be defined in a type specifier}}
  {
    int a;
    struct B // FIXME: error diagnostic message for nested definitions
             // https://reviews.llvm.org/D133574
             // fixme-error{{'A' cannot be defined in '__builtin_offsetof'}}
    {
      int c;
      int d;
    };
    B x;
  }, a);
}
