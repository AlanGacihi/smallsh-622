/*
 * Assignment 3: smallsh (Portfolio Assignment)
 * By: Thomas Sugimoto
 * sugimoth@oregonstate.edu
 * CS 344 Operating Systems
 * 11/3/2020
 * smallsh.c
 *
 * smallsh features:
 * - Provide a prompt for running commands
 * - Handle blank lines and comments, which are lines beginning with the # character
 * - Provide expansion for the variable $$
 * - Execute 3 commands exit, cd, and status via code built into the shell
 * - Execute other commands by creating new processes using a function from the exec family of functions
 * - Support input and output redirection
 * - Support running commands in foreground and background processes
 * - Implement custom handlers for 2 signals, SIGINT and SIGTSTP
 */

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <stdlib.h>
#include <fcntl.h>
#include <time.h>


// Constants Declaractions
#define MAX_ARGUMENTS 	512		// Maximum number of arguments in a command line
#define MAX_CMD_LEN 	2048	// Maximum number of characters in a command line
#define MAX_PROCESSES	1000	// Maximum number of active background processes

// Global Variables Declaractions
int   numArgs = 0;				// Number of arguments
char* argList[MAX_ARGUMENTS];	// A total list of all the arguments of the command
int   allowBackground = 1;		// If 1 processes with & can run in the background
int   isBackground = 0;			// Can only be set to 1 if allowBackground is 1
char  currentDir[100];			// String of the current directory
int   processes[MAX_PROCESSES];	// Array of all process's pids
int   numProcesses = 0;			// Total number of forked processes
int   processStatus;			// Process

// Sigaction variables
struct sigaction SIGINTAction;	// SIGINT handler
struct sigaction SIGTSTPAction; // SIGTSTP handler

// Function Declarations
int  getCommands(char* args);
void handle_SIGTSTP();
void makeCommands();
void exit_call();
void cd_call();
void status_call(int* errorSignal);
void otherCommands(int* errorSignal);
void childFork();
void parentFork(pid_t childPid);

/*
 * Function:  main 
 * -------------------------------------------------------------------------
 *  Initializes the custom signal handlers for CTRL-C and CTRL-Z. And loops
 *  continuously to read the lines of the shell.
 *
 *  args: none
 *
 *  returns: none
 */
int main() {
	char argsString[2048];						// String of all arguemnts
	
	// Handle CTRL-Z
	SIGTSTPAction.sa_handler = handle_SIGTSTP; 	// Direct SIGTSTP to the function handle_SIGTSTP()
    SIGTSTPAction.sa_flags = SA_RESTART; 		// Make sure signals don't interrupt processes
    sigfillset(&SIGTSTPAction.sa_mask);			// Block all catchable signals
    sigaction(SIGTSTP, &SIGTSTPAction, NULL);	// Install signal handler

    // Handle CTRL-C
    SIGINTAction.sa_handler=SIG_IGN;			// Ignore initially
    sigfillset(&SIGINTAction.sa_mask); 			// Block all catchable signals
    sigaction(SIGINT, &SIGINTAction, NULL);		// Install signal handler
	
	// Loop to continuously read the lines of shell
	while(1) {
		numArgs = getCommands(argsString);		// Parse the line
		argList[numArgs] = NULL; 				// Mark the end of the array
		makeCommands();							// Use the params to make commands
	}
	return 0;
}

/*
 * Function:  handle_SIGTSTP 
 * -------------------------------------------------------------------------
 *  Custom signal handler for SIGTSTP (when the user presses CTRL-Z).
 *  Switches between two modes: a foreground-only mode and norml mode.
 *
 *  args: none
 *
 *  returns: none
 */
void handle_SIGTSTP() {
	char* statusMessage;		// String of the message to write to stdout
	int statusMessageSize = -1;	// For write() messages must also state the number of characters
	char* promptMessage = ": "; // Also write ': ' since for this case getCommands() won't print it
	switch(allowBackground) {
		case 0:
			statusMessage = "\nExiting foreground-only mode\n";
			statusMessageSize = 30;
			allowBackground = 1;
			break;
		case 1:
			statusMessage = "\nEntering foreground-only mode (& is now ignored)\n";
			statusMessageSize = 50;
			allowBackground = 0;
			break;
		default:
			statusMessage = "\nError: allowBackground is not 0 or 1\n";
			statusMessageSize = 38;
			allowBackground = 1;
	}
	// Must use reentrant function for custom signal handlers
	write(STDOUT_FILENO, statusMessage, statusMessageSize);
	write(STDOUT_FILENO, promptMessage, 2);
}

