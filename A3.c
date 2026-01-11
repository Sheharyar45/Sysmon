#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysinfo.h>
#include <unistd.h>
#include <math.h>
#include <signal.h>
#include <sys/wait.h>

//macro for memory y axis
#define MEM_SCALE 12
//macro for cpu y axis
#define CPU_Y 10
#define offset 8

typedef struct {
    unsigned long total;
    unsigned long idle;
} CPUStats;

typedef struct {
    int samples; //variables to store the values of samples, tdelay and memory, cpu and cores flags
    int tdelay; 
    int memory;
    int cpu;
    int cores; //variables indicating if it should be shown. 1 (Default) is Yes 0 is No
} info;



void clear_screen() { //Function to clear the screen
    printf("\033[2J");
    printf("\033[H"); // Move the cursor to the top-left corner
}

void reset_cursor(int rows) {
    // Reset cursor to below the graph to avoid overwriting it
    printf("\033[%d;%dH", rows + 1, 0);
    fflush(stdout);  // Force the output to be written
}

// function to get the total ram in the system
int get_ram() {
    struct sysinfo info;
    if (sysinfo(&info) == 0) {
        int totalram = info.totalram / (1024 * 1024 * 1024);
        return totalram;
    } else {
        perror("sysinfo error, unable to get memory utilization");
        return 0;
    }
}

void draw_axes(int rows, int cols, char *o_label, char *y_label,int row_start) {
    // Draw Y-axis (vertical line)
    for (int i = row_start; i <= rows + row_start; i++) {
        printf("\033[%d;%dH|", i, offset);  // Move to (i, offset) and print '|'
    }

    // Draw X-axis (horizontal line)
    for (int j = offset; j <= cols+offset; j++) {
        printf("\033[%d;%dH-", rows + row_start, j);  // Move to (ROWS, j) and print '-'
    }

    // Label the axes
    printf("\033[%d;%dH%s", row_start+rows, 1, o_label);      // Origin
    fflush(stdout);  // Force the output to be written
    
    printf("\033[%d;%dH%s", row_start, 1,y_label);        // Y-axis label
    fflush(stdout);  // Force the output to be written
}

void plot_point(int x, int y, int rows, int row_start, char *label) {
    int row = rows - y;    // Adjust row for terminal coordinates
    int col = offset + x;       // Offset by Y-axis column

    // Move to the coordinate and print the point
    printf("\033[%d;%dH%s", row_start + row, col,label);
    fflush(stdout);  // Force the output to be written
}



// function to get the y value for the memory utilization for the current sample
int get_ram_y() {
    struct sysinfo info;
    if (sysinfo(&info) == 0) {
        float totalram = (float)(info.totalram/ (1024 * 1024 * 1024));
        float val = (float)(info.totalram - info.freeram) / (1024 * 1024 * 1024);
        int y = (int)(val/totalram * MEM_SCALE); //Calculating the y value for memory utilization
        
        return y;
    } else {
        perror("sysinfo error, unable to get memory utilization");
        exit(1);
    }
}
// function to get the cpu utilization for the current sample
CPUStats get_cpu_utilization() {
    FILE *fp = fopen("/proc/stat", "r");
    CPUStats stats;
    if (fp == NULL) {
        perror("fopen error, unable to read /proc/stat");
        exit(1);
    }
    char line[1024];
    unsigned long long int user, nice, system, idle, iowait, irq, softirq, steal, guest; // Variables to store the CPU data

    // Read the first line of /proc/stat
    if(fgets(line, sizeof(line), fp) == NULL) {
        perror("fgets error, unable to read /proc/stat");
        exit(1);
    }
    fclose(fp); // Close the file

    // Parse the CPU data
    sscanf(line, "cpu  %llu %llu %llu %llu %llu %llu %llu %llu %llu", 
           &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal, &guest);

    // Calculate total time and usage
    
    unsigned long long int idle_time = idle + iowait;
    unsigned long long int non_idle_time = user + nice + system + irq + softirq + steal + guest;    
    stats.total = idle_time + non_idle_time;
    stats.idle = idle_time;

    return stats;
}

