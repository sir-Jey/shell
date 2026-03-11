#include <sysexits.h>
#include <sys/wait.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <unistd.h>
#include <signal.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define MAX_COMMANDS        1024 // ~ MAX SHELL COMMANDS
#define MAX_ARGS            (sysconf(_SC_ARG_MAX))
#define BACKGROUND_ENABLED  1 


typedef struct {
    char **argv; /* [argV1] | [argV2] | [argV3] | [argV4]  */ 
    int argc; 
    char *output_file;
    int output_append;
    char *error_file;  
    int error_append;  
    char *input_file;  
    int background;   
} command_info_t;

static void print_argvs(command_info_t commands[], int cmd_count)  /* for debug */
{   
    printf("ARGVS\n");
    for (int i=0; i<cmd_count; i++) {
        printf("argv[%d]:", i);
        for (int j=0; j<commands[i].argc; j++)
            printf(" %s", commands[i].argv[j]);
        printf("\n");
    }
    printf("\n\n");
}   

static void print_argv(command_info_t commands) /* for debug */
{   
    printf("argv[0]:");
    for (int i=0; i<commands.argc; i++) 
        printf(" %s", commands.argv[i]);
    printf("\n\n");
}   

static void free_commands(command_info_t *commands, int cmd_count) {
    for (int i=0; i<cmd_count; i++) {
        for (int j=0; j<commands[i].argc; j++) {
            free(commands[i].argv[j]);
        }
        free(commands[i].argv);
    }
}   

static int check_help(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Использование: %s <команда> [аргументы...]\n", argv[0]);
        return(-1);
    }
    int i;

    for (i=1; i<argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Использование: %s <команда> [аргументы...]\n",argv[0]);
            return(0);
        }
    }
    return(1);
}   

static int validate_operator_placement(command_info_t commands[], int cmd_count, const char *find_operator, int is_last_command)
{
    int i;
    for (i=0; i<cmd_count; i++) {
        int operator_pos = -1;
        for (int j=0; j<commands[i].argc; j++) {
            if (strcmp(commands[i].argv[j], find_operator) == 0) {
                operator_pos = j;
                break;
            }
        }
        
        if (operator_pos != -1) {
            if ((is_last_command && i != cmd_count - 1) || (!is_last_command && i != 0)) {
                return -1;
            }
            
            if (operator_pos >= commands[i].argc - 1) {
                fprintf(stderr, "Fatal error: После оператора %s должен быть указан файл\n", find_operator);
                exit(1);
            }
            
            if (operator_pos == 0 && strcmp(find_operator, "<") != 0) {
                fprintf(stderr, "Fatal error: Оператор %s не может быть первым аргументом команды\n", find_operator);
                exit(EXIT_FAILURE);
            }
        }
    }
    return(0);
}  

