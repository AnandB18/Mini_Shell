/*
 * msh_parse.c
 *
 * Turns raw user input into `msh_sequence` -> `msh_pipeline` -> `msh_command`
 * trees. Responsibilities here: split on `;` and `|`, detect trailing `&`,
 * tokenize arguments, and parse `1>`, `1>>`, `2>`, `2>>` redirections.
 *
 * Public API contracts and per-function documentation live in msh_parse.h.
 * This file only adds module-level context and implementation notes.
 *
 * Memory: all strings reachable from a sequence are heap-allocated (strdup);
 * use msh_sequence_free / msh_pipeline_free / msh_command_free or the
 * sequence dequeue path documented in the header.
 */

#include <msh_parse.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <fcntl.h>

/* ------------------------------------------------------------------------- */
/* Internal AST shapes (must stay consistent with msh_execute expectations)   */
/* ------------------------------------------------------------------------- */

struct msh_command {
	char *array_arg[MSH_MAXARGS];
	int num_args;
	int last_command;
	char *cmd_string;
	int stdout_append;
	int stderr_append;
	char *stdout_file;
	char *stderr_file;
};

struct msh_pipeline {
	struct msh_command *array_command[MSH_MAXCMNDS];
	char *pipeline;
	int num_commands;
	int background;
	pid_t pgid;
};

struct msh_sequence {
	struct msh_pipeline **pipe_array;
	int num_pipelines;
	char *sequence;
};

/* ------------------------------------------------------------------------- */
/* Destructors                                                                */
/* ------------------------------------------------------------------------- */

void
msh_command_free(struct msh_command *c)
{
	for (int i = 0; i < c->num_args; i++) {
		free(c->array_arg[i]);
	}
	free(c->cmd_string);
	free(c->stdout_file);
	free(c->stderr_file);
	free(c);
}

void
msh_pipeline_free(struct msh_pipeline *p)
{
	
	for (int i = 0; i < p->num_commands; i++) {
		msh_command_free(p->array_command[i]);
	}
	free(p->pipeline);
	free(p);
}

void
msh_sequence_free(struct msh_sequence *s)
{

	for(int i = 0; i < s->num_pipelines; i++) {
		msh_pipeline_free(s->pipe_array[i]);
	}
	free(s->pipe_array);
	free(s->sequence);
	free(s);
}

/* ------------------------------------------------------------------------- */
/* Allocation                                                                 */
/* ------------------------------------------------------------------------- */

struct msh_sequence *
msh_sequence_alloc(void)
{
	struct msh_sequence* seq = (struct msh_sequence *)malloc(1 * sizeof(struct msh_sequence));
	if(seq == NULL) {
		return NULL;
	}

	seq->sequence = NULL;
	seq->num_pipelines = 0;
	seq->pipe_array = NULL;

	return seq;
}

/* ------------------------------------------------------------------------- */
/* Parsing: sequence (;) -> pipeline (|) -> command (tokens + redirection)   */
/* ------------------------------------------------------------------------- */

char *
msh_pipeline_input(struct msh_pipeline *p)
{
	return p->pipeline;
}

msh_err_t
msh_sequence_parse(char *str, struct msh_sequence *seq)
{
	msh_err_t err = 0;

	/* Count ';' separators: N semicolons => N+1 pipeline segments */
	if (strlen(str) != 0) {
		seq->num_pipelines = 1;
		for (int i = 0; i < (int)strlen(str); i++) {
			if (str[i] == ';') {
				seq->num_pipelines++;
			}
		}
	}

	seq->pipe_array = (struct msh_pipeline**)malloc((seq->num_pipelines) * sizeof(struct msh_pipeline *));
	seq->sequence = strdup(str);

	char *str_cpy = strdup(str);
	char *pipe, *ptr;
	int i = 0;

	for (pipe = strtok_r(str_cpy, ";", &ptr); pipe != NULL; pipe = strtok_r(NULL, ";", &ptr)) {
		seq->pipe_array[i] = (struct msh_pipeline *)malloc(sizeof(struct msh_pipeline));
		seq->pipe_array[i]->pipeline = strdup(pipe);
		seq->pipe_array[i]->num_commands = 0;
		seq->pipe_array[i]->background = 0;

		err = msh_pipeline_parse(pipe, seq->pipe_array[i]);
		i++;
	}

	free(str_cpy);
	return err;
}

