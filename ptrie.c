#include "ptrie.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/***
 * The prefix trie enables you to add strings that are tracked by the
 * data-structure, and to autocomplete to get the most-frequently
 * added string that has the query string as a prefix.
 */

struct ptrie_node {
    char* word;
    struct ptrie_array* next;
    int count;
    int max_count;
};

struct ptrie_array {
    struct ptrie_node character[256];
};
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
struct ptrie {
    struct ptrie_array* head;
};


/**
 * Allocate a new `ptrie` for the client to use.
 *
 * Note that this might return `NULL` in the case that we cannot
 * successfully allocate memory with `malloc`.
 */
struct ptrie *ptrie_allocate(void) {
    struct ptrie *pt = (struct ptrie*)calloc(1, sizeof(struct ptrie));
    if (pt == NULL) {
        return NULL;
    }

    pt->head = (struct ptrie_array*)calloc(1, sizeof(struct ptrie_array));

    if (pt->head == NULL) { 
        free(pt);
        return NULL;
    }
    
    return pt;
}

void pArray_free(struct ptrie_array *iterator) {
    if (iterator == NULL) {
        return;
    }

    for (int i = 0; i < 256; i++) {
    
        if(iterator->character[i].next != NULL) {
            //printf("here \n");
            pArray_free(iterator->character[i].next);
            iterator->character[i].next = NULL;
        }
        if (iterator->character[i].word != NULL){
            //printf("the index is %d\n", i);
            //printf("the word is: %s\n", iterator->character[i].word);
            free(iterator->character[i].word);
            iterator->character[i].word = NULL;
           
        }
    }
    //printf("freeing the array\n");
    free(iterator);
}
/**
 * Free an existing `ptrie`. This *must* free not just the `struct
 * ptrie`, but also all of the internal data-structure.
 *
 * Arguments:
 *
 * - `@pt` - The ptrie to free.
 */
void ptrie_free(struct ptrie *pt) {
    if (pt == NULL) {
        return;
    }
    
    
    //struct ptrie_array* iterator = pt->head;

    if(pt->head != NULL){
        pArray_free(pt->head);
    }
    
    
    free(pt);   
}



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
int ptrie_add(struct ptrie *pt, const char *str) {
    if (pt == NULL || str == NULL) {
        return -1;
    }

    struct ptrie_array *iterator = pt->head;
    int symbol = 0; 
    int length = (int)strlen(str);

    for (int i = 0; i < length; i++){
        symbol = (int)str[i];

        if (symbol < 32 || symbol >= 256) {
            return -1;
        }

        //symbol = index of ptriearray

        if (i == length - 1) {
            if (iterator->character[symbol].count == 0) {
                iterator->character[symbol].count++;
                iterator->character[symbol].word = strdup(str);
                ptrie_maxCount(pt, str, iterator->character[symbol].count);
                //printf("the word is added: %s\n", iterator->character[symbol].word);
                //printf("the index is: %d\n", symbol);
                if (iterator->character[symbol].word == NULL) {
                    return -1;
                }
            }
            else {
                iterator->character[symbol].count++;
                //printf("the word is added: %s\n", iterator->character[symbol].word);
                ptrie_maxCount(pt, str, iterator->character[symbol].count);
            }

            

            return 0;
        }
        else {
            if (iterator->character[symbol].next == NULL) {
                iterator->character[symbol].next = (struct ptrie_array*)calloc(1, sizeof(struct ptrie_array));
                if(iterator->character[symbol].next == NULL) {
                    return -1;
                }
            }

            iterator = iterator->character[symbol].next;
        }
    }
    return -1;
}

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
char *ptrie_autocomplete(struct ptrie *pt, const char *str) {
    //printf("the word is: %s\n", str);
    if (str == NULL) {
        return NULL;
    }

    if (pt == NULL || pt->head == NULL) {
        return strdup(str);
    }   

    
    struct ptrie_array *iterator = pt->head;
    int symbol = 0; 
    int length = (int)strlen(str);

    //printf("This is str: %s\n", str);
    //printf("This is the length: %d\n", length);

    for (int i = 0; i < length; i++) {
        symbol = (unsigned char)str[i];
        if (symbol < 32) return strdup(str);
        /* First handle "we are at last character of prefix" */
        if (i == length - 1) {
            if (iterator->character[symbol].count > 0 && iterator->character[symbol].count == iterator->character[symbol].max_count) {
                return strdup(iterator->character[symbol].word);
            }
            /* If no deeper trie, nothing better exists */
            if (iterator->character[symbol].next == NULL) {
                return strdup(str);
            }
            iterator = iterator->character[symbol].next;
            break;
        }
        /* non-last char must have next */
        if (iterator->character[symbol].next == NULL) {
            return strdup(str);
        }
        iterator = iterator->character[symbol].next;
    }
    

    while (iterator != NULL) {
        int max = 0;
        int idx = 0;
        int moved = 0;
        for (int i = 0; i < 256; i++) {
            if(iterator->character[i].max_count > max) {
                max = iterator->character[i].max_count;
                idx = i;
            }
        }
        if(max == 0) {
            return strdup(str);
        }
        if (iterator->character[idx].max_count == iterator->character[idx].count) {
            return strdup(iterator->character[idx].word);
        }

        if (iterator->character[idx].next != NULL) {
            iterator = iterator->character[idx].next;
            moved = 1;
        }

        if (moved != 1) {
            break;
        }
    }
    
    return strdup(str);


/*  
    while (iterator != NULL) {
        int moved = 0;
        for (int i = 32; i < 256; i++) {
            if (iterator->character[i].word != NULL) {
                //printf("this is the index of the returned word: %d\n", symbol);
                //printf("this is the word returned is: %s\n ", iterator->character[symbol].word);
                return strdup(iterator->character[i].word);
            }
            if (iterator->character[i].next != NULL) {
                iterator = iterator->character[i].next;
                moved = 1;
                break;
            }
        }

        if (moved != 1) {
            break;
        }
    }
    //printf("this is the word: %s\n ", strdup(str));
    return strdup(str);
*/
}

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


void ptrie_maxCount(struct ptrie *pt, const char *str, int count) {
    struct ptrie_array *iterator = pt->head;
    int length = (int)strlen(str);
    for (int i = 0; i < length; i++){
        int idx = str[i];

        if(iterator->character[idx].max_count < count) {
            iterator->character[idx].max_count = count;
        }

        iterator = iterator->character[idx].next; 
    }
}

