//===----------------------------------------------------------------------===//
//  This is a simple version of GCC#57812,
//  in which the later part of a loop
//  is unused.
//===----------------------------------------------------------------------===//

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

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

  while(1) {
    usleep(10000000);
    printf("HERE\n");
  }
}