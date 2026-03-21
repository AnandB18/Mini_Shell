/*
 * msh_execute.c
 *
 * Runtime execution and job-control logic for the mini shell.
 *
 * Responsibilities:
 * - Execute parsed pipelines (`msh_execute`) by creating pipes, forking,
 *   applying redirection, and calling `execvp`.
 * - Handle built-ins that must run in the shell process:
 *   `cd`, `exit`, `jobs`, `fg`, and `bg`.
 * - Track foreground and background jobs (`job_list`, `foreground_job`),
 *   including transitions between RUNNING and STOPPED states.
 * - Manage process waiting behavior for foreground commands and reap
 *   completed background jobs.
 * - Handle shell signals (`SIGINT`, `SIGTSTP`, `SIGCHLD`) and forward
 *   control signals to active foreground children.
 *
 * Layout (top to bottom):
 *   1. Data structures mirroring parsed commands and job state.
 *   2. Job list helpers and job-control built-ins.
 *   3. Pipe creation, per-child wiring, and file redirection.
 *   4. `fork_and_exec`, `msh_execute`, and foreground wait/background register.
 *   5. Signal setup (`msh_init`) and `sig_handler`.
 *
 * Global state:
 *   `job_list` / `j_count` track background jobs. `foreground_job`,
 *   `is_fg_active`, `foreground_pid`, and `foreground_process` describe the
 *   current or last foreground pipeline for signal delivery and `fg`/`bg`.
 *   This file is single-threaded; globals are updated only from the main
 *   shell loop and signal handlers.
 *
 * Boundaries:
 * - Parsing and command-structure construction are handled by the parser
 *   module (`msh_parse`), not this file.
 * - This file assumes incoming `msh_pipeline` / `msh_command` objects are
 *   already syntactically valid.
 */

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

/* ------------------------------------------------------------------------- */
/* Parsed command / pipeline shapes (mirror what the parser fills in).       */
/* ------------------------------------------------------------------------- */

/*
 * One stage of a pipeline: program name, argv, optional stdout/stderr files.
 * Field meanings match the parser; accessors in msh_parse.h hide some layout.
 */
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

/*
 * Full user line: ordered commands, original text, background flag, pgid slot.
 */
struct msh_pipeline {
	struct msh_command *array_command[MSH_MAXCMNDS];
	char *pipeline;
	int num_commands;
	int background;
	pid_t pgid;
};

/* ------------------------------------------------------------------------- */
/* Background job table and foreground tracking                              */
/* ------------------------------------------------------------------------- */

typedef enum {
	JOB_RUNNING,
	JOB_STOPPED,
} job_status_t;

/*
 * One background (or stopped) job: display string, leader PID, run state.
 */
struct jobs {
	char *process;
	pid_t id;
	job_status_t status;
};

/*
 * Snapshot of the active foreground pipeline for signal handlers: all child
 * PIDs so SIGINT/SIGTSTP can target every stage.
 */
struct fg_job {
	int nprocs;
	pid_t pids[MSH_MAXCMNDS];
};

struct fg_job foreground_job = {.nprocs = 0, .pids = {0}};
int is_fg_active = 0;
pid_t foreground_pid = -1;
char *foreground_process = NULL;

struct jobs job_list[MSH_MAXBACKGROUND];
int j_count = 0;

/* ------------------------------------------------------------------------- */
/* Job list helpers                                                          */
/* ------------------------------------------------------------------------- */

/**
 * Append a background job to the global job list.
 *
 * Stores a strdup'd copy of `process` in the new job entry for `jobs`
 * output; that copy is owned by `job_list`.
 *
 * @param jpid PID of the job (used as the job identifier for lookup).
 * @param process Human-readable command line string for `jobs` output.
 * @return Index of the new entry in `job_list`.
 */
int
add_job(pid_t jpid, char* process) {
	job_list[j_count].process = strdup(process);
	job_list[j_count].id = jpid;
    job_list[j_count].status = JOB_RUNNING;

    int idx = j_count;
	j_count++;
	return idx;
}

