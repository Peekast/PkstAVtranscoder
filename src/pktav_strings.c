
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/**
 * Duplicates a given string by allocating a new block of memory and copying the content
 * of the input string into it. If the allocation fails, it returns NULL.
 *
 * @param   s       The input string to duplicate.
 *
 * @return          A pointer to the newly allocated string containing the same content as the input,
 *                  or NULL if the memory allocation fails.
 */
char *pkst_strdup(const char *s) {
    char *d = calloc(1,strlen(s) + 1);   
    if (d == NULL) return NULL;        
    strcpy(d, s);                      
    return d;                          
}

/**
 * Allocates a block of memory of the specified length and initializes it to zero.
 * This function is a thin wrapper around the standard `calloc` function.
 *
 * @param   len     The number of bytes to allocate.
 *
 * @return          A pointer to the newly allocated memory block, or NULL if the memory allocation fails.
 */
void *pkst_alloc(size_t len) {
    return calloc(1, len);
}