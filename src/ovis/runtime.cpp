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

/* Checked builds (the default; `--fast` turns them off) know which source
   line is running, and keep a table of every value Ovis made, so a value
   used after it was freed is an error that names the lines. */
#ifdef OVIS_CHECKED
int64_t ovis_line;
#endif

static void ovis_fail(const char *message) {
  fflush(stdout);
#ifdef OVIS_CHECKED
  if (ovis_line > 0) {
    fprintf(stderr, "ovis: runtime error: %s (line %lld)\n", message,
            (long long)ovis_line);
    exit(1);
  }
#endif
  fprintf(stderr, "ovis: runtime error: %s\n", message);
  exit(1);
}

static void ovis_fail_with(const char *format, long long first, long long second) {
  char message[256];
  snprintf(message, sizeof message, format, first, second);
  ovis_fail(message);
}

/* Memory (proposals/memory.md). The program's memory is malloc; a new memory
   is an arena of chunks that is freed or cleared all at once. Memories form
   a stack: what is made now goes in the top one. A null ovis_memory is the
   program's. Everything allocated is zeroed. */
typedef struct ovis_chunk {
  struct ovis_chunk *next;
  size_t size;
  size_t used;
} ovis_chunk;

typedef struct ovis_memory {
  ovis_chunk *chunks; /* newest first */
  size_t next_size;
  uint32_t id; /* for the checked table: values name the memory they are in */
  struct ovis_memory *next_arena; /* every live arena, for telling values apart */
} ovis_memory;

static ovis_memory *ovis_arenas;
static ovis_memory **ovis_stack;
static size_t ovis_stack_depth, ovis_stack_capacity;

static ovis_memory *ovis_top(void) {
  return ovis_stack_depth ? ovis_stack[ovis_stack_depth - 1] : NULL;
}

/* The memory current outside the innermost `with`. */
static ovis_memory *ovis_outer(void) {
  return ovis_stack_depth > 1 ? ovis_stack[ovis_stack_depth - 2] : NULL;
}

static void *ovis_alloc_raw_in(ovis_memory *memory, size_t size) {
  if (!memory) {
    void *result = calloc(1, size ? size : 1);
    if (!result)
      ovis_fail("out of memory");
    return result;
  }
  size = (size + 15) & ~(size_t)15;
  ovis_chunk *chunk = memory->chunks;
  if (!chunk || chunk->size - chunk->used < size) {
    size_t room = memory->next_size ? memory->next_size : 4096;
    while (room < size)
      room *= 2;
    memory->next_size = room * 2;
    chunk = calloc(1, sizeof(ovis_chunk) + 16 + room);
    if (!chunk)
      ovis_fail("out of memory");
    chunk->size = room;
    chunk->next = memory->chunks;
    memory->chunks = chunk;
  }
  /* The data starts 16 bytes after the header, so it is aligned for any C
     type. */
  void *result = (char *)(chunk + 1) + 16 + chunk->used;
  chunk->used += size;
#ifdef OVIS_CHECKED
  /* Cleared memory is poisoned (below); what is made from it starts empty. */
  memset(result, 0, size);
#endif
  return result;
}

/* What kind of value an allocation is, for messages. */
enum { OVIS_TEXT, OVIS_LIST, OVIS_RECORD, OVIS_OTHER };

#ifdef OVIS_CHECKED
/* ---- the checked table: every value made, by its address ---- */
typedef struct {
  const void *start; /* NULL: empty; 1: a removed entry */
  size_t size;
  int64_t made_line, freed_line;
  uint32_t arena, generation;
  uint8_t kind;
  /* The variable it was last stored in, for messages; NULL if none. */
  const char *name;
} ovis_entry;

static ovis_entry *ovis_table;
static size_t ovis_table_capacity, ovis_table_used;

typedef struct {
  uint32_t generation;
  int alive;
  int64_t freed_line;
} ovis_arena_info;
static ovis_arena_info *ovis_arena_infos;
static uint32_t ovis_arena_count; /* ids start at 1; 0 is the program's memory */

static size_t ovis_slot(const void *start, size_t capacity) {
  return (size_t)(((uint64_t)(uintptr_t)start >> 4) * 0x9e3779b97f4a7c15ULL >> 24) &
         (capacity - 1);
}

