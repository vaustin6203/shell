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

/* Whether the shell is connected to an actual terminal or not. */
bool shell_is_interactive;

/* File descriptor for the shell input */
int shell_terminal;

/* Terminal mode settings for the shell */
struct termios shell_tmodes;

/* Process group id for the shell */
pid_t shell_pgid;

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
  {cmd_pwd, "pwd", "print current working directory"},
  {cmd_cd, "cd", "changes current working directory to the directory passed as an argument."}
};

/* Prints a helpful description for the given command */
int cmd_help(unused struct tokens *tokens) {
  for (unsigned int i = 0; i < sizeof(cmd_table) / sizeof(fun_desc_t); i++)
    printf("%s - %s\n", cmd_table[i].cmd, cmd_table[i].doc);
  return 1;
}

/* Exits this shell */
int cmd_exit(struct tokens *tokens) {
  tokens_destroy(tokens);
  exit(0);
}

/*Prints the current working directory to standard output. */
int cmd_pwd(unused struct tokens *tokens) {
    char *buff;
    buff = (char *) malloc(100 * sizeof(char));
    getcwd(buff, 100);
    fprintf(stdout,"%s\n", buff);
    free(buff);
    return 1;
}

/*Changes the current working directory to the directory passed as an argument. */
int cmd_cd(struct tokens *tokens) {
    chdir(tokens_get_token(tokens, 1));
    return 1;
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

int redirect_input(struct tokens *tokens) {
    size_t len = tokens_get_length(tokens);
    int index = -1;
    char *token_check;

    for (int i = 0; i < len; i++) {
        token_check = tokens_get_token(tokens, i);
	if (strcmp(token_check, "<") == 0) {
	    index = i + 1;
	    return index;
	}
    }
    return index;
}

int redirect_output(struct tokens *tokens) {
    size_t len = tokens_get_length(tokens);
    int index = -1;
    char *token_check;

    for (int i = 0; i < len; i++) {
        token_check = tokens_get_token(tokens, i);
        if (strcmp(token_check, ">") == 0) {
            index = i + 1;
            return index;
        }
    }
    return index;
}


bool  get_func(char *func) {
    char *path = getenv("PATH");
    char *token = strtok(path, ":");
    char *fullpath = malloc(1024*sizeof(char));

     while (token != NULL) {
	 strcpy(fullpath,token);
	 strcat(fullpath, "/");
	 strcat(fullpath, func);
	 if (access(fullpath, F_OK) == 0) {
	     strcpy(func, fullpath);
	     free(fullpath);
	     return true;
         }
	 token = strtok(NULL, ":");
     }
     free(fullpath);
     return false;
}

int main(unused int argc, unused char *argv[]) {
  init_shell();

  static char line[4096];
  int line_num = 0;

  /* Please only print shell prompts when standard input is not a tty */
  if (shell_is_interactive)
    fprintf(stdout, "%d: ", line_num);

  while (fgets(line, 4096, stdin)) {
    /* Split our line into words. */
    struct tokens *tokens = tokenize(line);

    /* Find which built-in function to run. */
    int fundex = lookup(tokens_get_token(tokens, 0));

    if (fundex >= 0) {
      cmd_table[fundex].fun(tokens);
    } else {
      /* REPLACE this to run commands as programs. */
      int status;
      pid_t cpid = fork();
      if (cpid == 0) {
	  size_t num_args = tokens_get_length(tokens);
          char *args[num_args + 1];

	  int index = redirect_input(tokens);
	  if (index > 0) {
	      char *in_file = tokens_get_token(tokens, index);
	      int in = open(in_file, O_RDONLY);
	      if (in == -1) {
		  perror("Cannot open input file");
		  return 0;
	      } else {
		  dup2(in, 0);
		  close(in);
	      }
	  }

         
          for (int i = 0; i < num_args; i++) {
         	args[i] = tokens_get_token(tokens, i);
     	  }

          args[num_args] = NULL;
      	  bool found = true;

      	  if (access(args[0], F_OK) != 0) {
        	found = get_func(args[0]);
	  }	
	  if (!found) {
            	printf("the file doesn't exist\n");
		return 1;
       	  } else {
	      	index = redirect_output(tokens);
		if (index > 0) {
		    char* file = tokens_get_token(tokens, index);
		    int out = open(file, O_WRONLY | O_TRUNC | O_CREAT, S_IRUSR | S_IRGRP | S_IWGRP | S_IWUSR);
		    if (out == -1) {
			perror("Cannot open output file\n");
                        return 0;
		    } else {
			dup2(out, 1);
			close(out);
			execv(args[0], args);
		    }
		} else {
		    execv(args[0], args);
		}
	  }
      } else if (cpid > 0){
	  wait(&status);
      } else {
	  perror("fork failed\n");
      }
   }

    if (shell_is_interactive)
      /* Please only print shell prompts when standard input is not a tty */
      fprintf(stdout, "%d: ", ++line_num);

    /* Clean up memory */
    tokens_destroy(tokens);
  }
  return 0;
}
