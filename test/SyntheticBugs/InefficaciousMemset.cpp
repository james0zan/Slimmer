#include <stdio.h>
#include <string.h>


int main(void) {
  char a[20];
  memset(a, 1, 20);
  memset(a, 1, 20);
  printf("%d\n", a[0]);
}