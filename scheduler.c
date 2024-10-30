#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <time.h>
#include <signal.h>

#define MAX_INPUT 1024
#define MAX_ARGS 100
#define MAX_HISTORY 100
#define MAX_QUEUE_SIZE 100
#define MAX_NCPU 16
#define MAX_COMMANDS_SUBMIT 100

int NCPU, TSLICE;

pid_t active_jobs[MAX_NCPU];

pid_t job_queue[MAX_QUEUE_SIZE];
int front=0, back=0, queue_size=0;

int queue_empty() {
    return queue_size == 0;
}

void enqueue_job(pid_t pid) {
    if(queue_size < MAX_QUEUE_SIZE){
        job_queue[back] = pid;
        back = (back+1)%MAX_QUEUE_SIZE;
        queue_size++;
    }
}

pid_t dequeue_job() {
    if(queue_size > 0){
        pid_t pid = job_queue[front];
        front = (front+1)%MAX_QUEUE_SIZE;
        queue_size--;
        return pid;
    }
    return -1;
}

typedef struct{
    pid_t pid;
    char command[MAX_INPUT];
    int num_time_slice_completion;
    int num_time_slice_wait;
    int time_slice_last_run;
}submitted_commands;

int jobs_submitted = 0;
submitted_commands commands_submitted[MAX_COMMANDS_SUBMIT];

void add_job_to_queue(pid_t pid, const char *command){
    if(jobs_submitted < MAX_COMMANDS_SUBMIT){
        enqueue_job(pid);
        commands_submitted[jobs_submitted].pid = pid;
        strncpy(commands_submitted[jobs_submitted].command, command, MAX_INPUT);
        commands_submitted[jobs_submitted].num_time_slice_completion = 0;
        commands_submitted[jobs_submitted].num_time_slice_wait = 0;
        commands_submitted[jobs_submitted].time_slice_last_run = -1;
        jobs_submitted++;
    }else{
        printf("ERROR: Jobs Submission Limit Excedded.\n");
    }
}

void display_info_submitted_jobs(){
    for(int i=0; i<jobs_submitted; i++){
        int pid = commands_submitted[i].pid;
        char* command = commands_submitted[i].command;
        int completion_time = commands_submitted[i].num_time_slice_completion * TSLICE;
        int wait_time = commands_submitted[i].num_time_slice_wait * TSLICE;
        printf("%d: %s (PID: %d, Completion Time: %dms, Wait Time: %dms)\n",
            i+1, command, pid, completion_time, wait_time);
    }
}

typedef struct{
    char input[MAX_INPUT];
    char command[MAX_INPUT];
    pid_t pid;
    time_t start_time;
    double duration;
}command_history;

command_history history[MAX_HISTORY];
int history_count = 0;

void add_to_history(const char *input, const char *command, pid_t pid, time_t start_time, double duration){
    if(history_count < MAX_HISTORY){
        strncpy(history[history_count].input, input, MAX_INPUT);
        strncpy(history[history_count].command, command, MAX_INPUT);
        history[history_count].pid = pid;
        history[history_count].start_time = start_time;
        history[history_count].duration = duration;
        history_count++;
    }else{
        printf("ERROR: History Limit Excedded.\n");
    }
}

void display_all_info(){
    for(int i=0; i<history_count; i++){
        printf("%d: %s (PID: %d, Start Time: %sDuration: %.2f seconds)\n", 
            i+1, history[i].input, history[i].pid, 
            ctime(&history[i].start_time), history[i].duration);
    }
}

void display_history(){
    for(int i=0; i<history_count; i++){
        printf("%s", history[i].input);
    }
}

void parse_input(char *user_input, char **command, char **args){
    char *token;
    int arg_index = 0;

    token = strtok(user_input, " \t\r\n");
    if(token == NULL){\
        *command = NULL;
        return;
    }

    *command = token;

    while(token != NULL && arg_index < MAX_ARGS-1){
        args[arg_index++] = token;
        token = strtok(NULL, " \t\r\n");
    }

    args[arg_index] = NULL;
}

void run_command(char *input, char *command, char **args){
    pid_t pid = fork();
    if(pid < 0){
        printf("ERROR: Fork Failed\n");
        return;
    }
    if(pid == 0){ 
        if(execvp(command, args) < 0){
            printf("ERROR: Command Execution Failed: %s\n", command);
            exit(1);
        }
        exit(0);
    }else{ 
        time_t start_time = time(NULL); 
        int status;
        waitpid(pid, &status, 0);
        time_t end_time = time(NULL);
        double duration = difftime(end_time, start_time);
        add_to_history(input, command, pid, start_time, duration);
    }
}