/*
 * Function:  getCommands 
 * -------------------------------------------------------------------------
 *  Prompts and fetches all the commands by the user to the smallsh shell.
 *  Saves the arguments to the global varaible argList[]
 *
 *  args: a char* of the all the arguments as one continuous string
 *
 *  returns: the number of arguments
 */
int getCommands(char* args) {
	int i, num_args = 0;
	char tempString [MAX_CMD_LEN];
	printf(": ");
	fflush(stdout);
	fgets(args, MAX_CMD_LEN, stdin);
	strtok(args, "\n");

	// Get first toekn of arguments
	char* token = strtok(args, " ");
	// Get the rest of the tokens
	while(token != NULL) {
		argList[num_args] = token;	// Write argument to to global variable

		// Handle the expansion of variable $$
		for(i = 1; i < strlen(argList[num_args]); i++) {
			if(argList[num_args][i] == '$' && argList[num_args][i-1] == '$') {
				// Replace $$ with NULL
				argList[num_args][i] = '\0';
				argList[num_args][i-1] = '\0';
				// Insert pid
				snprintf(tempString, MAX_CMD_LEN, "%s%d", argList[num_args], getpid());
				argList[num_args] = tempString;
			}
		}

		token = strtok(NULL, " ");	// Get next token
		num_args++;
	}
	return num_args;
}

/*
 * Function:  makeCommands 
 * -------------------------------------------------------------------------
 * Takes the arguments from argList[] and directes to the corresponding functions
 *
 * args: none
 *
 * returns: none
 */
void makeCommands() {
	int errorSignal = 0;	// 1 if status_call() was already called

	if(argList[0][0] == '#' || argList[0][0] == '\n') {
		// Ignore comments and empty lines
	}
	else if(strcmp(argList[0], "exit") == 0) {
		exit_call();
	}
	else if(strcmp(argList[0], "cd") == 0) {
		cd_call();
	}
	else if(strcmp(argList[0], "status") == 0) {
		status_call(&errorSignal);
	}
	else {
		otherCommands(&errorSignal);

		// Print out the status if it hasn't been already printed
		if(WIFSIGNALED(processStatus) && errorSignal == 0){ 
	        status_call(&errorSignal); 
	    }
	}
}

/*
 * Function:  exit_call 
 * -------------------------------------------------------------------------
 * Kills all currently running background process and exits the main program
 *
 * args: none
 *
 * returns: none
 */
void exit_call() {
	// Exit the program without terminating any processes
	if(numProcesses == 0)
		exit(0);
	// There are processes that must be terminated one by one
	else{
		int i;
		for(i = 0; i < numProcesses; i++) 
			kill(processes[i], SIGTERM);
		exit(1);
	}
}

/*
 * Function:  cd_call 
 * -------------------------------------------------------------------------
 * Built in directory navigator. If just 'cd' is entered it will bring the user to
 * the directory specified in the HOME environment variable. Else if parameters are
 * entered then it will move the directory to the specificied path. Works with both
 * relative and absolute paths.
 *
 * args: none
 *
 * returns: none
 */
void cd_call() {
	int error = 0;

	// No arguments for cd
	if(numArgs == 1) 
		error = chdir(getenv("HOME"));	// Change directory to home dir
	else 
		error = chdir(argList[1]);		// Navigate to directory in argument
	
	if(error == 0)
		printf("%s\n", getcwd(currentDir, 100)); // Writes to global variable and prints
	else
		printf("chdir() failed\n");
	fflush(stdout);
}

/*
 * Function:  status_call 
 * -------------------------------------------------------------------------
 * Prints out either the exit status or the terminating signal of the last 
 * foreground process ran by the shell.
 *
 * args: none
 *
 * returns: none
 */
void status_call(int* errorSignal) {
	int errHold = 0, sigHold = 0, exitValue;

	waitpid(getpid(), &processStatus, 0);		// Check the status of the last process

	if(WIFEXITED(processStatus)) 
        errHold = WEXITSTATUS(processStatus);	// Return the status of the normally terminated child

    if(WIFSIGNALED(processStatus)) 
        sigHold = WTERMSIG(processStatus);		// Return the status of an abnormally terminated child

    exitValue = errHold + sigHold == 0 ? 0 : 1;

    if(sigHold == 0) 
    	printf("exit value %d\n", exitValue);
    else {
    	*errorSignal = 1;
    	printf("terminated by signal %d\n", sigHold);
    }
    fflush(stdout);
}

