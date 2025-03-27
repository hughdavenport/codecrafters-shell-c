#include <assert.h>
#include <poll.h>
#include <termios.h>
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
#define UNREACHABLE() do { fprintf(stderr, "%s:%d: UNREACHABLE", __FILE__, __LINE__); exit(1); } while (false)

#define CTRL_C 003
#define CTRL_D 004

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

void close_open_files(void) {
  for (size_t i = 0; i < files.size; i ++) {
    if (files.data[i] != NULL) {
      fclose(files.data[i]);
      files.data[i] = NULL;
    }
  }
}

typedef struct {
  char buffer[4097];
  size_t capacity;
  size_t offset;
  int fd;
  bool eof;
} read_buffer;
read_buffer stdin_buf = {
  .fd = STDIN_FILENO,
};

#define WAIT_FOR_INPUT(buf) do { \
  assert((buf).offset >= (buf).capacity); \
  struct pollfd fd = { \
      .fd = (buf).fd, \
      .events = POLLIN, \
  }; \
  if (poll(&fd, 1, -1) < 0) { \
    (buf).eof = true; \
  } else { \
    assert(fd.revents != 0 && (fd.revents & POLLIN) != 0); \
  } \
} while (false)

#define ENSURE_INPUT(buf) do { \
  if ((buf).offset >= (buf).capacity) { \
    WAIT_FOR_INPUT((buf)); \
    if ((buf).eof) break; \
    ssize_t n = read((buf).fd, (buf).buffer, sizeof((buf).buffer) - 1); \
    assert(n >= 0); \
    (buf).offset = 0; \
    (buf).capacity = n; \
    if (n == 0) (buf).eof = true; \
  } \
} while (false)

char peek_char(read_buffer *buf) {
  ENSURE_INPUT(*buf);
  return buf->eof ? EOF : buf->buffer[buf->offset];
}

char read_char(read_buffer *buf) {
  ENSURE_INPUT(*buf);
  return buf->eof ? EOF : buf->buffer[buf->offset++];
}

bool is_eof(read_buffer *buf) {
  /* ENSURE_INPUT(*buf); */
  return buf->eof;
}