msh_err_t
msh_pipeline_parse(char *pipe, struct msh_pipeline *p) {
	msh_err_t err = 0;
	int start = 0;
	int end = (int)strlen(pipe) - 1;

	/* Trim leading whitespace; leading '|' means missing left-hand command */
	while (isspace((unsigned char)pipe[start]) != 0) {
		start++;
	}
	if (pipe[start] == '|') {
		err = MSH_ERR_PIPE_MISSING_CMD;
		return err;
	}

	/* Trim trailing whitespace; optional trailing '&' => background */
	while (isspace((unsigned char)pipe[end]) != 0) {
		end--;
	}
	if (pipe[end] == '&') {
		p->background = 1;
		end--;
		while (isspace((unsigned char)pipe[end]) != 0) {
			end--;
		}
		/* Strip '&' from mutable buffer and refresh p->pipeline copy */
		int i = 0;
		while (start <= end) {
			pipe[i] = pipe[start];
			i++;
			start++;
		}
		pipe[i] = '\0';
		free(p->pipeline);
		p->pipeline = strdup(pipe);
	}

	/* Trailing '|' after optional & strip => missing right-hand command */
	if (pipe[end] == '|') {
		err = MSH_ERR_PIPE_MISSING_CMD;
		return err;
	}

	char *pipe_cpy = strdup(pipe);
	char *cmd, *ptr;

	for (cmd = strtok_r(pipe_cpy, "|", &ptr); cmd != NULL; cmd = strtok_r(NULL, "|", &ptr)) {
		if (p->num_commands >= MSH_MAXCMNDS) {
			err = MSH_ERR_TOO_MANY_CMDS;
			break;
		}

		p->array_command[p->num_commands] = (struct msh_command *)malloc(sizeof(struct msh_command));
		p->array_command[p->num_commands]->cmd_string = strdup(cmd);
		p->array_command[p->num_commands]->last_command = 0;
		p->array_command[p->num_commands]->num_args = 0;
		p->array_command[p->num_commands]->stdout_append = 0;
		p->array_command[p->num_commands]->stderr_append = 0;
		p->array_command[p->num_commands]->stdout_file = NULL;
		p->array_command[p->num_commands]->stderr_file = NULL;

		for (int i = 0; i < MSH_MAXARGS; i++) {
			p->array_command[p->num_commands]->array_arg[i] = NULL;
		}

		err = msh_command_parse(cmd, p->array_command[p->num_commands]);
		if (err != 0) {
			msh_command_free(p->array_command[p->num_commands]);
			break;
		}

		p->num_commands++;
	}
	if (err != 0) {
		free(pipe_cpy);
		return err;
	}

	p->array_command[p->num_commands] = NULL;
	p->array_command[p->num_commands - 1]->last_command = 1;
	free(pipe_cpy);
	return err;
}

msh_err_t
msh_command_parse(char *cmd, struct msh_command *c)
{
	int err = 0;
	char *cmd_cpy = strdup(cmd);
	char *arg, *ptr;

	/* Space-separated tokens; redir ops take the next token as path; argv gets the rest; leave one slot for NULL terminator in array_arg. */
	for (arg = strtok_r(cmd_cpy, " ", &ptr); arg != NULL; arg = strtok_r(NULL, " ", &ptr)) {
		if (strcmp(arg, "1>") == 0 || strcmp(arg, "1>>") == 0 || strcmp(arg, "2>") == 0 || strcmp(arg, "2>>") == 0) {
			char *file = strtok_r(NULL, " ", &ptr);
			if (file == NULL) {
				err = MSH_ERR_NO_REDIR_FILE;
				break;
			}
			if (strcmp(arg, "1>") == 0 || strcmp(arg, "1>>") == 0) {
				if (c->stdout_file != NULL) {
					err = MSH_ERR_MULT_REDIRECTIONS;
					break;
				}
				c->stdout_file = strdup(file);
				c->stdout_append = (!strcmp(arg, "1>>")) ? 1 : 0;
			} else {
				if (c->stderr_file != NULL) {
					err = MSH_ERR_MULT_REDIRECTIONS;
					break;
				}
				c->stderr_file = strdup(file);
				c->stderr_append = (!strcmp(arg, "2>>")) ? 1 : 0;
			}

			continue;
		}

		if (c->num_args >= MSH_MAXARGS - 1) {
			err = MSH_ERR_TOO_MANY_ARGS;
			break;
		}

		c->array_arg[c->num_args] = strdup(arg);
		if (!c->array_arg[c->num_args]) {
			err = MSH_ERR_NOMEM;
			break;
		}
		c->num_args++;
	}
	c->array_arg[c->num_args] = NULL;

	free(cmd_cpy);
	return err;
}

/* ------------------------------------------------------------------------- */
/* Accessors and optional command user-data hooks (see msh_parse.h)           */
/* ------------------------------------------------------------------------- */

struct msh_pipeline *
msh_sequence_pipeline(struct msh_sequence *s)
{
	if (s->num_pipelines == 0) {
		return NULL;
	}
	struct msh_pipeline *p = s->pipe_array[0];

	/* Shift remaining pointers down — simple queue dequeue */
	for (int i = 0; i < s->num_pipelines - 1; i++) {
		s->pipe_array[i] = s->pipe_array[i + 1];
	}

	s->num_pipelines--;

	return p;
}

struct msh_command *
msh_pipeline_command(struct msh_pipeline *p, size_t nth)
{
	return p->array_command[nth];
}

int
msh_pipeline_background(struct msh_pipeline *p)
{
	return p->background;
}

int
msh_command_final(struct msh_command *c)
{
	return c->last_command;
}

void
msh_command_file_outputs(struct msh_command *c, char **stdout, char **stderr)
{
	*stdout = c->stdout_file;
	*stderr = c->stderr_file;
}

char *
msh_command_program(struct msh_command *c)
{
	return c->array_arg[0];
}

char **
msh_command_args(struct msh_command *c)
{
	return c->array_arg;
}

/* Assignment API stubs: shell does not attach per-command client data. */
void
msh_command_putdata(struct msh_command *c, void *data, msh_free_data_fn_t fn)
{
	(void)c;
	(void)data;
	(void)fn;
}

void *
msh_command_getdata(struct msh_command *c)
{
	(void)c;

	return NULL;
}
