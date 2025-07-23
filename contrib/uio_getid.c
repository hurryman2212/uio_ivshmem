#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <fcntl.h>
#include <unistd.h>

#include <sys/mman.h>

struct ivshmem_reg {
  volatile uint32_t intrmask;
  volatile uint32_t intrstatus;
  volatile uint32_t ivposition;
  volatile uint32_t doorbell;
  volatile uint32_t ivlivelist;
};

int main(int argc, char *argv[]) {
  if (argc != 2) {
    fprintf(stderr, "Usage: %s FILE\n", argv[0]);
    exit(EXIT_FAILURE);
  }
  const char *filename = argv[1];

  fprintf(stderr, "[UIO] Opening file %s...", filename);
  int fd = open(filename, O_RDWR);
  if (fd == -1) {
    perror("open");
    exit(EXIT_FAILURE);
  }
  fprintf(stderr, " Done!\n\n");

  fprintf(stderr, "[UIO] Mapping the file...");
  size_t pagesize = getpagesize();
  struct ivshmem_reg *reg_ptr =
      mmap(NULL, pagesize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (reg_ptr == MAP_FAILED) {
    perror("mmap");
    exit(EXIT_FAILURE);
  }
  fprintf(stderr, " Done!\n\n");

  printf("%u\n", reg_ptr->ivposition);

  fprintf(stderr, "[UIO] Unmapping the file...");
  if (munmap(reg_ptr, pagesize)) {
    perror("munmap");
    exit(EXIT_FAILURE);
  }
  fprintf(stderr, " Done!\n\n");

  fprintf(stderr, "[UIO] Closing the file...");
  if (close(fd)) {
    perror("close");
    exit(EXIT_FAILURE);
  }
  fprintf(stderr, " Done!\n\n");

  fprintf(stderr, "[UIO] Exiting...\n\n");

  return EXIT_SUCCESS;
}
