#include "dynamic_array.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/*
 * Ensure the array has room for one more element.
 * If not, grow the capacity (double it, or start at 8).
 * Returns 1 on success, 0 on allocation failure.
 */
static int da_ensure_capacity(DynamicArray *da) {
  if (!da) return 0;
  if (da->size < da->capacity) return 1;

  size_t new_cap = (da->capacity == 0) ? 8 : da->capacity * 2;
  char **new_data = (char **)realloc(da->data, new_cap * sizeof(char *));
  if (!new_data) {
    perror("realloc");
    return 0;
  }
  da->data = new_data;
  da->capacity = new_cap;
  return 1;
}

/*
 * Create a new dynamic array.
 * Caller can pass an initial capacity, but if it's 0 we pick 8 by default.
 */
DynamicArray* da_create(size_t init_capacity) {
  DynamicArray *da = (DynamicArray *)calloc(1, sizeof(DynamicArray));
  if (!da) {
    perror("malloc");
    return NULL;
  }

  if (init_capacity == 0) init_capacity = 8;

  da->data = (char **)malloc(init_capacity * sizeof(char *));
  if (!da->data) {
    perror("malloc");
    free(da);
    return NULL;
  }

  da->size = 0;
  da->capacity = init_capacity;
  return da;
}

/*
 * Append a new string to the end of the array.
 * We copy the string so the caller doesn't have to keep it alive.
 */
void da_put(DynamicArray *da, const char* val) {
  if (!da) return;
  if (!da_ensure_capacity(da)) return;

  char *copy = NULL;
  if (val) {
    size_t L = strlen(val);
    copy = (char *)malloc(L + 1);
    if (!copy) { perror("malloc"); return; }
    memcpy(copy, val, L + 1);
  }
  da->data[da->size++] = copy; // NULL values are allowed
}

/*
 * Return the string at a given index, or NULL if out of range.
 * The caller must not free or modify this string directly.
 */
char *da_get(DynamicArray *da, const size_t ind) {
  if (!da) return NULL;
  if (ind >= da->size) return NULL;
  return da->data[ind];
}

/*
 * Remove the element at index 'ind'.
 * Frees the string, then shifts everything left to keep the array packed.
 */
void da_delete(DynamicArray *da, const size_t ind) {
  if (!da) return;
  if (ind >= da->size) return;

  if (da->data[ind]) free(da->data[ind]);

  for (size_t i = ind + 1; i < da->size; i++) {
    da->data[i - 1] = da->data[i];
  }
  da->size--;
}

/*
 * Print all elements, one per line.
 * If an element is NULL, print a blank line instead.
 */
void da_print(DynamicArray *da) {
  if (!da) return;
  for (size_t i = 0; i < da->size; i++) {
    printf("%s\n", da->data[i] ? da->data[i] : "");
  }
}

/*
 * Free the entire array and all strings inside it.
 * Safe to call with NULL.
 */
void da_free(DynamicArray *da) {
  if (!da) return;
  if (da->data) {
    for (size_t i = 0; i < da->size; i++) {
      free(da->data[i]);
    }
    free(da->data);
  }
  free(da);
}