//
// Created by root on 3/11/24.
//

#ifndef DAFL_AFL_FUZZ_H
#define DAFL_AFL_FUZZ_H
#include "types.h"
#include "debug.h"
#include "alloc-inl.h"
#include <math.h>
#ifdef USE_GSL
#include <gsl/gsl_rng.h>
#include <gsl/gsl_randist.h>
#endif

// For interval tree: should be power of 2
#define INTERVAL_SIZE 1024
#define MAX_SCHEDULER_NUM 16
#define MAX_QUEUE_U64_SIZE 8192
#define QUEUE_U64_GLOBAL_ENQUEUE_NUM 100

enum selection_strategy {
  SELECT_DAFL,
  SELECT_RANDOM,
  SELECT_CLUSTER,
  SELECT_MAB,
};

struct proximity_score {
  u64 original;
  double adjusted;
  u32 covered;
  u32 *dfg_count_map; // Sparse map: [count]
  u32 *dfg_dense_map; // Dense map: [index, count]
};

struct dfg_node_info {
  u32 idx;
  u32 score;
  u32 max_paths;
};

struct array {
  u64 size;
  u64 *data;
};

struct array *array_create(u64 size) {
  struct array *arr = (struct array *)ck_alloc(sizeof(struct array));
  arr->size = size;
  arr->data = (u64 *)ck_alloc(size * sizeof(u64));
  return arr;
}

void array_free(struct array *arr) {
  ck_free(arr->data);
  ck_free(arr);
}

void array_set(struct array *arr, u64 index, u64 value) {
  if (index >= arr->size) {
    FATAL("Index out of bounds: %llu >= %llu", index, arr->size);
  }
  arr->data[index] = value;
}

u64 array_get(struct array *arr, u64 index) {
  if (index >= arr->size) {
    FATAL("Index out of bounds: %llu >= %llu", index, arr->size);
  }
  return arr->data[index];
}

void array_copy(struct array *dst, u32 *src, u64 size) {
  if (dst->size < size) {
    FATAL("Destination array is too small: %llu < %llu", dst->size, size);
  }
  for (u64 i = 0; i < size; i++) {
    dst->data[i] = (u64)src[i];
  }
}

u64 array_size(struct array *arr) {
  return arr->size;
}

/* Multi-armed bandit stuffs */

struct queue_u64 {
  struct array *data;
  u64 size;
  u64 front;
  u64 rear;
};

struct queue_u64 *queue_u64_create(u64 size) {
  struct queue_u64 *queue = (struct queue_u64 *)ck_alloc(sizeof(struct queue_u64));
  queue->data = array_create(size);
  queue->size = size;
  queue->front = 0;
  queue->rear = 0;
  return queue;
}

void queue_u64_free(struct queue_u64 *queue) {
  array_free(queue->data);
  ck_free(queue);
}

void queue_u64_clear(struct queue_u64 *queue) {
  queue->front = 0;
  queue->rear = 0;
  queue->size = 0;
  memset(queue->data->data, 0, array_size(queue->data) * sizeof(u64));
}

u64 queue_u64_index(struct queue_u64 *queue, u64 index) {
  return (queue->front + index) % array_size(queue->data);
}

u64 queue_u64_dequeue(struct queue_u64 *queue) {
  if (queue->size == 0) {
    return 0;
  }
  u64 value = array_get(queue->data, queue->front);
  queue->front = queue_u64_index(queue, 1);
  queue->size--;
  return value;
}

void queue_u64_enqueue(struct queue_u64 *queue, u64 value) {
  if (queue->size == array_size(queue->data)) {
    queue_u64_dequeue(queue);
  }
  array_set(queue->data, queue->rear, value);
  queue->rear = (queue->rear + 1) % array_size(queue->data);
  queue->size++;
}

u64 queue_u64_size(struct queue_u64 *queue) {
  return queue->size;
}

u64 queue_u64_peek(struct queue_u64 *queue, u64 index) {
  if (queue->size == 0) {
    return 0;
  }
  return array_get(queue->data, queue_u64_index(queue, index));
}

