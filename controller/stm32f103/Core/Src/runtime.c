#include <stddef.h>

/* Freestanding build: provide the one C runtime routine emitted by GCC. */
void *memset(void *destination, int value, size_t count)
{
    unsigned char *p = destination;
    while (count--) *p++ = (unsigned char)value;
    return destination;
}
