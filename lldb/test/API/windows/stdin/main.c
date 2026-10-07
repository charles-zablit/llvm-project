#include <stdio.h>
#include <string.h>

int main(void) {
  // The test waits for this line before typing, so that the inferior is past
  // startup and blocked reading stdin while it runs.
  printf("ready\n");
  fflush(stdout);

  char line[64] = {0};
  if (!fgets(line, sizeof(line), stdin))
    return 1;
  line[strcspn(line, "\r\n")] = '\0';
  printf("Input was '%s'\n", line);
  fflush(stdout);
  return 0;
}