u64 queue_u64_diff(struct queue_u64 *queue, u64 window_size) {
  if (queue->size == 0) {
    return 0;
  }
  window_size = window_size > (queue->size - 1) ? (queue->size - 1) : window_size;
  u64 front = queue_u64_peek(queue, queue->size - window_size - 1);
  u64 rear = queue_u64_peek(queue, queue->size - 1);
  return rear - front;
}

double queue_u64_gradient(struct queue_u64 *queue, u64 window_size) {
  if (queue->size == 0) {
    return 0.0;
  }
  window_size = window_size > (queue->size - 1) ? (queue->size - 1) : window_size;
  u64 diff = queue_u64_diff(queue, window_size);
  return (double)(diff) / (double)window_size;
}

/**
 * Multi-armed bandit (MAB) structure.
 *
 * It contains interesting and total inputs.
 */
struct mut_tracker {
  u32 size;
  u64 inter_num;
  u64 total_num;
  struct array *inter; // Interesting
  struct array *total; // Total
  struct queue_u64 *inter_queue;
  struct queue_u64 *total_queue;
  struct mut_tracker *old;
};

struct beta_dist {
  double alpha;
  double beta;
};

/**
 * Generate a random number from gamma distribution used by beta distribution.
 * 
 * This function using Marsaglia-Tsang method.
 * It is generated by AI.
 */
double gamma_rand(double shape, double scale) {
  if (shape <= 0.0 || scale <= 0.0) {
    fprintf(stderr, "Error: Shape and scale parameters must be positive for gamma distribution.\n");
    exit(1);
  }

  if (shape < 1.0) {
    // Transform shape < 1 to shape >= 1
    return gamma_rand(shape + 1.0, scale) * pow((double)rand() / RAND_MAX, 1.0 / shape);
  } else {
    // Marsaglia-Tsang method for shape >= 1
    double d = shape - 1.0 / 3.0;
    double c = 1.0 / sqrt(9.0 * d);
    double v, u, x;
    do {
      do {
        x = sqrt(-2.0 * log((double)rand() / RAND_MAX)) * 
            cos(2.0 * M_PI * (double)rand() / RAND_MAX); // Standard normal
        v = 1.0 + c * x;
      } while (v <= 0.0);
      v = v * v * v;
      u = (double)rand() / RAND_MAX;
    } while (log(u) > 0.5 * x * x + d * (1.0 - v + log(v)));

    return scale * d * v;
  }
}

/**
 * Samples a random number from beta distribution with Marsaglia-Tsang method.
 * 
 * This function first generates two random numbers from gamma distribution and compute the beta distribution.
 */
double beta_rand_mt(struct beta_dist dist) {
  double x = gamma_rand(dist.alpha, 1.0);
  double y = gamma_rand(dist.beta, 1.0);
  return x / (x + y);
}

#ifdef USE_GSL
/**
 * Samples a random number from beta distribution with GSL library.
 * 
 * This function calls gsl_ran_beta function from GSL library.
 */
double beta_rand_gsl(struct beta_dist dist) {
  const gsl_rng_type * T;
  gsl_rng * r;

  gsl_rng_env_setup();

  T = gsl_rng_default;
  r = gsl_rng_alloc(T);

  double sample = gsl_ran_beta(r, dist.alpha, dist.beta);

  gsl_rng_free(r);

  return sample;
}
#else
double beta_rand_gsl(struct beta_dist dist) {
  return beta_rand_mt(dist);
}
#endif

struct mut_tracker *mut_tracker_create() {
  struct mut_tracker *tracker = (struct mut_tracker *)ck_alloc(sizeof(struct mut_tracker));
  tracker->size = 17;
  tracker->inter = array_create(tracker->size);
  tracker->total = array_create(tracker->size);
  tracker->inter_queue = queue_u64_create(MAX_QUEUE_U64_SIZE);
  tracker->total_queue = queue_u64_create(MAX_QUEUE_U64_SIZE);
  return tracker;
}

