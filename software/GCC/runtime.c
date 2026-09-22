/* Minimal bare-metal newlib hooks. There is no semihosting dependency. */
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
extern char _heap_start, _heap_end;
void _init(void) {}
void _fini(void) {}
void *_sbrk(ptrdiff_t increment)
{
    static char *current;
    if (!current) current = &_heap_start;
    uintptr_t start = (uintptr_t)&_heap_start;
    uintptr_t limit = (uintptr_t)&_heap_end;
    uintptr_t here = (uintptr_t)current;
    if ((increment >= 0 && (uintptr_t)increment > limit - here) ||
        (increment < 0 && (uintptr_t)(-(increment + 1)) + 1 > here - start)) {
        errno = ENOMEM;
        return (void *)-1;
    }
    char *previous = current;
    current += increment;
    return previous;
}
