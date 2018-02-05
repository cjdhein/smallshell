#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>

// maximum number of arguments accepted for a command
#define MAX_ARGS 512

/****************************************************
 *													*
 *					GLOBAL VARIABLES				*
 *													*
 * **************************************************/

// used for comparing and recognizing commands
const char* cmd_exit = "exit";
const char* cmd_cd = "cd";
const char* cmd_status = "status";

// status of exiting foreground processes
// are placed here and retrieved from here for
// calls to get_status()
char status_buffer[50];

// the below are used when replacing '$$'
// string to hold the shell's PID
char shellPID[18];

//holds length of the PID
int shellPID_len = 0;

// for getting user input/output
// kept global so signal catcher can free memory
size_t bufferSize = 0; // holds how large the allocated buffer is 
char* lineEntered = NULL;	// points to a buffer allocated by getline(), holding our string + \n + \0
char* newString = NULL; //holds the new string if $$ have to be expanded

// for signal handling
struct sigaction SIGTSTP_action = {0};
struct sigaction SIGINT_action = {0};

// indicator of whether we are operating in foreground only mode
int foreground_only_mode = 0;

// store PID of processes in background (enables killing of processes when exiting shell)
pid_t bkrnd_PIDs[100];
int bkrnd_PID_iter; // points to next open spot


/****************************************************
 *													*
 *				FUNCTION DECLARATIONS				*
 *													*
 * **************************************************/


// main loop that run's the shell's prompt
// passes commands to processCommand)_
void runPrompt();

// Calls functions for built-in commands
// or exec()s non-built in commands
void processCommand(char* cmd);

// Handles the exit command
void exit_shell();

// Handles the cd command
void change_dir(char* argv[]);

// Handles the status command
void get_status();

//	check for finished / terminated children
void check_children();

// Returns the number of occurrences of $$ within string toCheck
int count_PID_sym(char* toCheck);

// If $$ exists in string 'original', newString becomes a copy of original
// but with all instances of $$ being expanded to the shell's PID
int replace_PID_sym(char* original);

// Function to handle SIGTSTP
void catchSIGTSTP(int signo);


// main() ... what more do you want?
int main()
{	
	int tmp = getpid(); //get pid of shell
	sprintf(shellPID,"%d",tmp); //store it into the variable
	shellPID_len = strlen(shellPID); //store it's length too
	runPrompt(); // get this show on the road

	return 0;
}

/************************************************************************
 *							count_PID_sym								*
 *	Counts the number of times '$$' occurs in the provided string		*
 *	Accepts: char* toCheck - a string containing the user's commands	*
 *	Returns: an integer of '$$' occurrences								*
 ***********************************************************************/
int count_PID_sym(char* toCheck)
{
	int PID_sym_total = 0;
	int iter = 0;
	while(toCheck[iter] != '\0')
	{
		if(toCheck[iter] == '$' && toCheck[iter+1] != '\0')
		{
			if(toCheck[iter + 1] == '$')
			{
				PID_sym_total++;
			}
		}
		iter++;
	}
	return PID_sym_total;
}

/************************************************************************
 *							replace_PID_sym								*
 *	Creates newString - a copy of original with each '$$' replaced by	*
 *	the shell's PID.													*
 *	Accepts: char* original - string containing user's commands			*
 *	Returns: boolean integer indicating if replacements occurred		*
 ***********************************************************************/
int replace_PID_sym(char* original)
{
	// tells us whether a replacement was made or not
	// in order to know what string to pass to command processor
	int replacement_occurred = 0;
	int PID_sym_total = count_PID_sym(original); // total number of $$ symbols in 'original'
	if(PID_sym_total > 0)
	{
		// allocate memory for newString
		newString = calloc((shellPID_len - 2) * PID_sym_total + sizeof(original), sizeof(char));
		
		// counters
		int i = 0; //original
		int j = 0; //newString

		// iterate until end of original string
		while(i < strlen(original))
		{
			// true if $$ is found
			if(original[i] == '$' && original[i+1] == '$')
			{
				// concatenate the PID onto newString
				strcat(newString, shellPID);
				j = strlen(newString); // set iterator to end of newString
				i += 2; // set iterator to just past $$
				replacement_occurred = 1; // flag that a replacement has occurred
			}
			else // no $$ found, copy character from original to newString
			{
				newString[j] = original[i];
				j++;
				i++;
			}
		}
		
	}
	return replacement_occurred;
}



/********************************************************************
 *							runPrompt								*
 *	Handles the reading and passing of user input into the command	*
 *	processing function.											*
 *	Calls functions to perform expansion of '$$' into shell PID.	*
 *******************************************************************/
