/******************************************************************************
 * succinct_tree.c
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

/* #include <stdio.h> */
/* #include <stdlib.h> */
/* #include <math.h> */

#include "lookup_tables.h"
#include "binary_trees.h"
#include "succinct_tree.h"
#include "bit_array.h"
#include "util.h"

#include "malloc_count.h"

#define min(a,b) \
  ({ __typeof__ (a) _a = (a); \
      __typeof__ (b) _b = (b); \
    _a < _b ? _a : _b; })

#define max(a,b) \
  ({ __typeof__ (a) _a = (a); \
      __typeof__ (b) _b = (b); \
    _a > _b ? _a : _b; })


/* ASSUMPTIONS:
 * - s = 256 (8 bits) (Following the sdsl/libcds implementations)
 * - k = 2 (Min-max tree will be a binary tree)
 * - Each thread has to process at least one chunk with parentheses (Problem with n <= s)
 */

rmMt* init_rmMt(unsigned long n) {
  rmMt* st = (rmMt*)malloc(sizeof(rmMt));
  st->s = 256;
  st->k = 2;
  st->n = n;
  st->num_chunks = ceil((double)n/st->s);
  st->height = ceil(log(st->num_chunks)/log(st->k)); // heigh = logk(num_chunks), Heigh of the min-max tree
  st->internal_nodes = (pow(st->k,st->height)-1)/(st->k-1); // Number of internal nodes;

  return st;
}

void free_rmMt(rmMt* st) {
  free(st->e_prime);
  free(st->m_prime);
  free(st->M_prime);
  bit_array_free(st->B);
  free(st);
}

void print_rmMt(rmMt* st) {
  fprintf(stderr, "Chunk size: %u\n", st->s);
  fprintf(stderr, "Arity: %u\n", st->k);
  fprintf(stderr, "Number of parentheses: %lu\n", st->n);
  fprintf(stderr, "Number of chunks (leaves): %u\n", st->num_chunks);
  fprintf(stderr, "Height: %u\n", st->height);
  fprintf(stderr, "Number of internal nodes: %u\n", st->internal_nodes);
}

