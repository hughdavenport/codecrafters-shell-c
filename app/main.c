#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <pwd.h>

#include <sys/wait.h>

#define UNIMPLENTED(msg) do { fprintf(stderr, "%s:%d: UNIMPLENTED: %s", __FILE__, __LINE__, msg); exit(1); } while (false)
#define UNREACHABLE(msg) do { fprintf(stderr, "%s:%d: UNREACHABLE", __FILE__, __LINE__); exit(1); } while (false)

typedef struct {
  char *command;
  char *description;
  int (*function)(char *program, char *rest, char *delim);
} command_t;

#define COMMAND(name, desc) (command_t){ \
    .command = #name, \
    .description = (desc), \
    .function = name ## _command \
}

#define ARRAY(X) \
struct { \
  size_t capacity; \
  size_t size; \
  X *data; \
}

#define ARRAY_ADD(arr, value) do { \
  if ((arr).size + 1 > (arr).capacity) { \
    size_t new_capacity = (arr).capacity == 0 ? 16 : (arr).capacity * 2; \
    (arr).data = realloc((arr).data, sizeof((arr).data[0]) * new_capacity); \
    assert((arr).data != NULL); \
    (arr).capacity = new_capacity; \
  } \
  (arr).data[(arr).size ++] = (value); \
} while (false)

ARRAY(command_t) builtins = {0};

char *read_arg(char *string, const char *delim, char **rest) {
  // Strip delim from start
  char *start = string;
  while (*start != '\0' && *start != '\n' && strchr(delim, *start) != NULL) start ++;

  char *p = start;
  if (*start == '\0') return NULL;
  if (*start == '\n') return NULL;

  if (*p == '"') {
    UNIMPLENTED("Double quoted arguments");
  } else if (*p == '\'') {
    UNIMPLENTED("Single quoted arguments");
  } else {
    while (*p != '\0' && *p != '\n' && strchr(delim, *p) == NULL) p ++;
    *rest = p;
    return strndup(start, p - start);
  }
}

extern char **environ;
int run_program(char *file_path, char *command, char *rest, char *delim) {
  ARRAY(char *) argv = {0};
  ARRAY_ADD(argv, command);
  char *arg;
  while ((arg = read_arg(rest, delim, &rest)) != NULL) {
    ARRAY_ADD(argv, arg);
  }

  ARRAY_ADD(argv, NULL);
  pid_t pid = fork();
  switch (pid) {
    case -1:
      assert(false && "Out of memory");
      UNREACHABLE();
      return -1;

    case 0:
      assert(execve(file_path, argv.data, environ) != -1);
      UNREACHABLE();
      return -1;

    default: {
      int wstatus = 0;
      assert(waitpid(pid, &wstatus, 0) != -1);
      for (size_t i = 1; i < argv.size; i ++) {
        free(argv.data[i]);
      }
      free(argv.data);
      return WEXITSTATUS(wstatus);
    }
  }
  UNREACHABLE();
  return -1;
}


int help_command(char *command, char *rest, char *delim) {
    printf("Available commands:\n");
    for (size_t i = 0; i < builtins.size; i ++) {
      command_t cmd = builtins.data[i];
      printf("    %-10s - %s\n", cmd.command, cmd.description);
    }
    return 0;
}

int exit_command(char *command, char *rest, char *delim) {
  char *arg = read_arg(rest, delim, &rest);

  int code = 0;
  if (arg != NULL) {
    char *end;
    code = strtol(arg, &end, 0);
    if (*end != '\0') {
      fprintf(stderr, "%s: numeric argument required\n", command);
      return 1;
    }
    if (code < 0 || code > 255) {
      fprintf(stderr, "%s: exit code must be 0-255\n", command);
      return 1;
    }
  }
  free(arg);
  free(builtins.data);
  exit(code);
  UNREACHABLE();
  return 0;
}

int echo_command(char *command, char *rest, char *delim) {
  // Get each argument
  char *arg = NULL;
  bool first = true;
  while ((arg = read_arg(rest, delim, &rest)) != NULL) {
    if (!first) printf(" ");
    else first = false;
    printf("%s", arg);
    free(arg);
  }
  printf("\n");
  return 0;
}

int type_command(char *command, char *rest, char *delim) {
  char *arg = NULL;
  int ret = 0;
  while ((arg = read_arg(rest, delim, &rest)) != NULL) {
    bool found = false;
    for (size_t i = 0; i < builtins.size; i ++) {
      if (strcmp(arg, builtins.data[i].command) == 0) {
        printf("%s is a shell builtin\n", arg);
        found = true;
        break;
      }
    }
    if (found) {
      free(arg);
      continue;
    }

    if (strchr(arg, '/') != NULL && access(arg, R_OK | X_OK) == 0) {
      printf("%s is %s\n", arg, arg);
      free(arg);
      continue;
    }

    char *path = getenv("PATH");
    if (path != NULL) {
      char *p = path;
      while (*p != '\0') {
        while (*p != '\0' && *p != ':') p ++;
        char *file_path = NULL;
        char *path_name = strndup(path, (int)(p - path));
        assert(asprintf(&file_path, "%s/%s", path_name, arg) != 0);
        free(path_name);
        if (access(file_path, R_OK | X_OK) == 0) {
          printf("%s is %s\n", arg, file_path);
          free(file_path);
          found = true;
          break;
        }
        if (*p != '\0') path = ++p;
        free(file_path);
      }
    }
    if (found) {
      free(arg);
      continue;
    }

    ret = 1;
    printf("%s: not found\n", arg);
    free(arg);
  }

  return ret;
}

