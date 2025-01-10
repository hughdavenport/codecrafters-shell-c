#include <stdio.h>
#include <string.h>

int main() {
  // Flush after every printf
  setbuf(stdout, NULL);

  while (feof(stdin) == 0) {
    printf("$ ");

    // Wait for user input
    char input[100];
    if (fgets(input, 100, stdin) == NULL) break;

    char *command = strtok(input, " \n");

    fprintf(stderr, "%s: command not found\n", command);
  }

  return 0;
}
