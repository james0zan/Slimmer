#include "SlimmerUtil.h"

/// Set a range [l, r) to be value _v.
///
void SegmentTree::Set(uint64_t l, uint64_t r, unsigned _v) {
  assert(_v > 0); int v = (int)_v; // 0 and -1 are reserved
  
  // If already covered
  if (value == v) return;
  
  // The range of this node is completely covered
  if (l <= left && r >= right) {
    value = v;
    delete l_child; l_child = NULL;
    delete r_child; r_child = NULL;
    return;
  }

  uint64_t mid = left/2 + right/2;
  if (left%2 && right%2) mid++;
  
  if (value != PARTIAL_VALUE) {
    // Split the segment into two.
    assert(l_child == NULL && r_child == NULL);  
    l_child = new SegmentTree(value, left, mid);
    r_child = new SegmentTree(value, mid, right);
    value = PARTIAL_VALUE;
  }

  if (l < mid) { l_child->Set(l, std::min(r, mid), v); }
  if (r > mid) { r_child->Set(std::max(l, mid), r, v); }

  if (l_child != NULL && r_child != NULL
    && l_child->value == r_child->value
    && l_child->value != PARTIAL_VALUE) {
    value = l_child->value;
    delete l_child; l_child = NULL;
    delete r_child; r_child = NULL;
  }
}

// void Destroy() {
//   if (l_child) l_child.Destroy();
//   delete l_child; l_child = NULL;
//   if (r_child) r_child.Destroy();
//   delete r_child; r_child = NULL;
// }

/// Collect the leaves within range [l, r).
///
std::vector<Segment> SegmentTree::Collect(uint64_t l, uint64_t r) {
  std::vector<Segment> res;
  Collect2(l, r, res);
  return res;  
}

/// Collect is a wrapper of Collect2.
///
void SegmentTree::Collect2(uint64_t l, uint64_t r, std::vector<Segment>& res) {
  if (value >= 0) {
    res.push_back(std::make_tuple(value, left, right));
    if (res.size() >= 2) {
      Segment& last = res[res.size() - 1];
      Segment& second_last = res[res.size() - 2];
      
      if (std::get<0>(second_last) == std::get<0>(last)
        && std::get<2>(second_last) == std::get<1>(last)) {
        second_last = std::make_tuple(
          std::get<0>(second_last), std::get<1>(second_last), std::get<2>(last));
        res.pop_back();
      }
    }
  } else if (value == -1) {
    uint64_t mid = left/2 + right/2;
    if (left%2 && right%2) mid++;
    if (l < mid) { l_child->Collect2(l, std::min(r, mid), res); }
    if (r > mid) { r_child->Collect2(std::max(l, mid), r, res); }
  }
}

/// Print all the elements of a SegmentTree.
/// 
/// \param indent - the level of this node.
///
void SegmentTree::Print(int indent) {
  if (l_child) {
    l_child->Print(indent + 4);
  }
  for (int i = 0; i < indent; ++i) printf(" ");
  if (l_child) {
    printf(" /\n");
    for (int i = 0; i < indent; ++i) printf(" ");
  }
  printf("[%lu,%lu):%d\n", left, right, value);
  if (r_child) {
    for (int i = 0; i < indent; ++i) printf(" ");
    printf(" \\\n");
    r_child->Print(indent + 4);
  }
}