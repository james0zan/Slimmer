#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

const char *format = "%d %p %p\n";
struct X {
  int a, b;
};

void Foo(X* tmp) {
  tmp->b = 2;
}

int main(void) {
  bool flag = false;
  for (int i = 0; i < 4; ++i) {
    if (i == 2) {
      flag = true;
    }
  }
  printf("%d\n", flag);

  while(1) sleep(1);
}

