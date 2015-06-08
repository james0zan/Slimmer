#include <stdio.h>

char *format = "%d %d %p %p\n";
struct X {
  int a, b;
};

int main(void) {
  X tmp;
  tmp.a = 1;
  tmp.b = 2;
  printf(format, tmp.a, tmp.b, format, &tmp);
}

