/*
 * msh_main.c
 *
 * Program entry and interactive REPL: read a line with linenoise, parse into
 * an msh_sequence, dequeue each msh_pipeline and run msh_execute, then free.
 * Also seeds a prefix trie with shell builtins and program names seen in
 * parsed lines so the first token can be tab-completed. Signal setup for the
 * shell happens in msh_init() (see msh_execute.c). Some control flow and
 * hooks here follow the course-provided template; behavior such as "empty
 * line exits" is intentional.
 */

#include <msh.h>
#include <msh_parse.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <ptrie.h>

#include <linenoise.h>

/* Trie of known command names for linenoise tab-completion (first token only). */
static struct ptrie *autocompletion_trie = NULL;

/**
 * If @p buf has only a first command token (optional leading spaces, no second token yet),
 * copy that token into @p prefix (nul-terminated, capacity @p cap) and return its length.
 * Otherwise return -1.
 */
static int
msh_first_token_prefix(const char *buf, char *prefix, size_t cap) {
	size_t tok_len;

	if (buf == NULL || prefix == NULL || cap == 0) {
		return -1;
	}
	while (*buf == ' ' || *buf == '\t') {
		buf++;
	}
	if (*buf == '\0') {
		return -1;
	}
	tok_len = strcspn(buf, " \t");
	if (buf[tok_len] != '\0') {
		return -1;
	}
	if (tok_len >= cap) {
		return -1;
	}
	memcpy(prefix, buf, tok_len);
	prefix[tok_len] = '\0';
	return (int)tok_len;
}

/**
 * linenoise completion hook: offer at most one trie-based suggestion for the first token.
 *
 * Skips leading whitespace, ignores input once a second token has started, copies the prefix into a fixed buffer, and calls ptrie_autocomplete. Frees the trie-owned suggestion string after use.
 *
 * @param buf Current input line (borrowed).
 * @param lc linenoise completion list to append to.
 */
void
msh_completion_cb(const char *buf, linenoiseCompletions *lc) {
	char prefix[256];
	int tok_len;

	if (autocompletion_trie == NULL || buf == NULL || lc == NULL) {
		return;
	}

	tok_len = msh_first_token_prefix(buf, prefix, sizeof prefix);
	if (tok_len < 0) {
		return;
	}

    char *suggestion = ptrie_autocomplete(autocompletion_trie, prefix);
    if (suggestion == NULL) {
        return;
    }

    /* avoid re-adding exact same text as "completion" */
    if (strcmp(suggestion, prefix) != 0) {
        linenoiseAddCompletion(lc, suggestion);
    }

    free(suggestion);
}

/**
 * linenoise hints hook: show the trie suffix after the typed first token (ghost text).
 *
 * Return value is heap-allocated; register linenoiseSetFreeHintsCallback(free).
 * Sets @p color / @p bold for xterm foreground styling when a hint is shown.
 *
 * @param buf Current input line (borrowed).
 * @param color Out: ANSI base color 31--37, or leave default if unused.
 * @param bold Out: 1 for bold hint, 0 otherwise.
 * @return strdup'd suffix after the prefix, or NULL.
 */
char *
msh_hints_cb(const char *buf, int *color, int *bold) {
	char prefix[256];
	int tok_len;
	char *suggestion;
	const char *suffix;
	char *hint;

	if (autocompletion_trie == NULL || buf == NULL || color == NULL || bold == NULL) {
		return NULL;
	}

	tok_len = msh_first_token_prefix(buf, prefix, sizeof prefix);
	if (tok_len < 0) {
		return NULL;
	}

	suggestion = ptrie_autocomplete(autocompletion_trie, prefix);
	if (suggestion == NULL) {
		return NULL;
	}
	if (strcmp(suggestion, prefix) == 0) {
		free(suggestion);
		return NULL;
	}
	if (strncmp(suggestion, prefix, (size_t)tok_len) != 0) {
		free(suggestion);
		return NULL;
	}
	suffix = suggestion + tok_len;
	if (suffix[0] == '\0') {
		free(suggestion);
		return NULL;
	}

	hint = strdup(suffix);
	free(suggestion);
	if (hint == NULL) {
		return NULL;
	}

	*color = 36;
	*bold = 0;
	return hint;
}

/**
 * Register shell builtin names in the autocompletion trie.
 *
 * @param pt Trie to populate (must be non-NULL).
 * @return 0 on success, -1 if any ptrie_add fails.
 */
