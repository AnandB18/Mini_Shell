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
// #define WCONTINUED __W_CONTINUED

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
    pid_t pgid;
};

typedef enum{
    JOB_RUNNING,
    JOB_STOPPED,
    JOB_TERMINATED
} job_status_t;

struct jobs {
	char* process;
	pid_t id;
    job_status_t status;
};

struct fg_job { 
    int nprocs;
    pid_t pids[MSH_MAXCMNDS];
};

struct fg_job foreground_job = {.nprocs = 0, .pids = {0}};
int is_fg_active = 0;
pid_t foreground_pid = -1; // Foreground process PIDs
char* foreground_process = NULL;


struct jobs job_list[MSH_MAXBACKGROUND];
int j_count = 0;


int
add_job(pid_t jpid, char* process) {
	job_list[j_count].process = strdup(process);
	job_list[j_count].id = jpid;
    job_list[j_count].status = JOB_RUNNING;

    int idx = j_count;
	j_count++;
	return idx;
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

int
get_job_idx(int pid) {
    for (int i = 0; i < j_count; i++) {
        if (job_list[i].id == pid){
            return i;
        }
    }

    return -1;
}

void
msh_jobs(int id) {
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
msh_fg(int idx) {
    if (idx < 0 || idx >= j_count) {
        return;
    }

    is_fg_active = 1;
    foreground_process = job_list[idx].process;
    foreground_pid = job_list[idx].id;
    foreground_job.nprocs = 1;
    foreground_job.pids[0] = job_list[idx].id;

    if (job_list[idx].status == JOB_STOPPED) {
        kill(foreground_pid, SIGCONT);
        job_list[idx].status = JOB_RUNNING;
    }

    int status;
    while(1) {
        pid_t wait_pid = waitpid(foreground_pid, &status, 0);

        if (wait_pid == -1) {
            if (errno == EINTR) {

                if (foreground_pid == -1) {
                    break;
                }
                continue;
            }
            else {
                perror("waitpid failed");
                break;
            }
        }
        else {
            break;
        }  
    }

    remove_job(job_list[idx].id);

    foreground_process = NULL;
    is_fg_active = 0;
    foreground_pid = -1;

    return;
}

void
msh_bg(int idx) {
    if (idx < 0 || idx >= j_count) {
        return;
    }
    if (job_list[idx].status == JOB_RUNNING) {
        return;
    }
    kill(job_list[idx].id, SIGCONT);
    job_list[idx].status = JOB_RUNNING;
    return;
}

void
msh_cd(struct msh_command *c) {
    char * path = c->array_arg[1];
    char * home_dir = getenv("HOME");
    int result = 0;

    if(path == NULL) {
        result = chdir(home_dir);
    }
    else if(strcmp(path,"~") == 0) {
        result = chdir(home_dir);

    }
    else if(path[0] == '~' && path[1] == '/') {
        int length = strlen(path + 1) + strlen(home_dir);
        char * new_path = (char *)malloc(length + 1);
        strcpy(new_path, home_dir);
        strcat(new_path, path + 1);

        result = chdir(new_path);

        free(new_path);
    }
    else {
        result = chdir(path);
    }

    if(result)
    {
        perror("failed cd");
    }

    return;
}

void
msh_execute(struct msh_pipeline *p) {
	struct msh_command *c = msh_pipeline_command(p,0);
	
    if (c == NULL || c->cmd_string == NULL || strlen(c->cmd_string) == 0) {
        return;
    }

	if(strcmp(msh_command_program(c), "cd") == 0) {
        msh_cd(c);
        return;
	}
	
	if(strcmp(msh_command_program(c), "exit") == 0) {
		exit(0);
	}

	if(strcmp(msh_command_program(c), "jobs") == 0) {
        if (c->array_arg[1] != NULL) {
            int index = atoi(c->array_arg[1]);
            if (index < 0 || index >= j_count) {
                printf("jobs: %s: no such job\n", c->array_arg[1]);
                return;
            }
            msh_jobs(index);
        }
        else {
            msh_jobs(-1);
        }
        return;
	}

	if(strcmp(msh_command_program(c), "fg") == 0) {
        int index = -1;
        if(c->array_arg[1] != NULL) {
            index = atoi(c->array_arg[1]);
        }
		else {
            if (j_count > 0) {
                index = j_count - 1;
            }
            else {
                return;
            }
        }
		msh_fg(index);
		return;
	} 

    if(strcmp(msh_command_program(c), "bg") == 0) {
        int index = -1;
        if(c->array_arg[1] != NULL) {
            index = atoi(c->array_arg[1]);
        }
        else {
            if (j_count > 0) {
                index = j_count - 1;
            }
            else {
                return;
            }
        }
        msh_bg(index);
        return;
    }
	

	// ./msh
	if(p->num_commands == 1) {
		pid_t pid = fork();

		if(pid == 0) {
            signal(SIGINT, SIG_DFL);     // reset signals
            signal(SIGTSTP, SIG_DFL);
            execvp(msh_command_program(c), msh_command_args(c));
            perror("execvp failed");
            exit(1);
		}
		else {
            if (msh_pipeline_background(p)) {
                int idx = add_job(pid, msh_pipeline_input(p));
                job_list[idx].status = JOB_RUNNING;
                return;
            }
            else {
                foreground_pid = pid;
                foreground_process = msh_pipeline_input(p);
                foreground_job.nprocs = 1;
                foreground_job.pids[0] = pid;
                is_fg_active = 1;
    
                int status;
                while(1) {
                    pid_t wait_pid = waitpid(pid, &status, 0);
                    // fprintf(stderr, "[dbg] waitpid returned=%d errno=%d is_fg_active=%d\n", (int)wait_pid, errno, is_fg_active);
                    if (wait_pid == -1) {
                        if (errno == EINTR) {
    
                            if (foreground_pid == -1) {
                                break;
                            }
                            continue;
                        }
                        else {
                            perror("waitpid failed");
                            break;
                        }
                    }
                    else {
                        break;
                    }  
                }
    
                foreground_pid = -1;
                foreground_process = NULL;
            }
		}
		return;
	}
	else {
		int n = p->num_commands;  // The number of processes 
    	int pipes[MSH_MAXCMNDS-1][2];  // Array of pipes to connect processes (for n processes, we need n-1 pipes)
    	pid_t pids[MSH_MAXCMNDS]; 

    // Create the n-1 pipes for the processes 
        if (n > 1) {
            for (int i = 0; i < n - 1; i++) {
                if (pipe(pipes[i]) == -1) {
                    perror("pipe failed");
                }
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
                    signal(SIGINT, SIG_DFL);
                    signal(SIGTSTP, SIG_DFL);
                    signal(SIGCHLD, SIG_DFL);
                    
                	// Write to the next pipe
                	dup2(pipes[i][1], STDOUT_FILENO);  // TODO: Redirect stdout to write to the current pipe
					close(pipes[i][1]);  // Close the write end of the current pipe
					close(pipes[i][0]);  // TODO: close the read end of the current pipe

					struct msh_command *cmd = msh_pipeline_command(p, i);
					execvp(msh_command_program(cmd), msh_command_args(cmd));
                    exit(0);
            	}

            	// If the child process is in the middle, 
            	// then it reads from the previous pipe and writes to the next pipe
            	else if (i > 0 && i < n - 1) { 
                    signal(SIGINT, SIG_DFL);
                    signal(SIGTSTP, SIG_DFL);
                    signal(SIGCHLD, SIG_DFL);

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
                    exit(0);					
            	}

            	// If the process is the last child, 
            	// then it reads from the previous pipe and writes to stdout
            	else {
                    signal(SIGINT, SIG_DFL);
                    signal(SIGTSTP, SIG_DFL);
                    signal(SIGCHLD, SIG_DFL);

                	// Read from the previous pipe
                	close(pipes[i-1][1]);  //TODO: close the write end of the previous pipe
                	dup2(pipes[i-1][0], STDIN_FILENO);  //TODO: redirect stdin to read from the previous pipe
                	close(pipes[i-1][0]);  //TODO: close the read end after redirection

					for (int j = 0; j < n - 1; j++) {
                    	if (j != i && j != i - 1) { 
                        	close(pipes[j][0]);
                        	close(pipes[j][1]);
                    	}
                	}

					struct msh_command *cmd = msh_pipeline_command(p, i);
					execvp(msh_command_program(cmd), msh_command_args(cmd));
                    exit(0);
            	}
            }
    	}

        // Parent process: close all pipe ends
        for (int i = 0; i < n - 1; i++) {
            close(pipes[i][0]);
            close(pipes[i][1]);
        }

        if (msh_pipeline_background(p)) {
            int idx = add_job(pids[n-1], msh_pipeline_input(p));
            job_list[idx].status = JOB_RUNNING;
            return;
        }
        else {
            foreground_job.nprocs = n;
            for (int j = 0; j < n; j++) {
                foreground_job.pids[j] = pids[j];
            }
            is_fg_active = 1;
            foreground_process = msh_pipeline_input(p);
            
            int fg_jobs_left = foreground_job.nprocs;
            int status;
            while(fg_jobs_left > 0) {
                pid_t wait_pid = waitpid(-1, &status, 0);
    
                if (wait_pid == -1) {
                    if (errno == EINTR) {
    
                        if (!is_fg_active) {
                            break;
                        }
                        continue;
                    } 
                    else {
                        perror("waitpid failed");
                        break;
                    }
                }
                for (int i = 0; i < foreground_job.nprocs; i++) {
                    if (foreground_job.pids[i] == wait_pid) {
                        foreground_job.pids[i] = -1;
                        fg_jobs_left--;
                        break;
                    }
                }
            } 
        }
    is_fg_active = 0;
    foreground_process = NULL;
    foreground_pid = -1;
    }
}

void 
sig_handler(int signal) {
    if (!is_fg_active) {
        return;
    }

    if (signal == SIGINT) {
        fprintf(stderr, "[dbg] SIGINT: kill pid=%d\n", (int)foreground_pid);
        for (int i = 0; i < foreground_job.nprocs; i++) {
            kill(foreground_job.pids[i], SIGTERM);
        }
    }
    else if (signal == SIGTSTP) {
        // fprintf(stderr, "[dbg] SIGTSTP: is_fg_active=%d nprocs=%d\n", is_fg_active, foreground_job.nprocs);
        for (int i = 0; i < foreground_job.nprocs; i++) {
            // fprintf(stderr, "[dbg]   stop pid=%d\n", (int)foreground_job.pids[i]);
        }
        for (int i = 0; i < foreground_job.nprocs; i++) {
            kill(foreground_job.pids[i], SIGTSTP);
        }

        if (foreground_process != NULL) {
            int idx = add_job(foreground_job.pids[0], foreground_process);
            job_list[idx].status = JOB_STOPPED;
        }
    }
    else if (signal == SIGCHLD) {
        int status;
        pid_t wait_pid = -1;

        while((wait_pid = waitpid(-1, &status, WNOHANG)) > 0) {
            // fprintf(stderr, "[dbg] SIGCHLD reaped pid=%d\n", (int)wait_pid);
            int idx = get_job_idx(wait_pid);
            // fprintf(stderr, "[dbg]   idx=%d\n", idx);
            if (idx == -1) {
                continue;
            }
            remove_job(wait_pid);
        }
        return;
    }

    foreground_pid = -1;
    foreground_process = NULL;
    is_fg_active = 0;
}

void
msh_init(void)
{
    //pid_t shell_pgid = getpid();

	struct sigaction sa; 
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = sig_handler;
	sigemptyset(&sa.sa_mask);

	if(sigaction(SIGINT, &sa, NULL) == -1) {
		perror("sigaction fialed");
	}

	if(sigaction(SIGTSTP, &sa, NULL) == -1) {
		perror("sigaction failed");
	}

    if(sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction failed");
    }

	return;
}
