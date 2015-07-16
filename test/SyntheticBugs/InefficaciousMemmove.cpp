#include <stdio.h>
#include <stdint.h>
#include <string.h>


int main(void) {
  uint64_t a = 1234, b;
  memcpy(&b, &a, sizeof(uint64_t));
  memcpy(&b, &a, sizeof(uint64_t));
  printf("%lu\n", b);
}