static int parse_commands(int argc, char *argv[], command_info_t commands[]) 
{   
    int counter_conv = 0;
    int is_output_file = 0;
    int is_input_file = 0;
    int is_append_output_file = 0;
    int is_error_file = 0;
    int is_error_append = 0;

    int end_index = 0;
    int num_arr = 0;

    for (int i=0; i<(MAX_COMMANDS < MAX_ARGS ? MAX_COMMANDS : MAX_ARGS) ; i++) {
        commands[i].argc = 0;
        commands[i].argv = NULL;
        commands[i].output_file = NULL;
        commands[i].output_append = 0;
        commands[i].error_file = NULL;
        commands[i].error_append = 0;
        commands[i].input_file = NULL;
        commands[i].background = 0;
    }

    for (int i=0; i<argc; i++) 
        if (strcmp(argv[i], ":") == 0) 
            counter_conv++;    
    
    for (int i=0; i<argc; i++) {
        if (strcmp(argv[i], ">") == 0) 
            is_output_file = 1;
        if (strcmp(argv[i], "<") == 0)
            is_input_file = 1;
        if (strcmp(argv[i], ">>") == 0)
            is_append_output_file = 1;
        if (strcmp(argv[i], "2>") == 0)
            is_error_file = 1;
        if (strcmp(argv[i], "2>>") == 0)
            is_error_append = 1;
    }

    if (argc > 1 && strcmp(argv[1], ":") == 0) {
        fprintf(stderr, "Fatal error: Конвейер не может быть первым аргументом\n");
        return -1;
    }
    if (strcmp(argv[argc-1], ":") == 0) {
        fprintf(stderr, "Fatal error: Конвейер не может быть последним аргументом\n");
        return -1;
    }

    while (end_index < argc) {
        int start = end_index;
        int count = 0;
        while (end_index < argc && strcmp(argv[end_index], ":") != 0) {
            count++;
            end_index++;
        }

        commands[num_arr].argv = malloc((count + 1) * sizeof(char*));
        if (commands[num_arr].argv == NULL) {
            perror("malloc");
            exit(EXIT_FAILURE);
        }
        
        int arg_idx = 0;
        for (int j=0; j<count; j++) {
            if (strcmp(argv[start+j], "&") == 0)
                commands[num_arr].background = BACKGROUND_ENABLED;
            else
                commands[num_arr].argv[arg_idx++] = strdup(argv[start+j]);
            
        }
        commands[num_arr].argc = arg_idx;
        commands[num_arr].argv[arg_idx] = NULL;
        
        if (end_index < argc && strcmp(argv[end_index], ":") == 0) {
            end_index++;
        }
        num_arr++;
    }

    if (is_output_file) {
        const char *operator = ">";
        if (validate_operator_placement(commands, num_arr, operator, 1) == -1) {
            fprintf(stderr, "Fatal error: Перенаправление вывода с перезаписью '>' допускается только у последней команды\n");
            exit(1);
        } else {
            if (commands[num_arr-1].argv != NULL && commands[num_arr-1].argc > 0) {
                for (int j=0; j<commands[num_arr-1].argc; j++) {
                    if (strcmp(commands[num_arr-1].argv[j], ">") == 0) {
                        commands[num_arr-1].output_file = strdup(commands[num_arr-1].argv[j+1]);
                        commands[num_arr-1].output_append = 0;
                        free(commands[num_arr-1].argv[j]);
                        free(commands[num_arr-1].argv[j+1]);
                        for (int k=j; k<commands[num_arr-1].argc - 2; k++) 
                            commands[num_arr-1].argv[k] = commands[num_arr-1].argv[k+2];
                        
                        commands[num_arr-1].argc -= 2;
                        commands[num_arr-1].argv[commands[num_arr-1].argc] = NULL;
                        break;
                    }
                }
            }
        }
    }

    if (is_append_output_file) {
        const char *operator = ">>";
        if (validate_operator_placement(commands, num_arr, operator, 1) == -1) {
            fprintf(stderr, "Fatal error: Перенаправление вывода с дозаписью '>>' допускается только у последней команды\n");
            exit(1);
        } else {
            if (commands[num_arr-1].argv !=NULL && commands[num_arr-1].argc > 0) {
                for (int j = 0; j < commands[num_arr-1].argc; j++) {
                    if (strcmp(commands[num_arr-1].argv[j], ">>") == 0) {
                        commands[num_arr-1].output_file = strdup(commands[num_arr-1].argv[j+1]);
                        commands[num_arr-1].output_append = 1;
                        free(commands[num_arr-1].argv[j]);
                        free(commands[num_arr-1].argv[j+1]);
                        for (int k=j; k<commands[num_arr-1].argc-2; k++) 
                            commands[num_arr-1].argv[k] = commands[num_arr-1].argv[k+2];
                        
                        commands[num_arr-1].argc -= 2;
                        commands[num_arr-1].argv[commands[num_arr-1].argc] = NULL;
                        break;
                    }
                }
            }
        }
    }

    if (is_error_file) {
        const char *operator = "2>";
        if (validate_operator_placement(commands, num_arr, operator, 1) == -1) {
            fprintf(stderr, "Fatal error: Перенаправление потока ошибок '2>' допускается только у последней команды\n");
            exit(1);
        } else {
            if (commands[num_arr-1].argv != NULL && commands[num_arr-1].argc > 0) {
                for (int j = 0; j < commands[num_arr-1].argc; j++) {
                    if (strcmp(commands[num_arr-1].argv[j], "2>") == 0) {
                        commands[num_arr-1].error_file = strdup(commands[num_arr-1].argv[j+1]);
                        commands[num_arr-1].error_append = 0;
                        free(commands[num_arr-1].argv[j]);
                        free(commands[num_arr-1].argv[j+1]);
                        for (int k=j; k<commands[num_arr-1].argc - 2; k++)
                            commands[num_arr-1].argv[k] = commands[num_arr-1].argv[k+2];
                        
                        commands[num_arr-1].argc -= 2;
                        commands[num_arr-1].argv[commands[num_arr-1].argc] = NULL;
                        break;
                    }
                }
            }
        }
    }

    if (is_error_append) {
        const char *operator = "2>>";
        if (validate_operator_placement(commands, num_arr, operator, 1) == -1) {
            fprintf(stderr, "Fatal error: Перенаправление потока ошибок с дозаписью '2>>' допускается только у последней команды\n");
            exit(1);
        } else {
            if (commands[num_arr-1].argv != NULL && commands[num_arr-1].argc > 0) {
                for (int j = 0; j < commands[num_arr-1].argc; j++) {
                    if (strcmp(commands[num_arr-1].argv[j], "2>>") == 0) {
                        commands[num_arr-1].error_file = strdup(commands[num_arr-1].argv[j+1]);
                        commands[num_arr-1].error_append = 1;
                        free(commands[num_arr-1].argv[j]);
                        free(commands[num_arr-1].argv[j+1]);
                        for (int k=j; k<commands[num_arr-1].argc-2; k++) {
                            commands[num_arr-1].argv[k] = commands[num_arr-1].argv[k+2];
                        }
                        commands[num_arr-1].argc -= 2;
                        commands[num_arr-1].argv[commands[num_arr-1].argc] = NULL;
                        break;
                    }
                }
            }
        }
    }

    if (is_input_file) {
        const char *operator = "<";
        if (validate_operator_placement(commands, num_arr, operator, 0) == -1) {
            fprintf(stderr, "Fatal error: Перенаправление ввода '<' допускается только у первой команды\n");
            exit(1);
        } else {
            if (commands[0].argv != NULL && commands[0].argc > 0) {
                for (int j = 0; j < commands[0].argc; j++) {
                    if (strcmp(commands[0].argv[j], "<") == 0) {
                        commands[0].input_file = strdup(commands[0].argv[j+1]);
                        free(commands[0].argv[j]);
                        free(commands[0].argv[j+1]);
                        for (int k=j; k<commands[0].argc-2; k++)
                            commands[0].argv[k] = commands[0].argv[k+2];
                        
                        commands[0].argc -= 2;
                        commands[0].argv[commands[0].argc] = NULL;
                        break;
                    }
                }
            }
        }
    }

    return num_arr;
}

