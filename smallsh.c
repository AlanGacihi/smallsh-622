#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <ctype.h>
#include <stdbool.h>

#define MAX_WORDS 512

char *words[MAX_WORDS];

size_t wordsplit(char const *line);
char *expand(char const *word);
void execute_command(char *command[], bool background);
void handle_sigint(int signum);
void handle_sigtstp(int signum);

int main(int argc, char *argv[])
{
    // Signal handling setup
    signal(SIGINT, handle_sigint);
    signal(SIGTSTP, handle_sigtstp);

    FILE *input = stdin;
    char *input_fn = "(stdin)";
    if (argc == 2)
    {
        input_fn = argv[1];
        input = fopen(input_fn, "r");
        if (!input)
        {
            perror("Error opening input file");
            exit(EXIT_FAILURE);
        }
    }
    else if (argc > 2)
    {
        fprintf(stderr, "Too many arguments\n");
        exit(EXIT_FAILURE);
    }

    char *line = NULL;
    size_t n = 0;
    ssize_t line_len;

    while (1)
    {
        // Display the interactive input prompt
        printf("myshell> ");
        fflush(stdout); // Flush the output to display the prompt immediately

        // Read input line from the user
        line_len = getline(&line, &n, input);

        // Check for empty line or EOF (Ctrl+D in Linux) to exit the loop
        if (line_len <= 1) // Empty line contains only the newline character
        {
            if (feof(input))
            {
                // End of input (Ctrl+D in Linux)
                printf("Exiting the shell...\n");
                break;
            }
            else
            {
                // Continue to the next iteration for an empty line
                continue;
            }
        }

        // Parse the input line into words
        size_t nwords = wordsplit(line);

        // Check if the command should be executed in the background
        bool background = false;
        if (nwords > 0 && strcmp(words[nwords - 1], "&") == 0)
        {
            background = true;
            words[nwords - 1] = NULL; // Remove "&" from the command
            nwords--;
        }

        // Handle built-in commands: exit and cd
        if (nwords > 0)
        {
            if (strcmp(words[0], "exit") == 0)
            {
                printf("Exiting the shell...\n");
                break;
            }
            else if (strcmp(words[0], "cd") == 0)
            {
                if (nwords == 1)
                {
                    // Change to the user's home directory
                    chdir(getenv("HOME"));
                }
                else if (nwords == 2)
                {
                    // Change to the specified directory
                    if (chdir(words[1]) == -1)
                    {
                        perror("cd");
                    }
                }
                else
                {
                    fprintf(stderr, "cd: Too many arguments\n");
                }
                continue;
            }
        }

        // Expand parameters in each word
        for (size_t i = 0; i < nwords; ++i)
        {
            char *exp_word = expand(words[i]);
            free(words[i]);
            words[i] = exp_word;
        }

        // Execute the command
        execute_command(words, background);
    }

    // Free allocated memory and close input file if not stdin
    free(line);
    if (input != stdin)
    {
        fclose(input);
    }

    return 0;
}

size_t wordsplit(char const *line)
{
    size_t wlen = 0;
    size_t wind = 0;

    char const *c = line;
    for (; *c && isspace(*c); ++c); /* discard leading space */

    for (; *c;)
    {
        if (wind == MAX_WORDS)
            break;
        /* read a word */
        if (*c == '#')
            break;
        for (; *c && !isspace(*c); ++c)
        {
            if (*c == '\\')
                ++c;
            void *tmp = realloc(words[wind], sizeof **words * (wlen + 2));
            if (!tmp)
            {
                perror("realloc");
                exit(EXIT_FAILURE);
            }
            words[wind] = tmp;
            words[wind][wlen++] = *c;
            words[wind][wlen] = '\0';
        }
        ++wind;
        wlen = 0;
        for (; *c && isspace(*c); ++c);
    }
    return wind;
}

char param_scan(char const *word, char **start, char **end)
{
    static char *prev;
    if (!word)
        word = prev;

    char ret = 0;
    *start = NULL;
    *end = NULL;
    char *s = strchr(word, '$');
    if (s)
    {
        char *c = strchr("$!?", s[1]);
        if (c)
        {
            ret = *c;
            *start = s;
            *end = s + 2;
        }
        else if (s[1] == '{')
        {
            char *e = strchr(s + 2, '}');
            if (e)
            {
                ret = *c;
                *start = s;
                *end = e + 1;
            }
        }
    }
    prev = *end;
    return ret;
}

