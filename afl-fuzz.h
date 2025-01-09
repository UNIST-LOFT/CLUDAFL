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

struct array {
  u32 size;
  u32 *data;
};

struct array *array_create(u32 size) {
  struct array *arr = (struct array *)ck_alloc(sizeof(struct array));
  arr->size = size;
  arr->data = (u32 *)ck_alloc(size * sizeof(u32));
  return arr;
}

void array_free(struct array *arr) {
  ck_free(arr->data);
  ck_free(arr);
}

void array_set(struct array *arr, u32 index, u32 value) {
  if (index >= arr->size) {
    FATAL("Index out of bounds: %u >= %u", index, arr->size);
  }
  arr->data[index] = value;
}

u32 array_get(struct array *arr, u32 index) {
  if (index >= arr->size) {
    FATAL("Index out of bounds: %u >= %u", index, arr->size);
  }
  return arr->data[index];
}

void array_copy(struct array *dst, u32 *src, u32 size) {
  if (dst->size < size) {
    FATAL("Destination array is too small: %u < %u", dst->size, size);
  }
  memcpy(dst->data, src, size * sizeof(u32));
}

u32 array_size(struct array *arr) {
  return arr->size;
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
  struct array *dfg_arr;

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
    fprintf(stderr, "[id %d, score %d], ", q->entry_id, q->prox_score);
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
  print_list(cluster->id, cluster->cluster_nodes);
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