/**
 * Remove the job whose PID matches `pid` from `job_list`, compacting the array.
 *
 * @param pid Process ID to remove.
 */
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

/**
 * Find the index in `job_list` for a given PID.
 *
 * @param pid Process ID to look up.
 * @return List index, or -1 if not found.
 */
int
get_job_idx(int pid) {
    for (int i = 0; i < j_count; i++) {
        if (job_list[i].id == pid){
            return i;
        }
    }

    return -1;
}

/* ------------------------------------------------------------------------- */
/* Built-in commands (run in the shell process, not via exec)                */
/* ------------------------------------------------------------------------- */

/**
 * Print the `jobs` built-in output.
 *
 * @param id If -1, print all jobs; otherwise print the single job at that index.
 */
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

/**
 * Bring a background job to the foreground (`fg` built-in).
 *
 * Marks the job as foreground, sends SIGCONT if it was stopped, waits until
 * the child reports a state change, then removes it from the job list and
 * clears foreground globals.
 *
 * @param idx Index into `job_list` (same numbering as `jobs` output).
 */
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

/**
 * Resume a stopped background job without waiting (`bg` built-in).
 *
 * No-op if the index is invalid or the job is already running.
 *
 * @param idx Index into `job_list`.
 */
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

/**
 * Change the shell's working directory (`cd` built-in).
 *
 * Supports no argument (HOME), `~`, and `~/path` by expanding with $HOME.
 * Prints an error if `chdir` fails.
 *
 * @param c Command whose argv[1] is the target path, if any.
 */
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

/**
 * Parse a job index from `fg` / `bg` argv.
 *
 * If `array_arg[1]` is present, sets `*index` from it via atoi.
 * Otherwise uses the most recently added job (`j_count - 1`).
 *
 * @param index Out: job index into `job_list`.
 * @param c Command line for `fg` or `bg`.
 * @return 0 on success, -1 if no job exists to default to.
 */
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

/* ------------------------------------------------------------------------- */
/* Pipes and file redirection (mostly used in child after fork)              */
/* ------------------------------------------------------------------------- */

/**
 * Create all pipe pairs needed for a pipeline of `num_cmds` processes.
 *
 * @param pipes Out: `num_cmds - 1` rows of [read_fd, write_fd].
 * @param num_cmds Number of commands in the pipeline.
 * @return 0 on success, -1 if any pipe() fails.
 */
int
create_pipes(int pipes[][2], int num_cmds) {
    for (int i = 0; i < num_cmds - 1; i++) {
        if (pipe(pipes[i]) == -1) {
            return -1;
        }
    }
    return 0;
}

/**
 * Close every fd in the pipe array (parent side after forking children).
 *
 * @param pipes Pipe array from create_pipes.
 * @param num_cmds Number of commands (closes `num_cmds - 1` pipes).
 */
void
close_pipes(int pipes[][2], int num_cmds) {
    for (int i = 0; i < num_cmds - 1; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }
    return;
}

/**
 * Wire stdin/stdout for child `i` in a multi-command pipeline.
 *
 * First command writes to pipes[0]; middle commands read/write between
 * adjacent pipes; last command reads from the previous pipe only and
 * closes unused pipe fds so reads/writes do not hang.
 *
 * @param i Zero-based index of this child in the pipeline.
 * @param num_cmds Total commands in the pipeline.
 * @param pipes Pipe array (length num_cmds - 1).
 */
void
redirect_pipes(int i, int num_cmds, int pipes[][2]) {
    if (i == 0) {
        dup2(pipes[i][1], STDOUT_FILENO);
        close(pipes[i][1]);
        close(pipes[i][0]);
    }
    else if (i > 0 && i < num_cmds - 1) {
        /* stdin from previous stage */
        close(pipes[i - 1][1]);
        dup2(pipes[i - 1][0], STDIN_FILENO);
        close(pipes[i - 1][0]);

        /* stdout to next stage */
        close(pipes[i][0]);
        dup2(pipes[i][1], STDOUT_FILENO);
        close(pipes[i][1]);
    }
    else {
        /* Last stage: stdin from previous pipe only */
        close(pipes[i - 1][1]);
        dup2(pipes[i - 1][0], STDIN_FILENO);
        close(pipes[i - 1][0]);

        /* Close every other pipe so the pipeline cannot deadlock */
        for (int j = 0; j < num_cmds - 1; j++) {
            if (j != i && j != i - 1) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
        }
    }
}

