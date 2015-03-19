#ifndef BINARY_TREES_H
#define BINARY_TREES_H

/* Auxiliar functions for binary trees */
#include "succinct_tree.h"

short is_root(long v) {
  return v==0;
}

// 0: true, 1: false
short is_left_child(long v) {
  if(is_root(v))
    return 0;
  return v%2;
}

short is_right_child(long v) {
  if(is_root(v))
    return 0;
  return !(v%2);
}

long parent(long v) {
  if(is_root(v))
    return 0;
  return (v-1)/2;
}

long left_child(long v) {
  return 2*v+1;
}

long right_child(long v) {
  return 2*v+2;
}

long right_sibling(long v) {
  return ++v;
}

long left_sibling(long v) {
  return --v;
}

long is_leaf(long v) {
  return (v >= (pow(2,heigh)-1));
}

#endif // BINARY_TREES_H