void mut_tracker_free(struct mut_tracker *tracker) {
  array_free(tracker->inter);
  array_free(tracker->total);
  queue_u64_free(tracker->inter_queue);
  queue_u64_free(tracker->total_queue);
  ck_free(tracker);
}

void mut_tracker_update(struct mut_tracker *tracker, u32 mut, u32 sel_num, u8 interesting, u32 multiplier) {
  if (mut >= tracker->size) {
    FATAL("Mutation index out of bounds: %u >= %u", mut, tracker->size);
  }
  if (sel_num == 0) return;
  u32 sel_num_adjusted = sel_num * multiplier;
  if (interesting) {
    tracker->inter->data[mut] += sel_num_adjusted;
    // tracker->inter_num += sel_num_adjusted;
  }
  tracker->total->data[mut] += sel_num_adjusted;
  // tracker->total_num += sel_num_adjusted;
}

void mut_tracker_update_num(struct mut_tracker *tracker, u8 is_interesting) {
  if (is_interesting) {
    tracker->inter_num++;
  }
  tracker->total_num++;
}

void mut_tracker_update_queue(struct mut_tracker *tracker) {
  queue_u64_enqueue(tracker->inter_queue, tracker->inter_num);
  queue_u64_enqueue(tracker->total_queue, tracker->total_num);
}

/**
 * Get the beta dist. of the input
 */
struct beta_dist mut_tracker_get(struct mut_tracker *tracker) {
  struct beta_dist dist;
  dist.alpha = (double)(tracker->inter_num + 2);
  dist.beta = (double)(tracker->total_num - tracker->inter_num + 2);
  return dist;
}

/**
 * Get the beta dist. of the mutator
 */
struct beta_dist mut_tracker_get_mut(struct mut_tracker *tracker, u32 mut) {
  struct beta_dist dist;
  dist.alpha = (double)(tracker->inter->data[mut] + 2);
  dist.beta = (double)(tracker->total->data[mut] - tracker->inter->data[mut] + 2);
  return dist;
}

double mut_tracker_get_short_term_gradient(struct mut_tracker *tracker, u32 len) {
  if (len == 0) return 0.0;
  if (len > MAX_QUEUE_U64_SIZE) len = MAX_QUEUE_U64_SIZE;
  u64 inter_diff = queue_u64_diff(tracker->inter_queue, len);
  u64 total_diff = queue_u64_diff(tracker->total_queue, len);
  if (total_diff == 0) return 0.0;
  return (double)inter_diff / (double)total_diff;
}

void mut_tracker_reset(struct mut_tracker *tracker) {
  if (tracker->old == NULL) {
    tracker->old = mut_tracker_create();
  }
  // Copy to old tracker
  for (u32 i = 0; i < tracker->size; i++) {
    tracker->old->inter->data[i] += tracker->inter->data[i];
    tracker->old->total->data[i] += tracker->total->data[i];
    tracker->inter->data[i] = 0;
    tracker->total->data[i] = 0;
  }
  tracker->old->inter_num += tracker->inter_num;
  tracker->old->total_num += tracker->total_num;
  // Reset the current tracker
  tracker->inter_num = 0;
  tracker->total_num = 0;
  queue_u64_clear(tracker->inter_queue);
  queue_u64_clear(tracker->total_queue);
}

double beta_mode(struct beta_dist dist) {
  return (dist.alpha - 1.0) / (dist.alpha + dist.beta - 2.0);
}

/**
 * Update the beta distribution.
 * Update the beta value with simple heuristic to prevent the extreme cases (i.e. beta_mode() >= 1.0).
 */
struct beta_dist beta_dist_update(struct beta_dist src, struct beta_dist global) {
  struct beta_dist dist;
  // Adjust the alpha and beta values based on update
  dist.alpha = src.alpha;
  dist.beta = ((src.beta - 2) * global.alpha / global.beta) + 2;
  return dist;
}

struct queue_entry {

