#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
// #include <readline/readline.h>
// #include <readline/history.h>
#include <editline/readline.h>

#define BUFFER_SIZE 30
#define PROMPT_MAX_LEN (PATH_MAX + 5)

#define MAX_ALIASES 100
#define MAX_ALIAS_NAME_LEN 256
#define MAX_ALIAS_COMMAND_LEN 256

typedef struct Alias {
  char name[MAX_ALIAS_NAME_LEN];
  char expansion[MAX_ALIAS_COMMAND_LEN];
} Alias;

typedef struct Prompt {
  char prompt[PROMPT_MAX_LEN];
  char *home;
  Alias **aliases;
  size_t alias_count;
  size_t max_aliases;
} Prompt;

void free_aliases(const Prompt *p) {
  for (size_t i = 0; i < p->alias_count; i++) {
    free(p->aliases[i]);
  }
}

void destroy_prompt(const Prompt *p) {
  free_aliases(p);
  free(p->aliases);
}

void expand_input(const Prompt *const p, char *buffer[]) {
  size_t i = 0;

  while (buffer[i] != NULL) {
    if (buffer[i][0] == '$') {
      const char *variable_name = buffer[i] + 1; // Skip the '$'
      const char *variable_value = getenv(variable_name);
      
      printf("var: %s\n", variable_value);
      printf("org: %s\n", variable_name);

      if (variable_value != NULL) {
        size_t new_size = strlen(variable_value);
        char *expanded_token = (char *)malloc(new_size + 1);

        if (expanded_token != NULL) {
          strcpy(expanded_token, variable_value);
          free(buffer[i]);
          buffer[i] = expanded_token;
        } else {
          fprintf(stderr, "Memory allocation failed for variable expansion.\n");
        }
      } else {
        strcpy(buffer[i], "");
      }
    }

    for (size_t j = 0; j < p->alias_count; j++) {
      if (strcmp(buffer[i], p->aliases[j]->name) == 0) {
        size_t new_size = strlen(p->aliases[j]->expansion);
        char *expanded_token = (char *)malloc(new_size + 1);

        if (expanded_token != NULL) {
          strcpy(expanded_token, p->aliases[j]->expansion);
          free(buffer[i]);
          buffer[i] = expanded_token;
        } else {
          fprintf(stderr, "Memory allocation failed for alias expansion.\n");
        }
      }
    }
    i++;
  }
}

char *get_line(const Prompt *const p) {
  // printf("%s", p->prompt);

  char *line = NULL;

  if ((line = readline(p->prompt)) == NULL) {
    perror("Error reading line");
    return false;
  }

  return line;
}

char **get_input(const Prompt *const p) {
  char *line = get_line(p);

  const char *token;
  size_t buffer_size = BUFFER_SIZE;
  char **tokens = (char **)malloc(sizeof(char *) * buffer_size);
  size_t pos = 0;

  if (tokens == NULL) {
    perror("Error allocating tokens");
    return false;
  }

  char *rest = NULL;

  token = strtok_r(line, " \n", &rest); // Tokenize by space and newline

  while (token != NULL) {
    tokens[pos] = strdup(token);
    pos++;

    if (pos >= buffer_size) {
      buffer_size += BUFFER_SIZE;
      char **tmp = realloc(tokens, sizeof(char *) * buffer_size);
      if (tmp == NULL) {
        perror("Error allocating tokens");
        return false;
      } else {
        tokens = tmp;
      }
    }

    token = strtok_r(NULL, " \n", &rest);
  }

  for (size_t j = pos; j < buffer_size; j++) {
    tokens[j] = NULL;
  }

  free(line);
  expand_input(p, tokens);

  return tokens;
}

void set_prompt(Prompt *const p) {
  char buffer[PROMPT_MAX_LEN];

  if (getcwd(buffer, sizeof(buffer)) == NULL) {
    perror("Cwd error");
    const char *prompt = ">>> ";
    strncpy(p->prompt, prompt, sizeof(char) * strlen(prompt));
  } else {

    if (strlen(buffer) >= PROMPT_MAX_LEN) {
      buffer[PROMPT_MAX_LEN - 1] = '\0';
    }

    snprintf(p->prompt, PROMPT_MAX_LEN, "%s\n>>> ", buffer);
  }
}

