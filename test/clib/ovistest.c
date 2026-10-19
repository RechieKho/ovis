#include "ovistest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct ovt_counter {
  unsigned int value;
};

int ovt_add(int a, int b) { return a + b; }
double ovt_scale(double value, float factor) { return value * factor; }
unsigned char ovt_wrap(unsigned short value) { return (unsigned char)value; }
signed char ovt_negate(signed char value) { return (signed char)-value; }
unsigned int ovt_big(void) { return 4000000000u; }
int ovt_answer(int *out) {
  *out = 42;
  return 0;
}
void ovt_half(double value, double *out) { *out = value / 2; }
const char *ovt_greet(const char *name) {
  static char text[64];
  snprintf(text, sizeof(text), "hello, %s", name);
  return text;
}
ovt_counter *ovt_counter_new(void) { return calloc(1, sizeof(ovt_counter)); }
void ovt_counter_add(ovt_counter *counter, unsigned int amount) {
  counter->value += amount;
}
unsigned int ovt_counter_get(const ovt_counter *counter) {
  return counter->value;
}
void ovt_counter_free(ovt_counter *counter) { free(counter); }
int ovt_counter_open(const char *name, ovt_counter **out) {
  if (strcmp(name, "bad") == 0)
    return OVT_ERROR;
  *out = ovt_counter_new();
  (*out)->value = (unsigned int)strlen(name);
  return 0;
}
int ovt_visit(int (*callback)(int), int value) {
  return callback ? callback(value) : -value;
}
int ovt_point_sum(ovt_point point) { return point.x + point.y; }
ovt_color ovt_next_color(ovt_color color) {
  return color == OVT_RED ? OVT_GREEN : OVT_BLUE;
}
ovt_point *ovt_point_new(int x, int y) {
  ovt_point *point = malloc(sizeof *point);
  point->x = x;
  point->y = y;
  return point;
}
int ovt_point_area(const ovt_point *point) { return point->x * point->y; }
void ovt_box_grow(struct ovt_box *box, int by) {
  for (; box; box = box->next) {
    box->width += by;
    box->corner.x -= by;
  }
}
double ovt_sum(const double *values, int count) {
  double total = 0;
  for (int index = 0; index < count; ++index)
    total += values[index];
  return total;
}
void ovt_fill(unsigned char *bytes, int count) {
  for (int index = 0; index < count; ++index)
    bytes[index] = (unsigned char)(index * 100);
}
int *ovt_squares(int count) {
  int *result = malloc(sizeof(int) * (size_t)count);
  for (int index = 0; index < count; ++index)
    result[index] = index * index;
  return result;
}
