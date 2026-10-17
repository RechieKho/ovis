#include <ovis/codegen/runtime.hpp>

namespace ovis::codegen {

// Linked into every compiled program. Kept in C so the only tool needed at
// build time besides Ovis itself is the system C compiler used for linking.
auto runtime_source() -> string_view_type {
  return R"runtime(
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
  int64_t length;
  int64_t capacity;
  char *data;
} ovis_list;

static void ovis_fail(const char *message) {
  fflush(stdout);
  fprintf(stderr, "ovis: runtime error: %s\n", message);
  exit(1);
}

void ovis_init(void) { srand((unsigned)time(NULL)); }

void *ovis_list_new(int64_t element_size, int64_t count) {
  if (count < 0)
    ovis_fail("a list cannot have a negative size");
  ovis_list *list = malloc(sizeof(ovis_list));
  list->length = count;
  list->capacity = count > 4 ? count : 4;
  list->data = calloc((size_t)list->capacity, (size_t)element_size);
  if (!list->data)
    ovis_fail("out of memory");
  return list;
}

int64_t ovis_list_len(void *pointer) { return ((ovis_list *)pointer)->length; }

void *ovis_list_at(void *pointer, int64_t index, int64_t element_size) {
  ovis_list *list = pointer;
  if (index < 0 || index >= list->length) {
    fflush(stdout);
    fprintf(stderr,
            "ovis: runtime error: index %lld is outside the list "
            "(it has %lld items, the first one is at index 0)\n",
            (long long)index, (long long)list->length);
    exit(1);
  }
  return list->data + index * element_size;
}

void *ovis_list_push(void *pointer, int64_t element_size) {
  ovis_list *list = pointer;
  if (list->length == list->capacity) {
    list->capacity *= 2;
    list->data = realloc(list->data, (size_t)(list->capacity * element_size));
    if (!list->data)
      ovis_fail("out of memory");
  }
  return list->data + list->length++ * element_size;
}

void ovis_check_divisor(int64_t divisor) {
  if (divisor == 0)
    ovis_fail("division by zero");
}

void ovis_print_int(int64_t value) { printf("%lld", (long long)value); }

void ovis_print_float(double value) {
  if (value == floor(value) && fabs(value) < 1e15)
    printf("%.1f", value);
  else
    printf("%.15g", value);
}

void ovis_print_bool(int32_t value) { fputs(value ? "true" : "false", stdout); }
void ovis_print_str(const char *value) { fputs(value, stdout); }
void ovis_print_space(void) { putchar(' '); }
void ovis_print_newline(void) { putchar('\n'); }

void ovis_print_list(void *pointer, int32_t kind) {
  ovis_list *list = pointer;
  putchar('[');
  for (int64_t index = 0; index < list->length; ++index) {
    if (index != 0)
      fputs(", ", stdout);
    switch (kind) {
    case 0:
      ovis_print_int(((int64_t *)list->data)[index]);
      break;
    case 1:
      ovis_print_float(((double *)list->data)[index]);
      break;
    case 2:
      ovis_print_bool(((int8_t *)list->data)[index]);
      break;
    default:
      printf("\"%s\"", ((const char **)list->data)[index]);
    }
  }
  putchar(']');
}

int64_t ovis_read_int(void) {
  long long value;
  fflush(stdout);
  if (scanf("%lld", &value) != 1)
    ovis_fail("expected a whole number as input");
  return value;
}

double ovis_read_float(void) {
  double value;
  fflush(stdout);
  if (scanf("%lf", &value) != 1)
    ovis_fail("expected a number as input");
  return value;
}

int64_t ovis_random(int64_t lowest, int64_t highest) {
  if (lowest > highest) {
    int64_t swap = lowest;
    lowest = highest;
    highest = swap;
  }
  return lowest + (int64_t)(rand() % (highest - lowest + 1));
}

int32_t ovis_str_eq(const char *left, const char *right) {
  return strcmp(left, right) == 0;
}
)runtime";
}

} // namespace ovis::codegen
