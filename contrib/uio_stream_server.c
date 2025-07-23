#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <unistd.h>

#include <sys/epoll.h>
#include <sys/mman.h>

int should_exit = 0;
void sigalrm_handler(int signum) { should_exit = 1; }

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

  fprintf(stderr, "[UIO] Setting up O_NONBLOCK %s...", filename);
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags == -1) {
    perror("fcntl(F_GETFL)");
    exit(EXIT_FAILURE);
  }
  flags |= O_NONBLOCK;
  if (fcntl(fd, F_SETFL, flags)) {
    perror("fcntl(F_SETFL)");
    exit(EXIT_FAILURE);
  }
  fprintf(stderr, " Done!\n\n");

  fprintf(stderr, "[UIO] Setting up Epoll %s...", filename);
  int epfd = epoll_create1(0);
  if (epfd == -1) {
    perror("epoll_create1");
    exit(EXIT_FAILURE);
  }
  struct epoll_event ev = {.events = EPOLLIN, .data.fd = fd};
  if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev)) {
    perror("epoll_ctl");
    exit(EXIT_FAILURE);
  }
  fprintf(stderr, " Done!\n\n");

  fprintf(stderr, "[UIO] Reading the interrupt...");
  if (epoll_wait(epfd, &ev, 1, -1) != 1) {
    perror("epoll_wait");
    exit(EXIT_FAILURE);
  }
  int target_fd = ev.data.fd;
  uint32_t value;
  if (read(target_fd, &value, sizeof(value)) != sizeof(value)) {
    perror("read");
    exit(EXIT_FAILURE);
  }
  fprintf(stderr, " Done!\n\n");

  fprintf(stderr, "[UIO] Starting the test... ");

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
  void *real_buf = device_mem + PAGE_SIZE;

  signal(SIGALRM, sigalrm_handler);
  alarm(10);

  char mybuf[RING_SIZE];

  unsigned long long total_read_count = 0;
  while (!should_exit) {
    size_t head = atomic_load_explicit(&ring_ptr->head, memory_order_acquire);
    size_t tail = atomic_load_explicit(&ring_ptr->tail, memory_order_relaxed);

    size_t avail = (head + RING_SIZE - tail) & RING_MASK;

    size_t to_read = (RING_SIZE < avail ? RING_SIZE : avail);
    size_t first_part =
        to_read < (RING_SIZE - tail) ? to_read : (RING_SIZE - tail);

    memcpy(mybuf, real_buf + tail, first_part);
    memcpy(mybuf + first_part, real_buf, to_read - first_part);

    atomic_store_explicit(&ring_ptr->tail, (tail + to_read) & RING_MASK,
                          memory_order_release);

    total_read_count += to_read;
  }

  atomic_store_explicit(&ring_ptr->closed, 1, memory_order_relaxed);
  fprintf(stderr, " Done!\n\n");

  fprintf(stderr, "[UIO] total_read_count: %llu\n\n", total_read_count);

  fprintf(stderr, "[UIO] Closing the file... ");
  if (close(fd)) {
    perror("close");
    exit(EXIT_FAILURE);
  }
  fprintf(stderr, " Done!\n\n");

  fprintf(stderr, "[UIO] Exiting...\n");

  return EXIT_SUCCESS;
}
