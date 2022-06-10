#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>

volatile static sig_atomic_t exit_val = 0;
volatile static sig_atomic_t background = 0;
volatile static sig_atomic_t end_proc = 0;

/*
 * Struct for command information.
 */
struct command
{
	char *func;
	char *argv[3];
	char *input;
	char *output;
	char *back;
};

/*
 * Parses commands entered by user.
 */

struct command *
createCommand(char *user_input)
{
	struct command *cmd = malloc(sizeof(struct command));
	char *saveptr;
	char *token = "";
	cmd->input = calloc(strlen(token) + 1, sizeof(char));
	cmd->output = calloc(strlen(token) + 1, sizeof(char));
	cmd->back = calloc(strlen(token) + 1, sizeof(char));
	strcpy(cmd->input, token);
	strcpy(cmd->output, token);
	strcpy(cmd->back, token);
	if (strcmp(user_input + strlen(user_input) - 1, "&") == 0)
	{
		// Handle background token first.
		strcpy(cmd->back, "&");
		user_input[strlen(user_input) - 2] = '\0';
	}

	// First token is the function name and argv[0].
	token = strtok_r(user_input, " ", &saveptr);
	cmd->func = calloc(strlen(token) + 1, sizeof(char));
	cmd->argv[0] = calloc(strlen(token) + 1, sizeof(char));
	strcpy(cmd->func, token);
	strcpy(cmd->argv[0], token);

	if (strcmp(cmd->func, "echo") == 0)
	{
		if (strlen(saveptr) == 0)
		{
			return cmd;
		}
		token = strtok_r(NULL, "\0", &saveptr);
		cmd->argv[1] = calloc(strlen(token) + 1, sizeof(char));
		strcpy(cmd->argv[1], token);
		cmd->argv[2] = NULL;
	}
	else
	{
		while (strlen(saveptr) != 0)
		{
			// Progress through user input and assign to appropriate struct variable.
			if (strncmp(saveptr, "<", 1) == 0)
			{
				// Next token is input.
				strcpy(saveptr, &saveptr[2]);
				token = strtok_r(NULL, " ", &saveptr);
				cmd->input = calloc(strlen(token) + 1, sizeof(char));
				strcpy(cmd->input, token);
			}
			else if (strncmp(saveptr, ">", 1) == 0)
			{
				// Next token is output.
				strcpy(saveptr, &saveptr[2]);
				token = strtok_r(NULL, " ", &saveptr);
				cmd->output = calloc(strlen(token) + 1, sizeof(char));
				strcpy(cmd->output, token);
			}
			else
			{
				// Next token is argv[1].
				token = strtok_r(NULL, " \0", &saveptr);
				if (strlen(saveptr) != 0 && strncmp(saveptr, ">", 1) != 0 && strncmp(saveptr, "<", 1) != 0)
				{
					// Arguments broken up by spaces.
					char *temptoken = "";
					temptoken = calloc(strlen(token) + 1, sizeof(char));
					strcpy(temptoken, token);
					strcat(temptoken, " ");
					token = strtok_r(NULL, " \0", &saveptr);
					strcat(temptoken, token);
					cmd->argv[1] = calloc(strlen(temptoken) + 1, sizeof(char));
					strcpy(cmd->argv[1], temptoken);
				}
				else
				{
					// Argument is single phrase, no spaces.
					cmd->argv[1] = calloc(strlen(token) + 1, sizeof(char));
					strcpy(cmd->argv[1], token);
				}
				cmd->argv[2] = NULL;
			}
		}
	}
	return cmd;
}

/*
 * Executes built in commands for the shell, including:
 * exit - ends the process and exits the shell
 * status - returns the last exit code of a process other than a built-in command
 * cd - changes directory to specified path (relative or absolute) or to path
 *	of home directory if no path is specified.
 */

int builtIn(char *command, int exit_val)
{
	if (strcmp(command, "exit") == 0)
	{
		// Built-in command for 'exit', breaks loop and ends program.
		end_proc = -1;
	}
	else if (strncmp(command, "status", 6) == 0)
	{
		// Built-in command for 'status', prints most recent exit value,
		// or 0 if only built-in commands have been run.
		if (exit_val <= 1)
		{
			printf("exit value %d\n", exit_val);
		}
		else
		{
			printf("terminated by signal %d\n", exit_val);
		}
		fflush(stdout);
	}
	else if (strncmp(command, "cd", 2) == 0)
	{
		// Built-in command for 'cd', changes directory using either
		// absolute or relative pathing. If no path is specified, sends
		// user to the home directory.
		if (strcmp(command, "cd") == 0)
		{
			chdir(getenv("HOME"));
		}
		else
		{
			chdir(&command[3]);
		}
	}
	return 0;
}