  u8* fname;                          /* File name for the test case      */
  u32 len;                            /* Input length                     */

  u8  cal_failed,                     /* Calibration failed?              */
      trim_done,                      /* Trimmed?                         */
      was_fuzzed,                     /* Had any fuzzing done yet?        */
      handled_in_cycle,               /* Was handled in current cycle?    */
      passed_det,                     /* Deterministic stages passed?     */
      has_new_cov,                    /* Triggers new coverage?           */
      var_behavior,                   /* Variable behavior?               */
      favored,                        /* Currently favored?               */
      fs_redundant;                   /* Marked as redundant in the fs?   */

  u32 bitmap_size,                    /* Number of bits set in bitmap     */
      exec_cksum;                     /* Checksum of the execution trace  */

  u64 prox_score;                     /* Proximity score of the test case */
  u32 entry_id;                       /* The ID assigned to the test case */

  u64 exec_us,                        /* Execution time (us)              */
      handicap,                       /* Number of queue cycles behind    */
      depth;                          /* Path depth                       */

  u8* trace_mini;                     /* Trace bytes, if kept             */
  u32 tc_ref;                         /* Trace bytes ref count            */

  // CLUDAFL
  u32 input_hash;
  u32 dfg_hash;
  u32 dfg_max;
  struct array *dfg_arr;
  struct mut_tracker *mut_tracker;

  struct queue_entry *next;           /* Next element, if any             */
};

struct list_entry {
  void *data;
  struct list_entry *prev;
  struct list_entry *next;
};

struct list {
  u32 size;
  struct list_entry *head;
  struct list_entry *tail;
};

struct list_entry* list_entry_create(void *data) {
  struct list_entry *entry = (struct list_entry *)ck_alloc(sizeof(struct list_entry));
  entry->data = data;
  entry->prev = NULL;
  entry->next = NULL;
  return entry;
}

struct list* list_create(void) {
  struct list *list = (struct list *)ck_alloc(sizeof(struct list));
  list->head = NULL;
  list->tail = NULL;
  list->size = 0;
  return list;
}

struct list_entry* list_insert_back(struct list *list, void *data) {
  struct list_entry *entry = list_entry_create(data);
  if (list->size == 0) {
    list->head = entry;
    list->tail = entry;
  } else {
    list->tail->next = entry;
    entry->prev = list->tail;
    list->tail = entry;
  }
  list->size++;
  return entry;
}

struct list_entry* list_insert_front(struct list *list, void *data) {
  struct list_entry *entry = list_entry_create(data);
  if (list->size == 0) {
    list->head = entry;
    list->tail = entry;
  } else {
    list->head->prev = entry;
    entry->next = list->head;
    list->head = entry;
  }
  list->size++;
  return entry;
}

struct list_entry* list_insert_left(struct list *list, struct list_entry *entry_next, void *data) {
  if (entry_next == NULL) { // Insert at the front
    return list_insert_front(list, data);
  }
  struct list_entry *entry = list_entry_create(data);
  entry->prev = entry_next->prev;
  entry->next = entry_next;
  entry_next->prev = entry;
  if (entry->prev != NULL) {
    entry->prev->next = entry;
  } else {
    list->head = entry;
  }
  list->size++;
  return entry;
}


struct list_entry* list_insert_right(struct list *list, struct list_entry *entry_prev, void *data) {
  if (entry_prev == NULL) { // Insert at the back
    return list_insert_back(list, data);
  }
  struct list_entry *entry = list_entry_create(data);
  entry->prev = entry_prev;
  entry->next = entry_prev->next;
  entry_prev->next = entry;
  if (entry->next != NULL) {
    entry->next->prev = entry;
  } else {
    list->tail = entry;
  }
  list->size++;
  return entry;
}

void list_remove(struct list *list, struct list_entry *entry) {
  if (entry->prev != NULL) {
    entry->prev->next = entry->next;
  } else {
    list->head = entry->next;
  }
  if (entry->next != NULL) {
    entry->next->prev = entry->prev;
  } else {
    list->tail = entry->prev;
  }
  ck_free(entry);
  list->size--;
}