static int exec_single(command_info_t commands) 
{
    pid_t pid;
    struct sigaction newmask, oldmask;
    if (commands.background == BACKGROUND_ENABLED) {
        newmask.sa_flags = SA_NOCLDWAIT;
        sigemptyset(&newmask.sa_mask);
        if (sigaction(SIGCHLD, &newmask, &oldmask) == -1) {
            perror("sigaction error");
            exit(EXIT_FAILURE);
        }
    }

    if ((pid = fork()) < 0) {
        perror("fork");
        exit(EXIT_FAILURE);
    } else if (pid == 0) {
        if (commands.output_file != NULL) {
            int fd;
            if (commands.output_append) {
                if ((fd = open(commands.output_file, O_WRONLY | O_CREAT | O_APPEND, 0666)) < 0) {
                    perror("open");
                    exit(EXIT_FAILURE);
                }
            } else {
                if ((fd = open(commands.output_file, O_WRONLY | O_CREAT | O_TRUNC, 0666)) < 0) {
                    perror("open");
                    exit(1);
                }
            }
            dup2(fd, STDOUT_FILENO);
            close(fd);
        }
        if (commands.error_file != NULL) {
            int fd;
            if (commands.error_append) {
                if ((fd = open(commands.error_file, O_WRONLY | O_CREAT | O_APPEND, 0666)) < 0) {
                    perror("open");
                    exit(1); 
                }
            } else {
                if ((fd = open(commands.error_file, O_WRONLY | O_CREAT | O_TRUNC, 0666)) < 0) {
                    perror("open");
                    exit(1); 
                }
            }
            dup2(fd, STDERR_FILENO);
            close(fd);
        }
        if (commands.input_file != NULL) {
            int fd;
            if ((fd = open(commands.input_file, O_RDONLY)) < 0) {
                if (errno == ENOENT) {
                    fprintf(stderr, "Fatal error: Файл %s не создан\n", commands.input_file);
                    exit(1);
                }
                fprintf(stderr, "open: невозможно открыть файл %s: %s\n", 
                    commands.input_file, strerror(errno));
                exit(1);
            }
            dup2(fd, STDIN_FILENO);
            close(fd);
        }
        
        execvp(commands.argv[0], commands.argv);
        perror("execvp");
        exit(1);
    }

    int exit_code = 0; 
    if (commands.background == BACKGROUND_ENABLED) {
        printf("[%d]", BACKGROUND_ENABLED);
        printf(" %d\n", pid);
        if (sigaction(SIGCHLD, &oldmask, NULL) == -1) {
            perror("sigaction error");
            exit(EXIT_FAILURE);
        }
    } else {
        int statuslock;
            if (waitpid(pid, &statuslock, 0) < 0) {
                if (errno == ECHILD) 
                    fprintf(stderr, "fatal error: Нет такого процесса с pid: %d: %s\n", pid,
                        strerror(errno));
                else
                    fprintf(stderr, "fatal error: Невозможно дождаться процесс: %d: %s\n",
                        pid, strerror(errno));
                
                exit(1);
            }

        exit_code = WEXITSTATUS(statuslock);
    }
    return exit_code;
}

