#include <signal.h>
#include <stdio.h>

#include <fcntl.h>

#include <sys/mman.h>

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

static int run_cond = 1;
void sigalrm_handle(__attribute__((unused)) int sig) { run_cond = 0; }

int main(int argc, char *argv[]) {
  if (argc != 5) {
    fprintf(stderr, "Usage: %s FILE TIME_S BLOCK_SIZE MEMSET_DATA_HEX_8B\n",
            argv[0]);
    exit(EXIT_FAILURE);
  }
  const char *filename = argv[1];
  const size_t time_s = strtoul(argv[2], NULL, 10);
  const size_t block_size = strtoul(argv[3], NULL, 10);
  const uint8_t memset_data_hex_8b = strtoul(argv[4], NULL, 16);

  log_init(NULL, 0, 0, 1, 0);
  log_enable(LOG_DEBUG);

  long pagesize = sysconf(_SC_PAGESIZE);
  if (pagesize == -1) {
    perror("sysconf");
    return EXIT_FAILURE;
  }

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
  struct ivshmem_buffer *device_mem = mmap(
      NULL, pagesize, PROT_READ | PROT_WRITE, MAP_SHARED, device_fd, pagesize);
  if (device_mem == MAP_FAILED) {
    perror("device_mem = mmap(pagesize)");
    return EXIT_FAILURE;
  }
  size_t buf_size = device_mem->size;
  if (munmap(device_mem, pagesize)) {
    perror("munmap");
    return EXIT_FAILURE;
  }

  device_mem = mmap(NULL, pagesize + buf_size, PROT_READ | PROT_WRITE,
                    MAP_SHARED, device_fd, pagesize);
  if (device_mem == MAP_FAILED) {
    perror("mmap(buf_size)");
    return EXIT_FAILURE;
  }

  void *proc_buf = malloc(buf_size);
  if (!proc_buf) {
    perror("malloc");
    exit(EXIT_FAILURE);
  }
  memset(proc_buf, memset_data_hex_8b, buf_size);

  device_mem->peer_id_w = reg->ivposition;
  printf("device_mem->peer_id_w = %u\n", device_mem->peer_id_w);
  device_mem->check_byte = memset_data_hex_8b;
  uint32_t peer_id_r = device_mem->peer_id_r;
  printf("device_mem->peer_id_r = %u\n", device_mem->peer_id_r);
  reg->doorbell = DOORBELL_MSG(peer_id_r);

  usersched_init(0);

  if (signal(SIGALRM, sigalrm_handle)) {
    perror("signal");
    exit(EXIT_FAILURE);
  }
  alarm(time_s);

  long double total_count = 0.0;
  while (run_cond) {
    /* Start usersched. */
    uint32_t wpeek, usersched_tsc = 100 * usersched_tsc_1us, pos_r_save;
    if (!(wpeek = usersched_spsc_prepare_write(
              0, &device_mem->pos_r, &device_mem->pos_w, device_mem->size,
              block_size, &usersched_tsc, &pos_r_save))) {
      /* Timeout in userspace.. */

      /* Notify that we are waiting on the interrupt line. */
      device_mem->wait_intr_w = 1;
      barrier();

      /* Check again before entering critical section. */
      if (pos_r_save == device_mem->pos_r) {
        /* Wait for interrupt. */
        uint32_t dummy = 0;
        log_abort_if_false(read(device_fd, &dummy, sizeof(dummy)) ==
                           sizeof(dummy));
        device_mem->wait_intr_w = 0;
      } else
        /* Cancel waiting interrupt. */
        device_mem->wait_intr_w = 0;
    } else {
      spsc_write(IVSHMEM_BUFFER_START(device_mem, pagesize), proc_buf,
                 &device_mem->pos_w, wpeek);

      /* Check whether we have to send intterupt. */
      if (unlikely(device_mem->wait_intr_r))
        reg->doorbell = DOORBELL_MSG(peer_id_r);

      total_count += wpeek;
    }
  }

  free(proc_buf);
  printf("Throughput: %LfMbps\n", total_count / (1024 * 1024));
  device_mem->run_cond = 0;
  return EXIT_SUCCESS;
}