struct list_entry* list_get(struct list *list, void *data) {
  struct list_entry *entry = list->head;
  while (entry != NULL) {
    if (entry->data == data) {
      return entry;
    }
    entry = entry->next;
  }
  return NULL;
}

struct list_entry* list_get_head(struct list *list) {
  return list->head;
}

u32 list_size(struct list *list) {
  if (list == NULL) return 0;
  return list->size;
}

void list_free(struct list *list) {
  struct list_entry *entry = list->head;
  while (entry != NULL) {
    struct list_entry *next = entry->next;
    ck_free(entry);
    entry = next;
  }
  ck_free(list);
}

// Define the vector structure
struct vector {
  void **data;
  size_t size;     // Number of elements currently in the vector
  size_t capacity; // Capacity of the vector (allocated memory size)
};

// Function to initialize a new vector
struct vector* vector_create(void) {
  struct vector* vec = (struct vector *)ck_alloc(sizeof(struct vector));
  if (vec == NULL) {
    printf("Memory allocation failed.\n");
    exit(EXIT_FAILURE);
  }
  vec->size = 0;
  vec->capacity = 0;
  vec->data = NULL;
  return vec;
}

struct vector* vector_clone(struct vector *vec) {
  struct vector *new_vec = vector_create();
  if (vec->size == 0) return new_vec;
  new_vec->size = vec->size;
  new_vec->capacity = vec->size + 1;
  new_vec->data = (void**)ck_alloc(new_vec->capacity * sizeof(void *));
  memcpy(new_vec->data, vec->data, vec->size * sizeof(void *));
  return new_vec;
}

void vector_clear(struct vector *vec) {
  vec->size = 0;
  memset(vec->data, 0, vec->capacity * sizeof(void *));
}

void vector_reduce(struct vector *vec) {
  size_t new_index = 0;
  for (u32 i = 0; i < vec->size; i++) {
    if (vec->data[i] != NULL) {
      vec->data[new_index] = vec->data[i];
      new_index++;
    }
  }
  vec->size = new_index;
}

// Function to add an element to the end of the vector
void push_back(struct vector* vec, void* element) {
  if (vec->size >= vec->capacity) {
    // Increase capacity by doubling it
    vec->capacity = (vec->capacity == 0) ? 8 : vec->capacity * 2;
    vec->data = (void **)ck_realloc(vec->data, vec->capacity * sizeof(void *));
    if (vec->data == NULL) {
      printf("Memory allocation failed.\n");
      exit(EXIT_FAILURE);
    }
  }
  vec->data[vec->size++] = element;
}

void vector_push_front(struct vector *vec, void *element) {
  push_back(vec, element);
  for (u32 i = vec->size - 1; i > 0; i--) {
    vec->data[i] = vec->data[i - 1];
  }
  vec->data[0] = element;
}

void *vector_pop_back(struct vector *vec) {
  if (vec->size == 0) return NULL;
  void *entry = vec->data[vec->size - 1];
  vec->size--;
  vec->data[vec->size] = NULL;
  return entry;
}

void *vector_pop(struct vector *vec, u32 index) {
  if (index >= vec->size) return NULL;
  if (index == vec->size - 1) return vector_pop_back(vec);
  void *entry = vec->data[index];
  for (u32 i = index; i < vec->size - 1; i++) {
    vec->data[i] = vec->data[i + 1];
  }
  vec->size--;
  return entry;
}

void* vector_pop_front(struct vector *vec) {
  return vector_pop(vec, 0);
}

void vector_free(struct vector* vec) {
  ck_free(vec->data);
  ck_free(vec);
}

void* vector_get(struct vector* vec, u32 index) {
  if (index >= vec->size) {
    return NULL;
  }
  return vec->data[index];
}

void vector_set(struct vector* vec, u32 index, void* element) {
  if (index < vec->size) {
    vec->data[index] = element;
  }
}

u32 vector_size(struct vector* vec) {
  return vec->size;
}

