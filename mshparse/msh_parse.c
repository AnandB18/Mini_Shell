#include <msh_parse.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

/**
 * Each command corresponds to either a program (in the `PATH`
 * environment variable, see `echo $PATH`), or a builtin command like
 * `cd`. Commands are passed arguments.
 */
struct msh_command{
	char* array_arg[MSH_MAXARGS];
	int num_args;
	char* cmd_string;  
	int last_command;
};

/**
 * A pipeline is a sequence of commands, separated by "|"s. The output
 * of a preceding command (before the "|") gets passed to the input of
 * the next (after the "|").
 */
struct msh_pipeline {
	struct msh_command* array_command[MSH_MAXCMNDS];
	char* pipeline;
	int num_commands;
	int background;
};

/**
 * A sequence of pipelines. Pipelines are separated by ";"s, enabling
 * a sequence to define a sequence of pipelines that execute one after
 * the other. A pipeline can run in the background, which enables us
 * to move on an execute the next pipeline.
 */
struct msh_sequence {
	struct msh_pipeline** pipe_array;
	int num_pipelines;
	char* sequence;

};

void
msh_command_free(struct msh_command *c)
{
	for (int i = 0; i < c->num_args; i++) {
		free(c->array_arg[i]);
	}
	free(c->cmd_string);
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

struct msh_sequence *
msh_sequence_alloc(void)
{
	struct msh_sequence* seq = (struct msh_sequence *)malloc(1 * sizeof(struct msh_sequence));
	
	seq->sequence = NULL;
	seq->num_pipelines = 0;

	if(seq == NULL) {
		return NULL;
	}
	
	return seq;
}

char *
msh_pipeline_input(struct msh_pipeline *p)
{
	return p->pipeline;
}

msh_err_t
msh_sequence_parse(char *str, struct msh_sequence *seq)
{
	msh_err_t err = 0;
	if(strlen(str) != 0) {
		seq->num_pipelines = 1;
		for (int i = 0; i < (int)strlen(str); i++) {
			if(str[i] == ';') {
				seq->num_pipelines++;
			}
		}	
	}

	seq->pipe_array = (struct msh_pipeline**)malloc((seq->num_pipelines) * sizeof(struct msh_pipeline));
	seq->sequence = strdup(str);

	//printf("sequence: %s\n", seq->sequence);
	//printf("sequence parameter: %s\n", str);

	char* str_cpy = strdup(str);
	char *pipe, *ptr;

	
	int i = 0;
    for (pipe = strtok_r(str_cpy, ";", &ptr); pipe != NULL; pipe = strtok_r(ptr, ";", &ptr))
	{
		seq->pipe_array[i] = (struct msh_pipeline*)malloc(sizeof(struct msh_pipeline));
		seq->pipe_array[i]->pipeline = strdup(pipe);
		seq->pipe_array[i]->num_commands = 0;
		seq->pipe_array[i]->background = 0;

		//printf("pipe before parsed: %s\n", pipe);
		err = msh_pipeline_parse(pipe, seq->pipe_array[i]);
		
		/*if (err != 0) {
			break;
		}
		*/
		i++;
	}
	
	free(str_cpy);
	return err;
}

msh_err_t 
msh_pipeline_parse(char *pipe, struct msh_pipeline *p){
	msh_err_t err = 0;
	int start = 0;
	int end = strlen(pipe) - 1;
		 
	//printf("pipeline: %s\n", p->pipeline);
	//printf("pipeline parameter: %s\n", pipe);

	while(isspace(pipe[start]) != 0) {
		start++;
	}
	if(pipe[start] == '|') {
		err = MSH_ERR_PIPE_MISSING_CMD;
		return err;
	}
	
	while(isspace(pipe[end]) != 0) {
		end--;
	}
	if(pipe[end] == '&') {
		p->background = 1;
		end--; 
	}
	if(pipe[end] == '|') {
		err = MSH_ERR_PIPE_MISSING_CMD;
		return err;
	}

	int i = 0;

	while (start <= end) {
		pipe[i] = pipe[start];
		i++;
		start++;
	}

	pipe[i] = '\0';


	char* pipe_cpy = strdup(pipe);
	char *cmd, *ptr;
	
    for (cmd = strtok_r(pipe_cpy, "|", &ptr); cmd != NULL; cmd = strtok_r(ptr, "|", &ptr))
    {
		if(p->num_commands >= MSH_MAXCMNDS) {
			err = MSH_ERR_TOO_MANY_CMDS;
			break;
			//return -7;
		}
		
		p->array_command[p->num_commands] = (struct msh_command*)malloc(sizeof(struct msh_command));
		p->array_command[p->num_commands]->cmd_string = strdup(cmd);
		p->array_command[p->num_commands]->last_command = 0;
		p->array_command[p->num_commands]->num_args = 0;

		for(int i = 0; i < MSH_MAXARGS; i++) {
			p->array_command[p->num_commands]->array_arg[i] = NULL;
		}
		
		//printf("command before parsed: %s\n", cmd);
		err = msh_command_parse(cmd, p->array_command[p->num_commands]);
		/*if (err != 0) {
			break;
		}
		*/

		p->num_commands++;
    }
	if(err != 0) {
		free(pipe_cpy);
		return err;
	}
	//p->array_command[p->num_commands] = NULL;

	p->array_command[p->num_commands - 1]->last_command = 1;
	free(pipe_cpy);
	return err;
}

msh_err_t 
msh_command_parse(char *cmd, struct msh_command *c) {
	//printf("command: %s\n", c->cmd_string);
	//printf("command parameter: %s\n", cmd);
	int err = 0;
	char* cmd_cpy = strdup(cmd);
	char *arg, *ptr;
	
	for (arg = strtok_r(cmd_cpy, " ", &ptr); arg != NULL; arg = strtok_r(ptr, " ", &ptr)) 
	{
		if(c->num_args >= MSH_MAXARGS) {
			err = MSH_ERR_TOO_MANY_ARGS;
			break;
		}

		c->array_arg[c->num_args] = strdup(arg);
		//printf("arg: %s\n", c->array_arg[c->num_args]);
		c->num_args++;

    }
	//c->array_arg[c->num_args] = NULL;

	free(cmd_cpy);
	return err;
}

struct msh_pipeline *
msh_sequence_pipeline(struct msh_sequence *s)
{
	if(s->num_pipelines == 0) {
		return NULL;
	}
	struct msh_pipeline* p = s->pipe_array[0];
	
	for(int i = 0; i < s->num_pipelines - 1; i++) {
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


//Don't Do
void
msh_command_file_outputs(struct msh_command *c, char **stdout, char **stderr)
{
	(void)c;
	(void)stdout;
	(void)stderr;
}

//first args
char *
msh_command_program(struct msh_command *c)
{
	return c->array_arg[0];
}

char **
msh_command_args(struct msh_command *c)
{
	return c->array_arg; //c->args
}


//Don't Do
void
msh_command_putdata(struct msh_command *c, void *data, msh_free_data_fn_t fn)
{
	(void)c;
	(void)data;
	(void)fn;
}

//don't do
void *
msh_command_getdata(struct msh_command *c)
{
	(void)c;

	return NULL;
}
