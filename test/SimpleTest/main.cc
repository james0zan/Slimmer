//===----------------------------------------------------------------------===//
//  This is a simple version of GCC#57812,
//  in which the later part of a loop
//  is unused.
//===----------------------------------------------------------------------===//

#include <stdio.h>

int main() {
  bool flag = false, flag2 = true;
  int x = 0;

  if (flag) {
    x = 1;
  } else if (!flag2) {
     x = 2;
  }
  printf("%d\n", x);
}