// Hashmap
struct key_value_pair {
  u32 key;
  void* value;
  struct key_value_pair* next;
};

struct hashmap {
  u32 size;
  u32 table_size;
  struct key_value_pair** table;
};

struct hashmap* hashmap_create(u32 table_size) {
  struct hashmap* map = (struct hashmap *)ck_alloc(sizeof(struct hashmap));
  if (map == NULL) {
    printf("Memory allocation failed.\n");
    exit(EXIT_FAILURE);
  }
  map->size = 0;
  map->table_size = table_size;
  map->table = (struct key_value_pair**)ck_alloc(table_size * sizeof(struct key_value_pair*));
  if (map->table == NULL) {
    printf("Memory allocation failed.\n");
    exit(EXIT_FAILURE);
  }
  for (u32 i = 0; i < table_size; i++) {
    map->table[i] = NULL;
  }
  return map;
}

static u32 hashmap_fit(u32 key, u32 table_size) {
  return key % table_size;
}

static void hashmap_resize(struct hashmap *map) {

  u32 new_table_size = map->table_size * 2;
  struct key_value_pair **new_table = (struct key_value_pair**)ck_alloc(new_table_size * sizeof(struct key_value_pair*));
  if (new_table == NULL) {
    printf("Memory allocation failed.\n");
    exit(EXIT_FAILURE);
  }
  for (int i = 0; i < map->table_size; i++) {
    struct key_value_pair* pair = map->table[i];
    while (pair != NULL) {
      struct key_value_pair *next = pair->next;
      u32 index = hashmap_fit(pair->key, new_table_size);
      pair->next = new_table[index];
      new_table[index] = pair;
      pair = next;
    }
  }
  ck_free(map->table);
  map->table = new_table;
  map->table_size = new_table_size;

}

u32 hashmap_size(struct hashmap* map) {
  return map->size;
}

// Function to insert a key-value pair into the hash map
void hashmap_insert(struct hashmap* map, u32 key, void* value) {
  u32 index = hashmap_fit(key, map->table_size);
  struct key_value_pair* newPair = (struct key_value_pair*)ck_alloc(sizeof(struct key_value_pair));
  if (newPair == NULL) {
    printf("Memory allocation failed.\n");
    exit(EXIT_FAILURE);
  }
  newPair->key = key;
  newPair->value = value;
  newPair->next = map->table[index];
  map->table[index] = newPair;
  map->size++;
  if (map->size > map->table_size / 2) {
    hashmap_resize(map);
  }
}

void hashmap_remove(struct hashmap *map, u32 key) {
  u32 index = hashmap_fit(key, map->table_size);
  struct key_value_pair* pair = map->table[index];
  struct key_value_pair* prev = NULL;
  while (pair != NULL) {
    if (pair->key == key) {
      if (!prev) {
        map->table[index] = pair->next;
      } else {
        prev->next = pair->next;
      }
      map->size--;
      ck_free(pair);
      return;
    }
    prev = pair;
    pair = pair->next;
  }
}

struct key_value_pair* hashmap_get(struct hashmap* map, u32 key) {
  u32 index = hashmap_fit(key, map->table_size);
  struct key_value_pair* pair = map->table[index];
  while (pair != NULL) {
    if (pair->key == key) {
      return pair;
    }
    pair = pair->next;
  }
  return NULL;
}

void hashmap_free(struct hashmap* map) {
  for (u32 i = 0; i < map->table_size; i++) {
    struct key_value_pair* pair = map->table[i];
    while (pair != NULL) {
      struct key_value_pair* next = pair->next;
      ck_free(pair);
      pair = next;
    }
  }
  ck_free(map->table);
  ck_free(map);
}

// Cluster
struct cluster {
  u32 id;
  // struct vector *children;  // vector<struct queue_entry*>
  struct list *cluster_nodes; // list<struct cluster_node*>
  struct list_entry *cur;
  struct list_entry *first_unhandled;
};

