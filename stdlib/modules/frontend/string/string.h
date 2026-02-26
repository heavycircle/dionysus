#ifndef MODULES_FRONTEND_STRING_STRING_H
#define MODULES_FRONTEND_STRING_STRING_H

#include <stddef.h>

/**
 * TODO
 */
char *strcpy(char *dest, const char *src);

/**
 * TODO
 */
char *strncpy(char *dest, const char *src, size_t count);

/**
 * TODO
 */
char *strcat(char *dest, const char *src);

/**
 * TODO
 */
char *strncat(char *dest, const char *src, size_t count);

/**
 * TODO
 */
size_t strlcat(char *dest, const char *src, size_t count);

/**
 * Compare S1 and S2, returning less than, equal to, or greater than zero if
 * S1 is lexographically less than, equal to, or greater than S2.
 *
 * @param  p1   First string to compare.
 * @param  p2   Second string to compare.
 * @return Difference when comparing two strings.
 */
int strcmp(const char *p1, const char *p2);

/**
 * Compare no more than N characters of S1 and S2, returning less than, equal
 * to, or greater than zero if S1 is lexographically less than, equal to, or
 * greater than S2.
 *
 * @param  p1   First string to compare.
 * @param  p2   Second string to compare.
 * @param  n    Maximum number of bytes to compare.
 * @return Difference when comparing two strings.
 */
int strncmp(const char *p1, const char *p2, size_t n);

/**
 * TODO
 */
size_t strlen(const char *s);

/**
 * TODO
 */
size_t strnlen(const char *s, size_t count);

/**
 * strrchr - Find the last occurrence of a character in a string.
 *
 * @param  s    The string to be searched.
 * @param  c    The character to search for.
 * @return Pointer to the last occurrence of c.
 */
char *strrchr(const char *s, int c);

/**
 * strnchr - Find a character in a length limited string.
 *
 * Note that the %NUL-terminator is considered part of the string, and can
 * be searched for.
 *
 * @param  s        The string to be searched.
 * @param  count    The number of characters to be searched.
 * @param  c        The character to search for.
 * @return Pointer to the found character.
 */
char *strnchr(const char *s, size_t count, int c);

/**
 * strspn - Calculate the length of the initial substring of @s which only contain letters in
 * @accept.
 *
 * @param  s        The string to be searched.
 * @param  accept   The string to search for.
 */
size_t strspn(const char *s, const char *accept);

/**
 * strcspn - Calculate the length of the initial substring of @s which does not contain letters in
 * @reject.
 *
 * @param  s        The string to be searched.
 * @param  reject   The string to avoid.
 */
size_t strcspn(const char *s, const char *reject);

/**
 * strpbrk - Find the first occurrence of a set of characters.
 *
 * @param  cs   The string to be searched.
 * @param  ct   The characters to search for.
 */
char *strpbrk(const char *cs, const char *ct);

/**
 * strsep - Split a string into tokens.
 *
 * strsep() updates @s to point after the token, ready for the next call.
 *
 * It returns empty tokens, too, behaving exactly like the libc function
 * of that name. In fact, it was stolen from glibc2 and de-fancy-fied.
 * Same semantics, slimmer shape. ;)
 *
 * @param  s    The string to be searched.
 * @param  ct   The characters to search for.
 * @return Pointer to the the split string.
 */
char *strsep(char **s, const char *ct);

/**
 * memset - Fill a region of memory with the given value.
 *
 * @param  s        Pointer to the start of the area.
 * @param  c        The byte to fill the area with.
 * @param  count    The size of the area.
 * @return Pointer to original memory.
 */
void *memset(void *s, int c, size_t count);

/**
 * memcpy - Copy one area of memory to another.
 *
 * @param  dest     Where to copy to.
 * @param  src      Where to copy from.
 * @param  count    The size of the area.
 * @return Pointer to destination memory.
 */
void *memcpy(void *dest, const void *src, size_t count);

/**
 * memmove - Copy one area of memory to another.
 *
 * Unlike memcpy(), memmove() copes with overlapping areas.
 *
 * @param  dest     Where to copy to.
 * @param  src      Where to copy from.
 * @param  count    The size of the area.
 * @return Pointer to moved memory.
 */
void *memmove(void *dest, const void *src, size_t count);

/**
 * memcmp - Compare two areas of memory.
 *
 * @param  cs       One area of memory.
 * @param  ct       Another area of memory.
 * @param  count    The size of the area.
 * @return TODO
 */
int memcmp(const void *cs, const void *ct, size_t count);

/**
 * memscan - Find a character in an area of memory.
 *
 * @param  addr The memory area.
 * @param  c    The byte to search for
 * @param  size The size of the area.
 * @return The address of the first occurrence of @c, or 1 byte past
 *         the area if @c is not found.
 */
void *memscan(void *addr, int c, size_t size);

/**
 * strstr - Find the first substring in a %NUL terminated string.
 *
 * @param  s1   The string to be searched.
 * @param  s2   The string to search for.
 * @return The first substring in @s1.
 */
char *strstr(const char *s1, const char *s2);

/**
 * strnstr - Find the first substring in a length-limited string.
 *
 * @param  s1   The string to be searched.
 * @param  s2   The string to search for.
 * @param  len  The maximum number of characters to search.
 * @return The first substring in @s1.
 */
char *strnstr(const char *s1, const char *s2, size_t len);

/**
 * memchr - Find a character in an area of memory.
 *
 * @param  s    The memory area.
 * @param  c    The byte to search for.
 * @param  n    The size of the area.
 * @return The address of the first occurrence of @c, or %NULL if @c
 *         if not found.
 */
void *memchr(const void *s, int c, size_t n);

#endif // MODULES_FRONTEND_STRING_STRING_H
