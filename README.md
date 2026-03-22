# Mini Shell (`msh`)

A small Unix shell written in C: it reads lines interactively, parses **command sequences** and **pipelines**, runs external programs with **`fork` / `exec`**, handles **redirection**, **foreground and background** jobs, and **signals** (`SIGINT`, `SIGTSTP`, `SIGCHLD`) so Ctrl+C and Ctrl+Z behave sensibly for foreground work. Built as a **systems programming** project focused on process control, pipes, and the terminal.

---

## Design & scope

**What a shell does (conceptually).** The shell sits in a loop: read a line of input, turn it into structured commands, then **execute** them. Execution means creating **child processes** with **`fork`**, replacing them with programs via **`exec`**, wiring **`stdin`/`stdout`/`stderr`** together with **pipes** and **file descriptors**, and **`wait`** ing (or not) depending on whether the pipeline is **foreground** or **background**. The shell process itself must stay alive to run **built-ins** (e.g. change directory) and to keep **job state** and **signal handlers** in one place.

**Pipelines.** A pipeline is a chain of commands where the output of one stage feeds the next. The implementation uses **`pipe`**, **`dup`/`dup2`**, and careful **`close`** so each child only sees the fds it needs.

**Foreground vs background.** A foreground pipeline is one the shell **blocks** on until it finishes (or stops on a signal). A trailing **`&`** marks a **background** pipeline: the shell records it and returns to the prompt without waiting, but must still **reap** children (e.g. `waitpid` with `WNOHANG` and/or `SIGCHLD`) so zombies don’t accumulate.

**Signals.** The terminal sends **SIGINT** (Ctrl+C) and **SIGTSTP** (Ctrl+Z) to the foreground group. This shell installs handlers so those signals can interrupt or stop a foreground pipeline instead of killing the shell itself. **SIGCHLD** is used when the design needs to notice when children exit.

**What this is *not* (intentional).** This is a **minimal** shell for learning, **not** a POSIX shell or bash clone. It does **not** aim for full **terminal job control** (e.g. running `vim`/`top` with full TTY semantics), **wildcard** expansion, **quoted** string rules, or **process groups** the way production shells do. The goal is a **clear, correct core**: parse, pipeline, redirect, wait, and signal handling **without** the full complexity of a traditional interactive shell.

---

## Features (high level)

- **Parsing** — User input is turned into sequences of pipelines and commands (parser lives under `mshparse/`).
- **Pipelines** — Multiple commands connected with `|`, with `stdin`/`stdout` wired through pipes.
- **Background jobs** — Trailing `&` runs a pipeline without blocking the shell.
- **Built-ins** — Shell-internal commands (e.g. `cd`, `exit`) run without `exec` in a child.
- **Job control** — Track jobs, bring them to the foreground / background where implemented (`jobs`, `fg`, `bg`).
- **File redirection** — Redirect stdout/stderr with `>`, `>>`, `2>`, etc., as specified by the assignment.
- **Signals** — Shell installs handlers so keyboard signals can reach the foreground pipeline.
- **Line editing** — Interactive input uses **[linenoise](https://github.com/gwu-cs-sysprog/linenoise)** (readline-style editing, history, tab completion hooks).

Exact behavior matches the course specification; see the original assignment README in the course repo if you need full grammar and edge cases.

---

## Repository layout

| Path | Role |
|------|------|
| `msh_main.c` | Entry point, readline loop, optional completion trie seeding |
| `msh_execute.c` | `fork`, `exec`, pipes, redirection, job list, signal setup |
| `mshparse/` | Parser library (`msh_sequence`, `msh_pipeline`, `msh_command`) |
| `ptrie.c` / `ptrie.h` | Prefix trie used for command-name frequency / autocomplete |
| `ln/` | **Not stored in Git** — fetched at build time (see below) |
| `tests/` | Course test harnesses |

---

## Build

**Requirements:** `gcc`, `make`, `git`, `wget`, standard POSIX libc.

From the repo root:

```bash
make
```

The Makefile downloads course **util** tarball and **`git clone`s [linenoise](https://github.com/gwu-cs-sysprog/linenoise)** into `ln/` if missing, then builds static libraries and the `msh` binary.

```bash
./msh
```

Type `exit` or an empty line (per the course shell rules) to leave the shell.

---

## Tests

```bash
make test
```

---

## Credits

This project started from the **GWU CSCI 2410** (“Systems Programming”) shell assignment. The handout shipped **APIs and stubs**, not a complete shell:

| Source | Role |
|--------|------|
| **`msh.h`** | Public types, limits, `msh_err_t`, `msh_pipeline_err2str`, declarations for `msh_init` / `msh_execute`. |
| **`msh_parse.h`** | Parser/shell API (sequence, pipeline, command accessors). |
| **`msh_parse.c`** (template) | Stub implementations only (`msh_sequence_alloc` returns `NULL`, `msh_sequence_parse` is a no-op, accessors return `NULL`/`0`, frees are empty). **Replaced** by a full parser implementation for the milestones. |
| **`msh_main.c`** (template) | Minimal REPL: `linenoise` input, `msh_sequence_parse` / dequeue / `msh_execute` loop, empty-line exit behavior. **Extended** with shell-specific features (e.g. history, completion trie, linenoise hooks). |
| **`msh_execute.c`** (template) | Empty `msh_execute` / `msh_init`. **Replaced** with the real process/pipe/redirection/signal/job-control logic. |
| **`Makefile`, `tests/`, `util/`** | Course build and test harness. |
| **`ln/` ([linenoise](https://github.com/gwu-cs-sysprog/linenoise))** | Third-party line editing; **BSD-style** license; cloned at **`make`** time (not always committed). |

**Implemented on top of that template** (my work for the course): the body of **`msh_parse.c`**, all of **`msh_execute.c`** (and `msh_init`), **`ptrie.c` / `ptrie.h`**, and non-trivial edits to **`msh_main.c`** (and any other files required by the milestones).

---

## License

- **Course materials** (`msh.h`, `msh_parse.h`, original stub layout, Makefile/tests layout) remain **subject to the course’s terms**; this README is for portfolio attribution only, not a claim to redistribute the handout independently of GWU’s rules.
- **Linenoise** is **BSD-licensed**; see `ln/LICENSE` after running `make` (or upstream [linenoise](https://github.com/antirez/linenoise)).

---

## Author

**Anand** — [github.com/AnandB18](https://github.com/AnandB18)

