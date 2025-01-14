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

typedef enum {
  UNQUOTED,
  SINGLE,
  DOUBLE,
} quote_mode;

#define ARRAY(X) \
struct { \
  size_t capacity; \
  size_t size; \
  X *data; \
}

#define ARRAY_ENSURE_CAPACITY(arr, cap) do { \
  if ((cap) > (arr).capacity) { \
    (arr).data = realloc((arr).data, sizeof((arr).data[0]) * (cap)); \
    assert((arr).data != NULL); \
    memset((arr).data + sizeof((arr).data[0]) * (arr).capacity, \
        '\0', sizeof((arr).data[0]) * ((cap) - (arr).capacity)); \
    (arr).capacity = (cap); \
  } \
} while (false)

#define ARRAY_ADD(arr, value) do { \
  if ((arr).size + 1 > (arr).capacity) { \
    size_t new_capacity = (arr).capacity == 0 ? 16 : (arr).capacity * 2; \
    ARRAY_ENSURE_CAPACITY((arr), (new_capacity)); \
  } \
  (arr).data[(arr).size ++] = (value); \
} while (false)

#define ARRAY_FREE(arr) do { \
  if ((arr).data != NULL) { \
    free((arr).data); \
    (arr).data = NULL; \
    (arr).size = 0; \
    (arr).capacity = 0; \
  } \
} while (false)

typedef ARRAY(char *) string_array;
typedef struct {
  char *command;
  char *description;
  int (*function)(string_array args);
} command_t;

#define COMMAND(name, desc) (command_t){ \
    .command = #name, \
    .description = (desc), \
    .function = name ## _command \
}

ARRAY(command_t) builtins = {0};
ARRAY(FILE *) files = {0};

void close_open_files() {
  for (size_t i = 0; i < files.size; i ++) {
    if (files.data[i] != NULL) {
      fclose(files.data[i]);
      files.data[i] = NULL;
    }
  }
}

char *_read_arg(char *string, const char *delim, char **rest, bool *quoted) {
  ARRAY(char) ret = {0};
  char *p = string;
  quote_mode quote = UNQUOTED;
  while (*p != '\0' && (quote != UNQUOTED || strchr(delim, *p) == NULL)) {
    if (quote == UNQUOTED && *p == '>') {
      if (p == string) {
        ARRAY_ADD(ret, '>');
        p ++;
        if (*p == '>') {
          ARRAY_ADD(ret, '>');
          p ++;
        }
      }
      *rest = p;
      ARRAY_ADD(ret, '\0');
      return ret.data;
    }

    switch (*p) {
      case '\\': {
        switch (quote) {
          case DOUBLE: {
            p ++;
            switch (*p) {
              case '\\':
              case '$':
              case '"':
              case '\n':
                ARRAY_ADD(ret, *p);
                break;

              default:
                ARRAY_ADD(ret, '\\');
                ARRAY_ADD(ret, *p);
                break;
            }
          }; break;

          case SINGLE:
            ARRAY_ADD(ret, *p);
            break;

          case UNQUOTED:
            p ++;
            ARRAY_ADD(ret, *p);
            break;

          default:
            UNREACHABLE();
            break;
        }
      }; break;

      case '"': {
        switch (quote) {
          case UNQUOTED:
            quote = DOUBLE;
            *quoted = true;
            break;

          case SINGLE:
            ARRAY_ADD(ret, '"');
            break;

          case DOUBLE:
            quote = UNQUOTED;
            break;

          default:
            UNREACHABLE();
            break;
        }
      }; break;

      case '\'': {
        switch (quote) {
          case UNQUOTED:
            quote = SINGLE;
            *quoted = true;
            break;

          case SINGLE:
            quote = UNQUOTED;
            break;

          case DOUBLE:
            ARRAY_ADD(ret, '\'');
            break;

          default:
            UNREACHABLE();
            break;
        }
      }; break;

      default:
        ARRAY_ADD(ret, *p);
        break;
    }
    p ++;
  }
  assert(quote == UNQUOTED);
  *rest = p;
  ARRAY_ADD(ret, '\0');
  return ret.data;
}

