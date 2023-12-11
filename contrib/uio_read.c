#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <fcntl.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
  if (argc != 3) {
    fprintf(stderr, "Usage: %s FILE COUNT\n", argv[0]);
    exit(EXIT_FAILURE);
  }
  const char *filename = argv[1];
  size_t count = strtoul(argv[2], NULL, 10);

  fprintf(stderr, "[UIO] Opening file %s... ", filename);
  int fd = open(filename, O_RDWR);
  if (fd == -1) {
    perror("open");
    exit(EXIT_FAILURE);
  }
  fprintf(stderr, "Done!\n\n");

  for (size_t i = 0; i < count; ++i) {
    uint32_t value;
    fprintf(stderr, "[UIO] Reading #%lu...\n", i);
    if (read(fd, &value, sizeof(value)) != sizeof(value)) {
      perror("read");
      exit(EXIT_FAILURE);
    }
    printf("%u\n", value);
  }
  fprintf(stderr, "[UIO] Reading is done!\n\n");

  fprintf(stderr, "[UIO] Closing the file... ");
  if (close(fd)) {
    perror("close");
    exit(EXIT_FAILURE);
  }
  fprintf(stderr, "Done!\n\n");

  fprintf(stderr, "[UIO] Exiting...\n");

  return EXIT_SUCCESS;
}
