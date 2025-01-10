#include <stdio.h>
#include <string.h>

int main() {
  // Flush after every printf
  setbuf(stdout, NULL);

  printf("$ ");

  // Wait for user input
  char input[100];
  fgets(input, 100, stdin);

  char *command = strtok(input, " \n");

  fprintf(stderr, "%s: command not found\n", command);

  return 0;
}
