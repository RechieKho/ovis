#ifndef OVISTEST_H
#define OVISTEST_H

#include <stddef.h>
#include <sys/queue.h>

/* A small C library the Ovis tests import, covering the cases the header
   importer has to handle. */

#define OVT_VERSION 3
#define OVT_ERROR (-1)
#define OVT_SCALE 2.5

typedef enum { OVT_RED, OVT_GREEN = 5, OVT_BLUE } ovt_color;

typedef struct ovt_counter ovt_counter;
typedef struct {
  int x;
  int y;
} ovt_point;

/* Adds two numbers. */
int ovt_add(int a, int b);
/* Multiplies by a float. */
double ovt_scale(double value, float factor);
/* Narrow and unsigned values. */
unsigned char ovt_wrap(unsigned short value);
signed char ovt_negate(signed char value);
unsigned int ovt_big(void);
/* Writes 42 into *out and returns 0. */
int ovt_answer(int *out);
/* Writes half of value into *out. */
void ovt_half(double value, double *out);
/* Greets someone; the result stays valid until the next call. */
const char *ovt_greet(const char *name);
/* Counters are opaque handles. */
ovt_counter *ovt_counter_new(void);
void ovt_counter_add(ovt_counter *counter, unsigned int amount);
unsigned int ovt_counter_get(const ovt_counter *counter);
void ovt_counter_free(ovt_counter *counter);
/* Opens a counter through an output parameter; returns OVT_ERROR on failure. */
int ovt_counter_open(const char *name, ovt_counter **out);
/* Callbacks can only be given null. */
int ovt_visit(int (*callback)(int), int value);
/* Structs by value, through C wrappers. */
int ovt_point_sum(ovt_point point);
ovt_point ovt_point_make(int x, int y);
ovt_color ovt_next_color(ovt_color color);

/* Structs Ovis reads as C records (references.md R2). */
struct ovt_box {
  ovt_point corner;
  int width;
  char label[8];
  double scale;
  unsigned char flags;
  struct ovt_box *next;
};
/* A point made by C: the caller frees it. */
ovt_point *ovt_point_new(int x, int y);
int ovt_point_area(const ovt_point *point);
/* A function-like macro that is one call. */
#define ovt_twice(x) ovt_add((x), (x))
#define ovt_hello() ovt_greet("macro")
/* Generic C macros (generics.md §9): each call is a copy for its
   arguments' C types. */
#define OVT_MAX(a, b) ((a) > (b) ? (a) : (b))
#define OVT_SQUARE(x) ((x) * (x))
#define OVT_FIRST_CHAR(s) ((s)[0])
#define OVT_IS_EMPTY(s) ((s)[0] == 0)
#define OVT_CLAMP(x, lo, hi) ((x) < (lo) ? (lo) : (x) > (hi) ? (hi) : (x))
#define OVT_ROTL32(x, n) (((x) << (n)) | ((x) >> (32 - (n))))
/* Statements, setting what it is given: called with addresses. */
#define OVT_SWAP(a, b) do { long long t = (a); (a) = (b); (b) = t; } while (0)
/* Types and fields as arguments. */
#define OVT_ZERO_OF(T) ((T)0)
#define OVT_OFFSET_OF(type, member) offsetof(type, member)
#define OVT_CONTAINER_OF(p, type, member) \
  ((type *)((char *)(p) - offsetof(type, member)))
/* Any number of arguments. */
#define OVT_COUNT(...) (sizeof((int[]){__VA_ARGS__}) / sizeof(int))
/* Picks a function by its argument's C type, as tgmath.h's do. */
float ovt_halvef(float x);
double ovt_halve(double x);
#define OVT_HALF(x) _Generic((x), float: ovt_halvef, default: ovt_halve)(x)
/* Builds tokens: left out. */
#define OVT_NAME(x) ovt_##x

/* A queue of items made with sys/queue.h's macros. */
struct ovt_item {
  int value;
  TAILQ_ENTRY(ovt_item) link;
};
TAILQ_HEAD(ovt_items, ovt_item);
/* A queue of `count` items valued 1 to count. */
struct ovt_items *ovt_items_of(int count);

/* Arrays (references.md R3). */
double ovt_sum(const double *values, int count);
void ovt_fill(unsigned char *bytes, int count);
/* count squares, made by C: the caller frees them. */
int *ovt_squares(int count);
/* Grows a box and every box after it. */
void ovt_box_grow(struct ovt_box *box, int by);

#endif
