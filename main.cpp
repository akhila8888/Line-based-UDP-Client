#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <calcLib.h>
#include "protocol.h"

int main(int argc, char *argv[])
{
  printf("calcProtocol = %zu bytes\n", sizeof(struct calcProtocol));
  printf("calcMessage = %zu bytes\n", sizeof(struct calcMessage));

  initCalcLib();
  char *ptr = randomType();

  double f1, f2, fresult;
  int i1, i2, iresult;

  if (ptr[0] == 'f')
  {
    printf("Float\t");
    f1 = randomFloat();
    f2 = randomFloat();

    if (strcmp(ptr, "fadd") == 0)
    {
      fresult = f1 + f2;
    }
    else if (strcmp(ptr, "fsub") == 0)
    {
      fresult = f1 - f2;
    }
    else if (strcmp(ptr, "fmul") == 0)
    {
      fresult = f1 * f2;
    }
    else if (strcmp(ptr, "fdiv") == 0)
    {
      fresult = f1 / f2;
    }
    printf("%s %8.8g %8.8g = %8.8g\n", ptr, f1, f2, fresult);
  }
  else
  {
    printf("Int\t");
    i1 = randomInt();
    i2 = randomInt();

    if (strcmp(ptr, "add") == 0)
    {
      iresult = i1 + i2;
    }
    else if (strcmp(ptr, "sub") == 0)
    {
      iresult = i1 - i2;
    }
    else if (strcmp(ptr, "mul") == 0)
    {
      iresult = i1 * i2;
    }
    else if (strcmp(ptr, "div") == 0)
    {
      iresult = i1 / i2;
    }
    printf("%s %d %d = %d\n", ptr, i1, i2, iresult);
  }

  char *lineBuffer = NULL;
  size_t lenBuffer = 0;

  printf("Print a command: ");
  getline(&lineBuffer, &lenBuffer, stdin);

  printf("got:> %s\n", lineBuffer);

  char command[10];
  sscanf(lineBuffer, "%s", command);

  printf("Command: |%s|\n", command);

  if (command[0] == 'f')
  {
    printf("Float\t");
    sscanf(lineBuffer, "%s %lg %lg", command, &f1, &f2);
    if (strcmp(command, "fadd") == 0)
    {
      fresult = f1 + f2;
    }
    else if (strcmp(command, "fsub") == 0)
    {
      fresult = f1 - f2;
    }
    else if (strcmp(command, "fmul") == 0)
    {
      fresult = f1 * f2;
    }
    else if (strcmp(command, "fdiv") == 0)
    {
      fresult = f1 / f2;
    }
    printf("%s %8.8g %8.8g = %8.8g\n", command, f1, f2, fresult);
  }
  else
  {
    printf("Int\t");
    sscanf(lineBuffer, "%s %d %d", command, &i1, &i2);
    if (strcmp(command, "add") == 0)
    {
      iresult = i1 + i2;
    }
    else if (strcmp(command, "sub") == 0)
    {
      iresult = i1 - i2;
    }
    else if (strcmp(command, "mul") == 0)
    {
      iresult = i1 * i2;
    }
    else if (strcmp(command, "div") == 0)
    {
      iresult = i1 / i2;
    }
    else
    {
      printf("No match\n");
    }
    printf("%s %d %d = %d\n", command, i1, i2, iresult);
  }

  free(lineBuffer);

  printf("sizeof(struct calcProtocol) = %zu\n", sizeof(struct calcProtocol));
  printf("sizeof(struct calcMessage) = %zu\n", sizeof(struct calcMessage));

  return 0;
}