float get_cpu_percentage(CPUStats prev, CPUStats curr) {
    unsigned long long int total_diff = curr.total - prev.total; //Calculating the difference between the total time
    unsigned long long int idle_diff = curr.idle - prev.idle;   //Calculating the difference between the idle time
    float utilization = 100.0 * (total_diff - idle_diff) / total_diff; //Calculating the CPU utilization
    return utilization;
}

void memory_child(int mem_pipe[], info flags){
    close(mem_pipe[0]); // Close read end of memory pipe
    for(int i = 0; i < flags.samples; i++){
        int y = get_ram_y(); //Function to get the y value for memory utilization
        if(write(mem_pipe[1], &y, sizeof(y)) == -1){ //Writing the value of y to the pipe
            perror("error writing to memory pipe");
            exit(1);
        }
        usleep(flags.tdelay); //Function to sleep for tdelay microseconds before plotting the next point
    }
    close(mem_pipe[1]); // Close write end of memory pipe after writing all the values
}
void cpu_child(int cpu_pipe[], info flags){
    close(cpu_pipe[0]); // Close read end of CPU pipe
    CPUStats prev_cpu = get_cpu_utilization(); // To keep track of the previous CPU utilization
    
    for(int i = 0; i < flags.samples; i++){
        usleep(flags.tdelay); //Function to sleep for tdelay microseconds before plotting the next point
        CPUStats curr = get_cpu_utilization();
        float utilization = get_cpu_percentage(prev_cpu, curr); //Function to get the CPU utilization for sample
        if(write(cpu_pipe[1], &utilization, sizeof(utilization)) == -1){ //Writing the value of CPU utilization to the pipe
            perror("error writing to CPU pipe");
            exit(1);
        }
        
        prev_cpu = curr;
    }
    close(cpu_pipe[1]); // Close write end of CPU pipe after writing all the values
}

void plot_values(int mrow, int cpurow, info flags){
    int mem_pipe[2], cpu_pipe[2]; 
    int a = pipe(mem_pipe);
    int b = pipe(cpu_pipe); //Creating pipes for memory and cpu utilization
    
    if(a == -1 || b == -1){
        perror("error creating pipes");
        exit(1);
    }

    if(flags.memory){
        pid_t m_pid = fork(); //Creating a child process to get the memory utilization values 
        if(m_pid == -1){
            perror("error creating child process");
            exit(1);
        }
        if(m_pid == 0){
            //reset handler for ctrl c using signal to ignore
            signal(SIGINT, SIG_IGN);
            memory_child(mem_pipe, flags); //Function to get the memory utilization values scaled to y axis
            exit(0); //Exiting the child process
        }
    }
    else{
        close(mem_pipe[0]); // Close read end of memory pipe
    }

    if(flags.cpu){
        pid_t c_pid = fork(); //Creating a child process to get the CPU utilization values
        if(c_pid == -1){
            perror("error creating child process");
            exit(1);
        }
        if(c_pid == 0){
            //reset handler for ctrl c using signal to ignore 
            signal(SIGINT, SIG_IGN);
            cpu_child(cpu_pipe, flags); //Function to get the CPU utilization values
            exit(0); //Exiting the child process
        }
    }
    else{
        close(cpu_pipe[0]); // Close read end of CPU pipe
    }

    close(mem_pipe[1]); // Close write end of memory pipe
    close(cpu_pipe[1]); // Close write end of CPU pipe

    int mem_y;
    float cpu_val;
    float totalram = (float)get_ram();
    int mem_read = read(mem_pipe[0], &mem_y, sizeof(mem_y));
    int cpu_read = read(cpu_pipe[0], &cpu_val, sizeof(cpu_val)); //Reading the values of memory and cpu utilization from the pipes
    int mcount = 0;
    int ccount = 0; // maintaining the count of the number of points plotted for memory and cpu utilization

    while((flags.memory && mcount < flags.samples) || (flags.cpu &&  ccount < flags.samples)){
        if(flags.memory && mem_read > 0 && mcount < flags.samples){
            plot_point(mcount + 1, mem_y, MEM_SCALE, mrow, "#"); //Function to plot the point for memory utilization
            char label[200];
            float val = (mem_y/(float)MEM_SCALE) * totalram;
            sprintf(label, "%.2f GB            ", val);
            printf("\033[%d;%dH %s", mrow-2, 9,label);
            reset_cursor(cpurow + 200);
            fflush(stdout); //Printing the memory utilization above its graph
            mcount++;
        }
        if(flags.cpu && cpu_read > 0 && ccount < flags.samples){
            plot_point(ccount + 1,cpu_val/10, CPU_Y, cpurow, ":"); //Function to plot the point for CPU utilization
            char label[200];
            sprintf(label, "%.2f %%", cpu_val);
            printf("\033[%d;%dH %s         ", cpurow-2, 9,label); //Printing the CPU utilization above its graph
            reset_cursor(cpurow + 200);
            fflush(stdout);
            ccount++;
        }
        mem_read = read(mem_pipe[0], &mem_y, sizeof(mem_y));
        cpu_read = read(cpu_pipe[0], &cpu_val, sizeof(cpu_val)); //Reading the values of memory and cpu utilization from the pipes
    }
    reset_cursor(cpurow + 20); //Function to reset the cursor to avoid overwriting the graph
    close(mem_pipe[0]); // Close read end of memory pipe
    close(cpu_pipe[0]); // Close read end of CPU pipe
    wait(NULL); //Waiting for the child processes to finish
    wait(NULL);
    
}

