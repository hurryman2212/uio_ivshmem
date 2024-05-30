#include <stdio.h>

#include <fcntl.h>

#include <sys/mman.h>
#include <sys/socket.h>

#include <x86linux/helper.h>

struct ivshmem_reg {
  volatile uint32_t intrmask;
  volatile uint32_t intrstatus;
  volatile uint32_t ivposition;
  volatile uint32_t doorbell;
  volatile uint32_t ivlivelist;
};
#define DOORBELL_MSG(peer_id) ((peer_id << 16) | (0 & UINT16_MAX))

struct ivshmem_buffer {
  uint32_t size;
  uint32_t pos_r;
  uint32_t pos_w;
  uint32_t wait_intr_r;
  uint32_t wait_intr_w;
  uint32_t peer_id_r;
  uint32_t peer_id_w;
  uint32_t run_cond;
  uint32_t check_byte;
};
#define IVSHMEM_BUFFER_START(buf_addr, pagesize) (((void *)buf_addr) + pagesize)
size_t align_to_pagesize(size_t val, size_t pagesize) {
  return (val + pagesize - 1) & ~(pagesize - 1);
}

int main(int argc, char *argv[]) {
  if (argc != 6) {
    fprintf(stderr,
            "Usage: %s FILE BLOCK_SIZE RCVBUF_OVERRIDE SNDBUF_OVERRIDE DEBUG\n",
            argv[0]);
    exit(EXIT_FAILURE);
  }
  const char *filename = argv[1];
  const size_t block_size = strtoul(argv[2], NULL, 10);
  const size_t rcvbuf_override = strtoul(argv[3], NULL, 10);
  const size_t sndbuf_override = strtoul(argv[4], NULL, 10);
  const int debug = atoi(argv[5]);

  log_init(NULL, 0, 0, 1, 0);
  log_enable(LOG_DEBUG);

  int sockfd;
  size_t rcvbuf = rcvbuf_override, sndbuf = sndbuf_override;
  socklen_t optlen;
  if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    perror("socket");
    exit(EXIT_FAILURE);
  }
  if (!rcvbuf) {
    optlen = sizeof(rcvbuf);
    if (getsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, &optlen) < 0) {
      perror("getsockopt(SO_RCVBUF)");
      exit(EXIT_FAILURE);
    }
  }
  printf("SO_RCVBUF: %lu bytes\n", rcvbuf);
  if (!sndbuf) {
    optlen = sizeof(sndbuf);
    if (getsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &sndbuf, &optlen) < 0) {
      perror("getsockopt(SO_SNDBUF)");
      exit(EXIT_FAILURE);
    }
  }
  printf("SO_SNDBUF: %lu bytes\n", sndbuf);

  long pagesize = sysconf(_SC_PAGESIZE);
  if (pagesize == -1) {
    perror("sysconf");
    return EXIT_FAILURE;
  }
  size_t buf_size = align_pow2_size(rcvbuf + sndbuf, pagesize);
  printf("buf_size: %lu\n", buf_size);

  int device_fd = open(filename, O_RDWR);
  if (device_fd == -1) {
    perror("open");
    return EXIT_FAILURE;
  }
  struct ivshmem_reg *reg =
      mmap(NULL, pagesize, PROT_READ | PROT_WRITE, MAP_SHARED, device_fd, 0);
  if (reg == MAP_FAILED) {
    perror("reg = mmap(pagesize)");
    return EXIT_FAILURE;
  }
  struct ivshmem_buffer *device_mem =
      mmap(NULL, pagesize + buf_size, PROT_READ | PROT_WRITE, MAP_SHARED,
           device_fd, pagesize);
  if (device_mem == MAP_FAILED) {
    perror("device_mem = mmap(pagesize)");
    return EXIT_FAILURE;
  }

  memset(device_mem, 0x00, sizeof(*device_mem));
  device_mem->size = buf_size;
  device_mem->peer_id_r = reg->ivposition;
  printf("device_mem->peer_id_r = %u\n", device_mem->peer_id_r);
  device_mem->run_cond = 1;

  void *proc_buf = malloc(buf_size);
  if (!proc_buf) {
    perror("proc_buf = malloc");
    exit(EXIT_FAILURE);
  }

  usersched_init(0);

  uint32_t dummy;
  if (read(device_fd, &dummy, sizeof(dummy)) != sizeof(dummy)) {
    perror("read");
    exit(EXIT_FAILURE);
  }
  printf("start!\n");
  uint32_t peer_id_w = device_mem->peer_id_w;
  printf("device_mem->peer_id_w = %u\n", device_mem->peer_id_w);

  void *debug_buf = NULL;
  if (debug) {
    debug_buf = malloc(buf_size);
    if (!debug_buf) {
      perror("debug_buf = malloc");
      exit(EXIT_FAILURE);
    }
    memset(debug_buf, device_mem->check_byte, buf_size);
  }

  long double total_count = 0.0;
  while (device_mem->run_cond) {
    /* Start usersched. */
    uint32_t rpeek, usersched_tsc = 100 * usersched_tsc_1us, pos_w_save;
    if (!(rpeek = usersched_spsc_prepare_read(
              0, &device_mem->pos_r, &device_mem->pos_w, device_mem->size,
              block_size, &usersched_tsc, &pos_w_save))) {
      /* Timeout in userspace.. */

      /* Notify that we are waiting on the interrupt line. */
      device_mem->wait_intr_r = 1;
      barrier();

      /* Check again before entering critical section. */
      if (pos_w_save == device_mem->pos_w) {
        /* Wait for interrupt. */
        uint32_t dummy = 0;
        log_abort_if_false(read(device_fd, &dummy, sizeof(dummy)) ==
                           sizeof(dummy));
        device_mem->wait_intr_r = 0;
      } else
        /* Cancel waiting interrupt. */
        device_mem->wait_intr_r = 0;
    } else {
      spsc_read(IVSHMEM_BUFFER_START(device_mem, pagesize), proc_buf,
                &device_mem->pos_r, rpeek);
      if (debug)
        log_abort_if_false(!memcmp(debug_buf, proc_buf, rpeek));

      /* Check whether we have to send intterupt. */
      if (unlikely(device_mem->wait_intr_w))
        reg->doorbell = DOORBELL_MSG(peer_id_w);

      total_count += rpeek;
    }
  }

  free(proc_buf);
  if (debug_buf)
    free(debug_buf);
  return EXIT_SUCCESS;
}
