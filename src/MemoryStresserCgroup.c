/**
 * cgroup_memory_limit.c - A demo of cgroup v2 memory control in Linux.
 * 
 * Description:
 * 1. Creates a cgroup under `/sys/fs/cgroup/memory_stress`.
 * 2. Sets a memory limit (1GB by default).
 * 3. Runs `stress-ng` to allocate up to 1GB of RAM inside the cgroup.
 * 4. Ensures cleanup on exit (Ctrl+C or SIGTERM).
 * 5. Monitors PSI values for the cgroup to show memory pressure.
 *
 * Watch PSI value on real-time: watch -n 1 cat /sys/fs/cgroup/memory_stress/memory.pressure
 *
 * Dependencies:
 *   - Linux kernel with cgroup v2 support.
 *   - `stress-ng` installed (`sudo apt install stress-ng`).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>  // For errno and EEXIST

#define CMD_BUFFER 256                 // Buffer size for file paths
#define CGROUP_PATH "/sys/fs/cgroup/memory_stress"  // Custom cgroup path
#define MEMORY_LIMIT "1G"              // Memory limit (1GB)
#define STRESS_MB 1024                 // RAM to allocate via stress-ng (1GB, 1024 MiB)

pid_t stress_ng_pid = 0;               // Global PID of stress-ng process

/**
 * Signal handler for graceful shutdown.
 * Kills stress-ng if running and exits.
 */
void handle_signal(int sig) {
    if (stress_ng_pid > 0) {
        printf("\nTerminating stress-ng (PID %d)...\n", stress_ng_pid);
        kill(stress_ng_pid, SIGTERM);   // Send SIGTERM to stress-ng
        waitpid(stress_ng_pid, NULL, 0); // Wait for cleanup
    }
    exit(0);
}

/**
 * Assigns a process to the cgroup by writing its PID to `cgroup.procs`.
 */
void assign_to_cgroup(pid_t pid) {
    char procs_path[CMD_BUFFER];
    snprintf(procs_path, sizeof(procs_path), "%s/cgroup.procs", CGROUP_PATH);

    FILE *fp = fopen(procs_path, "w");
    if (!fp) {
        perror("Failed to open cgroup.procs");
        exit(1);
    }
    fprintf(fp, "%d\n", pid);  // Assign PID to cgroup
    fclose(fp);
}

/**
 * Creates the cgroup directory if it doesn't exist.
 */
void create_cgroup_if_not_exists() {
    if (mkdir(CGROUP_PATH, 0755) != 0 && errno != EEXIST) {
        perror("Failed to create cgroup directory");
        exit(1);
    }
    printf("Cgroup created: %s\n", CGROUP_PATH);
}

/**
 * Enables the memory controller for the cgroup.
 */
void enable_memory_controller() {
    FILE *fp = fopen("/sys/fs/cgroup/cgroup.subtree_control", "w");
    if (!fp) {
        perror("Failed to enable memory controller");
        exit(1);
    }
    fprintf(fp, "+memory\n");  // Enable memory control
    fclose(fp);
    printf("Memory controller enabled.\n");
}

/**
 * Sets the memory limit for the cgroup (via `memory.max`).
 */
void set_memory_limit() {
    char limit_path[CMD_BUFFER];
    snprintf(limit_path, sizeof(limit_path), "%s/memory.max", CGROUP_PATH);

    FILE *fp = fopen(limit_path, "w");
    if (!fp) {
        perror("Failed to set memory limit");
        exit(1);
    }
    fprintf(fp, "%s\n", MEMORY_LIMIT);  // Write limit (e.g., "1G")
    fclose(fp);
    printf("Memory limit set to %s\n", MEMORY_LIMIT);
}

int main() {
    // Set up signal handlers for graceful exit
    signal(SIGINT, handle_signal);   // Handle Ctrl+C
    signal(SIGTERM, handle_signal);  // Handle `kill` command

    // Step 1: Create and configure the cgroup
    create_cgroup_if_not_exists();
    enable_memory_controller();
    set_memory_limit();

    // Step 2: Launch stress-ng in a child process to allocate 1GB of RAM
    printf("Allocating %d MiB via stress-ng...\n", STRESS_MB);
    char stress_args[CMD_BUFFER];
    snprintf(stress_args, sizeof(stress_args), "%dM", STRESS_MB);

    stress_ng_pid = fork();
    if (stress_ng_pid == 0) {
        // Child process: Replace with stress-ng
        execlp("stress-ng", "stress-ng", "--vm-bytes", stress_args, "--vm-keep", "-m", "1", NULL);
        perror("Failed to start stress-ng");
        exit(1);
    }

    // Step 3: Assign the child to the cgroup
    sleep(1);  // Brief delay to ensure stress-ng starts
    assign_to_cgroup(stress_ng_pid);
    printf("stress-ng (PID %d) assigned to cgroup.\n", stress_ng_pid);

    // Step 4: Wait for stress-ng to complete
    waitpid(stress_ng_pid, NULL, 0);
    printf("Done.\n");
    return 0;
}
