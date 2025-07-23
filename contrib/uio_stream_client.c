#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
  if (argc != 3) {
    fprintf(stderr, "Usage: %s FILE DEST_IVPOSITION\n", argv[0]);
    exit(EXIT_FAILURE);
  }
  const char *filename = argv[1];
  int16_t dest_ivposition = atoi(argv[2]);

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

#define RING_SIZE 131072
#define RING_MASK (RING_SIZE - 1)
#define PAGE_SIZE 4096

  void *device_mem = mmap(NULL, PAGE_SIZE + RING_SIZE, PROT_READ | PROT_WRITE,
                          MAP_SHARED, fd, PAGE_SIZE);
  if (device_mem == MAP_FAILED) {
    perror("mmap");
    return EXIT_FAILURE;
  }

  struct {
    atomic_size_t head;
    atomic_size_t tail;
    atomic_size_t closed;
  } *ring_ptr = device_mem;
  atomic_store_explicit(&ring_ptr->head, 0, memory_order_relaxed);
  atomic_store_explicit(&ring_ptr->tail, 0, memory_order_relaxed);
  atomic_store_explicit(&ring_ptr->closed, 0, memory_order_relaxed);
  void *real_buf = device_mem + PAGE_SIZE;

  char mybuf[RING_SIZE];

#define DEFAULT_MSIX_INDEX 0
  uint32_t msg = (uint32_t)dest_ivposition << 16 | DEFAULT_MSIX_INDEX;
  fprintf(stderr, "[UIO] Writing the interrupt...");
  reg_ptr->doorbell = msg;
  fprintf(stderr, " Done!\n\n");

  while (!ring_ptr->closed) {
    size_t head = atomic_load_explicit(&ring_ptr->head, memory_order_relaxed);
    size_t tail = atomic_load_explicit(&ring_ptr->tail, memory_order_acquire);

    size_t free_space = (tail + RING_SIZE - head - 1) & RING_MASK;

    size_t to_write = (RING_SIZE < free_space ? RING_SIZE : free_space);
    size_t first_part =
        to_write < (RING_SIZE - head) ? to_write : (RING_SIZE - head);

    memcpy(real_buf + head, mybuf, first_part);
    memcpy(real_buf, mybuf + first_part, to_write - first_part);

    atomic_store_explicit(&ring_ptr->head, (head + to_write) & RING_MASK,
                          memory_order_release);
  }

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
