#define _POSIX_C_SOURCE 200112L
#include "expresso.h"
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>   
#include <string.h>
#include <stdint.h>
#include <time.h>

typedef struct task {
  void (*work)(void *);
  void *data;
  unsigned int weight;
  struct task *next;
}task_t;

typedef struct {
  pthread_t *workers;        
  size_t worker_count;       

  task_t *task_head;         
  task_t *task_tail;        

  task_t **local_head;
  task_t **local_tail;

  size_t tasks_remaining;    

  pthread_mutex_t mutex;     
  pthread_cond_t cond;       
  int running;   
  int shutdown;  
  schedule_t schedule;

  size_t *tasks_executed;   
  size_t tasks_executed_total;

  double time;
}expresso_runtime_t;

static expresso_runtime_t runtime;

static schedule_t parse_schedule_env(const char *s)
{
  if (!s) return SCHEDULE_DYNAMIC;
  if (strcmp(s, "dynamic") == 0)  return SCHEDULE_DYNAMIC;
  if (strcmp(s, "static") == 0)   return SCHEDULE_STATIC;
  if (strcmp(s, "balanced") == 0) return SCHEDULE_BALANCED;
  return SCHEDULE_DYNAMIC;
}

static void queue_push(task_t **head, task_t **tail, task_t *t)
{
  t->next = NULL;
  if (*tail) (*tail)->next = t;
  else       *head = t;
  *tail = t;
}

static task_t *queue_pop(task_t **head, task_t **tail)
{
  task_t *t = *head;
  if (!t) return NULL;
  *head = t->next;
  if (!*head) *tail = NULL;
  t->next = NULL;
  return t;
}


static void *worker_thread(void *arg)
{
  size_t wid = (size_t)(uintptr_t)arg;

  while (1) {
    pthread_mutex_lock(&runtime.mutex);

    while (!runtime.running && !runtime.shutdown) {
      pthread_cond_wait(&runtime.cond, &runtime.mutex);
    }

    if (runtime.shutdown) {
      pthread_mutex_unlock(&runtime.mutex);
      break;
    }

    while (runtime.running && !runtime.shutdown) {
      int empty = 0;
      if (runtime.schedule == SCHEDULE_STATIC || runtime.schedule == SCHEDULE_BALANCED)
          empty = (runtime.local_head[wid] == NULL);
      else
          empty = (runtime.task_head == NULL);

      if (!empty) break;
      pthread_cond_wait(&runtime.cond, &runtime.mutex);
    }

    if (runtime.shutdown) {
      pthread_mutex_unlock(&runtime.mutex);
      break;
    }

    if (!runtime.running) {
      pthread_mutex_unlock(&runtime.mutex);
      continue;
    }

    task_t *task = NULL;

    if (runtime.schedule == SCHEDULE_STATIC || runtime.schedule == SCHEDULE_BALANCED) {
      task = queue_pop(&runtime.local_head[wid], &runtime.local_tail[wid]);
    } else {
      task = queue_pop(&runtime.task_head, &runtime.task_tail);
    }

    if (!task) {
      pthread_mutex_unlock(&runtime.mutex);
      continue;
    }

    pthread_mutex_unlock(&runtime.mutex);

    task->work(task->data);
    free(task);

    pthread_mutex_lock(&runtime.mutex);

    runtime.tasks_remaining--;
    runtime.tasks_executed[wid]++;
    runtime.tasks_executed_total++;

    pthread_cond_broadcast(&runtime.cond);

    pthread_mutex_unlock(&runtime.mutex);
  }

  return NULL;
}