/*
 * Function:  otherCommands 
 * -------------------------------------------------------------------------
 * Execute any commands other than the 3 built-in command by using fork(), 
 * exec() and waitpid()
 *
 * args: error signal used to pass information when a signal is terminated
 *
 * returns: none
 */
void otherCommands(int* errorSignal) {
	pid_t pid;				// pid of the child when forked
	isBackground = 0;

	// When there is a &, set this process to be in the background otherwise ignore
    if(strcmp(argList[numArgs-1], "&") == 0) {
    	// Only make it a background process if not in foreground only mode
    	if(allowBackground == 1) 
    		isBackground = 1;
    	// Ignore the argument for later
    	argList[numArgs - 1] = NULL;
    }

	pid = fork();					// Start process
	processes[numProcesses] = pid;	// Save process pid
	numProcesses++;

	switch(pid) {
		case -1: // Error
			perror("fork() failed\n");
			exit(1);
			break;

		case 0:  // Child
			childFork();
			break;

		default: // Parent
			parentFork(pid);
	}

	// Wait for the child to finish
	while ((pid = waitpid(-1, &processStatus, WNOHANG)) > 0) {
        printf("background pid %d is done: ", pid);
        fflush(stdout);
        status_call(errorSignal); 
	}
}

/*
 * Function:  childFork 
 * -------------------------------------------------------------------------
 * Execute the code for the child prcess:
 * - Parse and read input and output files and copy
 * - Enable a custom SIGINT handler
 * - Execute the non-build in command with execvp()
 *
 * args: none
 *
 * returns: none
 */
void childFork() {
	int i, haveInputFile = 0, haveOutputFile = 0;
	char inputFile[MAX_CMD_LEN], outputFile[MAX_CMD_LEN];
	
	// Get command arguments
	for(i = 0; argList[i] != NULL; i++) {
		// Input File Arguments
		if(strcmp(argList[i], "<") == 0) {
			haveInputFile = 1;
			argList[i] = NULL;
			strcpy(inputFile, argList[i+1]);
			i++;
		}
		// Output file Arugments
		else if(strcmp(argList[i], ">") == 0) {
			haveOutputFile = 1;
			argList[i] = NULL;
			strcpy(outputFile, argList[i+1]);
			i++;
		}
	}

	// Pass input file info
	if(haveInputFile) {
		int inputFileDes = 0;
		if ((inputFileDes = open(inputFile, O_RDONLY)) < 0) { 
            fprintf(stderr, "cannot open %s for input\n", inputFile);
            fflush(stdout); 
            exit(1); 
        }  
        dup2(inputFileDes, 0);
        close(inputFileDes);
	}

	// Pass output file info
	if(haveOutputFile) {
		int outputFileDes = 0;
		if((outputFileDes = open(outputFile, O_WRONLY | O_CREAT | O_TRUNC, 0644)) < 0) {
			fprintf(stderr, "cannot open %s for output\n", outputFile);
			fflush(stdout); 
			exit(1); 
		}
		dup2(outputFileDes, 1);
        close(outputFileDes);
	}

	// Give new CTRL-C handler so foreground processes can be terminated
	if(!isBackground) 
		SIGINTAction.sa_handler=SIG_DFL;
	sigaction(SIGINT, &SIGINTAction, NULL);

	// Run the non-built-in command
	if(execvp(argList[0], argList) == -1 ) {
        perror(argList[0]);
        exit(1); 
    }
}

/*
 * Function:  parentFork 
 * -------------------------------------------------------------------------
 * Execute code for the parent. Wait for child if its running in the foreground.
 * Or listen for the background child to finish.
 *
 * args: the pid of the child process for the parent to monitor and wait
 *
 * returns: none
 */
void parentFork(pid_t childPid) {
	if(isBackground == 1) {
		waitpid(childPid, &processStatus, WNOHANG);
		printf("background pid is %d\n", childPid);
		fflush(stdout); 
	}
	else {
		waitpid(childPid, &processStatus, 0);
	}
}