/******************************************************************************
 * succinct_tree.h
 *
 * Parallel construction of succinct trees
 * For more information: http://www.inf.udec.cl/~josefuentes/sea2015/
 *
 ******************************************************************************
 * Copyright (C) 2015 José Fuentes Sepúlveda <jfuentess@udec.cl>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *****************************************************************************/

#ifndef SUCCINCT_TREE_H
#define SUCCINCT_TREE_H

#include "bit_array.h"

#include "lookup_tables.h"

struct rmMt_t {
  unsigned int s; // Chunk size
  unsigned int k; // arity of the min-max tree
  unsigned long n; // number of parentheses
  unsigned int height;
  unsigned int internal_nodes; // Number of internal nodes
#ifdef ARCH64
  unsigned long num_chunks;
#else
  unsigned int num_chunks;
#endif
  int16_t* e_prime; // num_chunks leaves (it does not need internal nodes)
  int16_t* m_prime; // num_chunks leaves plus internal nodes
  int16_t* M_prime; // num_chunks leaves plus internal nodes
  int16_t* n_prime; // num_chunks leaves plus internal nodes

  // Input bitarray
  BIT_ARRAY* B;
};

typedef struct rmMt_t rmMt;

lookup_table *T;
unsigned int height;

/* Construction */

void free_rmMt(rmMt *);
rmMt* st_create(BIT_ARRAY *, unsigned long);

void print_rmMt(rmMt *);

/* Operations */

// It returns the position of the closing parenthesis that matches the opening
// parenthesis at position i. It is defined in the paper of Navarro and Sadakane
int32_t find_close(rmMt *, int32_t);

// It returns the position of the opening parenthesis that matches the closing
// parenthesis at position i. It is defined in the paper of Navarro and Sadakane
int32_t find_open(rmMt *, int32_t);

// Implementation of the primitive operation fwd_search(P,\pi,i,d)
// It is defined in the paper of Navarro and Sadakane
int32_t fwd_search(rmMt *, int32_t, int32_t);

// Implementation of the primitive operation bwd_search(P,\pi,i,d)
// It is defined in the paper of Navarro and Sadakane
int32_t bwd_search(rmMt *, int32_t, int32_t);

// Implementation of the primitive operation sum(P,\pi,i,j)
// It is defined in the paper of Navarro and Sadakane
// It is equivalent to the depth of the ith node or the excess value at ith position
int32_t sum(rmMt *, int32_t);

// Implementation of the operation rank_{0}(P,i)
// It is defined in the paper of Navarro and Sadakane
// To implement it, we use the following corollary:
// rank_{0}(P,i) = (i+1-sum(P,\pi,0,i))/2
int32_t rank_0(rmMt *, int32_t);

// Implementation of the operation rank_{1}(P,i)
// It is defined in the paper of Navarro and Sadakane
// To implement it, we use the following corollary:
// rank_{1}(P,i) = (i+1+sum(P,\pi,0,i))/2
int32_t rank_1(rmMt *, int32_t);

// Implementation of the operation select_{0}(P,i)
// It is defined in the paper of Navarro and Sadakane
// To implement it, we use the following corollary:
// select_{0}(P,i) = min{j|j \ge 0, sum(P,\pi,0,j) = j+1-2i}
int32_t select_0(rmMt *, int32_t);

// Implementation of the operation select_{1}(P,i)
// It is defined in the paper of Navarro and Sadakane
// To implement it, we use the following corollary:
// select_{1}(P,i) = min{j|j \ge 0, sum(P,\pi,0,j) = 2i-j-1}
int32_t select_1(rmMt *, int32_t);

int32_t match(rmMt *, int32_t);

int32_t parent_t(rmMt *, int32_t);

#endif // SUCCINCT_TREE_H