static int exec_pipeline(command_info_t *commands, int cmd_count) 
{
    int pids[cmd_count];
    int prev_pipes[2] = {-1, -1};
    int exit_code = 0;

    struct sigaction newmask, oldmask;
    if (commands->background == BACKGROUND_ENABLED) {
        newmask.sa_flags = SA_NOCLDWAIT;
        sigemptyset(&newmask.sa_mask);
        if (sigaction(SIGCHLD, &newmask, &oldmask) == -1) {
            perror("sigaction error");
            exit(EXIT_FAILURE);
        }
    }
    
    for (int i=0; i<cmd_count; i++) {
        int next_pipes[2] = {-1, -1};
        if (i < cmd_count-1) {
            if (pipe(next_pipes) == -1) {
                perror("pipe");
                exit(1);
            }
        }

        if ((pids[i] = fork()) < 0) {
            perror("fork");
            exit(1);
        } else if (pids[i] == 0) {
            if (prev_pipes[0] != -1) {
                close(prev_pipes[1]);
                dup2(prev_pipes[0], STDIN_FILENO);
                close(prev_pipes[0]);
            }

            if (prev_pipes[0] == -1 && commands[0].input_file != NULL) {
                int fd;
                if ((fd = open(commands[0].input_file, O_RDONLY)) < 0) {
                    if (errno == ENOENT) {
                        fprintf(stderr, "Fatal error: Файл %s не создан\n", commands[0].input_file);
                        exit(1);
                    }
                    fprintf(stderr, "open: невозможно открыть файл %s: %s\n", 
                        commands[0].input_file, strerror(errno));
                    exit(1);
                }   
                dup2(fd, STDIN_FILENO);
                close(fd);
            }
    
            if (next_pipes[0] != -1) {
                close(next_pipes[0]);
                dup2(next_pipes[1], STDOUT_FILENO);
                close(next_pipes[1]);
            }
            
            if (next_pipes[0] == -1 && commands[cmd_count-1].output_file != NULL) {
                int fd;
                if (commands[cmd_count-1].output_append) {
                    if ((fd = open(commands[cmd_count-1].output_file, O_WRONLY, O_CREAT | O_APPEND)) < 0){
                        perror("open");
                        exit(1);
                    }
                } else {
                    if ((fd = open(commands[cmd_count-1].output_file, O_WRONLY | O_CREAT | O_TRUNC, 0666)) <0) {
                        perror("open");
                        exit(1);  
                    }
                }
                dup2(fd, STDOUT_FILENO);
                close(fd);
            }
            if (next_pipes[0] == -1 && commands[cmd_count-1].error_file != NULL) {
                int fd;
                if (commands[cmd_count-1].error_append) {
                    if ((fd = open(commands[cmd_count-1].error_file, O_WRONLY | O_CREAT | O_APPEND, 0666)) < 0) {
                        perror("open");
                        exit(1);
                    }
                } else {
                    if ((fd = open(commands[cmd_count-1].error_file, O_WRONLY | O_CREAT | O_TRUNC, 0666)) < 0) {
                        perror("open");
                        exit(1);  
                    }
                }
                dup2(fd, STDERR_FILENO);
                close(fd);
            }
            execvp(commands[i].argv[0], commands[i].argv);
            perror("execvp");
            exit(1);
        }
        if (prev_pipes[0] != -1) {
            close(prev_pipes[0]);
            close(prev_pipes[1]);
        }

        prev_pipes[0] = next_pipes[0];
        prev_pipes[1] = next_pipes[1];
    }

    if (prev_pipes[0] != -1) {
        close(prev_pipes[0]);
        close(prev_pipes[1]);
    }

    if (commands->background == BACKGROUND_ENABLED) {
        printf("[%d]", BACKGROUND_ENABLED);
        for (int i=0; i<cmd_count; i++) 
            printf(" %d", pids[i]);
        printf("\n");

        if (sigaction(SIGCHLD, &oldmask, NULL) == -1) {
            perror("sigaction error");
            exit(EXIT_FAILURE);
        }
    } else {
        int statuslock = 0;
        for (int i=0; i<cmd_count; i++) {
            if (waitpid(pids[i], &statuslock, 0) == -1) {
                if (errno != ECHILD) {
                    perror("waitpid");
                    exit(EXIT_FAILURE);
                }
            }
            if (i == cmd_count-1)
                exit_code = WEXITSTATUS(statuslock);
        }
    } 
    
    return exit_code;
}

