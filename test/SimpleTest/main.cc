#include <stdio.h>

// const char *format = "%d %p %p\n";
// struct X {
//   int a, b;
// };

// void Foo(X* tmp) {
//   tmp->b = 2;
// }

int main(void) {
  // X tmp;
  // tmp.a = 1;
  // Foo(&tmp);
  // int c = 1;
  // printf(format, tmp.a, format, &tmp);
  
  int a = 0;
  bool is = true;
  if (is) {
    a = 1;
  }
  // int a = 1;
  printf("%d\n", a);
}

