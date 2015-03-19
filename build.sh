#
# Before to compile, load the environment variables to use Cilk Plus
#

echo "Compiling sequential algorithm ..."
gcc -O2 -DARCH64 -ffast-math -DNOPARALLEL -o seq_st main.c succinct_tree.c lookup_tables.c util.c basic.c bit_array.c -lm -lrt

echo "Compiling parallel algorithm ..."
gcc -O2 -DARCH64 -ffast-math -o par_st -fcilkplus -lcilkrts main.c succinct_tree.c lookup_tables.c util.c basic.c bit_array.c -lrt -lm

echo "Compiling sequential algorithm (Working space) ..."
gcc -O2 -DARCH64 -ffast-math -DNOPARALLEL -DMALLOC_COUNT -o mem_st *.c -lm -lrt -ldl

echo "Done."
