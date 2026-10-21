#include <string.h>
#include <zlib.h>

#include "shout.h"

long packed_size(int count) {
  char text[601] = "";
  for (int index = 0; index < count && index < 100; ++index)
    strcat(text, "Ovis! ");
  unsigned char packed[1200];
  uLongf size = sizeof packed;
  if (compress(packed, &size, (const unsigned char *)text, strlen(text)) != Z_OK)
    return -1;
  return (long)size;
}
