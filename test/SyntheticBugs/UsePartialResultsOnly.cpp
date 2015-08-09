//===----------------------------------------------------------------------===//
//  This is a simple version of Apache#45464,
//  in which a proper input flag can be used
//  for bypassing unneeded computations.
//===----------------------------------------------------------------------===//

#include <stdio.h>

int a, b;

void Foo(int flags) {
  if (flags & 1) {
    a = 1;
  }
  if (flags & 2) {
    b = 2;
  }
}

int main() {
  a = b = 0;
  Foo(3);
  printf("%d\n", a);
}