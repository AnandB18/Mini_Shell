/*
 * ptrie.c
 *
 * Prefix trie over byte-indexed edges (0..255). Each inserted string is walked
 * character-by-character; the final edge holds a strdup copy of the full string
 * and a per-string frequency count. max_count on each node along the path is
 * the largest count of any completed string that passes through that edge, so
 * autocomplete can prefer hotter branches. Valid keys use bytes in [32,255)
 * (rejects control chars < 32). Public API contracts are documented in ptrie.h.
 */

#include "ptrie.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ------------------------------------------------------------------------- */
/* Internal layout                                                           */
/* ------------------------------------------------------------------------- */

/* One slot in a radix row: optional continuation, optional completed word here, counts for tie-breaking. */
struct ptrie_node {
	char *word;
	struct ptrie_array *next;
	int count;
	int max_count;
};

/* Fixed 256-way branch; index is the raw unsigned byte of the next character. */
struct ptrie_array {
	struct ptrie_node character[256];
};

struct ptrie {
	struct ptrie_array *head;
};

/* ------------------------------------------------------------------------- */
/* Lifecycle                                                                 */
/* ------------------------------------------------------------------------- */

/* See ptrie.h */
struct ptrie *ptrie_allocate(void) {
	struct ptrie *pt = (struct ptrie *)calloc(1, sizeof(struct ptrie));
	if (pt == NULL) {
		return NULL;
	}

	pt->head = (struct ptrie_array *)calloc(1, sizeof(struct ptrie_array));

	if (pt->head == NULL) {
		free(pt);
		return NULL;
	}

	return pt;
}

/* Recursively free child arrays and strdup'd words at each node, then the array itself. */
void pArray_free(struct ptrie_array *iterator) {
	if (iterator == NULL) {
		return;
	}

	for (int i = 0; i < 256; i++) {
		if (iterator->character[i].next != NULL) {
			pArray_free(iterator->character[i].next);
			iterator->character[i].next = NULL;
		}
		if (iterator->character[i].word != NULL) {
			free(iterator->character[i].word);
			iterator->character[i].word = NULL;
		}
	}
	free(iterator);
}

/* See ptrie.h */
void ptrie_free(struct ptrie *pt) {
	if (pt == NULL) {
		return;
	}

	if (pt->head != NULL) {
		pArray_free(pt->head);
	}

	free(pt);
}

/* ------------------------------------------------------------------------- */
/* Insert and frequency propagation                                          */
/* ------------------------------------------------------------------------- */

/* Walk the same path as the string and raise max_count on each edge whenever this completion's count exceeds the stored max. */
void ptrie_maxCount(struct ptrie *pt, const char *str, int count) {
	struct ptrie_array *iterator = pt->head;
	int length = (int)strlen(str);
	for (int i = 0; i < length; i++) {
		int idx = str[i];

		if (iterator->character[idx].max_count < count) {
			iterator->character[idx].max_count = count;
		}

		iterator = iterator->character[idx].next;
	}
}

/* See ptrie.h */
int ptrie_add(struct ptrie *pt, const char *str) {
	if (pt == NULL || str == NULL) {
		return -1;
	}

	struct ptrie_array *iterator = pt->head;
	int symbol = 0;
	int length = (int)strlen(str);

	for (int i = 0; i < length; i++) {
		symbol = (int)str[i];

		if (symbol < 32 || symbol >= 256) {
			return -1;
		}

		if (i == length - 1) {
			if (iterator->character[symbol].count == 0) {
				iterator->character[symbol].count++;
				iterator->character[symbol].word = strdup(str);
				ptrie_maxCount(pt, str, iterator->character[symbol].count);
				if (iterator->character[symbol].word == NULL) {
					return -1;
				}
			} else {
				iterator->character[symbol].count++;
				ptrie_maxCount(pt, str, iterator->character[symbol].count);
			}

			return 0;
		} else {
			if (iterator->character[symbol].next == NULL) {
				iterator->character[symbol].next = (struct ptrie_array *)calloc(1, sizeof(struct ptrie_array));
				if (iterator->character[symbol].next == NULL) {
					return -1;
				}
			}

			iterator = iterator->character[symbol].next;
		}
	}
	return -1;
}

/* ------------------------------------------------------------------------- */
/* Autocomplete                                                              */
/* ------------------------------------------------------------------------- */

/* See ptrie.h */
char *ptrie_autocomplete(struct ptrie *pt, const char *str) {
	if (str == NULL) {
		return NULL;
	}

	if (pt == NULL || pt->head == NULL) {
		return strdup(str);
	}

	struct ptrie_array *iterator = pt->head;
	int symbol = 0;
	int length = (int)strlen(str);

	for (int i = 0; i < length; i++) {
		symbol = (unsigned char)str[i];
		if (symbol < 32) return strdup(str);
		if (i == length - 1) {
			if (iterator->character[symbol].count > 0 && iterator->character[symbol].count == iterator->character[symbol].max_count) {
				return strdup(iterator->character[symbol].word);
			}
			if (iterator->character[symbol].next == NULL) {
				return strdup(str);
			}
			iterator = iterator->character[symbol].next;
			break;
		}
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
			if (iterator->character[i].max_count > max) {
				max = iterator->character[i].max_count;
				idx = i;
			}
		}
		if (max == 0) {
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
}

/* Optional debug helper; not implemented here (see ptrie.h). */
void ptrie_print(struct ptrie *pt);