/**
 * Apply per-command stdout/stderr file redirection after pipe setup.
 *
 * Opens paths from the parsed command (create/truncate or append), dup2s
 * to STDOUT_FILENO / STDERR_FILENO, then closes the open fds.
 *
 * @param cmd Command whose optional stdout/stderr files are applied.
 * @return 0 on success, -1 if open fails (child should not exec).
 */
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

/* ------------------------------------------------------------------------- */
/* Fork/exec, parent wait, and top-level execute                             */
/* ------------------------------------------------------------------------- */

/**
 * Register a background pipeline in the job list after fork.
 *
 * Uses the last child's PID and the full pipeline string for display.
 *
 * @param p Parsed pipeline (for input string).
 * @param num_cmds Number of children in `pids`.
 * @param pids Child PIDs from fork_and_exec.
 */
void
add_background_job(struct msh_pipeline *p, int num_cmds, pid_t pids[]) {
    int idx = add_job(pids[num_cmds-1], msh_pipeline_input(p));
    job_list[idx].status = JOB_RUNNING;
    return;
}

/**
 * Wait for completion (or stop) of a foreground pipeline.
 *
 * Tracks foreground PIDs, waits for child state changes, and clears
 * foreground state when the active job is no longer running.
 *
 * @param p Pipeline being run in the foreground.
 * @param num_cmds Number of child processes in the job.
 * @param pids Child PIDs for this foreground job.
 *
 * Note: Uses waitpid on pids[0] repeatedly; each reaped child is cleared
 * from foreground_job.pids[] until all stages have reported.
 */
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
    while (fg_jobs_left > 0) {
        /* Wait for any state change (exit or stop) on the first live pid */
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

/**
 * Check and execute shell built-in commands.
 *
 * Supports built-ins that must run in-process (`cd`, `exit`, `jobs`,
 * `fg`, `bg`). If the command is not a built-in, no action is taken.
 *
 * @param c Command to inspect/execute.
 * @return 1 if a built-in was handled, 0 otherwise.
 */
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
		} else {
			msh_jobs(-1);
		}
		return 1;
	}
	if (strcmp(msh_command_program(c), "fg") == 0) {
		int index = -1;
		if (parse_input_idx(&index, c) == -1) {
			return 1;
		}
		msh_fg(index);
		return 1;
	}
	if (strcmp(msh_command_program(c), "bg") == 0) {
		int index = -1;
		if (parse_input_idx(&index, c) == -1) {
			return 1;
		}
		msh_bg(index);
		return 1;
	}
	return 0;
}

/**
 * fork_and_exec — fork one child per pipeline stage and exec the program.
 *
 * In each child: reset signal handlers to defaults, connect stdin/stdout
 * via redirect_pipes when num_cmds > 1, apply redirect_file_outputs, then
 * execvp. Parent returns after all forks succeed; parent must close pipes
 * and wait or register background jobs separately.
 *
 * @param p Parsed pipeline containing commands.
 * @param num_cmds Number of commands in the pipeline.
 * @param pipes Pre-created pipe fd array (size num_cmds - 1 when used).
 * @param pids Output array populated with child PIDs (parent only).
 * @return 0 on success, -1 if fork fails.
 */
int
fork_and_exec(struct msh_pipeline *p, int num_cmds, int pipes[][2], pid_t pids[]) {
	for (int i = 0; i < num_cmds; i++) {
		pids[i] = fork();
		if (pids[i] == -1) {
			return -1;
		}

		if (pids[i] == 0) {
			/* Child: restore default signals, then become the command */
			signal(SIGINT, SIG_DFL);
			signal(SIGTSTP, SIG_DFL);
			signal(SIGCHLD, SIG_DFL);

			if (num_cmds > 1) {
				redirect_pipes(i, num_cmds, pipes);
			}

			struct msh_command *cmd = msh_pipeline_command(p, i);

			if (redirect_file_outputs(cmd) != 0) {
				exit(0);
			}

			execvp(msh_command_program(cmd), msh_command_args(cmd));
			exit(0);
		}
		/* Parent: continues loop to fork remaining stages */
	}
	return 0;
}

