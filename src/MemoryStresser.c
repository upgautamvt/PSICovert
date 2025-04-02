/*
This is a memory stresser tool that run stress-ng.
Run the program.
From different terminal shell,
    We can track free memory using watch -n 1 "free -h"
    We can also track system PSI using watch -n 1 cat /proc/pressure/memory

 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>

#define CMD_BUFFER 256
pid_t stress_ng_pid = 0;

void handle_signal(int sig) {
    if (stress_ng_pid > 0) {
        printf("\nTerminating stress-ng (PID %d)...\n", stress_ng_pid);
        kill(stress_ng_pid, SIGTERM);
        waitpid(stress_ng_pid, NULL, 0);
    }
    exit(0);
}

int get_memory_available_mib() {
    FILE *fp;
    char buffer[CMD_BUFFER];
    int mem_free = 0, swap_free = 0;

    // Get memory free
    fp = popen("free -m | awk '/Mem:/ {print $7}'", "r");
    if (fp) {
        if (fgets(buffer, sizeof(buffer), fp) != NULL) {
            mem_free = atoi(buffer);
        }
        pclose(fp);
    }

    // Get swap free
    fp = popen("free -m | awk '/Swap:/ {print $4}'", "r");
    if (fp) {
        if (fgets(buffer, sizeof(buffer), fp) != NULL) {
            swap_free = atoi(buffer);
        }
        pclose(fp);
    }

    return mem_free + swap_free;
}

int main() {
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    int total_available_mib = get_memory_available_mib();
    int allocate_mib = total_available_mib * 80 / 100;
    int allocate_gib = allocate_mib / 1024;

    printf("Allocating %d GiB...\n", allocate_gib);

    char allocate_mib_str[CMD_BUFFER];
    snprintf(allocate_mib_str, sizeof(allocate_mib_str), "%dM", allocate_mib);

    stress_ng_pid = fork();
    if (stress_ng_pid == 0) {
        // Child process runs stress-ng
        execlp("stress-ng", "stress-ng", "--vm-bytes", allocate_mib_str, "--vm-keep", "-m", "1", NULL);
        perror("execlp failed");
        exit(1);
    }

    // Parent process waits for the child to finish
    waitpid(stress_ng_pid, NULL, 0);
    return 0;
}