void runPrompt()
{	

	// Create and initialize handler for SIGTSTP
	SIGTSTP_action.sa_handler = catchSIGTSTP;
	sigfillset(&SIGTSTP_action.sa_mask);
	SIGTSTP_action.sa_flags = 0;
	SIGTSTP_action.sa_flags = SA_RESTART; // any read,write,open in progress when signal is received will be restarted
	sigaction(SIGTSTP, &SIGTSTP_action, NULL); // catch and redirect to function

	// Create and initialize handler for SIGINT
	SIGINT_action.sa_handler = SIG_IGN; // set to SIG_IGN so shell will not terminate
	sigfillset(&SIGINT_action.sa_mask);
	SIGINT_action.sa_flags = 0;
	sigaction(SIGINT,&SIGINT_action, NULL);
	
	// main loop to read user commands
	while (1)
	{
		// check for terminate/finished children
		check_children();

		// print prompt
		printf(": ");
		fflush(stdout);

		// read line from user
		getline(&lineEntered, &bufferSize, stdin);
		
		// if line is somehow NULL, continue at next looper iteration 
		if(lineEntered == NULL)
			continue;

		// Remove the trailing \n that getline adds 
		// (this makes an empty entry equal to \0
		lineEntered[strcspn(lineEntered, "\n")] = '\0';

		// if the line is not empty...
		if(strcmp(lineEntered,"\0") != 0)
		{			
			// replaced is a bool indicating if any $$ symbols had to be replaced
			int replaced = replace_PID_sym(lineEntered);
			
			if(replaced) //if an instance of $$ was replaced, call processCommand on the newString
				processCommand(newString);
			else // call it on original string
				processCommand(lineEntered);
		}

		// Free the memory allocated 
		free(lineEntered);
		memset(lineEntered, '\0', sizeof(lineEntered));
		lineEntered = NULL;			
		free(newString);
		newString = NULL;
	}
}

/********************************************************************
 *							processCommand							*
 *																	*
 *	Handles the processing of user commands. First, tokenizing cmd	*
 *	string and checking of tokens for input, output, and			*
 *	background processing requests. Second, calls functions for		*
 *	built-in commands (status, exit, cd). Third, spawns children	*
 *	to handle exec calls for other commands.						*
 *																	*
 *	Accepts: cmd_string - string with user's input					*
 *******************************************************************/

