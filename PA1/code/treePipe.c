// Barış Pome - 31311
// CS307 - OS - 24-25 Fall - PA1

#include <sys/wait.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// Utility functions

// The function to print info about node to the console
void dash_printer(int number)
{
    if (number > 0)
    {
        for (int i = 0; i < number; i++)
        {
            fprintf(stderr, "---");
        }
    }
}

// The function to check argc returning 1 indicates success returning 0 indicates failure
int argument_checker(int argc)
{
    if (argc != 4)
    {
        fprintf(stderr, "Usage: treePipe <current depth> <max depth> <left-right>\n");
        return 0;
    }
    return 1;
}

// The function to get number if the node is root
int num_for_root(int current_depth)
{
    int num_root;
    if (current_depth == 0)
    {
        fprintf(stderr, "Please enter num1 for the root: ");
        scanf("%d", &num_root);
    }
    else
    {
        // Read num1 for non-root nodes
        scanf("%d", &num_root);
    }
    return num_root;
}

// The function for pipe redirections
void setup_pipe_redirection(int pipe_to_child[2], int pipe_from_child[2])
{
    // Close the unused write end of the pipe to child
    close(pipe_to_child[1]);
    // Close the unused read end of the pipe from child
    close(pipe_from_child[0]);
    // Redirect standard input to the read end of the pipe to child
    dup2(pipe_to_child[0], STDIN_FILENO);
    // Redirect standard output to the write end of the pipe from child
    dup2(pipe_from_child[1], STDOUT_FILENO);
    // Close the original pipe file descriptors after redirection
    close(pipe_to_child[0]);
    close(pipe_from_child[1]);
}

// Function to read data from a pipe and convert to integer
int read_from_pipe(int pipe_fd)
{
    char buffer[10] = {0};                 // Buffer to store the data read from the pipe
    read(pipe_fd, buffer, sizeof(buffer)); // Read from the pipe
    close(pipe_fd);                        // Close the pipe after reading
    return atoi(buffer);                   // Convert the string to an integer and return it
}

// Function to format values into strings
void format_values(int current_depth, int max_depth, int is_left, char *depth, char *max, char *lr)
{
    sprintf(lr, "%d", is_left);              // Format the left-right indicator (0 or 1) as a string
    sprintf(depth, "%d", current_depth + 1); // Format current_depth + 1 as a string
    sprintf(max, "%d", max_depth);           // Format max_depth as a string
}

int main(int argc, char *argv[])
{
    if (!argument_checker(argc))
    {
        return 1;
    }
    // get the information of program
    int l_r = atoi(argv[3]);
    int max_depth = atoi(argv[2]);
    int current_depth = atoi(argv[1]);

    dash_printer(current_depth);
    fprintf(stderr, "> Current depth: %d, lr: %d\n", current_depth, l_r);

    // default value that comes from the leaf nodes
    int num_leaf = 1;
    int num_root = num_for_root(current_depth);

    if (current_depth < max_depth)
    {
        // Left child process
        int parent_child_l[2]; 
        int child_parent_l[2];
        pipe(parent_child_l);
        pipe(child_parent_l);
        if (pipe(parent_child_l) == -1 || pipe(child_parent_l) == -1)
        {
            perror("Pipe failed");
            exit(EXIT_FAILURE);
        }

        pid_t left_child_pid = fork();
        if (left_child_pid < 0)
        {
            perror("Fork failed");
            exit(EXIT_FAILURE);
        }
        else if (left_child_pid == 0)
        {
            // Left child process setup for pipe redirection
            setup_pipe_redirection(parent_child_l, child_parent_l);

            char depth[10], max[10], lr[2];
            format_values(current_depth, max_depth, 0, depth, max, lr); // Format values for left child

            char *args[] = {"./treePipe", depth, max, lr, NULL};
            execvp("./treePipe", args);
            perror("Exec failed");
            return 1;
        }
        close(parent_child_l[0]);
        close(child_parent_l[1]);

        // Send num1 to left child
        dprintf(parent_child_l[1], "%d\n", num_root);
        close(parent_child_l[1]);

        // Get result from left child
        num_root = read_from_pipe(child_parent_l[0]);

        dash_printer(current_depth);
        fprintf(stderr, "> My num1 is: %d\n", num_root);

        // Right child process
        int child_parent_r[2];
        int parent_child_r[2];
        pipe(child_parent_r);
        pipe(parent_child_r);
        if (pipe(parent_child_r) == -1 || pipe(child_parent_r) == -1)
        {
            perror("Pipe failed");
            exit(EXIT_FAILURE);
        }

        pid_t right_child_pid = fork();
        if (right_child_pid < 0)
        {
            perror("Fork failed");
            exit(EXIT_FAILURE);
        }
        else if (right_child_pid == 0)
        {
            // Right child process setup for pipe redirection
            setup_pipe_redirection(child_parent_r, parent_child_r);

            char lr[2];
            char depth[10]; 
            char max[10];
            format_values(current_depth, max_depth, 1, depth, max, lr); // Format values for right child

            char *args[] = {"./treePipe", depth, max, lr, NULL};
            execvp("./treePipe", args);
            perror("Exec failed");
            return 1;
        }

        close(child_parent_r[0]);
        close(parent_child_r[1]);
        dprintf(child_parent_r[1], "%d\n", num_root);
        close(child_parent_r[1]);

        // Get result from right child
        num_leaf = read_from_pipe(parent_child_r[0]);

        dash_printer(current_depth);
        fprintf(stderr, "> Current depth: %d, lr: %d, my num1: %d, my num2: %d\n",
                current_depth, l_r, num_root, num_leaf);

        wait(NULL);
        wait(NULL);
    }
    else
    {
        dash_printer(current_depth);
        fprintf(stderr, "> My num1 is: %d\n", num_root);
    }

    // Program process (left or right)
    int program_send[2];
    int program_get[2];
    pipe(program_send);
    pipe(program_get);

    pid_t program_pid = fork();
    if (program_pid == 0)
    {
        // The pipe redirection for program process
        setup_pipe_redirection(program_send, program_get);

        char *prog;
        if (l_r)
        {
            prog = "./right";
        }
        else
        {
            prog = "./left";
        }
        char *args[] = {prog, NULL};
        execvp(prog, args);
        perror("Exec failed");
        return 1;
    }

    close(program_send[0]);
    close(program_get[1]);
    
    dprintf(program_send[1], "%d\n%d\n", num_root, num_leaf);
    close(program_send[1]);

    // Get result from program process
    int result = read_from_pipe(program_get[0]);

    dash_printer(current_depth);
    fprintf(stderr, "> My result is: %d\n", result);

    if (current_depth == 0)
    {
        printf("The final result is: %d\n", result);
    }
    else
    {
        printf("%d\n", result);
    }
    wait(NULL);
    return 0;
}