bool execute_external_command(char *const buffer[]) {
  pid_t child_pid = fork();

  if (child_pid == -1) {
    perror("Fork failed");
    return false;
  }

  if (child_pid == 0) {
    if (execvp(buffer[0], buffer) == -1) {
      perror("Exec failed");
      return false;
    }

  } else {
    int status;
    wait(&status);
  }

  return true;
}

bool change_dir(const Prompt *const p, const char *const arg) {
  if (arg == NULL || strcmp(arg, "~") == 0) {
    if (p->home == NULL) {
      fprintf(stderr, "cd: argument required\n");
      return false;
    } else {
      if (chdir(p->home) != 0) {
        perror("CD Error");
        return false;
      }
    }
  } else {

    if (chdir(arg) != 0) {
      perror("CD Error");
      return false;
    }
  }

  return true;
}

void set_alias(Prompt *const p, char *buffer[]) {
  const char *const name = buffer[1];
  const char *const expansion = buffer[2];

  if (name == NULL || expansion == NULL) {
    perror("Alias required 2 arguments");
    return;
  }

  if (strlen(name) > MAX_ALIAS_NAME_LEN ||
      strlen(expansion) > MAX_ALIAS_COMMAND_LEN) {
    perror("Alias too long");
    return;
  }

  if (p->alias_count >= p->max_aliases) {
    p->max_aliases += MAX_ALIASES;
    Alias **tmp = realloc(p->aliases, p->max_aliases);

    if (tmp == NULL) {
      perror("error allocating aliases");
      return;
    }
    p->aliases = tmp;
  }

  Alias *alias = (Alias *)malloc(sizeof(Alias));

  if (alias == NULL) {
    perror("Error creating alias");
    return;
  }

  strncpy(alias->expansion, expansion, MAX_ALIAS_COMMAND_LEN - 1);
  alias->expansion[MAX_ALIAS_COMMAND_LEN - 1] = '\0';
  strncpy(alias->name, name, MAX_ALIAS_NAME_LEN - 1);
  alias->name[MAX_ALIAS_NAME_LEN - 1] = '\0';

  p->aliases[p->alias_count] = alias;
  p->alias_count++;
}

bool execute_command(Prompt *const p, char **tokens) {
  if (tokens[0] == NULL) {
    return true;
  }

  if (strcmp(tokens[0], "exit") == 0) {
    return false;
  }

  if (strcmp(tokens[0], "unalias") == 0) {
    free_aliases(p);
    p->alias_count = 0;
    return true;
  }

  if (strcmp(tokens[0], "cd") == 0) {
    if (change_dir(p, tokens[1])) {
      set_prompt(p);
    }
    return true;
  }

  if (strcmp(tokens[0], "alias") == 0) {
    set_alias(p, tokens);
    return true;
  }

  return execute_external_command(tokens);
}

void run(Prompt *p) {
  set_prompt(p);
  do {
    char **buffer = get_input(p);
    if (!execute_command(p, buffer)) {
      free(buffer);
      break;
    }
    free(buffer);
  } while (true);
}

void init_prompt(Prompt *const p) {
  p->aliases = (Alias **)malloc(MAX_ALIASES * sizeof(Alias *));
  p->alias_count = 0;
  p->max_aliases = MAX_ALIASES;

  const char *const home = getenv("HOME");

  // check to prevent recursion errors
  if (home != NULL && strcmp(home, "~") != 0) {
    strncpy(p->home, home, PROMPT_MAX_LEN - 1);
    p->home[PROMPT_MAX_LEN - 1] = '\0';
  } else {
    p->home = NULL;
  }
}

int main(void) {
  signal(SIGINT, SIG_IGN);  // Ignore Ctrl+C
  signal(SIGTSTP, SIG_IGN); // Ignore Ctrl+Z

  Prompt p;
  init_prompt(&p);
  run(&p);
  destroy_prompt(&p);

  return 0;
}
