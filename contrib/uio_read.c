#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <fcntl.h>
#include <unistd.h>

#include <sys/epoll.h>

int main(int argc, char *argv[]) {
  if (argc != 3) {
    fprintf(stderr, "Usage: %s FILE COUNT\n", argv[0]);
    exit(EXIT_FAILURE);
  }
  const char *filename = argv[1];
  size_t count = strtoul(argv[2], NULL, 10);

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

  for (size_t i = 0; i < count; ++i) {
    fprintf(stderr, "[UIO] Reading #%lu...", i);
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
    printf(" Done! (%u)\n", value);
  }
  fprintf(stderr, "\n");

  fprintf(stderr, "[UIO] Closing the file... ");
  if (close(fd)) {
    perror("close");
    exit(EXIT_FAILURE);
  }
  fprintf(stderr, " Done!\n\n");

  fprintf(stderr, "[UIO] Exiting...\n");

  return EXIT_SUCCESS;
}
