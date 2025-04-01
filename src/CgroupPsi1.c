#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/wait.h>

#define CGROUP_PATH "/sys/fs/cgroup/psicgroup1"

void add_pid_to_cgroup(pid_t pid) {
    char procs_file[256];
    snprintf(procs_file, sizeof(procs_file), "%s/cgroup.procs", CGROUP_PATH);

    int fd = open(procs_file, O_WRONLY);
    if (fd < 0) {
        perror("open cgroup.procs");
        exit(EXIT_FAILURE);
    }

    char buf[32];
    int len = snprintf(buf, sizeof(buf), "%d\n", pid);
    if (write(fd, buf, len) != len) {
        perror("write pid to cgroup.procs");
        close(fd);
        exit(EXIT_FAILURE);
    }
    close(fd);
}

int main() {
    // Create the cgroup directory under the unified cgroup v2 hierarchy.
    if (mkdir(CGROUP_PATH, 0755) < 0) {
        if (errno != EEXIST) {
            perror("mkdir");
            exit(EXIT_FAILURE);
        }
    }
    printf("Created (or already exists) cgroup at: %s\n", CGROUP_PATH);

    // Fork to create the first child process (P1)
    pid_t pid1 = fork();
    if (pid1 < 0) {
        perror("fork for P1");
        exit(EXIT_FAILURE);
    }
    if (pid1 == 0) {
        // Child process P1: perform a trivial task
        printf("Child process P1 (PID %d) started.\n", getpid());
        sleep(30);  // Simulate work
        exit(EXIT_SUCCESS);
    }

    // Fork to create the second child process (P2)
    pid_t pid2 = fork();
    if (pid2 < 0) {
        perror("fork for P2");
        exit(EXIT_FAILURE);
    }
    if (pid2 == 0) {
        // Child process P2: perform a trivial task
        printf("Child process P2 (PID %d) started.\n", getpid());
        sleep(30);  // Simulate work
        exit(EXIT_SUCCESS);
    }

    // Parent process: add both child processes to the cgroup.
    add_pid_to_cgroup(pid1);
    add_pid_to_cgroup(pid2);
    printf("Added P1 (PID %d) and P2 (PID %d) to cgroup %s\n", pid1, pid2, CGROUP_PATH);

    // Allow some time for the processes to start and for PSI stats to accumulate.
    sleep(2);

    // Read and print the Memory Pressure Stall Information (PSI) for the cgroup.
    char psi_file[256];
    snprintf(psi_file, sizeof(psi_file), "%s/memory.pressure", CGROUP_PATH);

    int fd = open(psi_file, O_RDONLY);
    if (fd < 0) {
        perror("open memory.pressure");
        wait(NULL);
        wait(NULL);
        exit(EXIT_FAILURE);
    }

    char psi_buf[1024];
    ssize_t n = read(fd, psi_buf, sizeof(psi_buf) - 1);
    if (n < 0) {
        perror("read memory.pressure");
        close(fd);
        wait(NULL);
        wait(NULL);
        exit(EXIT_FAILURE);
    }
    psi_buf[n] = '\0';  // Null-terminate the read data

    printf("\nMemory Pressure Stall Information (PSI) for cgroup '%s':\n%s\n", CGROUP_PATH, psi_buf);
    close(fd);

    // Wait for child processes to finish.
    wait(NULL);
    wait(NULL);

    // Optionally, remove the created cgroup directory.
    // rmdir(CGROUP_PATH);

    return 0;
}