void draw_core(int x, int y){
    //Drawing the core
    for(int i =0; i < 3; i++){
        if(i == 0){
            printf("\033[%d;%dH+", y, x);
            printf("\033[%d;%dH|", y+1, x);
            printf("\033[%d;%dH+", y+2, x);
        }
        else if(i == 1){
            printf("\033[%d;%dH--", y, x+1);
            printf("\033[%d;%dH--", y+2, x+1);
        }
        else{
            printf("\033[%d;%dH+", y, x+3);
            printf("\033[%d;%dH|", y+1, x+3);
            printf("\033[%d;%dH+", y+2, x+3);
        }
    }
}
void plot_cores(int cores, int coresrow){
    int height = sqrt(cores); //Calculating the height of the square
    int width = cores/height; //Calculating the width of the square

    int count = 0; 
    int y = coresrow;

    for (int i = 0; i < cores; i++) {

        if (count == width){
            y = y + 5;
            count = 0;
        }
        draw_core(1 + 8* count, y); //Function to draw the core
        count++;
    }
    reset_cursor(coresrow + 200); //Function to reset the cursor to avoid overwriting the graph
}

void cores_child(int cores_pipe[]){

    close(cores_pipe[0]); // Close read end of cores pipe
    FILE *file = fopen("/proc/cpuinfo", "r"); //Opening the file /proc/cpuinfo to get the number of cores
    if (file == NULL) {
        perror("fopen error, unable to read /proc/cpuinfo");
        close(cores_pipe[1]);
        return;
    }
    char buffer[1024];  
    int cores = 0;  
    while (fgets(buffer, sizeof(buffer), file)) {
        if (strstr(buffer, "processor")) {
            cores++; //Incrementing the number of cores
        }
    }
    fclose(file); //Closing the file
    if(write(cores_pipe[1], &cores, sizeof(cores)) == -1){ //Writing the number of cores to the pipe
        perror("error writing to cores pipe");
        exit(1);
    }
    close(cores_pipe[1]); // Close write end of cores pipe
}

void freq_child(int freq_pipe[]){
    close(freq_pipe[0]); // Close read end of frequency pipe
    FILE *max_freq_file = fopen("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq", "r"); //Opening the file to get the max frequency
    if (max_freq_file)
    {
        float base_freq_ghz = 0.0;
        if(fscanf(max_freq_file, "%f", &base_freq_ghz) == 0){ //Reading the frequency from the file
            perror("error reading from /sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq");
            exit(1);
        }
        base_freq_ghz = base_freq_ghz / 1000000.0; // KHz → GHz
        if(write(freq_pipe[1], &base_freq_ghz, sizeof(base_freq_ghz)) == -1){ //Writing the frequency to the pipe
            perror("error writing to frequency pipe");
            exit(1);
        }
        fclose(max_freq_file); //Closing the file
    }
    else
    {
        perror("fopen error, unable to read /sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq");
    }
    close(freq_pipe[1]); // Close write end of frequency pipe
}

