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

/**
 * Each command corresponds to either a program (in the `PATH`
 * environment variable, see `echo $PATH`), or a builtin command like
 * `cd`. Commands are passed arguments.
 */
 struct msh_command{
	char* array_arg[MSH_MAXARGS];
	int num_args;
	int last_command;
	char* cmd_string;  
	int stdout_append;
	int stderr_append;
	char* stdout_file;
	char* stderr_file;	
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
        pid_t wait_pid = waitpid(foreground_pid, &status, WUNTRACED);

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

int
parse_input_idx(int *index, struct msh_command *c) {
    if(c->array_arg[1] != NULL) {
        *index = atoi(c->array_arg[1]);
    }
    else {
        if (j_count > 0) {
            *index = j_count - 1;
        }
        else {
            return -1;
        }
    }
    return 0;
}
int
check_builtin(struct msh_command *c) {
    if (strcmp(msh_command_program(c), "cd") == 0) {
        msh_cd(c);
        return 1;
    }
    if (strcmp(msh_command_program(c), "exit") == 0) {
        exit(0);
    }
    if (strcmp(msh_command_program(c), "jobs") == 0) {
        if (c->array_arg[1] != NULL) {
            int index = atoi(c->array_arg[1]);
            if (index < 0 || index >= j_count) {
                printf("jobs: %s: no such job\n", c->array_arg[1]);
                return 1;
            }
            msh_jobs(index);
        }
        else {
            msh_jobs(-1);
        }
        return 1;
    }
    if (strcmp(msh_command_program(c), "fg") == 0) {
        int index = -1;
        if(parse_input_idx(&index, c) == -1){
            return 1;
        }
		msh_fg(index);
		return 1;
    }
    if (strcmp(msh_command_program(c), "bg") == 0) {
        int index = -1;
        if(parse_input_idx(&index, c) == -1){
            return 1;
        }
        msh_bg(index);
        return 1;
    }
    return 0;
}

int
create_pipes(int pipes[][2], int num_cmds) {
    for (int i = 0; i < num_cmds - 1; i++) {
        if (pipe(pipes[i]) == -1) {
            return -1;
        }
    }
    return 0;
}

void
close_pipes(int pipes[][2], int num_cmds) {
    for (int i = 0; i < num_cmds - 1; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }
    return;
}

void
redirect_pipes(int i, int num_cmds, int pipes[][2]) {
    if (i == 0) {
        dup2(pipes[i][1], STDOUT_FILENO);
        close(pipes[i][1]);
        close(pipes[i][0]);
    }
    else if (i > 0 && i < num_cmds - 1) {
        // Read from the previous pipe
        close(pipes[i-1][1]);  // TODO: close the write end of the previous pipe
        dup2(pipes[i-1][0], STDIN_FILENO);  // TODO: redirect stdin to read from the previous pipe
        close(pipes[i-1][0]);  // TODO: close the read end of the previous pipe

        // Write to the next pipe
        close(pipes[i][0]);  // TODO: close the read end of the current pipe
        dup2(pipes[i][1], STDOUT_FILENO);  //TODO: redirect stdout to write to the current pipe
        close(pipes[i][1]);  // TODO: the write end of the current pipe
    }
    else {
        // Read from the previous pipe
        close(pipes[i-1][1]);  //TODO: close the write end of the previous pipe
        dup2(pipes[i-1][0], STDIN_FILENO);  //TODO: redirect stdin to read from the previous pipe
        close(pipes[i-1][0]);  //TODO: close the read end after redirection

        for (int j = 0; j < num_cmds - 1; j++) {
            if (j != i && j != i - 1) { 
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
        }
    }
}

int
redirect_file_outputs(struct msh_command *cmd) {
    char* outfile = NULL;
    char* errfile = NULL;
    int open_flags = 0;

    msh_command_file_outputs(cmd, &outfile, &errfile);

    if (outfile != NULL) {
        if (cmd->stdout_append) {
            open_flags = O_APPEND;
        }
        else {
            open_flags = O_TRUNC;
        }

        int fd = open(outfile, O_WRONLY | O_CREAT | open_flags, 0666);
        if (fd == -1) {
            perror("open file redirection failed");
            return -1;
        }
        dup2(fd, STDOUT_FILENO);
        close(fd);
    }

    if (errfile != NULL) {
        if (cmd->stderr_append) {
            open_flags = O_APPEND;
        }
        else {
            open_flags = O_TRUNC;
        }

        int fd = open(errfile, O_WRONLY | O_CREAT | open_flags, 0666);
        if (fd == -1) {
            perror("open file redirection failed");
            return -1;
        }
        dup2(fd, STDERR_FILENO);
        close(fd);
    }
    return 0;
}

int
fork_and_exec(struct msh_pipeline *p, int num_cmds, int pipes[][2], pid_t pids[]) {
    // Fork n child processes
    for (int i = 0; i < num_cmds; i++) {
        pids[i] = fork();
        if (pids[i] == -1) {
            return -1;
        }

        if (pids[i] == 0) { 
            signal(SIGINT, SIG_DFL);
            signal(SIGTSTP, SIG_DFL);
            signal(SIGCHLD, SIG_DFL);

            if (num_cmds > 1) {
                redirect_pipes(i, num_cmds, pipes);
            }
            
            struct msh_command *cmd = msh_pipeline_command(p, i);

            if(redirect_file_outputs(cmd) != 0) {
                exit(0);
            }

            execvp(msh_command_program(cmd), msh_command_args(cmd));
            exit(0);
        }
    }
    return 0;
}

void
add_background_job(struct msh_pipeline *p, int num_cmds, pid_t pids[]) {
    int idx = add_job(pids[num_cmds-1], msh_pipeline_input(p));
    job_list[idx].status = JOB_RUNNING;
    return;
}

void
wait_foreground_job(struct msh_pipeline *p, int num_cmds, pid_t pids[]) {
    foreground_job.nprocs = num_cmds;
    for (int j = 0; j < num_cmds; j++) {
        foreground_job.pids[j] = pids[j];
    }
    is_fg_active = 1;
    foreground_process = msh_pipeline_input(p);
    
    int fg_jobs_left = foreground_job.nprocs;
    int status;
    while(fg_jobs_left > 0) {
        pid_t wait_pid = waitpid(foreground_job.pids[0], &status, WUNTRACED);

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
    is_fg_active = 0;
    foreground_process = NULL;
    foreground_pid = -1;
    return;
}

void
msh_execute(struct msh_pipeline *p) {
	struct msh_command *first_cmd = msh_pipeline_command(p,0);
    if (first_cmd == NULL || first_cmd->cmd_string == NULL || strlen(first_cmd->cmd_string) == 0) {
        return;
    }

    if (check_builtin(first_cmd)) {
        return;
    }

    int num_cmds = p->num_commands;  // The number of processes 
    int pipes[MSH_MAXCMNDS-1][2];  // Array of pipes to connect processes (for n processes, we need n-1 pipes)
    pid_t pids[MSH_MAXCMNDS]; 

    // Create the n-1 pipes for the processes 
    if (num_cmds > 1) {
        if(create_pipes(pipes, num_cmds) == -1) {
            perror("create pipes failed");
            return;
        }
    }

    if(fork_and_exec(p, num_cmds, pipes, pids) == -1) {
        perror("fork and exec failed");
        close_pipes(pipes, num_cmds);
        return;
    }

    if(num_cmds > 1) {
        close_pipes(pipes, num_cmds);
    }

    if (msh_pipeline_background(p)) {
        add_background_job(p, num_cmds, pids);
    }
    else {
        wait_foreground_job(p, num_cmds, pids);
    }
}

void 
sig_handler(int signal) {
    if (signal == SIGINT) {
        if (!is_fg_active) {
            return;
        }
        
        for (int i = 0; i < foreground_job.nprocs; i++) {
            kill(foreground_job.pids[i], SIGTERM);
        }
    }
    else if (signal == SIGTSTP) {
        if (!is_fg_active) {
            return;
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

        for (int i = 0; i < j_count; i++) {
            wait_pid = waitpid(job_list[i].id, &status, WNOHANG);
            if (wait_pid > 0) {
                remove_job(wait_pid);
            }
        }
        return;
    }

    foreground_pid = -1;
    foreground_process = NULL;
    is_fg_active = 0;
}

void
msh_init(void) {
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

    struct sigaction sa_chld = sa;
    sa_chld.sa_flags |= SA_RESTART;
    if(sigaction(SIGCHLD, &sa_chld, NULL) == -1) {
        perror("sigaction failed");
    }

	return;
}