struct cluster_node {
  u32 node_id;
  u32 depth;
  struct cluster_node *parent;
  struct hashmap *child_node_map; // hashmap<u32, struct cluster_node*>
};

struct cluster_manager {
  struct vector *clusters; // vector<struct cluster*>
  u32 cur_cluster;
};

void print_list(u32 id, struct list *list) {
  struct list_entry *entry = list->head;
  fprintf(stderr, "Cluster %d: ", id);
  while (entry != NULL) {
    struct queue_entry *q = (struct queue_entry *)entry->data;
    fprintf(stderr, "[id %d, score %lld], ", q->entry_id, q->prox_score);
    entry = entry->next;
  }
  fprintf(stderr, "\n");
}

// Cluster functions
struct cluster *cluster_create(u32 id) {
  struct cluster *new_cluster = (struct cluster *)ck_alloc(sizeof(struct cluster));
  if (!new_cluster) {
    PFATAL("Memory allocation failed");
  }
  new_cluster->id = id;
  new_cluster->cluster_nodes = list_create();
  new_cluster->cur = NULL;
  new_cluster->first_unhandled = NULL;
  return new_cluster;
}

u32 cluster_size(struct cluster *cluster) {
  if (!cluster) return 0;
  return list_size(cluster->cluster_nodes);
}

//Adds a queue_entry to the cluster in a sorted manner.
u32 cluster_add_child(struct cluster *cluster, struct queue_entry *entry) {
  if (!cluster || !entry) return 0; 
  // sorted insertion: larger ones go to the front
  struct list_entry *entry_node = list_get_head(cluster->cluster_nodes);
  struct list_entry *last_added_entry = NULL;
  cluster->first_unhandled = NULL;
  while (entry_node) {
    struct queue_entry *cur_entry = (struct queue_entry*)entry_node->data;
    if (!cluster->first_unhandled && !cur_entry->handled_in_cycle)
      cluster->first_unhandled = entry_node;
    if (cur_entry->prox_score <= entry->prox_score) {
      last_added_entry = list_insert_left(cluster->cluster_nodes, entry_node, entry);
      break;
    }
    entry_node = entry_node->next;
  }
  // If the entry is the smallest (or list is empty), add it to the end
  if (!last_added_entry)
    last_added_entry = list_insert_back(cluster->cluster_nodes, entry);
  // print_list(cluster->id, cluster->cluster_nodes);
  if (!cluster->first_unhandled)
    cluster->first_unhandled = last_added_entry;
  return 1;
}

//Removes a child (queue_entry) from the cluster.
//Returns 1 on success, 0 on failure (e.g., entry not found).
u8 cluster_remove_child(struct cluster *cluster, struct queue_entry *entry) {
  if (!cluster || !entry) return 0;

  struct list_entry *entry_node = list_get(cluster->cluster_nodes, entry);
  if (entry_node) {
    list_remove(cluster->cluster_nodes, entry_node);
    return 1;
  }

  return 0; // Entry not found
}

void cluster_free(struct cluster *cluster) {
  if (!cluster) return;
  list_free(cluster->cluster_nodes);
  ck_free(cluster);
}

/**
 * Select random cluster.
 */
struct cluster *select_cluster_random(struct cluster_manager *manager) {
  if (!manager) return NULL;
  u32 random_index = rand() % vector_size(manager->clusters);
  struct cluster *clu = (struct cluster *)vector_get(manager->clusters, random_index);
  return clu;
}

// Cluster Node functions
struct cluster_node *cluster_node_create(u32 node_id, u32 depth, struct cluster_node *parent) {
  struct cluster_node *new_node = (struct cluster_node *)ck_alloc(sizeof(struct cluster_node));
  if (!new_node) {
    perror("Memory allocation failed");
    exit(EXIT_FAILURE);
  }
  new_node->node_id = node_id;
  new_node->depth = depth;
  new_node->parent = parent;
  new_node->child_node_map = hashmap_create(16); // Initial table size of 16, adjust as needed
  return new_node;
}

