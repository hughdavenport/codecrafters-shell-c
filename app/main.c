#include <ctype.h>
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

struct termios old_termios;

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


bool read_input(read_buffer *buf, bool block) {
  if (buf->eof) return false;
  if (buf->offset >= buf->capacity) {
    struct pollfd fd = {
        .fd = buf->fd,
        .events = POLLIN,
    };
    int p = 0;
    while (p == 0) {
      p = poll(&fd, 1, block ? -1 : 0);
      if (p == -1) {
        switch (errno) {
          case EINTR:
          case EAGAIN:
            if (!block) return false;
            p = 0;
            continue;
          default:
            perror("poll");
            abort();
        }
      }
      if (p == 0 && !block) return false;
      if (p != 1 || fd.revents == 0) {
        fprintf(stderr, "poll: p = %d, fd.revents = %d\n", p, fd.revents);
        abort();
      }
    }
    if ((fd.revents & POLLIN) != 0) {
      ssize_t n = read(buf->fd, buf->buffer, sizeof(buf->buffer) - 1);
      if (n < 0) {
        perror("read");
        abort();
      }
      if (n == 0) {
        assert(!block);
        return false;
      }
      buf->offset = 0;
      buf->capacity = n;
      if ((fd.revents & (POLLERR|POLLHUP|POLLNVAL)) != 0) {
        buf->eof = true;
      }
      return true;
    } else if ((fd.revents & (POLLERR|POLLHUP|POLLNVAL)) != 0) {
      buf->eof = true;
      return false;
    } else {
      fprintf(stderr, "poll: fd.revents = %d\n", fd.revents);
      abort();
    }
  }
  return true;
}

char peek_char(read_buffer *buf) {
  if (buf->eof) return EOF;
  if (!read_input(buf, false)) {
    return EOF; /* Non blocking, should check is_eof() */
  }
  return buf->eof ? EOF : buf->buffer[buf->offset];
}

char read_char(read_buffer *buf) {
  if (buf->eof) return EOF;
  if (!read_input(buf, true)) {
    assert(buf->eof);
    return EOF;
  }
  char c = buf->buffer[buf->offset++];
  fprintf(stdout, "%c", c); /* echo */
  return c;
}

bool is_eof(read_buffer *buf) {
  if (buf->eof) return true;
  if (read_input(buf, true)) {
    return false;
  } else {
    assert(buf->eof);
    return true;
  }
}