char *_read_arg(const char *delim, bool *quoted, bool *escaped, quote_mode *quote, bool *error) {
  ARRAY(char) ret = {0};
  *escaped = false;
  while (peek_char(&stdin_buf) != EOF && (*quote != UNQUOTED || strchr(delim, peek_char(&stdin_buf)) == NULL)) {
    if (!*escaped && *quote == UNQUOTED) {
      if (ret.size == 1 && ret.data[0] == '>') {
        if (peek_char(&stdin_buf) == '>') ARRAY_ADD(ret, read_char(&stdin_buf));
        ARRAY_ADD(ret, '\0');
        return ret.data;
      } else if (ret.size > 0 && peek_char(&stdin_buf) == '>') {
        break;
      }
    }
    *escaped = false;

    switch (peek_char(&stdin_buf)) {
      case EOF:
        *error = true;
        break;

      case CTRL_C: {
        ARRAY_FREE(ret);
        *error = true;
        return NULL;
      }; break;

      case CTRL_D: {
        UNIMPLENTED("EOF mid arg");
      }; break;

      case '\\': {
        switch (*quote) {
          case DOUBLE: {
            read_char(&stdin_buf);
            switch (peek_char(&stdin_buf)) {
              case EOF:
                break;

              case CTRL_C: {
                UNIMPLENTED("CTRL-C mid arg");
              }; break;

              case CTRL_D: {
                UNIMPLENTED("EOF mid arg");
              }; break;

              case '\n':
                // FIXME read PS2
                printf("> ");
                // fallthrough
              case '\\':
              case '$':
              case '"':
              case '>':
                *escaped = true;
                ARRAY_ADD(ret, peek_char(&stdin_buf));
                break;

              default:
                *escaped = true;
                ARRAY_ADD(ret, '\\');
                ARRAY_ADD(ret, peek_char(&stdin_buf));
                break;
            }
          }; break;

          case SINGLE:
            ARRAY_ADD(ret, peek_char(&stdin_buf));
            break;

          case UNQUOTED:
            read_char(&stdin_buf);
            switch (peek_char(&stdin_buf)) {
              case EOF:
                break;

              case CTRL_C: {
                UNIMPLENTED("CTRL-C mid arg");
              }; break;

              case CTRL_D: {
                UNIMPLENTED("EOF mid arg");
              }; break;

              case '\n':
                // FIXME read PS2
                printf("> ");
                break;

              default:
                ARRAY_ADD(ret, peek_char(&stdin_buf));
                break;
            }
            break;

          default:
            UNREACHABLE();
            break;
        }
      }; break;

      case '"': {
        switch (*quote) {
          case UNQUOTED:
            *quote = DOUBLE;
            *quoted = true;
            break;

          case SINGLE:
            ARRAY_ADD(ret, '"');
            break;

          case DOUBLE:
            *quote = UNQUOTED;
            break;

          default:
            UNREACHABLE();
            break;
        }
      }; break;

      case '\'': {
        switch (*quote) {
          case UNQUOTED:
            *quote = SINGLE;
            *quoted = true;
            break;

          case SINGLE:
            *quote = UNQUOTED;
            break;

          case DOUBLE:
            ARRAY_ADD(ret, '\'');
            break;

          default:
            UNREACHABLE();
            break;
        }
      }; break;

      case '\n':
        // FIXME read PS2
        printf("> ");
      default:
        ARRAY_ADD(ret, peek_char(&stdin_buf));
        break;
    }
    if (!*error) read_char(&stdin_buf);
  }
  if (is_eof(&stdin_buf)) {
    ARRAY_FREE(ret);
    switch (*quote) {
      case SINGLE:
        fprintf(stderr, "syntax error: Unexpected EOF while looking for matching single quote << ' >>\n");
        *error = true;
        break;

      case DOUBLE:
        fprintf(stderr, "syntax error: Unexpected EOF while looking for matching double quote << \" >>\n");
        *error = true;
        break;

      case UNQUOTED:
        break;

      default:
        UNREACHABLE();
        break;
    }
    return NULL;
  }
  assert(*quote == UNQUOTED);
  ARRAY_ADD(ret, '\0');
  return ret.data;
}

