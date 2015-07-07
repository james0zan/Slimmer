#include <stdio.h>

// const char *format = "%d %p %p\n";
// struct X {
//   int a, b;
// };

// void Foo(X* tmp) {
//   tmp->b = 2;
// }

bool phi(bool r, bool y){
    return y || r ;
}

int main(void) {
  // X tmp;
  // tmp.a = 1;
  // Foo(&tmp);
  // int c = 1;
  // printf(format, tmp.a, format, &tmp);
  
  printf("%d\n", phi(false, true));
}

