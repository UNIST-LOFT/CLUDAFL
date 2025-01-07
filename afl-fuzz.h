//
// Created by root on 3/11/24.
//

#ifndef DAFL_AFL_FUZZ_H
#define DAFL_AFL_FUZZ_H
#include "types.h"
#include "debug.h"
#include "alloc-inl.h"

// For interval tree: should be power of 2
#define INTERVAL_SIZE 1024
#define MAX_SCHEDULER_NUM 16
#define MAX_QUEUE_U32_SIZE 12

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

  u32 input_hash;
  u32 dfg_hash;

  struct queue_entry *next;           /* Next element, if any             */

};

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

#endif //DAFL_AFL_FUZZ_H
