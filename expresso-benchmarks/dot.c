/**
 * @file dot.c
 * @brief Computes the dot product of two vectors.
 *
 * This is a benchmark program for the ExPResSO library. It has a reference
 * sequential and a room for a parallel implementation using ExPResSO.
 *
 * This program accepts exactly two optional arguments:
 *   1. size of the vectors,
 *   2. number of elements of the vectors processed by each parallelizable task.
 *
 * The second argument is applicable only to the parallel implementation.
 */

#include <stdio.h>
#include <stdlib.h>
#include "expresso.h"

typedef struct {
  double *v1;
  double *v2;
  int start;
  int end;
  double partial_sum;
} dot_task_t;

void dot_task(void *arg) {
  dot_task_t *t = (dot_task_t *) arg;
  double sum = 0.0;

  for(int i = t->start; i < t->end; i++) {
    sum += t->v1[i] * t->v2[i];
  }

  t->partial_sum = sum;
}

/**
 * @fn int main(int argc, char ** argv)
 * @arg argc Number of command-line arguments (including the executable name).
 * @arg argv Values of command-line arguments (including the executable name).
 * @brief Main function.
 */
int main(int argc, char ** argv) {
  int size = 1024, block = 64;
  if(argc == 2) {
    fprintf(stderr, "Missing block size!\n");
    return 1;
  }
  if(argc > 2) {
    size = atoi(argv[1]);
    block = atoi(argv[2]);
  }
  if(block > size) {
    block = size;
  }
  
  double * v1 = (double *) malloc((size_t) size * sizeof(double)),
         * v2 = (double *) malloc((size_t) size * sizeof(double));
  double sum_reference = 0.0, sum_expresso = 0.0;
  double time_reference = 0.0, time_expresso = 0.0;

  // Initialization with random values.
  for(int i = 0; i < size; i++) {
    v1[i] = rand() % 10;
    v2[i] = rand() % 10;
  }

  // Reference sequential implementation.
  time_reference = expresso_wall_time();
  for(int i = 0; i < size; i++) {
    sum_reference += v1[i] * v2[i];
  }
  time_reference = expresso_wall_time() - time_reference;

  // TODO: Parallel implementation using ExPResSO.
  expresso_initialize();
  int ntasks = (size + block - 1) / block;
  dot_task_t *tasks = malloc(ntasks * sizeof(dot_task_t));

  time_expresso = expresso_wall_time();

  for(int t = 0; t < ntasks; t++) {
    int start = t * block;
    int end   = start + block;
    if(end > size) end = size;

    tasks[t].v1 = v1;
    tasks[t].v2 = v2;
    tasks[t].start = start;
    tasks[t].end = end;
    tasks[t].partial_sum = 0.0;

    expresso_task(dot_task, &tasks[t]);
  }

  //expresso_schedule_set(SCHEDULE_STATIC);
  expresso_wait();

  for(int t = 0; t < ntasks; t++) {
    sum_expresso += tasks[t].partial_sum;
  }
  free(tasks);

  time_expresso = expresso_wall_time() - time_expresso;
  expresso_stats();
  expresso_finalize();

  printf("Reference result: %g\n", sum_reference);
  printf(" ExPResSO result: %g\n", sum_expresso);

  // Results check.
  if(sum_reference != sum_expresso) {
    fprintf(stderr, "Bad results :-(\n");
    free(v1); free(v2);
    return 1;
  }

  printf("Reference computation time: %g s\n", time_reference);
  printf(" ExPResSO computation time: %g s\n", time_expresso);

  printf("Good results :-)\n");
  free(v1); free(v2);
  return 0;
}
