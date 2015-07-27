//===----------------------------------------------------------------------===//
//  This is a simple version of GCC#57812,
//  in which the later part of a loop
//  is unused.
//===----------------------------------------------------------------------===//

#include <stdio.h>

int main() {
  bool flag = false;
  for (int i = 0; i < 4; ++i) {
    if (i == 2) {
      flag = true;
    }
  }
  printf("%d\n", flag);
}