char *read_tilde_arg(char *string, const char *delim, char **rest, bool *quoted) {
  char *p = string + 1;
  struct passwd *passwd = NULL;

  if (*p == '\0' || strchr(delim, *p) != NULL) {
    *rest = p;
    char *home = getenv("HOME");
    if (home == NULL) {
      return strdup("~");
    }
    return strdup(home);
  }

  switch (*p) {
    case '/': {
      char *arg = _read_arg(p, delim, rest, quoted);
      char *home = getenv("HOME");
      if (home == NULL) {
        size_t arg_len = strlen(arg);
        char *ret = malloc(arg_len + 2);
        ret[0] = '~';
        strcpy(ret + 1, arg);
        ret[arg_len + 1] = '\0';
        free(arg);
        return ret;
      }
      size_t len = strlen(home);
      size_t arg_len = strlen(arg);
      char *full_path = malloc(len + arg_len + 1);
      assert(full_path != NULL);
      strcpy(full_path, home);
      strcpy(full_path + len, arg);
      full_path[len + arg_len] = '\0';
      free(arg);
      return full_path;
    }; break;

    default: {
      // ~user
      while ((passwd = getpwent()) != NULL) {
        size_t len = strlen(passwd->pw_name);
        if (strncmp(p, passwd->pw_name, len) == 0) {
          setpwent();
          endpwent();
          p += len;


          if (*p == '\0' || strchr(delim, *p) != NULL) {
            *rest = p;
            return strdup(passwd->pw_dir);
          }

          if (*p == '/') {
            char *arg = _read_arg(p, delim, rest, quoted);
            size_t dir_len = strlen(passwd->pw_dir);
            size_t arg_len = strlen(arg);
            char *full_path = malloc(dir_len + arg_len + 1);
            assert(full_path != NULL);
            strcpy(full_path, passwd->pw_dir);
            strcpy(full_path + dir_len, arg);
            full_path[dir_len + arg_len] = '\0';
            free(arg);
            return full_path;
          }
        }
      }
      setpwent();
      endpwent();
      // Haven't found user, fall out
    }; break;
  }
  return _read_arg(string, delim, rest, quoted);
}

char *read_arg(char *string, const char *delim, char **rest, bool *quoted) {
  *quoted = false;
  char *start = string;
  while (*start != '\0' && strchr(delim, *start) != NULL) start ++;

  if (*start == '\0' || *start == '\n') {
    return NULL;
  }

  if (*start == '~') {
    return read_tilde_arg(start, delim, rest, quoted);
  } else {
    return _read_arg(start, delim, rest, quoted);
  }
}

extern char **environ;
int run_program(char *file_path, string_array args) {
  ARRAY(char *) argv = {0};
  for (size_t i = 0; i < args.size; i ++) {
    ARRAY_ADD(argv, args.data[i]);
  }
  ARRAY_ADD(argv, NULL);
  pid_t pid = fork();
  switch (pid) {
    case -1:
      assert(false && "Out of memory");
      UNREACHABLE();
      return -1;

    case 0:
      for (size_t i = 0; i < files.size; i ++) {
        if (files.data[i] != NULL) {
          int fd = fileno(files.data[i]);
          assert(dup2(fd, i) == i);
        }
      }
      assert(execve(file_path, argv.data, environ) != -1);
      UNREACHABLE();
      return -1;

    default: {
      int wstatus = 0;
      assert(waitpid(pid, &wstatus, 0) != -1);
      ARRAY_FREE(argv);
      return WEXITSTATUS(wstatus);
    }
  }
  UNREACHABLE();
  return -1;
}


