#ifndef PTRIE_H
#define PTRIE_H

/***
 * The prefix trie enables you to add strings that are tracked by the
 * data-structure, and to autocomplete to get the most-frequently
 * added string that has the query string as a prefix.
 */

/**
 * The ptrie is the main structure that the client uses/sees. Think of
 * this like the class in Java. We might need additional structures
 * that we'll use to implement that ptrie, but the client likely won't
 * use those as part of the API.
 *
 * To understand how object oriented programming works in C, see
 * "Object Orientation: Initialization, Destruction, and Methods" in
 * the class lectures.
 */
struct ptrie;

/**
 * Allocate a new `ptrie` for the client to use.
 *
 * Note that this might return `NULL` in the case that we cannot
 * successfully allocate memory with `malloc`.
 */
struct ptrie *ptrie_allocate(void);

/**
 * Free an existing `ptrie`. This *must* free not just the `struct
 * ptrie`, but also all of the internal data-structure.
 *
 * Arguments:
 *
 * - `@pt` - The ptrie to free.
 */
void ptrie_free(struct ptrie *pt);

/**
 * `ptrie_add` adds a string to the ptrie. If the string has
 * previously been added, increase the count that tracks how many
 * times it was added, so that we can track frequency.
 *
 * Arguments:
 *
 * - `@pt` - The ptrie to add the string into.
 * - `@str` - The string to add into the `pt`. The `str` is *owned* by
 *     the caller, and is only *borrowed* by this function. Thus, if
 *     you want to store the `str` as part of the data-structure,
 *     you'll have to copy it into the data-structure (recall:
 *     `strdup`). See the section on "Memory Ownership" in the
 *     lectures.
 * - `@return` - Return `0` upon successful addition. Return `-1` if
 *     the `str` could not be added due to `malloc` failure, or if the
 *     string has invalid characters (ascii values < 32, see
 *     https://upload.wikimedia.org/wikipedia/commons/1/1b/ASCII-Table-wide.svg).
 */
int ptrie_add(struct ptrie *pt, const char *str);

/**
 * `ptrie_autocomplete` provides an autocompletion for a given string,
 * driven by the frequency of the addition of various strings. It
 * returns the string that has been added the most for which `str` is
 * its prefix. Return a copy of `str` if no such strings have `str` as
 * a prefix. If two strings with an *equal* frequency of addition have
 * prefixes that match the `str`, the one with a lower
 * `ptrie_char2off` value is the one returned (see the helper in
 * `ptrie.c`).
 *
 * An example:
 *
 * ```c
 * struct ptrie *pt = ptrie_allocate();
 * ptrie_add(pt, "he");
 * ptrie_add(pt, "hey");
 * ptrie_add(pt, "hello");
 * ptrie_add(pt, "hello");
 * ptrie_add(pt, "helloworld");
 * assert(strcmp(ptrie_autocomplete(pt, "h"), "hello") == 0);
 * ptrie_add(pt, "hey");
 * ptrie_add(pt, "hey");
 * assert(strcmp(ptrie_autocomplete(pt, "h"), "hey") == 0);
 * ptrie_free(pt);
 * ```
 */
char *ptrie_autocomplete(struct ptrie *pt, const char *str);

/**
 * `ptrie_print` is a utility function that you are *not* required to
 * implement, but that is quite useful for debugging. It is easiest to
 * implement using a pre-order traversal with recursion.
 *
 * Arguments:
 *
 * - `@pt` - The prefix trie to print.
 */
void ptrie_print(struct ptrie *pt);

void ptrie_maxCount(struct ptrie *pt, const char *str, int count);

#endif /* PTRIE_H */
