#include "SegmentTree.hpp"
#include <stdio.h>

using namespace std;

int main() {
  auto tree = SegmentTree<int>::NewTree();
  
  // Test merging
  tree->Set(4, 10, 1);  
  tree->Set(12, 20, 1);
  tree->Set(10, 12, 1);
  printf("=========\n");
  // tree->Print(0);
  auto cur = tree->Collect(0, SegmentTree<int>::MAX_RANGE);
  for (auto i: cur) {
    printf("[%lu,%lu): %d %d\n", i.left, i.right, i.type, i.value);
  }

  // Test cover
  tree->Set(30, 100, 2);
  tree->Set(9, 11, 2);
  printf("=========\n");
  // tree->Print(0);
  cur = tree->Collect(0, SegmentTree<int>::MAX_RANGE);
  for (auto i: cur) {
    printf("[%lu,%lu): %d %d\n", i.left, i.right, i.type, i.value);
  }
}