rmMt* st_create(BIT_ARRAY* B, unsigned long n) {
  rmMt* st = init_rmMt(n);
  
  // num_chunks leaves (it does not need internal nodes)
  st->e_prime = (int16_t*)calloc(st->num_chunks,sizeof(int16_t));
  // num_chunks leaves plus internal nodes
  st->m_prime = (int16_t*)calloc(st->num_chunks + st->internal_nodes,sizeof(int16_t));
  // num_chunks leaves plus internal nodes
  st->M_prime = (int16_t*)calloc(st->num_chunks + st->internal_nodes,sizeof(int16_t));
  st->n_prime = (int16_t*)calloc(st->num_chunks + st->internal_nodes,sizeof(int16_t));
  st->B = B;
  
  if(st->s >= n){
    fprintf(stderr, "Error: Input size is smaller or equal than the chunk size (input size: %lu, chunk size: %u)\n", n, st->s);
    exit(0);
  }
  
  /*
   * STEP 2: Computation of arrays e', m', M' and n'
   */
  unsigned int num_threads = __cilkrts_get_nworkers();
  // Each thread works on 'chunks_per_thread' consecutive chunks of B 
  unsigned int chunks_per_thread = ceil((double)st->num_chunks/num_threads);
  
  /*
   * STEP 2.1: Each thread computes the prefix computation in a range of the bit array
   */

  cilk_for(unsigned int thread = 0; thread < num_threads; thread++) {
    unsigned int chunk = 0;
    unsigned chunk_limit; // It is possible that the last thread process less chunks
    
    if((thread == num_threads - 1) && (st->num_chunks%chunks_per_thread != 0))
      chunk_limit = st->num_chunks%chunks_per_thread;
    else
      chunk_limit = chunks_per_thread;

    int16_t min = 0, max = 0, partial_excess = 0;
    
    // Each thread traverses their chunks
    for(chunk = 0; chunk < chunk_limit; chunk++) {
      int16_t num_mins = 1; // Number of occurrences of the minimum value in the chunk
      unsigned int llimit = 0, ulimit = 0;
      
      // Compute the limits of the current chunk
      if(thread == (num_threads - 1) && chunk == (chunk_limit-1) && n % (st->num_chunks * st->s) != 0){
	llimit = thread*chunks_per_thread*st->s+(st->s*chunk);
	ulimit = n;
      }
      else {
	llimit = thread*chunks_per_thread*st->s + (st->s*chunk);
	ulimit = llimit + st->s;
	if(st->n < st->s)
	  ulimit = n;
      }
      
      unsigned int symbol=0;
      
      for(symbol=llimit; symbol<ulimit; symbol++) {
	// Excess computation
	if(bit_array_get_bit(B, symbol) == 0)
	  --partial_excess;
	else
	  ++partial_excess;

	// Minimum computation
	if(symbol==llimit) {
	  min = partial_excess; // By default the minimum value is the first excess value
	  max = partial_excess; // By default the maximum value is the first excess value
	  num_mins = 1;
	}
	else {
	  if(partial_excess < min) {
	    min = partial_excess;
	    num_mins = 1;
	  } else if(partial_excess == min)
	    num_mins++;

	  if(partial_excess > max)
	    max = partial_excess;	  
	}
      }

      st->e_prime[thread*chunks_per_thread+chunk] = partial_excess;
      st->m_prime[st->internal_nodes + thread*chunks_per_thread+chunk] = min;
      st->M_prime[st->internal_nodes + thread*chunks_per_thread+chunk] = max;
      st->n_prime[st->internal_nodes + thread*chunks_per_thread+chunk] = num_mins;
    }
  }

  /*
   * STEP 2.2: Computation of the final prefix computations (desired values)
   */
  
  for(unsigned int thread=1; thread < num_threads-1; thread++) {
    st->e_prime[thread*chunks_per_thread+chunks_per_thread-1] += st->e_prime[(thread-1)*chunks_per_thread+chunks_per_thread-1];
  }  

  cilk_for(unsigned int thread=1; thread < num_threads; thread++) {
    unsigned int chunk = 0;
    unsigned int ul = chunks_per_thread;

    if(thread == num_threads-1)
      ul = st->num_chunks - (num_threads-1)*chunks_per_thread;
    
   /*
     * Note 1: Thread 0 does not need to update their excess values
     * Note 2:Thread 0 does not need to update the minimum value of its first chunk
     */
    for(chunk=0; chunk < ul; chunk++) {
      if((thread == num_threads-1) || (chunk < chunks_per_thread -1))
      	st->e_prime[thread*chunks_per_thread+chunk] += st->e_prime[(thread-1)*chunks_per_thread+chunks_per_thread-1];
      st->m_prime[st->internal_nodes + thread*chunks_per_thread+chunk] += st->e_prime[(thread-1)*chunks_per_thread+chunks_per_thread-1];
      st->M_prime[st->internal_nodes + thread*chunks_per_thread+chunk] += st->e_prime[(thread-1)*chunks_per_thread+chunks_per_thread-1];
      
    }
  }
    
  /*
   * STEP 2.3: Completing the internal nodes of the min-max tree
   */
      
  int p_level = ceil(log(num_threads)/log(st->k)); /* p_level = logk(num_threads), level at which each thread has at least one 
						  subtree to process in parallel */
  unsigned int num_subtrees = pow(st->k,p_level); /* num_subtrees = k^p_level, number of subtrees of the min-max tree 
						 that will be computed in parallel at level p_level.
						 num_subtrees is O(num_threads) */
  
  //unsigned int subtree = 0;

  uint total_chunks = st->internal_nodes + st->num_chunks;
  cilk_for(unsigned int subtree = 0; subtree < num_subtrees; subtree++) {
      for(int lvl = st->height-1; lvl >= p_level; lvl--){ //The current level that is being constructed.
	//Note: The last level (leaves) is already constructed
	unsigned int num_curr_nodes = pow(st->k, lvl-p_level); //Number of nodes at curr_level level that belong to the subtree
      
      for(unsigned int node = 0; node < num_curr_nodes; node++) {
  	unsigned int pos = pow(st->k,lvl)-1 + node + subtree*num_curr_nodes;// Position in the final array of 'node'.
  									    //Note: It should be less than the offset
  	unsigned int lchild = pos*st->k+1, rchild = (pos+1)*st->k; //Range of children of 'node' in the final array
	
  	/* for(unsigned int child = lchild; (child <= rchild) && (child < st->num_chunks); child++) { */
  	for(unsigned int child = lchild; (child <= rchild) && (child <
  	total_chunks); child++) {	  
  	  if(child == lchild){// first time
  	    st->m_prime[pos] = st->m_prime[child];
  	    st->M_prime[pos] = st->M_prime[child];
  	    st->n_prime[pos] = st->n_prime[child];
  	  }
  	  else {
  	    if(st->m_prime[child] < st->m_prime[pos]) {
  	      st->m_prime[pos] = st->m_prime[child];
  	      st->n_prime[pos] = 1;
	    }
	    else if(st->m_prime[child] == st->m_prime[pos])
	      st->n_prime[pos]++;
	    
  	    if(st->M_prime[child] > st->M_prime[pos])
  	      st->M_prime[pos] = st->M_prime[child];
  	  }
  	}
      }
    }
  }
   
  for(int lvl=p_level-1; lvl >= 0 ; lvl--){ // O(num_threads)
    
    unsigned int num_curr_nodes = pow(st->k, lvl); // Number of nodes at curr_level level that belong to the subtree
    unsigned int node = 0, child = 0;
    
    for(node = 0; node < num_curr_nodes; node++) {
      unsigned int pos = (pow(st->k,lvl)-1)/(st->k-1) + node; // Position in the final array of 'node'
      unsigned int lchild = pos*st->k+1, rchild = (pos+1)*st->k; // Range of children of 'node' in the final array
      for(child = lchild; child <= rchild; child++){
	if(st->m_prime[child] == st->M_prime[child])
	  continue;
	
	if(child == lchild) { // first time
	  st->m_prime[pos] = st->m_prime[child];
	  st->M_prime[pos] = st->M_prime[child];
	  st->n_prime[pos] = st->n_prime[child];
	}
	else {
	  if(st->m_prime[child] < st->m_prime[pos]) {
	    st->m_prime[pos] = st->m_prime[child];
  	    st->n_prime[pos] = 1;
	  }
	  else if(st->m_prime[child] == st->m_prime[pos])
	    st->n_prime[pos]++;

	  if(st->M_prime[child] > st->M_prime[pos])
	    st->M_prime[pos] = st->M_prime[child];
	}
      }
    }
  }
  
  /*
   * STEP 3: Computation of all universal tables
   */

  T = create_lookup_tables();

  return st;
}