int expresso_initialize() {

  const char *env = getenv("EXPRESSO_WORKER_COUNT");
  size_t workers = 0;

  if (env) {
    workers = (size_t) atoi(env);
  }

  if (workers == 0) {
    long ncpu = sysconf(_SC_NPROCESSORS_ONLN);
    if (ncpu > 0) {
        workers = (size_t) ncpu;
    } else {
        workers = 1;
    }
  }

  runtime.worker_count = workers;
  runtime.tasks_remaining = 0;
  runtime.task_head = NULL;
  runtime.task_tail = NULL;
  runtime.tasks_executed = calloc(runtime.worker_count, sizeof(size_t));
  runtime.tasks_executed_total = 0;

  runtime.schedule = parse_schedule_env(getenv("EXPRESSO_SCHEDULE"));

  pthread_mutex_init(&runtime.mutex, NULL);
  pthread_cond_init(&runtime.cond, NULL);

  runtime.running = 0;
  runtime.shutdown = 0;
  runtime.time = 0.0;

  size_t parallel_workers = (runtime.worker_count > 0) ? (runtime.worker_count - 1) : 0;
  runtime.workers = (parallel_workers > 0) ? calloc(parallel_workers, sizeof(pthread_t)) : NULL;

  runtime.local_head = calloc(runtime.worker_count, sizeof(task_t*));
  runtime.local_tail = calloc(runtime.worker_count, sizeof(task_t*));

  for (size_t i = 0; i < parallel_workers; ++i) {
    pthread_create(&runtime.workers[i], NULL, worker_thread, (void*)(uintptr_t)(i+1));
  }
  
  return 0;
}

size_t expresso_worker_count() { return runtime.worker_count; }


schedule_t expresso_schedule_get()
{
  pthread_mutex_lock(&runtime.mutex);
  schedule_t s = runtime.schedule;
  pthread_mutex_unlock(&runtime.mutex);
  return s;
}


void expresso_schedule_set(schedule_t policy)
{
  pthread_mutex_lock(&runtime.mutex);

  if (runtime.running) {
    pthread_mutex_unlock(&runtime.mutex);
    return;
  }

  runtime.schedule = policy;
  pthread_mutex_unlock(&runtime.mutex);
}

int expresso_task(void (*work)(void *), void *data) {
  return expresso_weighted_task(work, data, 1);
}

int expresso_weighted_task(
  void (* work) (void *), void * data, unsigned int weight
)
{

  if (!work) {
    return -1;
  }

  task_t *task = malloc(sizeof(task_t));
  if (!task) {
    return -1;
  }

  task->work = work;
  task->data = data;
  if(weight == 0)
    task->weight = 1;
  else  
    task->weight = weight;
  task->next = NULL;

  pthread_mutex_lock(&runtime.mutex);

  if (runtime.task_tail) {
    runtime.task_tail->next = task;
  }else {
    runtime.task_head = task;
  }
  runtime.task_tail = task;

  ++runtime.tasks_remaining;

  pthread_mutex_unlock(&runtime.mutex);

  return 0;
}
int expresso_wait()
{
  runtime.time = expresso_wall_time();

  pthread_mutex_lock(&runtime.mutex);
  
  memset(runtime.tasks_executed, 0, runtime.worker_count * sizeof(size_t));
  runtime.tasks_executed_total = 0;

  if (runtime.schedule == SCHEDULE_STATIC) {
    for (size_t i = 0; i < runtime.worker_count; ++i) {
      runtime.local_head[i] = NULL;
      runtime.local_tail[i] = NULL;
    }

    size_t idx = 0;
    task_t *t = runtime.task_head;
    runtime.task_head = runtime.task_tail = NULL;

    while (t) {
      task_t *next = t->next;
      t->next = NULL;

      size_t target = idx % runtime.worker_count; 
      queue_push(&runtime.local_head[target], &runtime.local_tail[target], t);

      idx++;
      t = next;
    }
  }

  if (runtime.schedule == SCHEDULE_BALANCED) {    
  for (size_t i = 0; i < runtime.worker_count; ++i) {
    runtime.local_head[i] = NULL;
    runtime.local_tail[i] = NULL;
  }

  size_t *load = calloc(runtime.worker_count, sizeof(size_t));

  task_t *t = runtime.task_head;
  runtime.task_head = runtime.task_tail = NULL;

  while (t) {
    task_t *next = t->next;
    t->next = NULL;

    size_t best = 0;
    for (size_t i = 1; i < runtime.worker_count; ++i) {
      if (load[i] < load[best]) best = i;
    }

    queue_push(&runtime.local_head[best], &runtime.local_tail[best], t);
    load[best] += t->weight;

    t = next;
  }

    free(load);
  }
  runtime.running = 1;
  pthread_cond_broadcast(&runtime.cond);

  size_t wid = 0;

  while (runtime.tasks_remaining > 0) {

    while (runtime.tasks_remaining > 0) {
      int empty = 0;
      if (runtime.schedule == SCHEDULE_STATIC || runtime.schedule == SCHEDULE_BALANCED)
        empty = (runtime.local_head[wid] == NULL);
      else
        empty = (runtime.task_head == NULL);

      if (!empty) break;
      pthread_cond_wait(&runtime.cond, &runtime.mutex);
    }

    task_t *task = NULL;
    if (runtime.schedule == SCHEDULE_STATIC || runtime.schedule == SCHEDULE_BALANCED)
      task = queue_pop(&runtime.local_head[wid], &runtime.local_tail[wid]);
    else
      task = queue_pop(&runtime.task_head, &runtime.task_tail);

    if (!task) continue;

    pthread_mutex_unlock(&runtime.mutex);

    task->work(task->data);
    free(task);

    pthread_mutex_lock(&runtime.mutex);

    runtime.tasks_remaining--;
    runtime.tasks_executed[wid]++;
    runtime.tasks_executed_total++;
    
    pthread_cond_broadcast(&runtime.cond);
  }

  runtime.running = 0;
  pthread_cond_broadcast(&runtime.cond);

  pthread_mutex_unlock(&runtime.mutex);

  runtime.time = expresso_wall_time() - runtime.time;

  return 0;
}

