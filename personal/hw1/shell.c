#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include "tokenizer.h"

/* Convenience macro to silence compiler warnings about unused function parameters. */
#define unused __attribute__((unused))

/* Define standard Buffersize */
#define BUFFSIZE 4096

/* Whether the shell is connected to an actual terminal or not. */
bool shell_is_interactive;

/* File descriptor for the shell input */
int shell_terminal;

/* Terminal mode settings for the shell */
struct termios shell_tmodes;

/* Process group id for the shell */
pid_t shell_pgid;

/* Current working directory */
static char cdir[BUFFSIZE];

int cmd_exit(struct tokens *tokens);
int cmd_help(struct tokens *tokens);
int cmd_pwd(struct tokens *tokens);
int cmd_cd(struct tokens *tokens);

/* Built-in command functions take token array (see parse.h) and return int */
typedef int cmd_fun_t(struct tokens *tokens);

/* Built-in command struct and lookup table */
typedef struct fun_desc {
  cmd_fun_t *fun;
  char *cmd;
  char *doc;
} fun_desc_t;

fun_desc_t cmd_table[] = {
  {cmd_help, "?", "show this help menu"},
  {cmd_exit, "exit", "exit the command shell"},
  {cmd_pwd, "pwd", "view current directory"},
  {cmd_cd, "cd", "change directory to argument directory path"},
};

void set_sig_handler(__sighandler_t handler) {
    int signums[] = {SIGINT, SIGQUIT, SIGTTOU};

    for(unsigned int i = 0; i < sizeof(signums)/sizeof(int); i++) {
        if(signal(signums[i], handler) == SIG_ERR) {
            perror("signal");
            exit(1);
        }
    }
}

void ext_exec(char **args) {
    pid_t pid = fork();

    if(pid < 0) {
        perror("Fork failed");
    } else if(pid > 0) {
        int status;
        waitpid(pid, &status, WUNTRACED | WCONTINUED);
        tcsetpgrp(0, getpgrp());
    } else {
        setpgrp();
        tcsetpgrp(0, getpgrp());
        set_sig_handler(SIG_DFL);
        char prog[BUFFSIZE];
        if(strstr(args[0], "/") == 0) {
            char *poss_paths = getenv("PATH");
            char *path = strtok(poss_paths, ":");
            while(path != NULL) {
                sprintf(prog, "%s/%s", path, args[0]);
                execv(prog, args);
                path = strtok(NULL, ":");
            }
        } else {
            strcpy(prog, args[0]);
            execv(prog, args);
        }
        if(errno) {
            perror("Program execution error");
            exit(0);
        }
    }
}

/* Prints a helpful description for the given command */
int cmd_help(unused struct tokens *tokens) {
  for (unsigned int i = 0; i < sizeof(cmd_table) / sizeof(fun_desc_t); i++)
    printf("%s - %s\n", cmd_table[i].cmd, cmd_table[i].doc);
  return 1;
}

/* Exits this shell */
int cmd_exit(unused struct tokens *tokens) {
  exit(0);
}

/* Prints current directory */
int cmd_pwd(unused struct tokens *tokens) {
    if(getcwd(cdir, sizeof(cdir)) != NULL) { printf("%s\n", cdir); return 0; }
    printf("pwd: Error");
    return -1;
}

/* Move to directory given in argument */
int cmd_cd(struct tokens *tokens) {
    if(chdir(tokens_get_token(tokens, 1)) == 0) {
        strcpy(cdir, tokens_get_token(tokens, 1));
        return 0;
    }
    printf("cd: Error");
    return -1;
}

/* Looks up the built-in command, if it exists. */
int lookup(char cmd[]) {
  for (unsigned int i = 0; i < sizeof(cmd_table) / sizeof(fun_desc_t); i++)
    if (cmd && (strcmp(cmd_table[i].cmd, cmd) == 0))
      return i;
  return -1;
}