/*
 * Opens a file for reading, or throws an error if file is not found.
 *
 * The following input redirection was modifed from
 * code provided by the CS 344 lecture,
 * Title: Exploration: Processes and I/O
 */
int openInput(char *input)
{
	// Open file for reading
	int inputFD = open(input, O_RDONLY);
	if (inputFD == -1)
	{
		perror(input);
		exit(1);
	}
	// Redirect stdin to source file
	int result = dup2(inputFD, 0);
	if (result == -1)
	{
		perror("input dup2()");
		exit(1);
	}
	return inputFD;
}

/*
 * Opens a file for writing, or throws an error if file cannot be opened or created.
 *
 * The following output redirection was modifed from
 * code provided by the CS 344 lecture,
 * Title: Exploration: Processes and I/O
 */
int openOutput(char *output)
{
	// Open file for writing
	int outputFD = open(output, O_WRONLY | O_CREAT | O_TRUNC, 0640);
	if (outputFD == -1)
	{
		perror("output open()");
		fflush(stdout);
		exit(1);
	}
	// Redirect stdout to source file
	int result = dup2(outputFD, 1);
	if (result == -1)
	{
		perror("output dup2");
		fflush(stdout);
		exit(1);
	}
	return outputFD;
}

/*
 * Custom signal handler for SIGINT
 */
void handle_SIGINT(int signo)
{
	int save_err = errno;
	exit_val = 2;
	char *message = ("terminated by signal 2\n");
	write(STDOUT_FILENO, message, 24);
	errno = save_err;
}

/*
 * Custom signal handler for SIGTSTP
 */
void handle_SIGTSTP(int signo)
{
	int save_err = errno;
	if (background == 0)
	{
		char *message = "\nEntering foreground-only mode (& is now ignored)\n";
		write(STDOUT_FILENO, message, 52);
		background = 1;
	}
	else
	{
		char *message = "\nExiting foreground-only mode\n";
		write(STDOUT_FILENO, message, 31);
		background = 0;
	}
	end_proc = 1;
	errno = save_err;
}

/*
 *	Compile the program as follows:
 *		gcc --std=gnu99 -g -Wall -o smallsh smallsh.c
 */