void processCommand(char* cmd_string)
{
	//hold arguments once tokenized
	char* arg_array[MAX_ARGS] = {NULL};
	
	//hold args that will be passed to an execvp() call
	char* exec_args[MAX_ARGS] = {NULL};
	
	// hold total count of all arguments from cmd_string
	int full_arg_count = 0;
	
	// holds the count of arguments without '&' '<' or '>' or redirection filenames
	// used when passing into exec function
	int arg_count_for_exec = 0; 
	
	// for iterating over arguments
	int arg_iter = 0; 
	



// delimiter used to tokenize
	const char delim[2] = " ";
//token
	char *token;
		
//tokenize to split cmd_string into components
	token = strtok(cmd_string,delim);
	while(token != NULL) // token = NULL when nothing left to read
	{
		// allocate proper size for this arg
		//arg_array[arg_iter] = malloc(sizeof(char) * strlen(token) + 1);
		arg_array[arg_iter] = calloc(128, sizeof(char));

		// copy from token into arg_array and increment iterator
		strcpy(arg_array[arg_iter], token);
		arg_iter++;

		//get next token
		token = strtok(NULL, delim);
	}



// set full_arg_count and reset arg_iter to 0
	full_arg_count = arg_iter;
	arg_iter = 0;
	
// flipped once normal args are done
// (reached input / output redirect / background indicator)
// used to ensure we only pass proper arguments to the exec function
	int reached_end_of_exec_args = 0;
	
// below variables used for input / output redirection
	int index_of_input_redirect = 0;
	int index_of_output_redirect = 0;
	
// function as boolean
	int input_redirect_requested = 0;
	int	output_redirect_requested = 0;	

// boolean for whether to run in background or not
	int run_in_background = 0;


// loop through args to determine type (built-in vs external),
// if input/output redirection is requested, and if it should run
// in the background
	for(arg_iter = 0; arg_iter < full_arg_count; arg_iter++)
	{
		
		// if element is <, flag for input redirection
		if(strcmp(arg_array[arg_iter], "<") == 0)
		{
			input_redirect_requested = 1; // toggle input redirect flag
			index_of_input_redirect = arg_iter + 1; // next element will be input file
			
			reached_end_of_exec_args = 1; // all exec args must be before input / output redirection and background processing request
		}
		// if element is >, flag for output redirection
		else if(strcmp(arg_array[arg_iter],">") == 0)
		{
			output_redirect_requested = 1; // toggle output redirect flag
			index_of_output_redirect = arg_iter + 1; // next element will be output fil
					
			reached_end_of_exec_args = 1; // all exec args must be before input / output redirection and background processing request

		}
		// if element is &, flag for background running
		else if(strcmp(arg_array[arg_iter], "&") == 0)
		{
			// ensure it is the last element, if not, ignore
			if ((arg_iter + 1) == full_arg_count)
			{
				// if shell is in foreground only mode, request is ignored
				if(!foreground_only_mode)
					run_in_background = 1;
				
				reached_end_of_exec_args = 1;	// all exec args must be before input / output redirection and background processing request
			}

		}

		// if we have not hit the end of exec args, increment their counter
		else
		{
			if(!reached_end_of_exec_args)
				arg_count_for_exec++;
		}
	}
	

	
	//copy args into exec_args
	for(arg_iter = 0; arg_iter < arg_count_for_exec; arg_iter++)
	{
		exec_args[arg_iter] = arg_array[arg_iter];
	}
	

/* Start parsing commands */

	// built-in exit command received
	if(strcmp(arg_array[0], cmd_exit) == 0)
	{
		// call exit_shell function
		exit_shell();
	}

	// built-in change directory command received
	else if(strcmp(arg_array[0],cmd_cd) == 0)
	{
		char* cd_args[1]; //args for function
		cd_args[0] = arg_array[1]; //copy args
		change_dir(cd_args); //call function
	}

	//built-in status command received
	else if(strcmp(arg_array[0],cmd_status) == 0)
	{
		//run built in status command
		get_status();
	}
	// comment line
	else if(arg_array[0][0] == '#')
	{}	// do nothing
	
/* No built-in command recognized */
// the below will handle non-built in commands
// via child processes
	else
	{
		// hold child Pid and exit method
		pid_t spawnPid = -5;
		int childExitMethod = -5;

		//create child
		spawnPid = fork();
		
		// this is the child	
		if(spawnPid == 0)
		{
			//set sigint to default aciton so sig int will terminate
			//foreground processes
			SIGINT_action.sa_handler = SIG_DFL;
			SIGINT_action.sa_flags = 0;
			sigaction(SIGINT,&SIGINT_action, NULL);

			//check if input redirection is occuring
			if(input_redirect_requested)
			{
				// hold file path for input file
				char inputFilePath[128];
				memset(inputFilePath, '\0', 128 * sizeof(char));

				// prepend with ./ for proper path
				strcat(inputFilePath, "./");
				// append argument containing path
				strcat(inputFilePath, arg_array[index_of_input_redirect]);
				
				// file descriptor for the input file
				// set to read only
				int inputFD = open(inputFilePath, O_RDONLY);
				fcntl(inputFD,F_SETFD,FD_CLOEXEC);
				
				// if unable to open
				if(inputFD == -1)
				{
					printf("ERROR - open() failed on \"%s\"\n", inputFilePath);
					exit(1);
				}
				else
				{
					// perform redirection
					dup2(inputFD,STDIN_FILENO);
				}
			}
			
			//check if output redirection is occuring
			if(output_redirect_requested)
			{
				// hold file path for output file
				char outputFilePath[128];
				memset(outputFilePath, '\0', 128 * sizeof(char));
				
				// prepend with ./ for proper path
				strcat(outputFilePath, "./");
				// append argument containing path
				strcat(outputFilePath, arg_array[index_of_output_redirect]);
		
				// file descriptor for output file
				// set to write only / create and truncate
				int outputFD = open(outputFilePath, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
				fcntl(outputFD,F_SETFD,FD_CLOEXEC);

				// if unable to open, throw error
				if(outputFD == -1)
				{
					printf("ERROR - open() failed on \"%s\"\n", outputFilePath);
					exit(1);
				}
				else
				{
					// perform redirection
					dup2(outputFD,STDOUT_FILENO);
				}
				
			}

			// check if process should run in background
			if(run_in_background)
			{
				// open default file for input output 
				int default_background_inout = open("/dev/null",O_RDWR);
				fcntl(default_background_inout,F_SETFD,FD_CLOEXEC);

				// if input or output redirection has not already been done
				// set to defaul for background process
				if(!input_redirect_requested)
					dup2(default_background_inout, STDIN_FILENO);
				if(!output_redirect_requested)
					dup2(default_background_inout, STDOUT_FILENO);
			}			
		
			// if execvp returns < 0, error occurred
			if(execvp(*exec_args,exec_args) < 0)
			{
				perror("Exec failure!");
				exit(1);
			}
			

			// child process will exit here if exec did not take
			exit(0);

		}
		
/* Parent process will continue from here after fork */

		// hold pid of the finished child
		pid_t finished_child = -5; 
	
		//check if running in background
		if(run_in_background)
		{
			// notify that process is in background
			printf("Process %d started in background\n", spawnPid);

			// add PID to array of background pids and increment iterator
			bkrnd_PIDs[bkrnd_PID_iter] = spawnPid;
			bkrnd_PID_iter++;
		}
		// if in foreground, call waitpid and wait for finish/termination
		else
		{
			finished_child = waitpid(spawnPid,&childExitMethod,0);
		}
			
		if(finished_child > 0)
		{
		//check how child exited
			if(WIFEXITED(childExitMethod) != 0)
			{
				// child exited, get status and store into status_buffer
				int exit_status = WEXITSTATUS(childExitMethod);
				memset(status_buffer,'\0',sizeof(status_buffer));
				sprintf(status_buffer,"Child %d exited with status: %d\n",finished_child, exit_status);
			}
			if(WIFSIGNALED(childExitMethod) != 0)
			{
				// child terminated, get terminating signal, store into status_buffer, and print message t screen
				int term_signal = WTERMSIG(childExitMethod);
				memset(status_buffer,'\0',sizeof(status_buffer));
				sprintf(status_buffer,"Child %d terminated with signal: %d\n",finished_child, term_signal);
				printf("Child %d terminated with signal: %d\n",finished_child, term_signal);
				fflush(stdout);
			}			
		}
	
		
	
		// free allocated arguments
		arg_iter = 0;
		while(arg_array[arg_iter] != NULL)
		{
			free(arg_array[arg_iter]);
			arg_iter++;
		}		

	}

}

/****************************************
 *				exit_shell				*
 * Loops through all background PIDs	*
 * and sends SIGTERM to ensure they		*
 * terminate properly before exiting	*
 * shell								*
 * *************************************/
void exit_shell()
{
	int i;
	for(i=0; i < bkrnd_PID_iter; i++)
	{
		kill(bkrnd_PIDs[i], SIGTERM);
	}
	exit(0);
}

/********************************************************
 *						change_dir						*	
 *														*
 * Changes the pwd of the shell to the provided path.	*		
 *														*
 * Accepts: argv containing the path for the desired	* 
 * directory.											*
 * ******************************************************/
void change_dir(char *argv[])
{
	char* buffer = calloc(100, sizeof(char));
	getcwd(buffer,100);
	fflush(stdout);
	int result;

	if(argv[0] != NULL)
	{
		buffer = argv[0];
		result = chdir(buffer);
		if(result == -1)
		{
			printf("An error has occurred in changing directory. Please ensure directory exists.\n");
			fflush(stdout);
		}
	}
	else
	{
		buffer = getenv("HOME");
		printf("Changing Directory to %s\n", buffer);
		result = chdir(buffer);
		if(result == -1)
		{
			printf("An error has occurred in changing directory. Please ensure directory exists.\n");
			fflush(stdout);
		}		
	}

}

/************************************************
 *					get_status					*
 *												*
 * Print the status (exit code / term signal)	*
 * of the last process ran						*
 * **********************************************/
void get_status()
{
	printf("%s", status_buffer);
}

/************************************************************
 *						check_children						*
 *															*
 * Checks for terminated or finished children that were		*
 * running in the background, printing their exit status.	* 
 ***********************************************************/
void check_children()
{
		int childExitMethod = -5;
		
		pid_t finished_child = waitpid(-1, &childExitMethod, WNOHANG);
		
		// since waitpid will return 0 if nothing terminated,
		// this will check if any child has returned and stop once all
		// waiting children have returned
		while(finished_child > 0)
		{
				//check how child exited
			if(WIFEXITED(childExitMethod) != 0)
			{
				int exit_status = WEXITSTATUS(childExitMethod);
				printf("\nBackground PID %d exited. Status: %d\n", finished_child, exit_status);
				fflush(stdout);
			}
			if(WIFSIGNALED(childExitMethod) != 0)
			{
				int term_signal = WTERMSIG(childExitMethod);
				printf("\nBackground PID %d terminated. Signal: %d\n",finished_child, term_signal);
				fflush(stdout);
			}		

			// continue waitpid()ing until return of < 0
			finished_child = waitpid(-1, &childExitMethod, WNOHANG);
		}
}


/****************************************
 *				catchSIGTSTP			*	
 *										*
 * Handles SIGTSTP signal that toggles	* 
 * foreground only mode.				*
 * *************************************/
void catchSIGTSTP(int signo)
{
	// if in foreground mode, toggle off
	if(foreground_only_mode)
	{
		foreground_only_mode = 0;

		// write message notifying of mode change
		char* msg_off = "Terminal has returned to normal mode. Background requests will be acknowledged.\n";
		write(STDOUT_FILENO,msg_off,81);

	}
	// turn foreground mode on
	else
	{
		foreground_only_mode = 1;

		// write message notifying of mode change
		char* msg_on = "Terminal is now in foreground only mode. Any requests to run processes in background (&) will be ignored.\n";
		write(STDOUT_FILENO,msg_on,107);
	}
	
}