int pwd_command(char *command, char *rest, char *delim) {
  char buf[4096] = {0};
  char *cwd = getcwd(buf, 4096);
  assert(cwd != NULL);
  printf("%s\n", cwd);
  return 0;
}

int cd(char *file_path) {
  int ret = chdir(file_path);
  if (ret < 0) {
    switch (errno) {
      case EACCES:
        fprintf(stderr, "cd: %s: Permission denied\n", file_path);
        return 1;

      case ENOENT:
      case ENOTDIR:
        fprintf(stderr, "cd: %s: No such file or directory\n", file_path);
        return 1;
    }
    assert(false && "Uncaught error occured in chdir");
    UNREACHABLE();
    return 1;
  }
  return 0;
}

int cd_command(char *command, char *rest, char *delim) {
  char *arg = read_arg(rest, delim, &rest);
  if (arg == NULL) {
    char *home = getenv("HOME");
    if (home == NULL) {
      printf("cd: HOME not set\n");
      return 1;
    }
    return cd(home);
  }

  char *next = read_arg(rest, delim, &rest);
  if (next != NULL) {
    printf("cd: too many arguments\n");
    return 1;
  }

  int ret = 0;
  if (arg[0] == '~') {
    struct passwd *passwd = NULL;
    switch (arg[1]) {
      case '\0': {
        free(arg);
        char *home = getenv("HOME");
        if (home == NULL) {
          printf("cd: HOME not set\n");
          return 1;
        }
        return cd(home);
      }; break;

      case '/': {
        char *home = getenv("HOME");
        if (home == NULL) {
          printf("cd: HOME not set\n");
          return 1;
        }
        size_t len = strlen(home);
        size_t arg_len = strlen(arg + 1);
        char *full_path = malloc(len + arg_len + 1);
        assert(full_path != NULL);
        strcpy(full_path, home);
        strcpy(full_path + len, arg + 1);
        full_path[len + arg_len] = '\0';
        free(arg);
        int ret = cd(full_path);
        free(full_path);
        return ret;
      }; break;

      default: {
        // ~user
        while ((passwd = getpwent()) != NULL) {
          size_t len = strlen(passwd->pw_name);
          if (strncmp(arg + 1, passwd->pw_name, len) == 0) {
            setpwent();
            endpwent();
            switch (arg[len + 1]) {
              case '\0':
                free(arg);
                return cd(passwd->pw_dir);

              case '/': {
                size_t dir_len = strlen(passwd->pw_dir);
                size_t arg_len = strlen(arg + len + 1);
                char *full_path = malloc(dir_len + arg_len + 1);
                assert(full_path != NULL);
                strcpy(full_path, passwd->pw_dir);
                strcpy(full_path + dir_len, arg + len + 1);
                full_path[len + arg_len] = '\0';
                free(arg);
                ret = cd(full_path);
                free(full_path);
                return ret;
              }
            }
          }
        }
        setpwent();
        endpwent();
        // Haven't found user, fall out to normal cd
      }; break;
    }
  }
  ret = cd(arg);
  free(arg);
  return ret;
}

int main(int argc, char **argv) {
  ARRAY_ADD(builtins, COMMAND(help, "Displays help about commands."));
  ARRAY_ADD(builtins, COMMAND(exit, "Exit the shell, with optional code."));
  ARRAY_ADD(builtins, COMMAND(echo, "Prints any arguments to stdout."));
  ARRAY_ADD(builtins, COMMAND(type, "Prints the type of command arguments."));
  ARRAY_ADD(builtins, COMMAND(pwd, "Prints current working directory."));
  ARRAY_ADD(builtins, COMMAND(cd, "Change current working directory."));

  // Flush after every printf
  setbuf(stdout, NULL);
  char *program_path = argv[0];
  char *last_slash = strrchr(program_path, '/');
  char *program = program_path;
  if (last_slash != NULL) program = last_slash + 1;

  while (feof(stdin) == 0) {
    printf("$ ");

    // Wait for user input
    char input[100];
    if (fgets(input, 100, stdin) == NULL) break;

    char *delim = " \n";
    char *rest = NULL;
    char *command = read_arg(input, delim, &rest);
    if (command == NULL) continue;

    int code = -1;
    for (size_t i = 0; i < builtins.size; i ++) {
      if (strcmp(command, builtins.data[i].command) == 0) {
        free(command);
        // FIXME store return value
        code = builtins.data[i].function(program, rest, delim);
        break;
      }
    }
    if (code == -1) {
      if (strchr(command, '/') != NULL && access(command, R_OK | X_OK) == 0) {
        code = run_program(command, command, rest, delim);

        free(command);
        continue;
      }

      char *path = getenv("PATH");
      if (path != NULL) {
        char *p = path;
        while (*p != '\0') {
          while (*p != '\0' && *p != ':') p ++;
          char *file_path = NULL;
          char *path_name = strndup(path, (int)(p - path));
          assert(asprintf(&file_path, "%s/%s", path_name, command) != 0);
          free(path_name);
          if (access(file_path, R_OK | X_OK) == 0) {
            code = run_program(file_path, command, rest, delim);
            free(file_path);
            free(command);
            break;
          }
          if (*p != '\0') path = ++p;
          free(file_path);
        }
      }

      if (code == -1) {
        fprintf(stderr, "%s: command not found\n", command);
        free(command);
      }
    }
  }

  free(builtins.data);
  return 0;
}
