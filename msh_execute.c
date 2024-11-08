#include <msh_parse.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>

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


void
msh_execute(struct msh_pipeline *p)
{
	
	if(p->num_commands == 1) {
		struct msh_command *cmd = msh_pipeline_command(p, 0);
		execvp(msh_command_program(cmd), msh_command_args(cmd));
	}
	else {
		int n = p->num_commands;  // The number of processes 
    	int pipes[n-1][2];  // Array of pipes to connect processes (for n processes, we need n-1 pipes)
    	pid_t pids[n];      // Array to store child process IDs


    // Create the n-1 pipes for the processes 
    	for (int i = 0; i < n - 1; i++) {
        	if (pipe(pipes[i]) == -1) {
            	perror("pipe failed");
        	}
 		}

    // Fork n child processes
    	for (int i = 0; i < n; i++) {
        	pids[i] = fork();
        	if (pids[i] == -1) {
            	perror("fork failed");
        	}

        	if (pids[i] == 0) { 
            	// If the child process is the first child process, 
            	// then it writes to the first pipe
            	if (i == 0) { 
                	// Write to the next pipe
                	dup2(pipes[i][1], STDOUT_FILENO);  // TODO: Redirect stdout to write to the current pipe
					close(pipes[i][1]);  // Close the write end of the current pipe
					close(pipes[i][0]);  // TODO: close the read end of the current pipe

					struct msh_command *cmd = msh_pipeline_command(p, i);
					execvp(msh_command_program(cmd), msh_command_args(cmd));

            	}

            	// If the child process is in the middle, 
            	// then it reads from the previous pipe and writes to the next pipe
            	else if (i > 0 && i < n - 1) { 
                	// Read from the previous pipe
                	close(pipes[i-1][1]);  // TODO: close the write end of the previous pipe
                	dup2(pipes[i-1][0], STDIN_FILENO);  // TODO: redirect stdin to read from the previous pipe
                	close(pipes[i-1][0]);  // TODO: close the read end of the previous pipe

                	// Write to the next pipe
                	close(pipes[i][0]);  // TODO: close the read end of the current pipe
                	dup2(pipes[i][1], STDOUT_FILENO);  //TODO: redirect stdout to write to the current pipe
                	close(pipes[i][1]);  // TODO: the write end of the current pipe
                	
					struct msh_command *cmd = msh_pipeline_command(p, i);
					execvp(msh_command_program(cmd), msh_command_args(cmd));
					
            	}

            	// If the process is the last child, 
            	// then it reads from the previous pipe and writes to stdout
            	else {
                	// Read from the previous pipe
                	close(pipes[i-1][1]);  //TODO: close the write end of the previous pipe
                	dup2(pipes[i-1][0], STDIN_FILENO);  //TODO: redirect stdin to read from the previous pipe
                	close(pipes[1][0]);  //TODO: close the read end after redirection

					for (int j = 0; j < n - 1; j++) {
                    	if (j != i && j != i - 1) { 
                        	close(pipes[j][0]);
                        	close(pipes[j][1]);
                    	}
                	}

					struct msh_command *cmd = msh_pipeline_command(p, i);
					execvp(msh_command_program(cmd), msh_command_args(cmd));
            	}

            	exit(0);
        	}
    	}


    	// Parent process: close all pipe ends
    	for (int i = 0; i < n - 1; i++) {
        	close(pipes[i][0]);
        	close(pipes[i][1]);
    	}

    	// Wait for all child processes to finish
    	for (int i = 0; i < n; i++) {
        	waitpid(pids[i], NULL, 0);  // Wait for each child process to terminate
    	}

	}
	return;
}

void
msh_init(void)
{
	return;
}

//helper function
//fork_and_exec(cmd)
	//fork
	//if in child
		//setup file class
		//execvp

//mshexec(pipeleine p)
	//iterate through pipeline
	

	//fork.exec(c) 
