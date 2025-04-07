//sender (victim)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>

#define CGROUP_PATH "/sys/fs/cgroup/memory_stress"
#define MEMORY_LIMIT "1G"

pid_t stress_ng_pid1 = 0;
pid_t stress_ng_pid2 = 0;


void handle_signal(int sig) {
    if (stress_ng_pid1 > 0) {
        printf("\nTerminating stress-ng (PID %d)...\n", stress_ng_pid1);
        kill(stress_ng_pid1, SIGTERM);
        waitpid(stress_ng_pid1, NULL, 0);
    }
    if (stress_ng_pid2 > 0) {
        printf("\nTerminating stress-ng (PID %d)...\n", stress_ng_pid2);
        kill(stress_ng_pid2, SIGTERM);
        waitpid(stress_ng_pid2, NULL, 0);
    }
    exit(0);
}


void assign_to_cgroup(pid_t pid) {
    char procs_path[256];
    snprintf(procs_path, sizeof(procs_path), "%s/cgroup.procs", CGROUP_PATH);

    FILE *fp = fopen(procs_path, "a");
    if (!fp) {
        perror("Failed to open cgroup.procs");
        exit(1);
    }
    fprintf(fp, "%d\n", pid);
    fclose(fp);
}


void create_cgroup_if_not_exists() {
    if (mkdir(CGROUP_PATH, 0755) != 0 && errno != EEXIST) {
        perror("Failed to create cgroup directory");
        exit(1);
    }
}


void enable_memory_controller() {
    FILE *fp = fopen("/sys/fs/cgroup/cgroup.subtree_control", "a");
    if (!fp) {
        perror("Failed to enable memory controller");
        exit(1);
    }
    fprintf(fp, "+memory\n");
    fclose(fp);
}

void set_memory_limit() {
    char limit_path[256];
    snprintf(limit_path, sizeof(limit_path), "%s/memory.max", CGROUP_PATH);

    FILE *fp = fopen(limit_path, "w");
    if (!fp) {
        perror("Failed to set memory limit");
        exit(1);
    }
    fprintf(fp, "%s\n", MEMORY_LIMIT);
    fclose(fp);
}


void run_stress_ng(int memory_limit_mb) {
    char stress_args[256];
    snprintf(stress_args, sizeof(stress_args), "%dM", memory_limit_mb);

    pid_t stress_ng_pid = fork();
    if (stress_ng_pid == 0) {
        assign_to_cgroup(0);
        execlp("stress-ng", "stress-ng", "--vm-bytes", stress_args, "--vm-keep", "-m", "1", NULL);
        perror("Failed to start stress-ng");
        exit(1);
    }
}

void send_single_bit(int bit) {
   //second process. Now they compete for memory
   if (bit == 1) {
      run_stress_ng(1024); //Watcher observes high PSI values
   } else {
      run_stress_ng(1); // Watcher observes 0 PSI values
   }
}

int main(int argc, char *argv[]) {
	if (argc < 2) {
    		fprintf(stderr, "Usage: %s <bit_value>\n", argv[0]);
    		return EXIT_FAILURE;
	}
	int bit = atoi(argv[1]);
	signal(SIGINT, handle_signal);
	signal(SIGTERM, handle_signal);
	create_cgroup_if_not_exists();
	enable_memory_controller();
	set_memory_limit();
	run_stress_ng(200); // first process
	send_single_bit(bit);

    // Wait for both stress-ng processes to complete
    waitpid(stress_ng_pid1, NULL, 0);  // Wait for the first stress-ng process
    waitpid(stress_ng_pid2, NULL, 0);  // Wait for the second stress-ng process

	return 0;
}