int
msh_ptrie_builtins(struct ptrie *pt) {
    if (pt == NULL) {
        return -1;
    }
    
    //add the builtins to the trie
    if(ptrie_add(pt, "cd") < 0) {
        return -1;
    }
    if(ptrie_add(pt, "exit") < 0) {
        return -1;
    }
    if(ptrie_add(pt, "jobs") < 0) {
        return -1;
    }
    if(ptrie_add(pt, "fg") < 0) {
        return -1;
    }
    if(ptrie_add(pt, "bg") < 0) {
        return -1;
    }

	return 0;
}

/**
 * Add each command's argv[0] from a pipeline into the trie for future completion.
 *
 * @param pt Destination trie.
 * @param p Parsed pipeline (borrowed).
 */
static void
msh_add_pipeline_ptrie(struct ptrie *pt, struct msh_pipeline *p) {
    if (pt == NULL || p == NULL) return;

    for (size_t i = 0; ; i++) {
        struct msh_command *cmd = msh_pipeline_command(p, i);
        if (cmd == NULL) break;

        char *prog = msh_command_program(cmd);
        if (prog != NULL && prog[0] != '\0') {
            (void)ptrie_add(pt, prog); /* ignore failure safely */
        }
    }
}

/**
 * Read one interactive line via linenoise (prompt is configurable in-body).
 *
 * An empty string is freed and returned as NULL so the REPL exits per template rules. Non-empty lines are appended to linenoise history; caller must free non-NULL return values.
 *
 * @return Heap-allocated line, or NULL to signal exit.
 */
char *
msh_input(void) {
	char *line;

	/* You can change this displayed string to whatever you'd like ;-) */
	line = linenoise("Anand's Shell> ");
	if (line && strlen(line) == 0) {
		
		free(line);

		return NULL;
	}
	if (line) linenoiseHistoryAdd(line);

	return line;
}

/**
 * Shell entry: validate argv, init linenoise history and signals, build completion trie and parse sequence, then run read/parse/execute until msh_input returns NULL.
 *
 * @param argc Expected 1 (no extra arguments).
 * @param argv argv[0] is program name for usage text.
 * @return EXIT_SUCCESS on normal exit, EXIT_FAILURE on init usage failure, or parser error code on msh_sequence_parse failure.
 */
int
main(int argc, char *argv[]) {
	struct msh_sequence *s;

	/* No script mode: this binary is interactive only. */
	if (argc > 1) {
		fprintf(stderr, "Usage: %s\n", argv[0]);

		return EXIT_FAILURE;
	}
	/* See ln/README.markdown for linenoise; run make if ln/ is missing. */
	linenoiseHistorySetMaxLen(1<<16);

	msh_init();

	/* Completion trie: builtins first, then names from commands user runs. */
	autocompletion_trie = ptrie_allocate();
	if (autocompletion_trie == NULL) {
		printf("MSH Error: Could not allocate autocompletion trie at initialization\n");
		return EXIT_FAILURE;
	}

	linenoiseSetCompletionCallback(msh_completion_cb);
	linenoiseSetHintsCallback(msh_hints_cb);
	linenoiseSetFreeHintsCallback(free);

	if(msh_ptrie_builtins(autocompletion_trie) < 0) {
		printf("MSH Error: Could not add builtins to autocompletion trie at initialization\n");
		ptrie_free(autocompletion_trie);
		return EXIT_FAILURE;
	}

	/* Reused for each user line; pipelines are drained before the next read. */
	s = msh_sequence_alloc();
	if (s == NULL) {
		printf("MSH Error: Could not allocate msh sequence at initialization\n");
		ptrie_free(autocompletion_trie);
		return EXIT_FAILURE;
	}

	/* Template REPL: msh_input NULL (e.g. empty line) breaks and exits cleanly. */
	while (1) {
		char *str;
		struct msh_pipeline *p;
		msh_err_t err;
		
		str = msh_input();
		//printf("This is the input: %s", str);
		if (!str){
			break;
		}  /* you must maintain this behavior: an empty command exits */

		/* str is consumed; on error the process exits with parser error code. */
		err = msh_sequence_parse(str, s);
		if (err != 0) {
			printf("MSH Error: %s\n", msh_pipeline_err2str(err));

			return err;
		}

		/* dequeue pipelines and sequentially execute them */
		while ((p = msh_sequence_pipeline(s)) != NULL) {
			msh_add_pipeline_ptrie(autocompletion_trie, p);
			msh_execute(p);
			msh_pipeline_free(p);
		}
		free(str);
	}

	/* Normal exit path after break from msh_input returning NULL. */
	msh_sequence_free(s);
	ptrie_free(autocompletion_trie);
	return 0;
}
