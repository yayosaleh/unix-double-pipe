/*--------------------------------------------------------------------

Description: Pipes the standard output of one process
             into the standard input of two other processes.
             Usage: dp <cmd1 arg...> : <cmd2 arg...> : <cmd3 arg....>
             Output from process created with cmd1 is piped to
             processes created with cmd2 and cmd3

-------------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

// Error messages
const char *PIPE_ERR = "Error: failed to create pipe.\n";
const char *FORK_ERR = "Error: failed to fork.\n";
const char *DUP_ERR = "Error: failed to duplicate file descriptor.\n";
const char *EXEC_ERR = "Error: execvp failed.\n";

// Prototypes
int doublePipe(char **, char **, char **);

// Parses the command line arguments into three arrays of strings one for each command to execv()
int main(int argc, char *argv[])
{

   int i,j;
   char *cmd1[10];
   char *cmd2[10];
   char *cmd3[10];
   if(argc == 1)
   {
     printf("Usage: dp <cmd1 arg...> : <cmd2 arg...> : <cmd3 arg....>\n");
     exit(1);
   }

   // Get the first command
   for(i = 1, j = 0; i < argc; i++, j++)
   {
      if(!strcmp(argv[i], ":")) break; // Found first command
      cmd1[j] = argv[i];
   }
   cmd1[j] = NULL;
   if(i == argc) // Missing :
   {
      printf("Bad command syntax - only one command found\n");
      exit(1);
   }
   else i++;

   // Get the second command
   for(j = 0; i < argc; i++, j++)
   {
      if(!strcmp(argv[i], ":")) break; // Found second command
      cmd2[j] = argv[i];
   }
   cmd2[j] = NULL;
   if(i == argc) // Missing :
   {
      printf("Bad command syntax - only two commands found\n");
      exit(1);
   }
   else i++;

   // Get the third command
   for(j=0; i < argc; i++, j++) cmd3[j] = argv[i];
   cmd3[j] = NULL;
   if(j == 0) // No command after last :
   {
      printf("Bad command syntax - missing third command\n");
      exit(1);
   }

   exit(doublePipe(cmd1, cmd2, cmd3));
}

/* Helper Functions */

// Closes both ends of each pipe provided in array of pipe file descriptors
void closePipes(int pipes[][2], int count) {
    for (int i = 0; i < count; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }
}

// Forks caller process, redirects provided pipe end to/from target, and executes provided command
void createAndExecuteProcess(char **cmd, int fd_to_dup, int fd_to_target, int pipes_to_close[][2], int close_count) {
   
   // Fork and check for failure
   int pid = fork();
   if (pid == -1) { 
      perror(FORK_ERR);
      exit(EXIT_FAILURE); 
   }
   
   // Child process
   if (pid == 0) { 
      
      // Pipe-file redirection
      // Note: this relies on file descriptors being inherited from parent
      if (dup2(fd_to_dup, fd_to_target) == -1) {
         perror(DUP_ERR);
         exit(EXIT_FAILURE); 
      }

      // Close all pipe ends for proper synchronization (EOF, etc.)
      // Note: dup2() renders duplicated file descriptor redundant
      closePipes(pipes_to_close, close_count);

      // Execute command 
      // Note: program will replace process image and close open pipe end upon termination
      if (execvp(cmd[0], cmd) == -1) { 
         perror(EXEC_ERR);
         exit(EXIT_FAILURE); 
      }

   } 

}

/*--------------------------------------------------------------------------

Function: doublePipe()

Description: Starts three processes, one for each of cmd1, cmd2, and cmd3.
             The parent process will receive the output from cmd1 and
             pipe it to the other two processes.

-------------------------------------------------------------------------*/

int doublePipe(char **cmd1, char **cmd2, char **cmd3)
{
	
   // Create three pipes and check for failure
   int head_to_tee[2], tee_to_leg1[2], tee_to_leg2[2];
   if (pipe(head_to_tee) == -1 || pipe(tee_to_leg1) == -1 || pipe(tee_to_leg2) == -1 ) {
      perror(PIPE_ERR);
      exit(EXIT_FAILURE); 
   }

   // Create list of pipes whose ends must be closed by parent, head and leg processes
   int pipes_to_close[3][2] = {{head_to_tee[0], head_to_tee[1]}, {tee_to_leg1[0], tee_to_leg1[1]}, {tee_to_leg2[0], tee_to_leg2[1]}};   

   // Create head process (executes cmd1) and redirect its stdout to head-to-tee pipe's write end
   createAndExecuteProcess(cmd1, head_to_tee[1], STDOUT_FILENO, pipes_to_close, 3);

   // Create tee process (distributes head's output to legs)
   int pid_tee = fork();
   if (pid_tee == -1) {
      perror(FORK_ERR);
      exit(EXIT_FAILURE); 
   }
   
   // Tee 
   if (pid_tee == 0) { 

      // Close unused pipe ends
      close(head_to_tee[1]);
      close(tee_to_leg1[0]);
      close(tee_to_leg1[0]);

      // Buffer data from head-to-tee pipe and distribute to both legs
      char buffer[1024];
      ssize_t bytesRead;
      while ((bytesRead = read(head_to_tee[0], buffer, sizeof(buffer))) > 0) {
         write(tee_to_leg1[1], buffer, bytesRead);
         write(tee_to_leg2[1], buffer, bytesRead);
      }

      // Close remaining pipe ends
      close(head_to_tee[0]);
      close(tee_to_leg1[1]);
      close(tee_to_leg1[1]);

      // Terminate process (prevent fall through)
      exit(0);

   }

   // Create leg processes (execute cmd2,3) and redirect tee-to-leg pipe read end to stdin
   createAndExecuteProcess(cmd2, tee_to_leg1[0], STDIN_FILENO, pipes_to_close, 3);
   createAndExecuteProcess(cmd3, tee_to_leg2[0], STDIN_FILENO, pipes_to_close, 3);

   // Parent clean-up

   // Close all pipes (since none are used)
   closePipes(pipes_to_close, 3);

   // Wait for and reap exit status of all four child processes (prevents children from becoming orphans/zombies)
   for (int i = 0; i < 4; i++) { wait(NULL); }

   return 0; 
}