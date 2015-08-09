#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define CAS(x,y,z) __atomic_compare_exchange_n(x,y,z,true,0,0)

int main() {
  int x = 0;
  int cmp = 0, val = 1;
  CAS(&x,&cmp,val); // if (z == x) z = &y;
  printf("%d\n", x);
  return 0;
}