void show_cores(int coresrow){
    int cores_pipe[2]; //Creating a pipe to get the number of cores
    int freq_pipe[2]; //Creating a pipe to get the frequency
    int a = pipe(cores_pipe);
    int b = pipe(freq_pipe); //Creating pipes for cores and frequency
    if(a == -1 || b == -1){
        perror("error creating pipes");
        exit(1);
    }
    pid_t c_pid = fork(); //Creating a child process to get the number of cores
    if(c_pid == -1){
        perror("error creating child process");
        exit(1);
    }
    if(c_pid == 0){
        signal(SIGINT, SIG_IGN);
        cores_child(cores_pipe); //Function to get the number of cores
        exit(0); //Exiting the child process
    }
    pid_t f_pid = fork(); //Creating a child process to get the frequency
    if(f_pid == -1){
        perror("error creating child process");
        exit(1);
    }
    if(f_pid == 0){
        signal(SIGINT, SIG_IGN);
        freq_child(freq_pipe); //Function to get the frequency
        exit(0); //Exiting the child process
    }

    close(cores_pipe[1]); // Close write end of cores pipe
    close(freq_pipe[1]); // Close write end of frequency pipe

    int cores;
    float base_freq_ghz;
    
    while(read(cores_pipe[0], &cores, sizeof(cores)) > 0); //Reading the number of cores from the pipe
    while(read(freq_pipe[0], &base_freq_ghz, sizeof(base_freq_ghz)) > 0); //Reading the frequency from the pipe



    char label[200];
    sprintf(label, "Number of Cores: %d @ %.2f Ghz", cores,base_freq_ghz);
    printf("\033[%d;%dH %s", coresrow-2, 1,label); //Printing the number of cores and frequency

    plot_cores(cores,coresrow); //Function to plot the number of cores and frequency

    close(cores_pipe[0]); // Close read end of cores pipe
    close(freq_pipe[0]); // Close read end of frequency pipe
    wait(NULL); //Waiting for the child processes to finish
    wait(NULL);

}






void show(info flags){
    clear_screen(); //Function to clear the screen
    printf("Nbr of samples: %d -- every %d microSecs ( %.3fsecs)\n\n", flags.samples, flags.tdelay, flags.tdelay/1000000.0); //Printing the values of samples and tdelay
    char label[20];
    int mrow = 5;
    int cpurow = 5;
    int coresrow = 5; //Variables to keep track of the row  for memory, cpu and cores

    if(flags.memory){
        printf("\033[%d;%dH%s", mrow-2, 1,"v Memory  ");
        sprintf(label, "%d GB", get_ram());
        draw_axes(MEM_SCALE,flags.samples,"0 GB",label,mrow); //Function to draw the axes
        cpurow = 22;
        coresrow = 22;
    }
    if(flags.cpu){
        printf("\033[%d;%dH%s", cpurow-2, 1,"v CPU  ");
        draw_axes(CPU_Y,flags.samples,"  0%","100%",cpurow); //Function to draw the axes
        coresrow = coresrow + 16;
    }
    
    if(flags.cores){
        show_cores(coresrow); //Function to show the number of cores
    }

    //Function to plot the values of memory and cpu utilization
    if(flags.memory || flags.cpu){
        plot_values(mrow, cpurow, flags); //Function to plot the values of memory and cpu utilization
    }

    reset_cursor(coresrow + 20); //Function to reset the cursor to avoid overwriting the grap
}