char *build_str(char const *start, char const *end)
{
    static size_t base_len = 0;
    static char *base = NULL;

    if (!start)
    {
        // Reset; new base string, return old one
        char *ret = base;
        base = NULL;
        base_len = 0;
        return ret;
    }
    // Append [start, end) to base string
    // If end is NULL, append the whole start string to the base string.
    size_t n = end ? end - start : strlen(start);
    size_t newsize = sizeof *base * (base_len + n + 1);
    void *tmp = realloc(base, newsize);
    if (!tmp)
    {
        perror("realloc");
        exit(EXIT_FAILURE);
    }
    base = tmp;
    memcpy(base + base_len, start, n);
    base_len += n;
    base[base_len] = '\0';

    return base;
}

char *expand(char const *word)
{
    char const *pos = word;
    char *start, *end;
    char c = param_scan(pos, &start, &end);
    build_str(NULL, NULL);
    build_str(pos, start);
    while (c)
    {
        if (c == '!')
            build_str("<BGPID>", NULL);
        else if (c == '$')
            build_str("<PID>", NULL);
        else if (c == '?')
            build_str("<STATUS>", NULL);
        else if (c == '{')
        {
            build_str("<Parameter: ", NULL);
            build_str(start + 2, end - 1);
            build_str(">", NULL);
        }
        pos = end;
        c = param_scan(pos, &start, &end);
        build_str(pos, start);
    }
    return build_str(start, NULL);
}

void execute_command(char *command[], bool background)
{
    pid_t pid = fork();
    if (pid == -1)
    {
        perror("fork");
        return;
    }

    if (pid == 0)
    {
        // Child process
        // Check for input/output redirection
        int in_fd = fileno(stdin);
        int out_fd = fileno(stdout);
        int append = 0;

        for (int i = 0; command[i] != NULL; i++)
        {
            if (strcmp(command[i], "<") == 0)
            {
                // Input redirection
                if (command[i + 1] == NULL)
                {
                    fprintf(stderr, "Error: Missing filename for input redirection\n");
                    exit(EXIT_FAILURE);
                }
                FILE *input_file = fopen(command[i + 1], "r");
                if (!input_file)
                {
                    perror("Error opening input file");
                    exit(EXIT_FAILURE);
                }
                in_fd = fileno(input_file);
                command[i] = NULL; // Remove the "<" from the command
            }
            else if (strcmp(command[i], ">") == 0)
            {
                // Output redirection
                if (command[i + 1] == NULL)
                {
                    fprintf(stderr, "Error: Missing filename for output redirection\n");
                    exit(EXIT_FAILURE);
                }
                FILE *output_file = fopen(command[i + 1], "w");
                if (!output_file)
                {
                    perror("Error opening output file");
                    exit(EXIT_FAILURE);
                }
                out_fd = fileno(output_file);
                command[i] = NULL; // Remove the ">" from the command
            }
            else if (strcmp(command[i], ">>") == 0)
            {
                // Output redirection (append mode)
                if (command[i + 1] == NULL)
                {
                    fprintf(stderr, "Error: Missing filename for output redirection\n");
                    exit(EXIT_FAILURE);
                }
                FILE *output_file = fopen(command[i + 1], "a");
                if (!output_file)
                {
                    perror("Error opening output file");
                    exit(EXIT_FAILURE);
                }
                out_fd = fileno(output_file);
                append = 1;
                command[i] = NULL; // Remove the ">>" from the command
            }
        }

        // Redirect input/output to the specified file descriptors
        if (in_fd != fileno(stdin))
        {
            if (dup2(in_fd, fileno(stdin)) == -1)
            {
                perror("dup2");
                exit(EXIT_FAILURE);
            }
            close(in_fd);
        }

        if (out_fd != fileno(stdout))
        {
            if (dup2(out_fd, fileno(stdout)) == -1)
            {
                perror("dup2");
                exit(EXIT_FAILURE);
            }
            close(out_fd);
        }

        // Execute the command
        execvp(command[0], command);
        perror("execvp"); // This line will be reached only if execvp fails
        exit(EXIT_FAILURE);
    }
    else
    {
        // Parent process
        if (!background)
        {
            // Wait for the child to finish (foreground execution)
            int status;
            waitpid(pid, &status, 0);
        }
        else
        {
            // Background execution
            printf("Background process created with PID: %d\n", pid);
        }
    }
}

void handle_sigint(int signum)
{
    // Ignore the SIGINT signal in the shell process
    signal(signum, SIG_IGN);
    printf("\n");
    fflush(stdout);
}

void handle_sigtstp(int signum)
{
    // Ignore the SIGTSTP signal in the shell process
    signal(signum, SIG_IGN);
    printf("\n");
    fflush(stdout);
}