int32_t sum(rmMt* st, int32_t idx) {

  if(idx >= st->n)
    return -1;
  
  int32_t chk = idx/st->s;
  int32_t excess = 0;

  // Previous chunk
  if(chk)
    excess += st->e_prime[chk-1];
  
  int llimit = chk*st->s;
  int rlimit = (idx/8)*8;

  word_t j=0;
  for(j=llimit; j<rlimit; j+=8) {
#ifdef ARCH64
    int32_t sum_idx = (((st->B)->words[j>>logW]) & (0xFFL<<(j&(word_size-1)))) >> (j&(word_size-1));
#else
    int32_t sum_idx = (((st->B)->words[j>>logW]) & (0xFF<<(j&(word_size-1)))) >> (j&(word_size-1));
#endif

    excess += T->word_sum[sum_idx];
  }

  for(uint i=j; i<=idx; i++)
    excess += 2*bit_array_get_bit(st->B,i)-1;

  return excess;
}

// Check a leaf from left to right
int32_t check_leaf_r(rmMt* st, int32_t i, int32_t d) {
  int end = (i/st->s+1)*st->s;
  int llimit = (((i)+8)/8)*8;
  int rlimit = (end/8)*8;
  int32_t excess = d;
  int32_t output;
  int32_t j = 0;
  
  for(j=i+1; j< min(end, llimit); j++){
    excess += 2*bit_array_get_bit(st->B,j)-1;
    if(excess == d-1)
      return j;
  }

  for(j=llimit; j<rlimit; j+=8) {
    int32_t desired = d - 1 - excess; // desired value must belongs to the range [-8,8]
    
#ifdef ARCH64
    int32_t sum_idx = (((st->B)->words[j>>logW]) & (0xFFL<<(j&(word_size-1)))) >> (j&(word_size-1));
#else
    int32_t sum_idx = (((st->B)->words[j>>logW]) & (0xFF<<(j&(word_size-1)))) >> (j&(word_size-1));
#endif
    
    if (desired >= -8 && desired <= 8) {
    uint16_t ii = (desired+8<<8) + sum_idx;
        
    int8_t x = T->near_fwd_pos[ii];
    if(x < 8)
      return j+x;
  }
    excess += T->word_sum[sum_idx];
  }
  
  for (j=max(llimit,rlimit); j < end; ++j) {
    excess += 2*bit_array_get_bit(st->B,j)-1;
    if (excess == d-1) {
      return j;
    }
  }
  
  return i-1;
}