static ovis_entry *ovis_find(const void *start) {
  if (!ovis_table_capacity)
    return NULL;
  for (size_t at = ovis_slot(start, ovis_table_capacity); ovis_table[at].start;
       at = (at + 1) & (ovis_table_capacity - 1))
    if (ovis_table[at].start == start)
      return &ovis_table[at];
  return NULL;
}

static void ovis_grow_table(void) {
  ovis_entry *old = ovis_table;
  size_t old_capacity = ovis_table_capacity;
  ovis_table_capacity = old_capacity ? old_capacity * 2 : 1024;
  ovis_table = calloc(ovis_table_capacity, sizeof(ovis_entry));
  if (!ovis_table)
    ovis_fail("out of memory");
  ovis_table_used = 0;
  for (size_t index = 0; index < old_capacity; ++index)
    if (old[index].start > (const void *)1) {
      size_t at = ovis_slot(old[index].start, ovis_table_capacity);
      while (ovis_table[at].start)
        at = (at + 1) & (ovis_table_capacity - 1);
      ovis_table[at] = old[index];
      ++ovis_table_used;
    }
  free(old);
}

static void ovis_register(const void *start, size_t size, uint32_t arena, int kind) {
  if ((ovis_table_used + 1) * 2 > ovis_table_capacity)
    ovis_grow_table();
  size_t at = ovis_slot(start, ovis_table_capacity);
  while (ovis_table[at].start > (const void *)1 && ovis_table[at].start != start)
    at = (at + 1) & (ovis_table_capacity - 1);
  if (ovis_table[at].start != start)
    ++ovis_table_used;
  ovis_table[at] = (ovis_entry){start, size, ovis_line, 0, arena,
                                arena ? ovis_arena_infos[arena].generation : 0,
                                (uint8_t)kind, NULL};
}

static const char *ovis_kind_name(int kind) {
  return kind == OVIS_TEXT ? "text" : kind == OVIS_LIST ? "list"
         : kind == OVIS_RECORD ? "record" : "value";
}

/* "this text" or "the text in 'line'". */
static const char *ovis_described(const ovis_entry *entry) {
  static char text[96];
  if (entry->name)
    snprintf(text, sizeof text, "the %s in '%s'", ovis_kind_name(entry->kind),
             entry->name);
  else
    snprintf(text, sizeof text, "this %s", ovis_kind_name(entry->kind));
  return text;
}

/* Whether a value is still there; says why not, naming the lines. Values the
   table doesn't know (text written in the program, memory C made) are fine. */
static void ovis_check_live(const void *pointer) {
  const ovis_entry *entry = ovis_find(pointer);
  if (!entry)
    return;
  char message[256];
  if (entry->freed_line) {
    snprintf(message, sizeof message,
             "%s was freed on line %lld; it was made on line %lld",
             ovis_described(entry), (long long)entry->freed_line,
             (long long)entry->made_line);
    ovis_fail(message);
  }
  if (entry->arena) {
    const ovis_arena_info *info = &ovis_arena_infos[entry->arena];
    if (!info->alive || info->generation != entry->generation) {
      snprintf(message, sizeof message,
               "%s was made on line %lld in a memory that was freed or "
               "cleared on line %lld",
               ovis_described(entry), (long long)entry->made_line,
               (long long)info->freed_line);
      ovis_fail(message);
    }
  }
}

/* Freed values are poisoned and held back for a while before the real free,
   so a stale pointer is still recognised. */
#define OVIS_DELAY 512
static void *ovis_delayed[OVIS_DELAY];
static size_t ovis_delayed_next;

static void ovis_delay_free(void *pointer) {
  void *old = ovis_delayed[ovis_delayed_next];
  if (old) {
    ovis_entry *entry = ovis_find(old);
    if (entry)
      entry->start = (const void *)1; /* forgotten */
    free(old);
  }
  ovis_delayed[ovis_delayed_next] = pointer;
  ovis_delayed_next = (ovis_delayed_next + 1) % OVIS_DELAY;
}