void expresso_stats()
{
  pthread_mutex_lock(&runtime.mutex);

  printf("ExPResSO stats:\n");
  switch (runtime.schedule)
  {
  case SCHEDULE_DYNAMIC:
    printf("  schedule: SCHEDULE_DYNAMIC\n");
    break;
  case SCHEDULE_STATIC:
    printf("  schedule: SCHEDULE_STATIC\n");
    break;
  case SCHEDULE_BALANCED:
    printf(" schedule: SCHEDULE_BALANCED\n");
    break;
  }
  if(runtime.tasks_executed_total != 0)
    for (size_t i = 0; i < runtime.worker_count; ++i) {
      printf("  worker %zu executed %zu tasks (%4.2f%%)\n", i, runtime.tasks_executed[i], 100 * ((double)runtime.tasks_executed[i] / (double)runtime.tasks_executed_total));
    }
  else
    for (size_t i = 0; i < runtime.worker_count; ++i) {
      printf("  worker %zu executed %zu tasks (0%%)\n", i, runtime.tasks_executed[i]);
    }

  printf("  total executed tasks: %zu\n", runtime.tasks_executed_total);
  
  printf("  execution time: %g\n", runtime.time);

  pthread_mutex_unlock(&runtime.mutex);
}

double expresso_wall_time()
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec + 1e-9 * (double)ts.tv_nsec;
}

int expresso_finalize()
{
  pthread_mutex_lock(&runtime.mutex);
  runtime.shutdown = 1;
  runtime.running = 1; 
  pthread_cond_broadcast(&runtime.cond);
  pthread_mutex_unlock(&runtime.mutex);

  size_t parallel_workers = (runtime.worker_count > 0) ? (runtime.worker_count - 1) : 0;
  for (size_t i = 0; i < parallel_workers; ++i) {
    pthread_join(runtime.workers[i], NULL);
  }

  free(runtime.workers);
  free(runtime.local_head); free(runtime.local_tail);
  free(runtime.tasks_executed);
  runtime.tasks_executed = NULL;
  runtime.local_head = NULL;
  runtime.local_tail = NULL;
  runtime.workers = NULL;

  pthread_mutex_destroy(&runtime.mutex);
  pthread_cond_destroy(&runtime.cond);

  return 0;
}
