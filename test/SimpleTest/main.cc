#include <stdio.h>

const char *format = "%d %d %p %p\n";
struct X {
  int a, b;
};

int main(void) {
  X tmp;
  tmp.a = 1;
  tmp.b = 2;
  int c = 1;
  printf(format, tmp.a, tmp.b, format, &tmp);
  
  int a = 1;
  printf("%d\n", a);
}