int help_command(string_array args) {
  FILE *out = stdout;
  if (files.size > STDOUT_FILENO && files.data[STDOUT_FILENO] != NULL) {
    out = files.data[STDOUT_FILENO];
  }
  FILE *err = stdout;
  if (files.size > STDERR_FILENO && files.data[STDERR_FILENO] != NULL) {
    err = files.data[STDERR_FILENO];
  }

  if (args.size > 1) {
    char *arg = args.data[1];
    for (size_t i = 0; i < builtins.size; i ++) {
      command_t cmd = builtins.data[i];
      if (strcmp(cmd.command, arg) == 0) {

        fprintf(out, "    %-10s - %s\n", cmd.command, cmd.description);
        return 0;
      }
    }
    fprintf(err, "%s: Builtin %s not found\n", args.data[0], args.data[1]);
    return 1;
  }
  fprintf(out, "Available commands:\n");
  for (size_t i = 0; i < builtins.size; i ++) {
    command_t cmd = builtins.data[i];
    fprintf(out, "    %-10s - %s\n", cmd.command, cmd.description);
  }
  return 0;
}

int exit_command(string_array args) {
  FILE *err = stdout;
  if (files.size > STDERR_FILENO && files.data[STDERR_FILENO] != NULL) {
    err = files.data[STDERR_FILENO];
  }

  int code = 0;
  if (args.size > 1) {
    char *end;
    code = strtol(args.data[1], &end, 0);
    if (*end != '\0') {
      fprintf(err, "%s: numeric argument required\n", args.data[0]);
      return 1;
    }
    if (code < 0 || code > 255) {
      fprintf(err, "%s: exit code must be 0-255\n", args.data[0]);
      return 1;
    }
  }
  for (size_t i = 0; i < args.size; i ++) {
    free(args.data[i]);
    args.data[i] = NULL;
  }
  ARRAY_FREE(args);
  close_open_files();
  ARRAY_FREE(files);
  ARRAY_FREE(builtins);
  exit(code);
  UNREACHABLE();
  return 0;
}

int echo_command(string_array args) {
  FILE *out = stdout;
  if (files.size > STDOUT_FILENO && files.data[STDOUT_FILENO] != NULL) {
    out = files.data[STDOUT_FILENO];
  }

  for (size_t i = 1; i < args.size; i ++) {
    if (i > 1) fprintf(out, " ");
    fprintf(out, "%s", args.data[i]);
  }
  fprintf(out, "\n");
  return 0;
}

int type_command(string_array args) {
  FILE *out = stdout;
  if (files.size > STDOUT_FILENO && files.data[STDOUT_FILENO] != NULL) {
    out = files.data[STDOUT_FILENO];
  }
  FILE *err = stdout;
  if (files.size > STDERR_FILENO && files.data[STDERR_FILENO] != NULL) {
    err = files.data[STDERR_FILENO];
  }

  int ret = 0;
  for (size_t i = 1; i < args.size; i ++) {
    char *arg = args.data[i];
    bool found = false;
    for (size_t b_i = 0; b_i < builtins.size; b_i ++) {
      if (strcmp(arg, builtins.data[b_i].command) == 0) {
        fprintf(out, "%s is a shell builtin\n", arg);
        found = true;
        break;
      }
    }
    if (found) {
      continue;
    }

    if (strchr(arg, '/') != NULL && access(arg, R_OK | X_OK) == 0) {
      fprintf(out, "%s is %s\n", arg, arg);
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
          fprintf(out, "%s is %s\n", arg, file_path);
          free(file_path);
          found = true;
          break;
        }
        if (*p != '\0') path = ++p;
        free(file_path);
      }
    }
    if (found) {
      continue;
    }

    ret = 1;
    fprintf(err, "%s: not found\n", arg);
  }

  return ret;
}

int pwd_command(string_array args) {
  FILE *out = stdout;
  if (files.size > STDOUT_FILENO && files.data[STDOUT_FILENO] != NULL) {
    out = files.data[STDOUT_FILENO];
  }

  char buf[4096] = {0};
  char *cwd = getcwd(buf, 4096);
  assert(cwd != NULL);
  fprintf(out, "%s\n", cwd);
  return 0;
}

