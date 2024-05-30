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

int main(int argc, char **argv) {
  if (argc != 6) {
    fprintf(stderr, "Usage: %s FILE COUNT VECTOR PEER SLEEP_US\n", argv[0]);
    exit(EXIT_FAILURE);
  }
  const char *filename = argv[1];
  size_t count = strtoul(argv[2], NULL, 10);
  int16_t msi_index = atoi(argv[3]);
  int16_t ivposition = atoi(argv[4]);
  __useconds_t sleep_us = atoi(argv[5]);

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

  uint32_t msg = (uint32_t)ivposition << 16 | msi_index;

  for (size_t i = 0; i < count; ++i) {
    fprintf(stderr, "[UIO] Writing #%lu...", i);
    reg_ptr->doorbell = msg;
    if (sleep_us)
      usleep(sleep_us);
    fprintf(stderr, " Done!\n");
  }
  fprintf(stderr, "\n");

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