int main()
{
	int pid = getpid();
	char *str_pid;
	sprintf(str_pid, "%d", pid);
	int childArr[10] = {0};
	int inputFD = 0;
	int outputFD = 0;
	struct sigaction SIGINT_action = {{0}}, SIGTSTP_action = {{0}}, ignore_action = {{0}};

	/*
	 * The following signal handling was modifed from
	 * code provided by the CS 344 lecture,
	 * Title: Exploration: Signal Handling API
	 */

	// Fill SIGINT_action struct
	SIGINT_action.sa_handler = handle_SIGINT;
	sigfillset(&SIGINT_action.sa_mask);
	SIGINT_action.sa_flags = 0;

	// FILL SIGTSTP_action struct
	SIGTSTP_action.sa_handler = handle_SIGTSTP;
	sigfillset(&SIGTSTP_action.sa_mask);
	SIGTSTP_action.sa_flags = 0;

	// Fill ignore_action struct
	ignore_action.sa_handler = SIG_IGN;

	while (end_proc != -1)
	{
		// Register custom signal handlers.
		sigaction(SIGINT, &ignore_action, NULL);
		sigaction(SIGTSTP, &SIGTSTP_action, NULL);
		printf(": ");
		char command[2048] = "";
		/*
		 * The following getchar loop was inspired by code written
		 * by users vinayawsm and ericbn on stackoverflow
		 * Source: https://stackoverflow.com/questions/30939517/loop-through-user-input-with-getchar
		 */
		int i = 0;
		while ((command[i] = getchar()) != '\n')
		{
			if (end_proc == 1)
			{
				// Flag received from custom SIGTSTP handler, break loop and restart.
				break;
			}
			// Loop to get user input until user hits enter.
			if (strcmp(&command[i], "$") == 0 && strncmp(&command[i - 1], "$", 1) == 0)
			{
				// User entered $$, expand variable to parent ID.
				strcpy(command, strtok(command, "$"));
				strcat(command, str_pid);
				i = strlen(command) - 1;
			}
			i++;
		}
		if (end_proc == 1)
		{
			// Resets flag for custom SIGTSTP handler.
			strcpy(command, "#");
			end_proc = 0;
		}
		if (strncmp(command, "\n", 1) != 0)
		{
			// Remove excess newlines to ensure command readability.
			strcpy(command, strtok(command, "\n"));
		}
		if ((strcmp(command, "exit") == 0) || strncmp(command, "status", 6) == 0 || strncmp(command, "cd", 2) == 0)
		{
			// Run built-in command.
			builtIn(command, exit_val);
		}
		else if (strncmp(command, "#", 1) != 0 && strncmp(command, "\n", 1) != 0 && strncmp(command, " ", 1) != 0 && strncmp(command, "\0", 1) != 0)
		{
			// Searches for and runs any command that is not built-in.
			while (1)
			{
				// Parse entered command and fork to child process.
				sprintf(command, "%s", command);
				struct command *cmd = createCommand(command);
				pid_t child_pid = -5;
				int child_status;
				int exit_status = 0;
				child_pid = fork();

				switch (child_pid)
				{
				case -1:
					// Fork failed.
					perror("fork()\n");
					fflush(stdout);
					exit(1);
					kill(child_pid, SIGTERM);
					break;
				case 0:
					// Run command as fork.
					if (strcmp(cmd->input, "") != 0)
					{
						// Open source file for read only.
						inputFD = openInput(cmd->input);
						if (inputFD == -1)
						{
							break;
						}
					}
					else if (strcmp(cmd->back, "&") == 0 && background == 0)
					{
						// Background command witn no input, open /dev/null for input.
						inputFD = open("/dev/null", O_RDONLY);
						dup2(inputFD, 0);
					}
					if (strcmp(cmd->output, "") != 0)
					{
						// Open destination file for write only.
						outputFD = openOutput(cmd->output);
						if (outputFD == -1)
						{
							break;
						}
					}
					else if (strcmp(cmd->back, "&") == 0 && background == 0)
					{
						// Background command witn no output, open /dev/null for output
						outputFD = open("/dev/null", O_WRONLY);
						dup2(outputFD, 1);
					}
					// Run command by sending to execvp().
					exit_status = execvp(cmd->func, cmd->argv);
					switch (exit_status)
					{
					case -1:
						// Command not found, return error code and set exit value to 1.
						perror(cmd->func);
						fflush(stdout);
						exit(1);
						break;
					default:
						// Command found.
						exit(0);
					}
				default:
					// Wait for child process to end and catch exit value.
					if (strcmp(cmd->back, "&") == 0 && background == 0)
					{
						printf("background pid is %d\n", child_pid);
						childArr[0] = child_pid;
						child_pid = waitpid(child_pid, &child_status, WNOHANG);
					}
					else
					{
						// Reset exit_val to 0, unblock signal handlers.
						exit_val = 0;
						sigaction(SIGINT, &SIGINT_action, NULL);
						sigaction(SIGTSTP, &SIGTSTP_action, NULL);
						child_pid = waitpid(child_pid, &child_status, 0);
						if (exit_val == 2)
						{
							// Child process terminated with SIGINT.
							break;
						}
						if (WIFEXITED(child_status))
						{
							// Child process ended normally.
							exit_val = WEXITSTATUS(child_status);
							// Handles exit code for test functions.
							if (strcmp(cmd->func, "test") == 0 && strncmp(cmd->argv[1], "-f", 2) == 0)
							{
								if (access(&cmd->argv[1][3], F_OK) == 0)
								{
									// File exists.
									exit_val = 0;
								}
								else
								{
									// File does not exist.
									exit_val = 1;
								}
							}
						}
						else
						{
							// Child process ended abnormally.
							exit_val = WTERMSIG(child_status);
						}
					}
					break;
				}
				break;
			}
		}
		if (childArr[0] != 0)
		{
			// Check status of background processes before returning control to user.
			int result = waitpid(childArr[0], &exit_val, WNOHANG);
			if (result != 0)
			{
				if (exit_val == 0 || exit_val == 1)
				{
					// Process finished, return exit value.
					printf("background pid %d is done: exit value %d\n", childArr[0], exit_val);
				}
				else
				{
					// Process terminated, return termination signal.
					printf("background pid %d is done: terminated by signal %d\n", childArr[0], exit_val);
				}
				fflush(stdout);
				kill(childArr[0], SIGTERM);
				childArr[0] = 0;
			}
		}
		fflush(stdout);
		// Resets flag for custom SIGTSTP handler.
		if (end_proc == 1)
		{
			end_proc = 0;
		}
	}

	return EXIT_SUCCESS;
}