void run_piped_commands(char *input){
    char *command;
    char *args[MAX_ARGS];
    char *pipe_cmds[10];
    int pipe_count = 0;

    char user_input[MAX_INPUT];
    strncpy(user_input, input, MAX_INPUT);

    command = strtok(input, "|");
    while(command != NULL && pipe_count < 10){
        pipe_cmds[pipe_count++] = command;
        command = strtok(NULL, "|");
    }

    int pipes[2*(pipe_count-1)];

    for(int i=0; i<pipe_count-1; i++){
        if(pipe(pipes + i*2) < 0){
            printf("ERROR: Pipe Creation Failed");
            return;
        }
    }

    time_t start_time = time(NULL);
    pid_t pid1=0;

    for(int i=0; i<pipe_count; i++){
        char *args[MAX_ARGS];
        parse_input(pipe_cmds[i], &args[0], args);
        pid_t pid = fork();
        if(pid < 0){
            printf("ERROR: Fork Failed");
            return;
        }
        if(pid == 0){
            if(i < pipe_count-1){
                dup2(pipes[i*2+1], STDOUT_FILENO);
            }
            if(i > 0){
                dup2(pipes[(i-1)*2], STDIN_FILENO);
            }
            for(int j=0; j<2*(pipe_count-1); j++){
                close(pipes[j]);
            }
            execvp(args[0], args);
            printf("ERROR: Command Execution Failed");
            exit(1);
        }
        if(pid > 0){
            if(i == 0) pid1 = pid;
        }
    }

    for(int i=0; i<2*(pipe_count-1); i++){
        close(pipes[i]);
    }

    for(int i=0; i<pipe_count; i++){
        wait(NULL);
    }

    time_t end_time = time(NULL);
    double duration = difftime(end_time, start_time);
    add_to_history(user_input, user_input, pid1, start_time, duration);
}

void launch(char *input, char *command, char **args){
    run_command(input, command, args);
}

static void my_handler(int signum){
    if(signum == SIGINT){
        printf("\n");
        display_info_submitted_jobs();
        display_all_info();
        exit(0);
    }
}

void quit(){
    display_info_submitted_jobs();
    display_all_info();
    exit(0);
}

// Scheduler
int num_time_slice = 0;
void simple_scheduler(int NCPU, int TSLICE) {
    while(1){
        for(int i=0; i<NCPU; i++){
            if(queue_empty()) break;
            pid_t job_pid = dequeue_job();
            kill(job_pid, SIGCONT);
            active_jobs[i] = job_pid;
            for(int j=0; j<MAX_COMMANDS_SUBMIT; j++){
                if(commands_submitted[j].pid == job_pid){
                    commands_submitted[j].num_time_slice_completion++;
                    if(commands_submitted[j].time_slice_last_run != -1){
                        commands_submitted[j].num_time_slice_wait += (num_time_slice-commands_submitted[j].time_slice_last_run);
                    }
                    commands_submitted[j].time_slice_last_run = num_time_slice;
                }
            }
        }
        usleep(TSLICE*1000);

        for(int i=0; i<NCPU; i++){
            if(active_jobs[i]>0){
                if(kill(active_jobs[i], 0) == 0){
                    kill(active_jobs[i], SIGSTOP);
                    enqueue_job(active_jobs[i]); 
                }
                active_jobs[i] = 0;
            }
        }
        num_time_slice++;
    }
    printf("All submitted jobs successfully completed.\n");
}


void shell_loop(int argc, char** argv){

    if(argc<3) {
        printf("ERROR: Wrong Input Format, Use: %s <NCPU> <TSLICE>\n", argv[0]);
        exit(1);
    }
    NCPU = atoi(argv[1]);
    TSLICE = atoi(argv[2]);

    pid_t scheduler_pid = fork();
    if(scheduler_pid == 0){
        simple_scheduler(NCPU, TSLICE);
        exit(0);    
    }

    struct sigaction sig;
    memset(&sig, 0, sizeof(sig));
    sig.sa_handler = my_handler;
    sigaction(SIGINT, &sig, NULL);

    int status = 1;
    char *shell_user = "SimpleShell@abhishek: ";
    char user_input[MAX_INPUT];
    char input[MAX_INPUT];
    char *command;
    char *args[MAX_ARGS];

    do{
        printf("%s", shell_user);
        if(fgets(user_input, MAX_INPUT, stdin) == NULL){
            break;
        }
        strncpy(input, user_input, MAX_INPUT);
        if(strchr(user_input, '|') != NULL){
            run_piped_commands(user_input);
        }else{
            parse_input(user_input, &command, args);
            if(command == NULL){
                continue;
            }
            if(strcmp(command, "submit") == 0 && args[1] != NULL){
                pid_t job_pid = fork();
                if(job_pid == 0){
                    pause();
                    if(execl(args[1], args[1], NULL)<0){
                        printf("ERROR: Command Execution Failed");
                        exit(1);    
                    }
                    exit(0);
                }else{
                    add_job_to_queue(job_pid, args[1]);
                }
                continue;
            }

            if(strcmp(command, "history") == 0){
                display_history();
            }
            else if(strcmp(command, "exit") == 0){
                quit();
            }
            else{
                launch(input, command, args);
            }
        }
    }while(status);
}

int main(int argc, char** argv){
    shell_loop(argc, argv);
    return 0;
}
