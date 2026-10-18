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
void ovis_print_str(const char *value) { fputs(value ? value : "null", stdout); }
void ovis_print_space(void) { putchar(' '); }
void ovis_print_newline(void) { putchar('\n'); }

void ovis_print_quoted(const char *value) {
  printf("\"%s\"", value ? value : "null");
}

void *ovis_record_new(int64_t size) {
  void *record = calloc(1, (size_t)size);
  if (!record)
    ovis_fail("out of memory");
  return record;
}

void ovis_record_check(void *record) {
  if (!record)
    ovis_fail("a record was used before it was given a value");
}

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

/* After a number, the spaces and the line ending after it, so a line read
   next is the next line; a number further along the line stays. */
static void ovis_skip_line_end(void) {
  int character;
  while ((character = getchar()) == ' ' || character == '\t' ||
         character == '\r') {
  }
  if (character != '\n' && character != EOF)
    ungetc(character, stdin);
}

int64_t ovis_read_int(void) {
  long long value;
  fflush(stdout);
  if (scanf("%lld", &value) != 1)
    ovis_fail("expected a whole number as input");
  ovis_skip_line_end();
  return value;
}

double ovis_read_float(void) {
  double value;
  fflush(stdout);
  if (scanf("%lf", &value) != 1)
    ovis_fail("expected a number as input");
  ovis_skip_line_end();
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
  if (!left || !right)
    return left == right;
  return strcmp(left, right) == 0;
}

static char *ovis_alloc_text(size_t length) {
  char *text = malloc(length + 1);
  if (!text)
    ovis_fail("out of memory");
  text[length] = '\0';
  return text;
}

char *ovis_concat(const char *left, const char *right) {
  left = left ? left : "null";
  right = right ? right : "null";
  size_t left_length = strlen(left), right_length = strlen(right);
  char *text = ovis_alloc_text(left_length + right_length);
  memcpy(text, left, left_length);
  memcpy(text + left_length, right, right_length);
  return text;
}

char *ovis_int_to_str(int64_t value) {
  char *text = ovis_alloc_text(24);
  snprintf(text, 25, "%lld", (long long)value);
  return text;
}

char *ovis_float_to_str(double value) {
  char *text = ovis_alloc_text(32);
  if (value == floor(value) && fabs(value) < 1e15)
    snprintf(text, 33, "%.1f", value);
  else
    snprintf(text, 33, "%.15g", value);
  return text;
}

char *ovis_bool_to_str(int32_t value) { return value ? "true" : "false"; }

char *ovis_substring(const char *text, int64_t start, int64_t count) {
  int64_t length = text ? (int64_t)strlen(text) : 0;
  if (start < 0)
    start = 0;
  if (start > length)
    start = length;
  if (count < 0)
    count = 0;
  if (count > length - start)
    count = length - start;
  char *result = ovis_alloc_text((size_t)count);
  if (count)
    memcpy(result, text + start, (size_t)count);
  return result;
}

int64_t ovis_char_at(const char *text, int64_t index) {
  int64_t length = text ? (int64_t)strlen(text) : 0;
  if (index < 0 || index >= length) {
    fflush(stdout);
    fprintf(stderr,
            "ovis: runtime error: position %lld is outside the text "
            "(it has %lld characters, the first one is at position 0)\n",
            (long long)index, (long long)length);
    exit(1);
  }
  return (unsigned char)text[index];
}

char *ovis_chr(int64_t code) {
  char *text = ovis_alloc_text(1);
  text[0] = (char)code;
  return text;
}

char *ovis_buffer(int64_t size) {
  if (size < 1)
    ovis_fail("a buffer needs room for at least one character");
  char *text = calloc((size_t)size, 1);
  if (!text)
    ovis_fail("out of memory");
  return text;
}

/* Reads one line without its line break, or returns null at the end. */
char *ovis_read_line(void) {
  fflush(stdout);
  size_t capacity = 64, length = 0;
  char *text = malloc(capacity);
  int character;
  while ((character = getchar()) != EOF && character != '\n') {
    if (length + 1 == capacity)
      text = realloc(text, capacity *= 2);
    text[length++] = (char)character;
  }
  if (character == EOF && length == 0) {
    free(text);
    return NULL;
  }
  if (length && text[length - 1] == '\r')
    --length;
  text[length] = '\0';
  return text;
}

static int ovis_compare_int(const void *left, const void *right) {
  int64_t a = *(const int64_t *)left, b = *(const int64_t *)right;
  return (a > b) - (a < b);
}
static int ovis_compare_float(const void *left, const void *right) {
  double a = *(const double *)left, b = *(const double *)right;
  return (a > b) - (a < b);
}
static int ovis_compare_str(const void *left, const void *right) {
  const char *a = *(const char *const *)left, *b = *(const char *const *)right;
  return strcmp(a ? a : "", b ? b : "");
}

/* Sorts from smallest to largest; kind is 0 int, 1 float, 3 str. */
void ovis_list_sort(void *pointer, int32_t kind) {
  ovis_list *list = pointer;
  if (kind == 0)
    qsort(list->data, (size_t)list->length, 8, ovis_compare_int);
  else if (kind == 1)
    qsort(list->data, (size_t)list->length, 8, ovis_compare_float);
  else
    qsort(list->data, (size_t)list->length, 8, ovis_compare_str);
}

void ovis_list_reverse(void *pointer, int64_t element_size) {
  ovis_list *list = pointer;
  char swap[8];
  for (int64_t low = 0, high = list->length - 1; low < high; ++low, --high) {
    memcpy(swap, list->data + low * element_size, (size_t)element_size);
    memcpy(list->data + low * element_size, list->data + high * element_size,
           (size_t)element_size);
    memcpy(list->data + high * element_size, swap, (size_t)element_size);
  }
}

void *ovis_stdin(void) { return stdin; }
void *ovis_stdout(void) { return stdout; }
void *ovis_stderr(void) { return stderr; }
int64_t ovis_rand_max(void) { return RAND_MAX; }
int64_t ovis_clocks_per_sec(void) { return CLOCKS_PER_SEC; }
)runtime";
}

} // namespace ovis::codegen