/* Marks a value freed. Returns whether the table knew it. */
static int ovis_release(void *pointer, int kind) {
  ovis_entry *entry = ovis_find(pointer);
  if (!entry) {
    /* Not the start of anything Ovis made: inside one is a mistake. */
    for (size_t index = 0; index < ovis_table_capacity; ++index) {
      const ovis_entry *other = &ovis_table[index];
      if (other->start > (const void *)1 && !other->freed_line &&
          (const char *)pointer > (const char *)other->start &&
          (const char *)pointer < (const char *)other->start + other->size) {
        char message[200];
        snprintf(message, sizeof message,
                 "free was given a place inside a %s made on line %lld, not "
                 "the value itself",
                 ovis_kind_name(other->kind), (long long)other->made_line);
        ovis_fail(message);
      }
    }
    return 0;
  }
  if (entry->freed_line)
    ovis_fail_with("this was already freed on line %lld (it was made on line %lld)",
                   (long long)entry->freed_line, (long long)entry->made_line);
  (void)kind;
  entry->freed_line = ovis_line ? ovis_line : -1;
  memset(pointer, 0xdd, entry->size);
  if (!entry->arena)
    ovis_delay_free(pointer);
  return 1;
}

/* At exit: what the program's memory still holds, by the line that made it. */
static void ovis_report_leaks(void) {
  size_t lines = 0;
  struct {
    int64_t line;
    size_t count;
  } *report = NULL;
  for (size_t index = 0; index < ovis_table_capacity; ++index) {
    const ovis_entry *entry = &ovis_table[index];
    if (entry->start <= (const void *)1 || entry->arena || entry->freed_line)
      continue;
    size_t at = 0;
    while (at < lines && report[at].line != entry->made_line)
      ++at;
    if (at == lines) {
      report = realloc(report, (lines + 1) * sizeof *report);
      report[lines].line = entry->made_line;
      report[lines].count = 0;
      ++lines;
    }
    ++report[at].count;
  }
  for (size_t first = 0; first < lines; ++first)
    for (size_t second = first + 1; second < lines; ++second)
      if (report[second].line < report[first].line) {
        __typeof__(*report) swap = report[first];
        report[first] = report[second];
        report[second] = swap;
      }
  fflush(stdout);
  for (size_t index = 0; index < lines; ++index)
    fprintf(stderr, "ovis: line %lld made %zu value%s that %s never freed\n",
            (long long)report[index].line, report[index].count,
            report[index].count == 1 ? "" : "s",
            report[index].count == 1 ? "was" : "were");
  free(report);
}
#define OVIS_LIVE(pointer) ovis_check_live(pointer)
#else
#define OVIS_LIVE(pointer) ((void)0)
#endif

/* Allocates and, in a checked build, notes the value in the table. */
static void *ovis_alloc_in_kind(ovis_memory *memory, size_t size, int kind) {
  void *result = ovis_alloc_raw_in(memory, size);
#ifdef OVIS_CHECKED
  ovis_register(result, size ? size : 1, memory ? memory->id : 0, kind);
#else
  (void)kind;
#endif
  return result;
}

static void *ovis_alloc_in(ovis_memory *memory, size_t size) {
  return ovis_alloc_in_kind(memory, size, OVIS_OTHER);
}

void *ovis_alloc(int64_t size) { return ovis_alloc_in(ovis_top(), (size_t)size); }

/* Whether a pointer lies in the arena's chunks. */
static int ovis_in_arena(const ovis_memory *memory, const void *pointer) {
  for (const ovis_chunk *chunk = memory->chunks; chunk; chunk = chunk->next) {
    const char *begin = (const char *)(chunk + 1) + 16;
    if ((const char *)pointer >= begin && (const char *)pointer < begin + chunk->size)
      return 1;
  }
  return 0;
}

static int ovis_in_any_arena(const void *pointer) {
  for (const ovis_memory *memory = ovis_arenas; memory; memory = memory->next_arena)
    if (ovis_in_arena(memory, pointer))
      return 1;
  return 0;
}

void *ovis_memory_new(void) {
  ovis_memory *memory = calloc(1, sizeof(ovis_memory));
  if (!memory)
    ovis_fail("out of memory");
  memory->next_arena = ovis_arenas;
  ovis_arenas = memory;
#ifdef OVIS_CHECKED
  ovis_arena_infos = realloc(ovis_arena_infos,
                             (ovis_arena_count + 2) * sizeof(ovis_arena_info));
  memory->id = ++ovis_arena_count;
  ovis_arena_infos[memory->id] = (ovis_arena_info){1, 1, 0};
#endif
  return memory;
}

