#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <fcntl.h>
#include <unistd.h>

#include <sys/mman.h>

double gettimediff(const struct timespec *start, const struct timespec *end) {
  return (end->tv_sec - start->tv_sec) + (end->tv_nsec - start->tv_nsec) / 1e9;
}

int memtest(uint8_t *ptr, size_t size, uint8_t hex_8b) {
  for (size_t i = 0; i < size; ++i)
    if (ptr[i] != hex_8b) {
      fprintf(stderr, "(index: %lu / expected_val: %hhu) real_val: %hhu\n", i,
              hex_8b, ptr[i]);
      return -1;
    }
  return 0;
}

int main(int argc, char *argv[]) {
  if (argc != 4) {
    fprintf(stderr, "Usage: %s FILE SIZE HEX_8B\n", argv[0]);
    return EXIT_FAILURE;
  }

  char *path = argv[1];
  size_t size = (int)strtoul(argv[2], NULL, 10);
  uint8_t hex_8b = (int)strtoul(argv[3], NULL, 16);

  struct timespec start, end;
  double elapsed_sec;

  long pagesize = sysconf(_SC_PAGESIZE);
  if (pagesize == -1) {
    perror("sysconf");
    return EXIT_FAILURE;
  }

  clock_gettime(CLOCK_MONOTONIC, &start);
  uint8_t *host_mem = aligned_alloc(pagesize, size);
  if (!host_mem) {
    perror("aligned_alloc");
    return EXIT_FAILURE;
  }
  clock_gettime(CLOCK_MONOTONIC, &end);
  elapsed_sec = gettimediff(&start, &end);
  printf("[host_mem] (aligned_alloc) elapsed_sec: %.6f\n", elapsed_sec);

  clock_gettime(CLOCK_MONOTONIC, &start);
  memset(host_mem, hex_8b, size);
  clock_gettime(CLOCK_MONOTONIC, &end);
  elapsed_sec = gettimediff(&start, &end);
  printf("[host_mem] (memset) elapsed_sec: %.6f\n", elapsed_sec);

  clock_gettime(CLOCK_MONOTONIC, &start);
  if (memtest(host_mem, size, hex_8b))
    return EXIT_FAILURE;
  clock_gettime(CLOCK_MONOTONIC, &end);
  elapsed_sec = gettimediff(&start, &end);
  printf("[host_mem] (memtest) elapsed_sec: %.6f\n", elapsed_sec);

  clock_gettime(CLOCK_MONOTONIC, &start);
  int device_fd = open(path, O_RDWR);
  if (device_fd == -1) {
    perror("open");
    return EXIT_FAILURE;
  }
  uint8_t *device_mem =
      mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, device_fd, pagesize);
  if (device_mem == MAP_FAILED) {
    perror("mmap");
    return EXIT_FAILURE;
  }
  clock_gettime(CLOCK_MONOTONIC, &end);
  elapsed_sec = gettimediff(&start, &end);
  printf("[device_mem] (mmap) elapsed_sec: %.6f\n", elapsed_sec);

  clock_gettime(CLOCK_MONOTONIC, &start);
  memset(device_mem, hex_8b, size);
  clock_gettime(CLOCK_MONOTONIC, &end);
  elapsed_sec = gettimediff(&start, &end);
  printf("[device_mem] (memset) elapsed_sec: %.6f\n", elapsed_sec);

  clock_gettime(CLOCK_MONOTONIC, &start);
  if (memtest(device_mem, size, hex_8b))
    return EXIT_FAILURE;
  clock_gettime(CLOCK_MONOTONIC, &end);
  elapsed_sec = gettimediff(&start, &end);
  printf("[device_mem] (memtest) elapsed_sec: %.6f\n", elapsed_sec);

  free(host_mem);
  if (munmap(device_mem, size)) {
    perror("munmap");
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