// Check siblings from left to right
int32_t check_sibling_r(rmMt* st, int32_t i, int32_t d) {
  int llimit = i;
  int rlimit = i+st->s;
  int32_t output;
  int32_t excess = st->e_prime[(i-1)/st->s];
  int32_t j = 0;

  for(j=llimit; j<rlimit; j+=8) {
    int32_t desired = d - 1 - excess; // desired value must belongs to the range [-8,8]  
    
#ifdef ARCH64
    int32_t sum_idx = (((st->B)->words[j>>logW]) & (0xFFL<<(j&(word_size-1)))) >> (j&(word_size-1));
#else
    int32_t sum_idx = (((st->B)->words[j>>logW]) & (0xFF<<(j&(word_size-1)))) >> (j&(word_size-1));
#endif
    
    if (desired >= -8 && desired <= 8) {
      uint16_t ii = (desired+8<<8) + sum_idx;
      
      int8_t x = T->near_fwd_pos[ii];
      
      if(x < 8)
	return j+x;
    }
    excess += T->word_sum[sum_idx];
  }
    
  return i-1;
}

int32_t fwd_search(rmMt* st, int32_t i, int32_t d) {
    // Excess value up to the ith position 
    int32_t target = sum(st, i) + d;
    
    int chunk = i / st->s;
    int32_t output;
    long j;

    // Case 1: Check if the chunk of i contains fwd_search(B, i, target)
    output = check_leaf_r(st, i, target);
    if(output > i)
      return output;
    
    // Case 2: The answer is not in the chunk of i, but it is in its sibling
    // (assuming a binary tree, if i%2==0, then its right sibling is at position i+1)
    if(chunk%2 == 0) { // The current chunk has a right sibling
      // The answer is in the right sibling of the current node
      if(st->m_prime[st->internal_nodes + chunk+1] <= target && target <=
	 st->M_prime[st->internal_nodes+ chunk+1]) { 
	output = check_sibling_r(st, st->s*(chunk+1), target);
	if(output >= st->s*(chunk+1))
	  return output;
      }
    }
  
    // Case 3: It is necessary up and then down in the min-max tree
    long node = parent(chunk + st->internal_nodes); // Initial node
    // Go up the tree
    while (!is_root(node)) {
      if (is_left_child(node)) { // if the node is a left child
	node = right_sibling(node); // choose right sibling
	
	if (st->m_prime[node] <= target-1 && target-1 <= st->M_prime[node])
	  break;
      }
      node = parent(node); // choose parent
    }

    // Go down the tree
    if (!is_root(node)) { // found solution for the query
      while (!is_leaf(node, st)) {
	node = left_child(node); // choose left child
	if (!(st->m_prime[node] <= target-1 && target-1 <= st->M_prime[node])) {
	  node = right_sibling(node); // choose right child == right sibling of the left child
	  if(st->m_prime[node] > target-1 || target-1 > st->M_prime[node]) {
	    return -1;
	  }
	}
      }
      
      chunk = node - st->internal_nodes;
      return check_sibling_r(st, st->s*chunk, target);
    }
    return -1;
}