int exec_commands(char *argv[], int start, int end)
{
    command_info_t commands[MAX_COMMANDS];
    int background = 0;
    int len_argv = end-start+1;
    int cmd_count;
    int exit_code;
    
    if (end >= start && strcmp(argv[end], "&") == 0)
    {
        background = BACKGROUND_ENABLED;
        -- end;
    }

    cmd_count = parse_commands(len_argv, &argv[start], commands);
    
    if (cmd_count == -1)
        exit(EX_OSERR);

    if (background == BACKGROUND_ENABLED) {
        for (int i= 0; i<cmd_count; i++)
            commands[i].background = BACKGROUND_ENABLED;
    } 

    if (cmd_count == 1)
    {
        exit_code = exec_single(commands[0]);
    }
    else
    {
        exit_code = exec_pipeline(commands, cmd_count);
    }
    free_commands(commands, cmd_count);
    return(exit_code);
}

int main(int argc, char *argv[]) 
{
    if (check_help(argc, argv) == -1) 
        exit(EXIT_FAILURE);

    int i = 1;
    int last_status = 0;
    int is_and_operator = 0;
    int is_or_operator = 0;
    int start; 
    int statuslock;
    
    while (i < argc) 
    {
        start = i;
        while (i < argc && strcmp(argv[i], "&&") != 0 && strcmp(argv[i], "||") != 0) 
            i++;
        
        if (i < argc) {
            is_and_operator = (strcmp(argv[i], "&&") == 0);
            is_or_operator = (strcmp(argv[i], "||") == 0);
        }
        
        statuslock = exec_commands(argv, start, i-1);
        
        if (is_and_operator) {
            if (statuslock != 0)
                exit(statuslock);
        } else if (is_or_operator) {
            if (statuslock == 0)
                exit(statuslock);
        }
        last_status = statuslock;
        
        if (i < argc)
            i++;
    }
    
    exit(last_status);
}