void cluster_node_add_child(struct cluster_node *parent_node, struct cluster_node *child_node) {
  if (!parent_node || !child_node) return;
  hashmap_insert(parent_node->child_node_map, child_node->node_id, child_node);
}

void cluster_node_remove_child(struct cluster_node *parent_node, u32 child_node_id) {
  if (!parent_node) return;
  hashmap_remove(parent_node->child_node_map, child_node_id);
}

struct cluster_node *cluster_node_get_child(struct cluster_node *parent_node, u32 child_node_id) {
  if (!parent_node) return NULL;
  struct key_value_pair *pair = hashmap_get(parent_node->child_node_map, child_node_id);
  return pair ? (struct cluster_node *)pair->value : NULL;
}

void cluster_node_free(struct cluster_node *node) {
  if (!node) return;
  hashmap_free(node->child_node_map);
  ck_free(node);
}

// Cluster Manager functions
struct cluster_manager *cluster_manager_create(void) {
  struct cluster_manager *manager = (struct cluster_manager *)ck_alloc(sizeof(struct cluster_manager));
  if (!manager) {
    perror("Memory allocation failed");
    exit(EXIT_FAILURE);
  }
  manager->clusters = vector_create(); // Initial table size, adjust as needed
  return manager;
}

void cluster_manager_add_cluster(struct cluster_manager *manager, struct cluster *cluster) {
  if (!manager || !cluster) return;
  push_back(manager->clusters, cluster);
}

//Removes a cluster from the cluster manager based on its ID. -> Not used
// void cluster_manager_remove_cluster(struct cluster_manager *manager, u32 cluster_id) {
//   if (!manager) return;
//   hashmap_remove(manager->root_cluster_map, cluster_id);
// }

struct cluster *cluster_manager_get_or_add_cluster(struct cluster_manager *manager, u32 cluster_id) {
  if (!manager) return NULL;
  for (u32 i = 0; i < vector_size(manager->clusters); i++) {
    struct cluster *clu = (struct cluster *)vector_get(manager->clusters, i);
    if (clu->id == cluster_id) {
      return clu;
    }
  }
  cluster_manager_add_cluster(manager, cluster_create(cluster_id));
  return (struct cluster *)vector_get(manager->clusters, vector_size(manager->clusters) - 1);
}

struct cluster *cluster_manager_get_cluster(struct cluster_manager *manager, u32 index) {
  if (!manager) return NULL;
  return (struct cluster *)vector_get(manager->clusters, index);
}

void cluster_manager_free(struct cluster_manager *manager) {
  if (!manager) return;

  for (u32 i = 0; i < vector_size(manager->clusters); i++) {
    struct cluster *clu = (struct cluster *)vector_get(manager->clusters, i);
    cluster_free(clu);
  }

  vector_free(manager->clusters);
  ck_free(manager);
}

// Function to get cluster ID - you need to implement this based on your logic
u32 get_cluster_id(struct queue_entry* q) {
  // Replace this with your actual logic to determine the cluster ID
  // based on the queue_entry. This is just a placeholder.
  return q->entry_id % 5;
}

// Example of adding a new entry to a specific cluster
void add_entry_to_cluster(struct cluster_manager *manager, struct queue_entry *entry) {
  u32 cluster_id = get_cluster_id(entry);
  struct cluster *target_cluster = cluster_manager_get_cluster(manager, cluster_id);

  if (target_cluster) {
    cluster_add_child(target_cluster, entry);
  } else {
    // Optionally create a new cluster if it doesn't exist
    struct cluster *new_cluster = cluster_create(cluster_id);
    cluster_add_child(new_cluster, entry);
    cluster_manager_add_cluster(manager, new_cluster);
  }
}

// Function to select a random entry from a cluster's children
struct queue_entry *select_random_entry_from_cluster(struct cluster *cluster) {
  if (!cluster || cluster_size(cluster) == 0) return NULL;
  return (struct queue_entry *)list_get_head(cluster->cluster_nodes)->data;
}

#endif //DAFL_AFL_FUZZ_H
