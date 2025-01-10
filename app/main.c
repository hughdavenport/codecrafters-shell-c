#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    char *command = strtok(input, delim);
    if (strcmp(command, "exit") == 0) {
      char *arg = strtok(NULL, delim);
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
      exit(code);
    }

    fprintf(stderr, "%s: command not found\n", command);
  }

  return 0;
}