char *_read_arg(const char *delim, bool *quoted, bool *escaped, quote_mode *quote, bool *error) {
  ARRAY(char) ret = {0};
  *escaped = false;
  while (!is_eof(&stdin_buf) && (*quote != UNQUOTED || strchr(delim, peek_char(&stdin_buf)) == NULL)) {
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
        ARRAY_ADD(ret, peek_char(&stdin_buf));
        break;

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

  while (!is_eof(&stdin_buf) &&
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
  if (pipe(stdin_pipe) != 0) { perror("pipe stdin"); abort(); }
  if (pipe(stdout_pipe) != 0) { perror("pipe stdout"); abort(); }
  if (pipe(stderr_pipe) != 0) { perror("pipe stderr"); abort(); }
  pid_t pid = fork();
  switch (pid) {
    case -1:
      perror("fork");
      abort();
      UNREACHABLE();
      return -1;

    case 0:
      // FIXME get stdin working, pipe maybe
      if (close(stdin_pipe[1]) == -1) { perror("child close stdin_pipe[1]"); abort(); }
      if (close(stdout_pipe[0]) == -1) { perror("child close stdout_pipe[0]"); abort(); }
      if (close(stderr_pipe[0]) == -1) { perror("child close stderr_pipe[0]"); abort(); }
      if (dup2(stdin_pipe[0], STDIN_FILENO) == -1) { perror("child dup2 stdin"); abort(); }
      if (dup2(stdout_pipe[1], STDOUT_FILENO) == -1) { perror("child dup2 stdout"); abort(); }
      if (dup2(stderr_pipe[1], STDERR_FILENO) == -1) { perror("child dup2 stderr"); abort(); }
      for (size_t i = 0; i < files.size; i ++) {
        if (files.data[i] != NULL) {
          int fd = fileno(files.data[i]);
          close(i);
          if (dup2(fd, i) == -1) { perror("child dup2 files"); abort(); }
        }
      }
      if (execve(file_path, argv.data, environ) == -1) {
        perror("execve");
        abort();
      }
      UNREACHABLE();
      return -1;

    default: {
      ARRAY_FREE(argv);
      if (close(stdin_pipe[0]) == -1) { perror("parent close stdin_pipe[0]"); abort(); }
      if (close(stdout_pipe[1]) == -1) { perror("parent close stdout_pipe[1]"); abort(); }
      if (close(stderr_pipe[1]) == -1) { perror("parent close stderr_pipe[1]"); abort(); }
      int wstatus = 0;
      pid_t wait_ret;
      bool eof = false;
      read_buffer child_stdout_buf = {
        .fd = stdout_pipe[0],
      };
      read_buffer child_stderr_buf = {
        .fd = stderr_pipe[0],
      };

wait_loop:
      while (true) {
        wait_ret = waitpid(pid, &wstatus, WNOHANG);
        if (wait_ret == -1) {
          switch (errno) {
            case EAGAIN:
              continue;

            case ECHILD:
              goto wait_loop;

            default:
              perror("waitpid(pid, &status, WNOHANG)");
              abort();
          }
        }
        if (wait_ret != 0) break;

        if (!eof && read_input(&stdin_buf, false)) {
          size_t to_write = stdin_buf.capacity - stdin_buf.offset;
          for (size_t i = 0; i < to_write; i ++) {
            switch (stdin_buf.buffer[stdin_buf.offset + i]) {
              case CTRL_C: {
                // Write what was read up to the ^C out, then signal. Continue on from the byte after this
                int tmp = to_write;
                int off = stdin_buf.offset;
                to_write = i;
                while (to_write > 0) {
                  ssize_t ret = write(stdin_pipe[1], stdin_buf.buffer + stdin_buf.offset, to_write);
                  if (ret == -1) {
                    switch (errno) {
                      case EAGAIN:
                      case EINTR:
                        continue;

                      case EPIPE:
                        to_write = 0;
                        goto sigint;

                      default:
                        perror("parent write before sigint");
                        abort();
                    }
                  }
                  stdin_buf.offset += ret;
                  to_write -= ret;
                }
                // echo
                to_write = i;
                stdin_buf.offset = off;
                while (to_write > 0) {
                  ssize_t ret = write(STDOUT_FILENO, stdin_buf.buffer + stdin_buf.offset, to_write);
                  if (ret == -1) {
                    switch (errno) {
                      case EAGAIN:
                      case EINTR:
                        continue;

                      case EPIPE:
                        to_write = 0;
                        goto sigint;

                      default:
                        perror("parent write echo before sigint");
                        abort();
                    }
                  }
                  stdin_buf.offset += ret;
                  to_write -= ret;
                }
                stdin_buf.offset ++;
                to_write = tmp - i - 1;
                i = 0;
sigint:
                if (kill(pid, SIGINT) == -1) {
                  perror("kill sigint");
                  abort();
                }
              }; break;

              case CTRL_D: {
                // Write out up to here, set EOF
                int off = stdin_buf.offset;
                to_write = i;
                while (to_write > 0) {
                  ssize_t ret = write(stdin_pipe[1], stdin_buf.buffer + stdin_buf.offset, to_write);
                  if (ret == -1) {
                    switch (errno) {
                      case EAGAIN:
                      case EINTR:
                        continue;

                      case EPIPE:
                        to_write = 0;
                        goto sigint;

                      default:
                        perror("parent write before eof");
                        abort();
                    }
                  }
                  stdin_buf.offset += ret;
                  to_write -= ret;
                }
                // echo
                to_write = i;
                stdin_buf.offset = off;
                while (to_write > 0) {
                  ssize_t ret = write(STDOUT_FILENO, stdin_buf.buffer + stdin_buf.offset, to_write);
                  if (ret == -1) {
                    switch (errno) {
                      case EAGAIN:
                      case EINTR:
                        continue;

                      case EPIPE:
                        to_write = 0;
                        goto sigint;

                      default:
                        perror("parent write echo before eof");
                        abort();
                    }
                  }
                  stdin_buf.offset += ret;
                  to_write -= ret;
                }
                stdin_buf.offset ++;
                to_write = 0;
                i = 0;
                eof = true;
                while (close(stdin_pipe[1]) == -1) {
                  if (errno == EINTR) continue;
                  perror("parent close stdin");
                  abort();
                  break;
                }
              }; break;
            }
          }
          int tmp = to_write;
          int off = stdin_buf.offset;
          while (to_write > 0) {
            ssize_t ret = write(stdin_pipe[1], stdin_buf.buffer + stdin_buf.offset, to_write);
            if (ret == -1) {
              switch (errno) {
                case EAGAIN:
                case EINTR:
                  continue;

                case EPIPE:
                  to_write = 0;
                  continue;

                default:
                  perror("parent write");
                  abort();
              }
            }
            stdin_buf.offset += ret;
            to_write -= ret;
          }
          to_write = tmp;
          stdin_buf.offset = off;
          while (to_write > 0) {
            ssize_t ret = write(STDOUT_FILENO, stdin_buf.buffer + stdin_buf.offset, to_write);
            if (ret == -1) {
              switch (errno) {
                case EAGAIN:
                case EINTR:
                  continue;

                case EPIPE:
                  to_write = 0;
                  continue;

                default:
                  perror("parent write echo");
                  abort();
              }
            }
            stdin_buf.offset += ret;
            to_write -= ret;
          }
        }
        if (read_input(&child_stdout_buf, eof)) {
          size_t to_write = child_stdout_buf.capacity - child_stdout_buf.offset;
          while (to_write > 0) {
            ssize_t ret = write(STDOUT_FILENO, child_stdout_buf.buffer + child_stdout_buf.offset, to_write);
            if (ret == -1) {
              switch (errno) {
                case EAGAIN:
                case EINTR:
                  continue;

                default:
                  perror("stdout write");
                  abort();
              }
            }
            to_write -= ret;
            child_stdout_buf.offset += ret;
          }
        }
        if (read_input(&child_stderr_buf, eof)) {
          size_t to_write = child_stderr_buf.capacity - child_stderr_buf.offset;
          while (to_write > 0) {
            ssize_t ret = write(STDERR_FILENO, child_stderr_buf.buffer + child_stderr_buf.offset, to_write);
            if (ret == -1) {
              switch (errno) {
                case EAGAIN:
                case EINTR:
                  continue;

                default:
                  perror("stderr write");
                  abort();
              }
            }
            to_write -= ret;
            child_stderr_buf.offset += ret;
          }
        }
      }
      // FIXME put the stdin_buf back into stdin
      if (WIFEXITED(wstatus)) {
        return WEXITSTATUS(wstatus);
      } else if (WIFSIGNALED(wstatus)) {
        return 128 + WTERMSIG(wstatus);
      } else if (WIFSTOPPED(wstatus)) {
        return 128 + WSTOPSIG(wstatus);
      } else {
        perror("waitpid");
        fprintf(stderr, "wait returned %d, wstatus = %d\n", wait_ret, wstatus);
      }
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
  if (tcgetattr(STDIN_FILENO, &old_termios) == 0) {
    struct termios term = old_termios;
    term = old_termios;
    term.c_lflag &= ~(ICANON|ISIG|ECHO);
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

  // FIXME read PS1
  printf("$ ");
  do {

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
    // FIXME read PS1
    printf("$ ");
  } while (!is_eof(&stdin_buf));

  close_open_files();
  ARRAY_FREE(files);
  ARRAY_FREE(builtins);
  tcsetattr(STDIN_FILENO, TCSANOW, &old_termios);
  return 0;
}