static void ovis_memory_release_chunks(ovis_memory *memory, int keep_one) {
  ovis_chunk *chunk = memory->chunks;
  memory->chunks = NULL;
  while (chunk) {
    ovis_chunk *next = chunk->next;
    if (keep_one && !memory->chunks) {
      /* The newest chunk is the biggest: keep it, empty, for reuse. */
      memset((char *)(chunk + 1) + 16,
#ifdef OVIS_CHECKED
             0xdd,
#else
             0,
#endif
             chunk->used);
      chunk->used = 0;
      chunk->next = NULL;
      memory->chunks = chunk;
    } else {
      free(chunk);
    }
    chunk = next;
  }
}

void ovis_memory_clear(void *pointer) {
  if (!pointer)
    ovis_fail("the memory was not made (`new memory`)");
#ifdef OVIS_CHECKED
  ovis_arena_info *info = &ovis_arena_infos[((ovis_memory *)pointer)->id];
  ++info->generation;
  info->freed_line = ovis_line;
#endif
  ovis_memory_release_chunks(pointer, 1);
}

void ovis_memory_free(void *pointer) {
  ovis_memory *memory = pointer;
  if (!memory)
    return;
  for (size_t index = 0; index < ovis_stack_depth; ++index)
    if (ovis_stack[index] == memory)
      ovis_fail("a memory was freed while it is the one in use");
#ifdef OVIS_CHECKED
  ovis_arena_infos[memory->id].alive = 0;
  ovis_arena_infos[memory->id].freed_line = ovis_line;
#endif
  ovis_memory_release_chunks(memory, 0);
  for (ovis_memory **link = &ovis_arenas; *link; link = &(*link)->next_arena)
    if (*link == memory) {
      *link = memory->next_arena;
      break;
    }
  free(memory);
}

void ovis_memory_push(void *memory) {
  if (ovis_stack_depth == ovis_stack_capacity) {
    ovis_stack_capacity = ovis_stack_capacity ? ovis_stack_capacity * 2 : 8;
    ovis_stack = realloc(ovis_stack, ovis_stack_capacity * sizeof(ovis_memory *));
    if (!ovis_stack)
      ovis_fail("out of memory");
  }
  ovis_stack[ovis_stack_depth++] = memory;
}

void ovis_memory_pop(void) {
  if (ovis_stack_depth)
    --ovis_stack_depth;
}

/* The program's string constants are in a section of their own, so `free`
   leaves them alone. */
#if defined(__APPLE__)
extern const char ovis_const_begin[] __asm("section$start$__TEXT$__ovis_const");
extern const char ovis_const_end[] __asm("section$end$__TEXT$__ovis_const");
#else
extern const char __start_ovis_const[] __attribute__((weak));
extern const char __stop_ovis_const[] __attribute__((weak));
#define ovis_const_begin __start_ovis_const
#define ovis_const_end __stop_ovis_const
#endif

static int ovis_is_constant(const void *pointer) {
  return (const char *)pointer >= ovis_const_begin &&
         (const char *)pointer < ovis_const_end;
}

/* Text is freed unless it is a constant or lives in a new memory (which
   frees it with everything else). Anything else was made by Ovis or C and
   goes to free. */
void ovis_free_text(char *text) {
  if (!text || ovis_is_constant(text))
    return;
#ifdef OVIS_CHECKED
  if (ovis_release(text, OVIS_TEXT))
    return;
#endif
  if (ovis_in_any_arena(text))
    return;
  free(text);
}

void ovis_free_record(void *record) {
  if (!record)
    return;
#ifdef OVIS_CHECKED
  if (ovis_release(record, OVIS_RECORD))
    return;
#endif
  if (ovis_in_any_arena(record))
    return;
  free(record);
}

typedef struct {
  int64_t length;
  int64_t capacity;
  char *data;
  ovis_memory *memory; /* where the list and its storage were made */
} ovis_list;

void ovis_free_pointer(void *pointer) {
#ifdef OVIS_CHECKED
  /* A pointer Ovis made is freed as the value it is; others are C's. */
  if (pointer && ovis_find(pointer)) {
    ovis_release(pointer, OVIS_OTHER);
    return;
  }
#endif
  free(pointer);
}