int cd(char *file_path) {
  if (*file_path == 0) return 0;
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

int cd_command(string_array args) {
  FILE *err = stdout;
  if (files.size > STDERR_FILENO && files.data[STDERR_FILENO] != NULL) {
    err = files.data[STDERR_FILENO];
  }

  if (args.size > 2) {
    fprintf(err, "cd: too many arguments\n");
    return 1;
  }

  if (args.size == 1) {
    char *home = getenv("HOME");
    if (home == NULL) {
      fprintf(err, "cd: HOME not set\n");
      return 1;
    }
    return cd(home);
  }

  return cd(args.data[1]);
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

  while (feof(stdin) == 0) {
    printf("$ ");

    // Wait for user input
    char input[100];
    if (fgets(input, 100, stdin) == NULL) break;

    char *delim = " \n";
    string_array args = {0};
    bool error = false;
    char *rest = input;
    char *arg;
    bool quoted;
    while ((arg = read_arg(rest, delim, &rest, &quoted)) != NULL) {
      if (!quoted && (strcmp(arg, ">") == 0 || strcmp(arg, ">>") == 0)) {

        long fd = STDOUT_FILENO;
        if (args.size > 0) {
          char *end;
          long test = strtol(args.data[args.size - 1], &end, 0);
          if (arg != end && *end == '\0') {
            fd = test;
            args.size --;
            free(args.data[args.size]);
            args.data[args.size] = NULL;
          }
        }
        bool append = arg[1] == '>';
        free(arg);
        if (fd < 0) {
          fprintf(stderr, "redirection error, negative file descriptor\n");
          error = true;
          break;
        }

        arg = read_arg(rest, delim, &rest, &quoted);
        if (arg == NULL) {
          fprintf(stderr, "syntax error, missing filename of redirect\n");
          error = true;
          break;
        }

        ARRAY_ENSURE_CAPACITY(files, fd + 1);
        if (fd >= files.size) files.size = fd + 1;
        files.data[fd] = fopen(arg, append ? "a" : "w");
        free(arg);
      } else {
        ARRAY_ADD(args, arg);
      }
    }
    if (error) goto cont;
    if (args.size == 0) goto cont;
    char *command = args.data[0];

    int code = -1;
    for (size_t i = 0; i < builtins.size; i ++) {
      if (strcmp(command, builtins.data[i].command) == 0) {
        // FIXME store return value
        code = builtins.data[i].function(args);
        break;
      }
    }
    if (code == -1) {
      if (strchr(command, '/') != NULL && access(command, R_OK | X_OK) == 0) {
        code = run_program(command, args);

      } else {
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
              code = run_program(file_path, args);
              free(file_path);
              break;
            }
            if (*p != '\0') path = ++p;
            free(file_path);
          }
        }

        if (code == -1) {
          fprintf(stderr, "%s: command not found\n", command);
        }
      }
    }
cont:
    for (size_t i = 0; i < files.size; i ++ ) {
      if (files.data[i] != NULL) {
        fflush(files.data[i]);
      }
    }
    if (files.size > STDIN_FILENO && files.data[STDIN_FILENO] != NULL) {
      fclose(files.data[STDIN_FILENO]);
      files.data[STDIN_FILENO] = NULL;
    }
    if (files.size > STDOUT_FILENO && files.data[STDOUT_FILENO] != NULL) {
      fclose(files.data[STDOUT_FILENO]);
      files.data[STDOUT_FILENO] = NULL;
    }
    if (files.size > STDERR_FILENO && files.data[STDERR_FILENO] != NULL) {
      fclose(files.data[STDERR_FILENO]);
      files.data[STDERR_FILENO] = NULL;
    }
    for (size_t i = 0; i < args.size; i ++) {
      free(args.data[i]);
      args.data[i] = NULL;
    }
    ARRAY_FREE(args);
  }

  close_open_files();
  ARRAY_FREE(files);
  ARRAY_FREE(builtins);
  return 0;
}