/* Intialization procedures for this shell */
void init_shell() {
  /* Our shell is connected to standard input. */
  shell_terminal = STDIN_FILENO;

  /* Check if we are running interactively */
  shell_is_interactive = isatty(shell_terminal);

  if (shell_is_interactive) {
    /* If the shell is not currently in the foreground, we must pause the shell until it becomes a
     * foreground process. We use SIGTTIN to pause the shell. When the shell gets moved to the
     * foreground, we'll receive a SIGCONT. */
    while (tcgetpgrp(shell_terminal) != (shell_pgid = getpgrp()))
      kill(-shell_pgid, SIGTTIN);

    /* Saves the shell's process id */
    shell_pgid = getpid();

    /* Take control of the terminal */
    tcsetpgrp(shell_terminal, shell_pgid);

    /* Save the current termios to a variable, so it can be restored later. */
    tcgetattr(shell_terminal, &shell_tmodes);
  }
}

int main(unused int argc, unused char *argv[]) {
  init_shell();

  static char line[BUFFSIZE];
  int line_num = 0;

  /* Please only print shell prompts when standard input is not a tty */
  if (shell_is_interactive)
    fprintf(stdout, "%d: ", line_num);

  set_sig_handler(SIG_IGN);

  while (fgets(line, BUFFSIZE, stdin)) {
    /* Split our line into words. */
    struct tokens *tokens = tokenize(line);

    /* Build args and set file_out */
    char *args[BUFFSIZE];
    int arg_i = 0;
    char *file_out = "";
    int redirect_type = 0; // 0: none; 1: file overwrite; 2: file append
    for(unsigned int i = 0; i < tokens_get_length(tokens); i++) {
        char *tok_i = tokens_get_token(tokens, i);
        if(strcmp(tok_i, "<") == 0 || strcmp(tok_i, ">") == 0
           || strcmp(tok_i, "<<") == 0 || strcmp(tok_i, ">>") == 0) {

            if(strlen(tok_i) == 2) { redirect_type = 2; }
            else { redirect_type = 1; }

            /* If redirecting to or from nothing, display help */
            if(i == 0 || i == tokens_get_length(tokens)-1) {
                perror("File redirect error");
                memset(args, '\0', sizeof(args));
                args[0] = "?";
                break;
            } else {
                /* If file comes first, reset args and set file to last seen thing */
                if(strcmp(tok_i, "<") == 0 || strcmp(tok_i, "<<") == 0) {
                    memset(args, '\0', sizeof(args));
                    arg_i = 0;
                    file_out = tokens_get_token(tokens, i-1);
                } else {
                    /* If file comes second, stop adding to args, and set the next thing to file */
                    arg_i = -1;
                    file_out = tokens_get_token(tokens, i+1);
                }
            }
        } else {
            if(arg_i >= 0) { args[arg_i] = tok_i; arg_i++; }
        }
    }

    /* If there's a redirect, set it to the file */
    int saved_stdout = dup(1);
    if(redirect_type == 1) {
        int fd = open(file_out, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
        dup2(fd, 1);
    } else if(redirect_type == 2) {
        int fd = open(file_out, O_RDWR | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR);
        dup2(fd, 1);
    } else if(strcmp(file_out, "") != 0) { perror("Weird redirect error"); continue; }

    /* Find which built-in function to run. */
    int fundex = lookup(args[0]);

    if (fundex >= 0) {
        cmd_table[fundex].fun(tokens);
    } else {
        ext_exec(args);
    }

    /* Close file if there's a redirect */
    if(strcmp(file_out, "") != 0) { dup2(saved_stdout, 1); }

    /* Reset args */
    memset(args, '\0', sizeof(args));

    if (shell_is_interactive)
      /* Please only print shell prompts when standard input is not a tty */
      fprintf(stdout, "%d: ", ++line_num);

    /* Clean up memory */
    tokens_destroy(tokens);
  }

  return 0;
}
