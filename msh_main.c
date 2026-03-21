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

/* linenoise callback: suggest one completion when the user is still on the first word. */
void
msh_completion_cb(const char *buf, linenoiseCompletions *lc) {
    if (autocompletion_trie == NULL || buf == NULL || lc == NULL) {
        return;
    }

	/* skip leading spaces */
	while (*buf == ' ' || *buf == '\t') buf++;

	/* if empty after trimming, no suggestion */
	if (*buf == '\0') return;

	/* find first token length */
	size_t tok_len = strcspn(buf, " \t");

	/* if user already typed a second token, don't complete */
	if (buf[tok_len] != '\0') return;

	/* copy token prefix */
	char prefix[256];
	if (tok_len >= sizeof(prefix)) return;
	memcpy(prefix, buf, tok_len);
	prefix[tok_len] = '\0';

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

/* Insert shell builtin names into the completion trie; returns 0 on success. */
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

/* Learn program names from a parsed pipeline so later lines can complete them. */
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

/* Read one line of input; empty line after linenoise returns NULL (exit loop). Non-empty lines are added to history. */
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