// Naive implementation of bwd_search
int32_t naive_bwd_search(rmMt* st, int32_t i, int32_t d) {
  int begin = 0;
  int32_t excess = sum(st, i) + d;
  int32_t target = excess;
  int32_t output;
  int32_t j = 0;

  for(j=i; j >= begin; j--) {
    excess += 2*bit_array_get_bit(st->B,j)-1;
    if(excess == target)
      return j;
  }

  return -1;
}


// ToDo: Implement it more efficiently
int32_t bwd_search(rmMt* st, int32_t i, int32_t d) {
  return naive_bwd_search(st,i,d);
}

int32_t find_close(rmMt* st, int32_t i){
  if(bit_array_get_bit(st->B,i) == 0)
    return -1;

  return fwd_search(st, i, 0);
}

int32_t find_open(rmMt* st, int32_t i){
  if(bit_array_get_bit(st->B,i) == 1)
    return -1;

  return bwd_search(st, i, 0);  
}

int32_t rank_0(rmMt* st, int32_t i) {
  // Excess value up to the ith position
  if(i >= st->n)
    i = st->n-1;
    int32_t d = sum(st, i);
  
  return (i+1-d)/2;
}

int32_t rank_1(rmMt* st, int32_t i) {
    // Excess value up to the ith position 
  if(i >= st->n)
    i = st->n-1;
  int32_t d = sum(st, i);
  
  return (i+1+d)/2;
}


int32_t check_chunk(rmMt* st, int32_t i, int32_t d) {
  int llimit = i;
  int rlimit = i+st->s;
  int32_t output;
  int32_t excess = st->e_prime[(i-1)/st->s];
  int32_t j = 0;
   
  for(j=llimit; j<rlimit; j+=8) {
    int32_t desired = d - 1 - excess; // desired value must belongs to the range [-8,8]  
    
#ifdef ARCH64
    int32_t sum_idx = (((st->B)->words[j>>logW]) & (0xFFL<<(j&(word_size-1)))) >> (j&(word_size-1));
#else
    int32_t sum_idx = (((st->B)->words[j>>logW]) & (0xFF<<(j&(word_size-1)))) >> (j&(word_size-1));
#endif
    
    if (desired >= -8 && desired <= 8) {
      uint16_t ii = (desired+8<<8) + sum_idx;
      
      int8_t x = T->near_fwd_pos[ii];
      if(x < 8)
	return j+x;
    }
    excess += T->word_sum[sum_idx];
  } 
    
  return i-1;
}

// ToDo: Implement it more efficiently
int32_t select_0(rmMt* st, int32_t i){

  int32_t j = 0;

  // The answer is after the position 2*i-1
  int32_t excess = sum(st,2*i-1);

  // Note: The answer is not beyond the position 2*i-1+depth_max, where
  // depth_max is the maximal depth (excess) of the input tree
  int32_t llimit = 2*i-1;
  int32_t rlimit = llimit + st->M_prime[0];
  int32_t d = 0;

  for (j=llimit+1; j <=rlimit; ++j,++d) {
    if (excess == d)
      return j-1;
    excess += 2*bit_array_get_bit(st->B,j)-1;
  }

    return -1;
}

// ToDo: Implement it more efficiently
int32_t select_1(rmMt* st, int32_t i){
  int32_t j = 0;

  // Note: The answer is in the range [0,2*i-1] not beyond the position 2*i-1
  int32_t excess = 0;
  int32_t llimit = 0;
  int32_t rlimit = 2*i-1;
  int32_t d = 2*i-1;

  for (j=llimit; j <=rlimit; ++j,--d) {
    excess += 2*bit_array_get_bit(st->B,j)-1;
    if (excess == d)
      return j;
  }

    return -1;
}