/* A list is freed whole, not its items. */
void ovis_free_list(void *pointer) {
  ovis_list *list = pointer;
  if (!list)
    return;
#ifdef OVIS_CHECKED
  /* The storage isn't in the table: it goes at once, the header is held. */
  const ovis_entry *entry = ovis_find(list);
  if (entry && entry->freed_line)
    ovis_release(list, OVIS_LIST); /* the double free is reported */
  /* A view's items are C's (capacity -1): only the header is Ovis's. */
  if (!list->memory && list->capacity >= 0) {
    free(list->data);
    list->data = NULL;
    list->length = 0;
  }
  if (ovis_release(list, OVIS_LIST))
    return;
#endif
  if (list->memory)
    return;
  if (list->capacity >= 0)
    free(list->data);
  free(list);
}

/* `the 100 int32s at p`: a C array over memory C has (references.md §4).
   Its items are checked against the count; it can't grow. */
void *ovis_list_view(void *data, int64_t count) {
  if (count < 0)
    ovis_fail("an array cannot have a negative size");
  if (!data && count > 0)
    ovis_fail("an array was asked for at a pointer that is nothing");
  ovis_list *list = ovis_alloc_in_kind(ovis_top(), sizeof(ovis_list), OVIS_LIST);
  list->memory = ovis_top();
  list->length = count;
  list->capacity = -1;
  list->data = data;
  return list;
}

/* The items, for C: valid until the list grows. */
void *ovis_list_data(void *pointer) {
  ovis_list *list = pointer;
  OVIS_LIVE(list);
  return list ? list->data : NULL;
}

/* splitmix64: a good 64-bit generator from one number, seeded from the clock
   and the process, so two runs in the same second differ. */
static uint64_t ovis_random_state;

static uint64_t ovis_next_random(void) {
  uint64_t z = (ovis_random_state += 0x9e3779b97f4a7c15ULL);
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
  z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
  return z ^ (z >> 31);
}

void ovis_init(void) {
#if defined(OVIS_CHECKED) && defined(OVIS_REPORT_LEAKS)
  atexit(ovis_report_leaks);
#endif
  srand((unsigned)time(NULL));
  ovis_random_state = ((uint64_t)time(NULL) << 20) ^ (uint64_t)clock() ^
                      ((uint64_t)(uintptr_t)&ovis_random_state);
}

/* `keep`: copies of what the innermost `with` made, in the memory outside it
   (the program's, outside any `with`). Each value is copied once, so sharing
   and cycles survive: the map goes from what was copied to its copy. */
typedef struct {
  const void *from;
  void *to;
} ovis_pair;
static ovis_pair *ovis_pairs;
static size_t ovis_pair_capacity, ovis_pair_count;
static int ovis_keep_depth;

/* `a copy of`: the same walk, but of everything, into the current memory. */
static int ovis_copying;

static ovis_memory *ovis_keep_target(void) {
  return ovis_copying ? ovis_top() : ovis_outer();
}

void ovis_keep_begin(void) {
  if (ovis_keep_depth++ == 0) {
    memset(ovis_pairs, 0, ovis_pair_capacity * sizeof(ovis_pair));
    ovis_pair_count = 0;
  }
}

void ovis_keep_end(void) { --ovis_keep_depth; }

void ovis_copy_begin(void) {
  ovis_keep_begin();
  ovis_copying = 1;
}

void ovis_copy_end(void) {
  ovis_copying = 0;
  ovis_keep_end();
}

static size_t ovis_pair_slot(const void *from, size_t capacity) {
  return (size_t)(((uint64_t)(uintptr_t)from >> 4) * 0x9e3779b97f4a7c15ULL >> 20) &
         (capacity - 1);
}

void *ovis_keep_seen(const void *from) {
  if (!ovis_pair_capacity)
    return NULL;
  for (size_t at = ovis_pair_slot(from, ovis_pair_capacity);
       ovis_pairs[at].from; at = (at + 1) & (ovis_pair_capacity - 1))
    if (ovis_pairs[at].from == from)
      return ovis_pairs[at].to;
  return NULL;
}

