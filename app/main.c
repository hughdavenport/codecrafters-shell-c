#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UNIMPLENTED(msg) do { fprintf(stderr, "%s:%d: UNIMPLENTED: %s", __FILE__, __LINE__, msg); exit(1); } while (false)

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

int main(int argc, char **argv) {
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

    if (strcmp(command, "exit") == 0) {
      char *arg = read_arg(rest, delim, &rest);

      int code = 0;
      if (arg != NULL) {
        char *end;
        code = strtol(arg, &end, 0);
        if (*end != '\0') {
          fprintf(stderr, "%s: %s: numeric argument required\n", program, command);
          continue;
        }
        if (code < 0 || code > 255) {
          fprintf(stderr, "%s: %s: exit code must be 0-255\n", program, command);
          continue;
        }
      }
      free(arg);
      free(command);
      exit(code);
    } else if (strcmp(command, "echo") == 0) {
      // Get each argument
      char *arg = NULL;
      bool first = true;
      while ((arg = read_arg(rest, delim, &rest)) != NULL) {
        if (!first) printf(" ");
        else first = false;
        printf("%s", arg);
      }
      printf("\n");
    }

    fprintf(stderr, "%s: command not found\n", command);
    free(command);
  }

  return 0;
}
