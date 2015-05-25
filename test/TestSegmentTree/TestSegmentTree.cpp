#include "SlimmerUtil.h"

using namespace std;

int main() {
  auto tree = SegmentTree::NewTree();
  
  tree->Set(4, 10, 1);
  printf("=========\n");
  // tree->Print(0);
  auto cur = tree->Collect(0, SegmentTree::MAX_RANGE);
  for (auto i: cur) {
    printf("[%lu,%lu):%d\n", get<1>(i), get<2>(i), get<0>(i));
  }
  
  tree->Set(12, 20, 1);
  printf("=========\n");
  // tree->Print(0);
  cur = tree->Collect(0, SegmentTree::MAX_RANGE);
  for (auto i: cur) {
    printf("[%lu,%lu):%d\n", get<1>(i), get<2>(i), get<0>(i));
  }

  tree->Set(10, 12, 1);
  printf("=========\n");
  // tree->Print(0);
  cur = tree->Collect(0, SegmentTree::MAX_RANGE);
  for (auto i: cur) {
    printf("[%lu,%lu):%d\n", get<1>(i), get<2>(i), get<0>(i));
  }

  tree->Set(30, 100, 2);
  printf("=========\n");
  // tree->Print(0);
  cur = tree->Collect(0, SegmentTree::MAX_RANGE);
  for (auto i: cur) {
    printf("[%lu,%lu):%d\n", get<1>(i), get<2>(i), get<0>(i));
  }
}