static void ovis_keep_note(const void *from, void *to) {
  if ((ovis_pair_count + 1) * 2 > ovis_pair_capacity) {
    ovis_pair *old = ovis_pairs;
    size_t old_capacity = ovis_pair_capacity;
    ovis_pair_capacity = old_capacity ? old_capacity * 2 : 64;
    ovis_pairs = calloc(ovis_pair_capacity, sizeof(ovis_pair));
    if (!ovis_pairs)
      ovis_fail("out of memory");
    for (size_t index = 0; index < old_capacity; ++index)
      if (old[index].from) {
        size_t at = ovis_pair_slot(old[index].from, ovis_pair_capacity);
        while (ovis_pairs[at].from)
          at = (at + 1) & (ovis_pair_capacity - 1);
        ovis_pairs[at] = old[index];
      }
    free(old);
  }
  size_t at = ovis_pair_slot(from, ovis_pair_capacity);
  while (ovis_pairs[at].from)
    at = (at + 1) & (ovis_pair_capacity - 1);
  ovis_pairs[at].from = from;
  ovis_pairs[at].to = to;
  ++ovis_pair_count;
}

/* Whether a value was made in the memory `keep` leaves. */
int32_t ovis_keep_owned(const void *pointer) {
  if (!pointer)
    return 0;
  if (ovis_copying)
    return 1;
  ovis_memory *top = ovis_top();
  if (!top)
    return !ovis_is_constant(pointer);
  return ovis_in_arena(top, pointer);
}

char *ovis_keep_text(char *text) {
  if (!ovis_keep_owned(text))
    return text;
  char *seen = ovis_keep_seen(text);
  if (seen)
    return seen;
  size_t length = strlen(text);
  char *copy = ovis_alloc_in_kind(ovis_keep_target(), length + 1, OVIS_TEXT);
  memcpy(copy, text, length + 1);
  ovis_keep_note(text, copy);
  return copy;
}

void *ovis_keep_copy(void *record, int64_t size) {
  void *copy = ovis_alloc_in_kind(ovis_keep_target(), (size_t)size, OVIS_RECORD);
  memcpy(copy, record, (size_t)size);
  ovis_keep_note(record, copy);
  return copy;
}

void *ovis_keep_list(void *pointer, int64_t element_size) {
  ovis_list *list = pointer;
  if (!ovis_keep_owned(list))
    return list;
  ovis_list *seen = ovis_keep_seen(list);
  if (seen)
    return seen;
  ovis_memory *outer = ovis_keep_target();
  ovis_list *copy = ovis_alloc_in_kind(outer, sizeof(ovis_list), OVIS_LIST);
  copy->memory = outer;
  copy->length = list->length;
  copy->capacity = list->length > 4 ? list->length : 4;
  copy->data = ovis_alloc_raw_in(outer, (size_t)(copy->capacity * element_size));
  memcpy(copy->data, list->data, (size_t)(list->length * element_size));
  ovis_keep_note(list, copy);
  return copy;
}

/* The items of a list of text that was copied are kept too. */
void ovis_keep_list_text(void *original, void *copied) {
  if (original == copied)
    return;
  ovis_list *list = copied;
  for (int64_t index = 0; index < list->length; ++index)
    ((char **)list->data)[index] = ovis_keep_text(((char **)list->data)[index]);
}

void *ovis_list_new(int64_t element_size, int64_t count) {
  if (count < 0)
    ovis_fail("a list cannot have a negative size");
  ovis_memory *memory = ovis_top();
  ovis_list *list = ovis_alloc_in_kind(memory, sizeof(ovis_list), OVIS_LIST);
  list->memory = memory;
  list->length = count;
  list->capacity = count > 4 ? count : 4;
  list->data = ovis_alloc_raw_in(memory, (size_t)(list->capacity * element_size));
  return list;
}

int64_t ovis_list_len(void *pointer) {
  OVIS_LIVE(pointer);
  return ((ovis_list *)pointer)->length;
}

void *ovis_list_at(void *pointer, int64_t index, int64_t element_size) {
  ovis_list *list = pointer;
  OVIS_LIVE(list);
  if (index < 0 || index >= list->length)
    ovis_fail_with("index %lld is outside the list (it has %lld items, the "
                   "first one is at index 0)",
                   (long long)index, (long long)list->length);
  return list->data + index * element_size;
}

