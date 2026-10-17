#ifndef OVISTEST_H
#define OVISTEST_H

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
/* Structs by value are not supported yet. */
int ovt_point_sum(ovt_point point);
ovt_color ovt_next_color(ovt_color color);

#endif