/**
 * Execute a parsed pipeline.
 *
 * Handles built-ins directly in the shell process; otherwise creates any
 * needed pipes, forks child processes, applies redirections, and runs
 * external commands with execvp. Foreground pipelines are waited on;
 * background pipelines are recorded in the job list.
 *
 * @param p Parsed pipeline to execute.
 */
void
msh_execute(struct msh_pipeline *p) {
	struct msh_command *first_cmd = msh_pipeline_command(p, 0);

	if (first_cmd == NULL || first_cmd->cmd_string == NULL ||
	    strlen(first_cmd->cmd_string) == 0) {
		return;
	}

	/* Built-ins apply to the first word of the line only (single-stage). */
	if (check_builtin(first_cmd)) {
		return;
	}

	int num_cmds = p->num_commands;
	int pipes[MSH_MAXCMNDS - 1][2];
	pid_t pids[MSH_MAXCMNDS];

	if (num_cmds > 1) {
		if (create_pipes(pipes, num_cmds) == -1) {
			perror("create pipes failed");
			return;
		}
	}

	if (fork_and_exec(p, num_cmds, pipes, pids) == -1) {
		perror("fork and exec failed");
		close_pipes(pipes, num_cmds);
		return;
	}

	/* Parent must close all pipe ends so children see EOF correctly */
	if (num_cmds > 1) {
		close_pipes(pipes, num_cmds);
	}

	if (msh_pipeline_background(p)) {
		add_background_job(p, num_cmds, pids);
	} else {
		wait_foreground_job(p, num_cmds, pids);
    }
}

/* ------------------------------------------------------------------------- */
/* Signal handling                                                           */
/* ------------------------------------------------------------------------- */

/**
 * Handle interactive/job-control signals for the shell.
 *
 * - SIGINT: forwards termination to active foreground children.
 * - SIGTSTP: stops active foreground children and records as stopped job.
 * - SIGCHLD: reaps completed background jobs.
 *
 * @param signal Signal number delivered to the shell.
 */
void
sig_handler(int signal)
{
	if (signal == SIGINT) {
		if (!is_fg_active) {
			return;
		}
		for (int i = 0; i < foreground_job.nprocs; i++) {
			kill(foreground_job.pids[i], SIGTERM);
		}
	} else if (signal == SIGTSTP) {
		if (!is_fg_active) {
			return;
		}
		for (int i = 0; i < foreground_job.nprocs; i++) {
			kill(foreground_job.pids[i], SIGTSTP);
		}
		/* Record as stopped job so `fg` can continue it */
		if (foreground_process != NULL) {
			int idx = add_job(foreground_job.pids[0], foreground_process);
			job_list[idx].status = JOB_STOPPED;
		}
	} else if (signal == SIGCHLD) {
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

/**
 * Initialize shell signal handling.
 *
 * Installs handlers for SIGINT, SIGTSTP, and SIGCHLD so the shell can
 * control foreground/background jobs and reap completed children.
 *
 */
void
msh_init(void)
{
	struct sigaction sa;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = sig_handler;
	sigemptyset(&sa.sa_mask);

	if (sigaction(SIGINT, &sa, NULL) == -1) {
		perror("sigaction failed");
	}

	if (sigaction(SIGTSTP, &sa, NULL) == -1) {
		perror("sigaction failed");
	}

	/* SA_RESTART: allow wait/read in main loop to resume after SIGCHLD */
	struct sigaction sa_chld = sa;
	sa_chld.sa_flags |= SA_RESTART;
	if (sigaction(SIGCHLD, &sa_chld, NULL) == -1) {
		perror("sigaction failed");
	}
}
