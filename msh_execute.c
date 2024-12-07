#include <msh_parse.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>

#include <signal.h>

//#define MAX_PROCESSES 128

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

struct jobs {
	char* process;
	pid_t id;
	int background;
};

pid_t foreground_pid = -1; // Foreground process PIDs

pid_t background[MSH_MAXBACKGROUND]; // Background process PIDs
int bg_count = 0; // Counter for background processes

struct jobs job_list[MSH_MAXBACKGROUND];
int j_count = 0;


void
add_job(pid_t jpid, char* process) {
	job_list[j_count].process = strdup(process);
	job_list[j_count].id = jpid;
	job_list[j_count].background = 1;

	j_count++;
	return;
}

void
show_jobs(int id) {
	if (id == -1) {
		for(int i = 0; i < j_count; i++) {
			printf("[%d] %s\n", i, job_list[i].process);
		}
		
	}
	else{
		printf("[%d] %s", id, job_list[id].process);
	
	}
}

void
remove_job(pid_t pid) {
	for(int i = 0; i < j_count; i++) {
		if(job_list[i].id == pid) {
			for (int j = i; j < j_count - 1; j++) {
				job_list[j] = job_list[j+1];
			}
			j_count--;
			break;
		}
	}
}

void
msh_execute(struct msh_pipeline *p)
{
	struct msh_command *c = msh_pipeline_command(p,0);
	if (c == NULL || c->cmd_string == NULL || strlen(c->cmd_string) == 0) {
        return;
    }

	if(strcmp(msh_command_program(c), "cd") == 0) {
		if(strcmp(c->array_arg[1],"~") == 0) {
			chdir(getenv("HOME"));
			return;
		}
		else {
			chdir(c->array_arg[1]);
			return;
		}
	}
	
	if(strcmp(msh_command_program(c), "exit") == 0) {
		exit(0);
	}

	if(strcmp(msh_command_program(c), "jobs") == 0) {
		show_jobs(-1);
		return;
	}
	

	// ./msh
	if(p->num_commands == 1) {
		pid_t pid = fork();
		int status;
		

		if(pid == 0) {
			struct msh_command *cmd = msh_pipeline_command(p, 0);
			execvp(msh_command_program(cmd), msh_command_args(cmd));
			exit(0);
			//free(cmd);
		}
		else {
			if(!(p->background)) {
				foreground_pid = pid;
			}
			else {
				background[bg_count] = pid;
				bg_count++;
				add_job(pid,p->pipeline);
			}
		}

		if(!(p->background)) {
			waitpid(pid, NULL, 0);
			foreground_pid = -1;
		}
		else {
			while((pid = waitpid(-1, &status, WNOHANG)) > 0) {
				for(int i = 0; i < bg_count; i++) {
					if(background[i] == pid) {
						for(int j = i; j < bg_count; j++) { 
							background[j] = background[j+1];
						}
						bg_count--;
						break;
					}
				}	
				remove_job(pid);
			}
		}
		return;
	}
	else {
		int n = p->num_commands;  // The number of processes 
    	int pipes[n-1][2];  // Array of pipes to connect processes (for n processes, we need n-1 pipes)
    	pid_t pids[n]; 
		pid_t pid;
		int status;     // Array to store child process IDs


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
					//free(cmd);

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
					//free(cmd);
					
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
					//free(cmd);
            	}
        	}
			else {
				if(!(p->background)) {
					foreground_pid = pids[i];
				}
				else {
					background[bg_count] = pids[i];
					bg_count++;
					add_job(pids[i],p->pipeline);
				}
			}
    	}

		// Parent process: close all pipe ends
		for (int i = 0; i < n - 1; i++) {
        	close(pipes[i][0]);
    		close(pipes[i][1]);
    	}

		// Wait for all child processes to finish
		if(!(p->background)) {
    		for (int i = 0; i < n; i++) {
        		waitpid(pids[i], NULL, 0);  // Wait for each child process to terminate
    		}
			foreground_pid = -1;
		}
		else {
			while((pid = waitpid(-1, &status, WNOHANG)) > 0) {
				for(int i = 0; i < bg_count; i++) {
					if(background[i] == pid) {
						for(int j = i; j < bg_count; j++) { 
							background[j] = background[j+1];
						}
						bg_count--;
						break;
					}
				}
				remove_job(pid);
			}
		}
		return;
	}
}

void 
sig_handler(int signal) {
	if(signal == SIGINT) {
		if(foreground_pid != -1) {
			kill(foreground_pid, SIGINT);
		}
	}
	else if(signal == SIGTSTP) {
		background[bg_count] = foreground_pid;
		bg_count++;
		foreground_pid = -1;
	}
	/*else if(signal == SIGCHLD) {
		pid_t pid;
		int status;

		while((pid = waitpid(-1, &status, WNOHANG)) > 0) {
			for(int i = 0; i < bg_count; i++) {
				if(background[i] == pid) {
					for(int j = i; j < bg_count; j++) { 
						background[j] = background[j+1];
					}
					bg_count--;
					break;
				}
			}
			remove_job(pid);
		}
	}*/
}

void
msh_init(void)
{
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = sig_handler;

	sigemptyset(&sa.sa_mask);
	//sigaddset(&masked, sa);

	if(sigaction(SIGINT, &sa, NULL) == -1) {
		perror("sigaction fialed");
	}

	if(sigaction(SIGTSTP, &sa, NULL) == -1) {
		perror("sigaction failed");
	}
	return;
}