char *_read_tilde_arg(const char *delim, bool *quoted, bool *escaped, quote_mode *quote, bool *error) {
  assert(*quote == UNQUOTED);
  assert(!is_eof(&stdin_buf));
  assert(read_char(&stdin_buf) == '~');
  struct passwd *passwd = NULL;

  if (peek_char(&stdin_buf) == '\0' || strchr(delim, peek_char(&stdin_buf)) != NULL) {
    char *home = getenv("HOME");
    if (home == NULL) {
      return strdup("~");
    }
    return strdup(home);
  }

  switch (peek_char(&stdin_buf)) {
    case EOF:
      *error = true;
      break;

    case CTRL_C: {
      UNIMPLENTED("Ctrl-C mid arg");
    }; break;

    case CTRL_D: {
      UNIMPLENTED("EOF mid arg");
    }; break;

    case '/': {
      char *arg = _read_arg(delim, quoted, escaped, quote, error);
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
      ARRAY(char) username = {0};
      read_char(&stdin_buf);
      while (peek_char(&stdin_buf) != '\0' &&
          peek_char(&stdin_buf) != '/' &&
          strchr(delim, peek_char(&stdin_buf)) == NULL) {
        ARRAY_ADD(username, read_char(&stdin_buf));
      }
      while ((passwd = getpwent()) != NULL) {
        size_t len = strlen(passwd->pw_name);
        if (len == username.size && strncmp(username.data, passwd->pw_name, len) == 0) {
          setpwent();
          endpwent();

          if (peek_char(&stdin_buf) == '\0' || strchr(delim, peek_char(&stdin_buf)) != NULL) {
            return strdup(passwd->pw_dir);
          }

          if (peek_char(&stdin_buf) == '/') {
            char *arg = _read_arg(delim, quoted, escaped, quote, error);
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
      char *arg = _read_arg(delim, quoted, escaped, quote, error);
      size_t arg_len = strlen(arg);
      char *ret = malloc(username.size + arg_len + 2);
      assert(ret != NULL);
      ret[0] = '~';
      strncpy(ret + 1, username.data, username.size);
      strcpy(ret + username.size + 1, arg);
      ret[username.size + arg_len + 1] = '\0';
      free(arg);
      ARRAY_FREE(username);
      return ret;
    }; break;
  }
  return _read_arg(delim, quoted, escaped, quote, error);
}

char *read_arg(const char *delim, bool *quoted, bool *escaped, quote_mode *quote, bool *error) {
  assert(*quote == UNQUOTED);
  *quoted = false;

  while (peek_char(&stdin_buf) != EOF &&
      peek_char(&stdin_buf) != '\n' &&
      strchr(delim, peek_char(&stdin_buf)) != NULL) {
    read_char(&stdin_buf);
  }

  switch (peek_char(&stdin_buf)) {
    case CTRL_C: {
      UNIMPLENTED("Ctrl-C start arg");
    }; break;

    case CTRL_D: {
      UNIMPLENTED("EOF start arg");
    }; break;

    case '~':
      return _read_tilde_arg(delim, quoted, escaped, quote, error);

    case '\n':
      read_char(&stdin_buf);
      // fall through
    case EOF:
      return NULL;

    default:
      return _read_arg(delim, quoted, escaped, quote, error);
  }
  UNREACHABLE();
  return NULL;
}

extern char **environ;
int run_program(char *file_path, string_array args) {
  ARRAY(char *) argv = {0};
  for (size_t i = 0; i < args.size; i ++) {
    ARRAY_ADD(argv, args.data[i]);
  }
  ARRAY_ADD(argv, NULL);
  int stdin_pipe[2];
  int stdout_pipe[2];
  int stderr_pipe[2];
  assert(pipe(stdin_pipe) == 0);
  assert(pipe(stdout_pipe) == 0);
  assert(pipe(stderr_pipe) == 0);
  pid_t pid = fork();
  switch (pid) {
    case -1:
      assert(false && "Out of memory");
      UNREACHABLE();
      return -1;

    case 0:
      // FIXME get stdin working, pipe maybe
      assert(close(stdin_pipe[1]) != -1);
      assert(close(stdout_pipe[0]) != -1);
      assert(close(stderr_pipe[0]) != -1);
      assert(dup2(stdin_pipe[0], STDIN_FILENO) != -1);
      assert(dup2(stdout_pipe[1], STDOUT_FILENO) != -1);
      assert(dup2(stderr_pipe[1], STDERR_FILENO) != -1);
      for (size_t i = 0; i < files.size; i ++) {
        if (files.data[i] != NULL) {
          int fd = fileno(files.data[i]);
          close(i);
          assert(dup2(fd, i) == (int)i);
        }
      }
      assert(execve(file_path, argv.data, environ) != -1);
      UNREACHABLE();
      return -1;

    default: {
      ARRAY_FREE(argv);
      assert(close(stdin_pipe[0]) != -1);
      assert(close(stdout_pipe[1]) != -1);
      assert(close(stderr_pipe[1]) != -1);
      int wstatus = 0;
      while (true) {
        pid_t wait_ret = waitpid(pid, &wstatus, WNOHANG);
        assert(wait_ret != -1);
        if (wait_ret != 0) fprintf(stderr, "wait returned %d, wstatus = %d\n", wait_ret, wstatus);
        if (wait_ret != 0 && WIFEXITED(wstatus)) break;

        // FIXME do a poll on all of these buffers
        read_buffer stdout_buf = {
          .fd = stdout_pipe[0],
        };
        read_buffer stderr_buf = {
          .fd = stderr_pipe[0],
        };

        ENSURE_INPUT(stdin_buf);
        size_t to_write = stdin_buf.capacity - stdin_buf.offset;
        for (size_t i = 0; i < to_write; i ++) {
          switch (stdin_buf.buffer[stdin_buf.offset + i]) {
            case CTRL_C:
              fprintf(stderr, "Ctrl-C in run_program\n");
              break;

            case CTRL_D:
              fprintf(stderr, "Ctrl-D in run_program\n");
              break;

            default:
              fprintf(stderr, "Sending '%c'\n", stdin_buf.buffer[stdin_buf.offset + i]);
          }
        }
        while (to_write > 0) {
          ssize_t ret = write(stdin_pipe[1], stdin_buf.buffer + stdin_buf.offset, to_write);
          assert(ret >= 0);
          assert(ret > 0);
          stdin_buf.offset += ret;
          to_write -= ret;
        }
        ENSURE_INPUT(stdout_buf);
        for (size_t i = stdout_buf.offset; i < stdout_buf.capacity; i ++) {
          printf("%c", stdout_buf.buffer[stdout_buf.offset++]);
        }
        ENSURE_INPUT(stderr_buf);
        for (size_t i = stderr_buf.offset; i < stderr_buf.capacity; i ++) {
          fprintf(stderr, "%c", stderr_buf.buffer[stderr_buf.offset++]);
        }
      }
      assert(waitpid(pid, &wstatus, 0) != -1);
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
  FILE *err = stdout;
  if (files.size > STDERR_FILENO && files.data[STDERR_FILENO] != NULL) {
    err = files.data[STDERR_FILENO];
  }

  if (args.size > 1) {
    fprintf(err, "pwd: arguments not supported yet\n");
    return 1;
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
  struct termios old_termios;
  if (tcgetattr(STDIN_FILENO, &old_termios) == 0) {
    struct termios term = old_termios;
    term = old_termios;
    term.c_lflag &= ~(ICANON|ISIG);
    term.c_cc[VTIME] = 0;
    term.c_cc[VMIN] = 0;
    assert(tcsetattr(STDIN_FILENO, TCSANOW, &term) == 0);
  }
  ARRAY_ADD(builtins, COMMAND(help, "Displays help about commands."));
  ARRAY_ADD(builtins, COMMAND(exit, "Exit the shell, with optional code."));
  ARRAY_ADD(builtins, COMMAND(echo, "Prints any arguments to stdout."));
  ARRAY_ADD(builtins, COMMAND(type, "Prints the type of command arguments."));
  ARRAY_ADD(builtins, COMMAND(pwd, "Prints current working directory."));
  ARRAY_ADD(builtins, COMMAND(cd, "Change current working directory."));

  // Flush after every printf
  setbuf(stdout, NULL);

  if (argc > 1) {
    fprintf(stderr, "%s: no arguments supported\n", argv[0]);
    return 1;
  }

  while (is_eof(&stdin_buf) == 0) {
    // FIXME read PS1
    printf("$ ");

    char *delim = " \n";
    string_array args = {0};
    bool error = false;
    quote_mode quote = UNQUOTED;
    char *arg;
    bool quoted;
    bool escaped;
    while ((arg = read_arg(delim, &quoted, &escaped, &quote, &error)) != NULL) {
      if (!quoted && !escaped && (strcmp(arg, ">") == 0 || strcmp(arg, ">>") == 0)) {
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

        arg = read_arg(delim, &quoted, &escaped, &quote, &error);
        if (error || arg == NULL) {
          fprintf(stderr, "syntax error, missing filename of redirect\n");
          error = true;
          break;
        }
        if (error) goto cont;

        ARRAY_ENSURE_CAPACITY(files, (size_t)fd + 1);
        if ((size_t)fd >= files.size) files.size = (size_t)fd + 1;
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
  tcsetattr(STDIN_FILENO, TCSANOW, &old_termios);
  return 0;
}
