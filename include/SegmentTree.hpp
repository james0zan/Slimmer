#ifndef SLIMMER_SEGMENT_TREE_HPP
#define SLIMMER_SEGMENT_TREE_HPP

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <vector>

//===----------------------------------------------------------------------===//
//                           Segment Tree
//===----------------------------------------------------------------------===//

enum SegmentType {
  EMPTY_SEGMENT,
  PARTIAL_SEGMENT,
  COVERED_SEGMENT
};

template <typename T>
struct Segment {
  SegmentType type;
  T value;
  uint64_t left, right;
  Segment() {}
  Segment(SegmentType t, T v, uint64_t l, uint64_t r)
    : type(t), value(v), left(l), right(r) {}
};

template <typename T>
class SegmentTree {
public:
  SegmentTree() : type(EMPTY_SEGMENT) {}
  SegmentTree(SegmentType t, T v, uint64_t l, uint64_t r)
    : type(t), value(v), left(l), right(r), l_child(NULL), r_child(NULL) {}
  SegmentTree(SegmentType t, T v, uint64_t l, uint64_t r, SegmentTree<T>* lc, SegmentTree<T>* rc)
    : type(t), value(v), left(l), right(r), l_child(lc), r_child(rc) {}
  ~SegmentTree() {
    if (l_child) delete l_child; l_child = NULL;
    if (r_child) delete r_child; r_child = NULL;
  }
  static const uint64_t MAX_RANGE = (uint64_t)-1;
  static SegmentTree<T>* NewTree()  {
    return new SegmentTree<T>(EMPTY_SEGMENT, T(), 0, SegmentTree<T>::MAX_RANGE);
  }

  /// Set a range [l, r) to be value _v.
  ///
  void Set(uint64_t l, uint64_t r, T v) {
    // assert(_v > 0); int v = (int)_v; // 0 and -1 are reserved
    
    // If already covered
    if (type == COVERED_SEGMENT && value == v) return;
    
    // The range of this node is completely covered
    if (l <= left && r >= right) {
      type = COVERED_SEGMENT;
      value = v;
      delete l_child; l_child = NULL;
      delete r_child; r_child = NULL;
      return;
    }

    uint64_t mid = left/2 + right/2;
    if (left%2 && right%2) mid++;
    
    if (type != PARTIAL_SEGMENT) {
      // Split the segment into two.
      assert(l_child == NULL && r_child == NULL);  
      l_child = new SegmentTree(type, value, left, mid);
      r_child = new SegmentTree(type, value, mid, right);
      type = PARTIAL_SEGMENT;
    }

    if (l < mid) { l_child->Set(l, std::min(r, mid), v); }
    if (r > mid) { r_child->Set(std::max(l, mid), r, v); }

    if (l_child != NULL && r_child != NULL
      && (
        (l_child->type == COVERED_SEGMENT && r_child->type == COVERED_SEGMENT && l_child->value == r_child->value)
      || 
        (l_child->type == EMPTY_SEGMENT && r_child->type == EMPTY_SEGMENT)
      )) {
      type = l_child->type;
      value = l_child->value;
      delete l_child; l_child = NULL;
      delete r_child; r_child = NULL;
    }
  }

  /// Get the value at point x.
  /// \param x - the point that the user want to get.
  /// \return return false if the point is not covered.
  ///
  bool Get(uint64_t x, T& v) {
    if (type == EMPTY_SEGMENT) return false;
    if (type == COVERED_SEGMENT) {
      v = value;
      return true;
    }
    uint64_t mid = left/2 + right/2;
    if (left%2 && right%2) mid++;
    if (x < mid) { return l_child->Get(x, v); }
    else { return r_child->Get(x, v); }
  }

  /// Collect the leaves within range [l, r).
  ///
  std::vector<Segment<T> > Collect(uint64_t l, uint64_t r) {
    std::vector<Segment<T> > res;
    Collect2(l, r, res);
    return res;  
  }

  /// Collect is a wrapper of Collect2.
  ///
  void Collect2(uint64_t l, uint64_t r, std::vector<Segment<T> >& res) {
    if (type != PARTIAL_SEGMENT) {
      res.push_back(Segment<T>(type, value, left, right));
      if (res.size() >= 2) {
        Segment<T>& last = res[res.size() - 1];
        Segment<T>& second_last = res[res.size() - 2];
        if (
          ((second_last.type == EMPTY_SEGMENT && last.type == EMPTY_SEGMENT) ||
          (second_last.type == last.type && second_last.value == last.value))
          && second_last.right == last.left) {
          second_last.right = last.right;
          res.pop_back();
        }
      }
    } else {
      uint64_t mid = left/2 + right/2;
      if (left%2 && right%2) mid++;
      if (l < mid) { l_child->Collect2(l, std::min(r, mid), res); }
      if (r > mid) { r_child->Collect2(std::max(l, mid), r, res); }
    }
  }

  SegmentType type;
  T value;
  uint64_t left, right;
  SegmentTree<T> *l_child, *r_child;
};


#endif // SLIMMER_SEGMENT_TREE_HPP