void process_flags(int argc, char ** argv, info *flags){
    int s = 0;
    int t = 0; //variables to keep track of wether user has changed samples or tdelay
    int mem = 0;
    int cpu = 0;
    int cores = 0; //variables to keep track of wether user has inputted memory, cpu or cores flags

    char *prefix = "--samples=";
    int prefix_len = strlen(prefix); //To check if the argument is --samples=N 

    char *prefix2 = "--tdelay=";
    int prefix_len2 = strlen(prefix2); //To check if the argument is --tdelay=T


    for(int i = 1 ; i < argc ; i++){
            char* end_ptr;
            int value = (int)strtol(argv[i], &end_ptr, 10);

            if(i == 1 && value != 0 && *end_ptr == '\0' ){
                flags->samples = value;
                s = 1;

            }
            else if(i == 2 && value != 0 && *end_ptr == '\0' && s == 1 ){
                flags->tdelay = value;
                t = 1;
  
            }
            else if(strcmp(argv[i], "--memory") == 0 && mem == 0){
                if(flags->memory == 1){
                    flags->cpu = 0;
                    flags->cores = 0; //If user has inputted --memory then cpu and cores should be 0
                }
                else{
                    flags->memory = 1;
                }
                mem = 1;

            }
            else if(strcmp(argv[i], "--cpu") == 0 && cpu == 0){
                if(flags->cpu == 1){
                    flags->memory = 0;
                    flags->cores = 0; //If user has inputted --cpu then memory and cores should be 0
                }
                else{
                    flags->cpu = 1;
                }
                cpu = 1;


            }

            else if(strcmp(argv[i], "--cores") == 0 && cores == 0){
                if(flags->cores == 1){
                    flags->memory = 0;
                    flags->cpu = 0; //If user has inputted --cores then memory and cpu should be 0
                }
                else{
                    flags->cores = 1;
                }
                cores = 1;

            }


            else if(strncmp(argv[i], prefix, prefix_len) == 0 && s == 0){
                const char *numberPart = argv[i] + prefix_len; //To get N from --samples=N
                int num = (int)strtol(numberPart, &end_ptr, 10);
                if(num != 0 && *end_ptr == '\0'){
                    flags->samples = num;
                    s = 1;
                }
                else{
                    printf("Wrong format of inputs, refer to readme\n");
                    exit(1);
                    return;
                }
            }

            else if(strncmp(argv[i], prefix2, prefix_len2) == 0 && t == 0){
                const char *numberPart = argv[i] + prefix_len2; //To get T from --tdelay=T
                int num = (int)strtol(numberPart, &end_ptr, 10);
                if(num != 0 && *end_ptr == '\0'){
                    flags->tdelay = num;
                    t = 1;
                    
                }
                else{
                    printf("Wrong format of inputs, refer to readme\n");
                    exit(1);
                    return;
                }
            }
            else{
                printf("Wrong format of inputs, refer to readme\n");
                exit(1);
                return;
            }

        }
}

// Signal handler for Ctrl-C
void handle_c(int signal) {
    char ans;
    
    printf("\033[%d;%dH%s", 1, 70,"Do you want to exit? (y/n)"); //Prompt the user to exit
    fflush(stdout);  // Force the output to be written
    if(scanf(" %c", &ans) == EOF) {
        perror("scanf error");
        exit(1);
    }

    // Consume the leftover newline
    while (getchar() != '\n'); // Clear input buffer
    
    if (ans == 'y' || ans == 'Y') {
        clear_screen(); // Clear the screen
        reset_cursor(0); // Reset the cursor
        pid_t pgid = getpgrp();  // Get the current process group ID
        killpg(pgid, SIGTERM);   // Terminate all processes in the group
        exit(0);  // Terminate the program
    } else {
        //clear the line
        printf("\033[%d;%dH%s", 1, 70,"                                         "); //Prompt the user to exit
        return; // Do nothing, keep the process running
    }
}

// Signal handler for Ctrl-Z (ignore)
void handle_z(int signal) {
    return; // Do nothing
}




int main(int argc, char ** argv){
    info flags; //Creating an object of the struct info to store the values of samples, tdelay and memory, cpu and cores flags
    flags.samples = 20;
    flags.tdelay = 500000; //Setting the default values of samples and tdelay
    flags.memory = 1;
    flags.cpu = 1;
    flags.cores = 1; //Setting the default values of memory, cpu and cores flags

    struct sigaction ctrlC, ctrlZ;

    ctrlC.sa_handler = handle_c;
    ctrlZ.sa_handler = handle_z; //Setting the signal handlers for Ctrl-C and Ctrl-Z

    ctrlC.sa_flags = 0;
    ctrlZ.sa_flags = 0;

    if(sigaction(SIGINT, &ctrlC, NULL) == -1){
        perror("Error setting signal handler for Ctrl-C");
        exit(1);
    }
    if(sigaction(SIGTSTP, &ctrlZ, NULL) == -1){
        perror("Error setting signal handler for Ctrl-Z");
        exit(1);
    }
    process_flags(argc, argv, &flags); //Function to process the flags and update values of samples, tdelay and memory, cpu and cores flags
    
    show(flags);

}