void *ovis_list_push(void *pointer, int64_t element_size) {
  ovis_list *list = pointer;
  OVIS_LIVE(list);
  if (list->capacity < 0)
    ovis_fail("an array over C's memory can't grow");
  if (list->length == list->capacity) {
    /* The storage grows in the memory the list was made in. */
    int64_t grown = list->capacity * 2;
    if (list->memory) {
      char *bigger = ovis_alloc_raw_in(list->memory, (size_t)(grown * element_size));
      memcpy(bigger, list->data, (size_t)(list->length * element_size));
      list->data = bigger;
    } else {
      list->data = realloc(list->data, (size_t)(grown * element_size));
      if (!list->data)
        ovis_fail("out of memory");
      /* realloc doesn't zero the new half. */
      memset(list->data + list->capacity * element_size, 0,
             (size_t)((grown - list->capacity) * element_size));
    }
    list->capacity = grown;
  }
  return list->data + list->length++ * element_size;
}

void ovis_overflow(void) {
  ovis_fail("a whole number got too big (or too small) to keep");
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
void ovis_print_str(const char *value) {
  OVIS_LIVE(value);
  fputs(value ? value : "null", stdout);
}
void ovis_print_space(void) { putchar(' '); }
void ovis_print_newline(void) { putchar('\n'); }

void ovis_print_quoted(const char *value) {
  OVIS_LIVE(value);
  printf("\"%s\"", value ? value : "null");
}

void *ovis_record_new(int64_t size) {
  return ovis_alloc_in_kind(ovis_top(), (size_t)size, OVIS_RECORD);
}

/* Checked builds: a value given to a C function must still be there, and a
   value stored in a variable takes its name for messages. */
void ovis_live(void *pointer) {
  if (pointer)
    OVIS_LIVE(pointer);
}

void ovis_name(void *pointer, const char *name) {
#ifdef OVIS_CHECKED
  ovis_entry *entry = pointer ? ovis_find(pointer) : NULL;
  if (entry && !entry->freed_line)
    entry->name = name;
#else
  (void)pointer;
  (void)name;
#endif
}

/* Reading or writing through a pointer: not through null. */
void ovis_pointer_check(void *pointer) {
  if (!pointer)
    ovis_fail("a pointer that is nothing was read or written through");
  OVIS_LIVE(pointer);
}

/* Text into a C struct's `char[N]`: at most N-1 bytes and a NUL. */
void ovis_c_text_set(char *field, int64_t size, const char *text) {
  OVIS_LIVE(text);
  if (size <= 0)
    return;
  size_t length = text ? strlen(text) : 0;
  if (length > (size_t)size - 1)
    length = (size_t)size - 1;
  if (length)
    memcpy(field, text, length);
  field[length] = '\0';
}

void ovis_record_check(void *record) {
  if (!record)
    ovis_fail("a record was used before it was given a value");
  OVIS_LIVE(record);
}

// A value typed as one trait may be asked for another trait's phrase (a
// list of records that are several traits): its table has no entry for a
// record that doesn't implement that trait.
void ovis_method_check(void *method) {
  if (!method)
    ovis_fail("this value does not implement the trait of the phrase "
              "used on it");
}

// `(a) comes before (b)` on two values of a trait: the impl the first picks
// takes the second as its own record. Every record starts with its tag.
void ovis_same_record_check(void *first, void *second) {
  ovis_record_check(second);
  if (*(int64_t *)first != *(int64_t *)second)
    ovis_fail("a phrase of a trait was given two different kinds of record; "
              "it needs two of the same kind");
}

void ovis_print_list(void *pointer, int32_t kind) {
  ovis_list *list = pointer;
  OVIS_LIVE(list);
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
  /* The count of values, as unsigned so the whole range fits (0 means 2^64). */
  uint64_t span = (uint64_t)highest - (uint64_t)lowest + 1;
  uint64_t offset = ovis_next_random();
  if (span != 0) {
    /* Throw away the top few values so every result is equally likely. */
    uint64_t limit = UINT64_MAX - UINT64_MAX % span;
    while (offset >= limit)
      offset = ovis_next_random();
    offset %= span;
  }
  return (int64_t)((uint64_t)lowest + offset);
}

int32_t ovis_str_eq(const char *left, const char *right) {
  OVIS_LIVE(left);
  OVIS_LIVE(right);
  if (!left || !right)
    return left == right;
  return strcmp(left, right) == 0;
}

// Text in dictionary order, for `<`; nothing sorts before any text.
int32_t ovis_str_cmp(const char *left, const char *right) {
  OVIS_LIVE(left);
  OVIS_LIVE(right);
  if (!left || !right)
    return (left != 0) - (right != 0);
  const int result = strcmp(left, right);
  return (result > 0) - (result < 0);
}

static char *ovis_alloc_text(size_t length) {
  char *text = ovis_alloc_in_kind(ovis_top(), length + 1, OVIS_TEXT);
  text[length] = '\0';
  return text;
}

char *ovis_concat(const char *left, const char *right) {
  OVIS_LIVE(left);
  OVIS_LIVE(right);
  left = left ? left : "null";
  right = right ? right : "null";
  size_t left_length = strlen(left), right_length = strlen(right);
  char *text = ovis_alloc_text(left_length + right_length);
  memcpy(text, left, left_length);
  memcpy(text + left_length, right, right_length);
  return text;
}

void ovis_format_int(char *buffer, int64_t value) {
  snprintf(buffer, 32, "%lld", (long long)value);
}

void ovis_format_float(char *buffer, double value) {
  if (value == floor(value) && fabs(value) < 1e15)
    snprintf(buffer, 32, "%.1f", value);
  else
    snprintf(buffer, 32, "%.15g", value);
}

/* A join of any number of parts is one text, made in one allocation. */
char *ovis_join(int64_t count, const char **parts) {
  size_t length = 0;
  for (int64_t index = 0; index < count; ++index) {
    OVIS_LIVE(parts[index]);
    if (!parts[index])
      parts[index] = "null";
    length += strlen(parts[index]);
  }
  char *text = ovis_alloc_text(length);
  size_t at = 0;
  for (int64_t index = 0; index < count; ++index) {
    size_t part_length = strlen(parts[index]);
    memcpy(text + at, parts[index], part_length);
    at += part_length;
  }
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
  OVIS_LIVE(text);
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
  OVIS_LIVE(text);
  int64_t length = text ? (int64_t)strlen(text) : 0;
  if (index < 0 || index >= length)
    ovis_fail_with("position %lld is outside the text (it has %lld "
                   "characters, the first one is at position 0)",
                   (long long)index, (long long)length);
  return (unsigned char)text[index];
}

/* Writes one character into text Ovis made (a buffer): the prelude builds
   text in place with it. Checked builds keep it inside the allocation. */
void ovis_set_char(char *text, int64_t index, int64_t code) {
  OVIS_LIVE(text);
#ifdef OVIS_CHECKED
  const ovis_entry *entry = ovis_find(text);
  if (index < 0 || (entry && (size_t)index >= entry->size))
    ovis_fail("a character was written outside its text");
#endif
  text[index] = (char)code;
}

char *ovis_chr(int64_t code) {
  char *text = ovis_alloc_text(1);
  text[0] = (char)code;
  return text;
}

char *ovis_buffer(int64_t size) {
  if (size < 1)
    ovis_fail("a buffer needs room for at least one character");
  return ovis_alloc_in_kind(ovis_top(), (size_t)size, OVIS_TEXT);
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
  /* Read into a buffer of its own; the text is made where the program is. */
  char *result = ovis_alloc_in_kind(ovis_top(), length + 1, OVIS_TEXT);
  memcpy(result, text, length + 1);
  free(text);
  return result;
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
  OVIS_LIVE(list);
  if (kind == 0)
    qsort(list->data, (size_t)list->length, 8, ovis_compare_int);
  else if (kind == 1)
    qsort(list->data, (size_t)list->length, 8, ovis_compare_float);
  else
    qsort(list->data, (size_t)list->length, 8, ovis_compare_str);
}

/* Takes out the item at index (counting from 0); later items move up. */
void ovis_list_remove(void *pointer, int64_t index, int64_t element_size) {
  ovis_list *list = pointer;
  char *item = ovis_list_at(pointer, index, element_size);
  memmove(item, item + element_size,
          (size_t)((list->length - index - 1) * element_size));
  list->length--;
}

void ovis_list_reverse(void *pointer, int64_t element_size) {
  ovis_list *list = pointer;
  OVIS_LIVE(list);
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
