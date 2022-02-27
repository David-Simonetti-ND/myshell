#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>

// *** Code taken from treecopy.c 

// struct used to store how many directory and files have been copied as well as the number of bytes copied
typedef struct copy_info
{
    int num_dir;
    int num_files;
    int num_bytes;
} copy_info;

// arguments are the source file path and the destination file path, and copies a single file,
// also updates copy_info
// *** whenever we get an error with a systemcall, we do not continue but attempt to close as many files and free as much allocated memory as possible
// if these actions also have an error (like we cant close a file), something is seriously wrong and we exit
// this is why there is nested error cases for the system calls 
int filecopy(const char *source, const char *dest, copy_info *copy_info)
{
    // open file to copy
    int input_fd = open(source, O_RDONLY, 0);
    if ( input_fd < 0 ) {
        fprintf(stderr, "copy: Unable to open file %s: %s\n", source, strerror(errno));
        return 1;
    }

    // open file destination and copy permissions
    struct stat stat_buffer;
    int stat_err = stat(source, &stat_buffer);
    if ( stat_err == -1 ) {
        fprintf(stderr, "copy: Unable to stat file %s: %s\n", source, strerror(errno));
        int close_err = close(input_fd);
        if ( close_err < 0 ) { // something seriously wrong happened
            fprintf(stderr, "copy: Unable to close file %s: %s\n", source, strerror(errno));
            exit(1);
        }
	    return 1;
    }
    // create destination file
    int dest_fd = open(dest, O_CREAT|O_WRONLY, stat_buffer.st_mode);
    if ( dest_fd < 0 ) {
        fprintf(stderr, "copy: Unable to create file %s: %s\n", dest, strerror(errno));
	    int close_err = close(input_fd);
        if ( close_err < 0 ) { // something seriously wrong happened
            fprintf(stderr, "copy: Unable to close file %s: %s\n", source, strerror(errno));
            exit(1);
        }
	    return 1;
    }

    char buffer[4096]; // read in source file in 4kb chunks
    // ints to store return values from the read and write system calls
    int read_ret;
    int write_ret;
    int total_bytes_written = 0;
    while (1)
    {
        // attempt to read in a chunk from the source file
        // 4 cases: read_ret = 4096, read_ret < 4096, read_ret = 0, read_ret is less than 1
        read_ret = read(input_fd, buffer, 4096);
        if ( read_ret < 0 ) {
            if (errno == EINTR) { continue; } // if we encounter an interrupt error, go back to the start of the loop and try to read again
            // if we get a fatal error, attempt to close the files and return
            fprintf(stderr, "copy: Unable to read from file %s: %s\n", source, strerror(errno));
            int close_err = close(input_fd);
            if ( close_err < 0 ) {
                fprintf(stderr, "copy: Unable to close file %s: %s\n", source, strerror(errno));
                exit(1);
            }
            close_err = close(dest_fd);
            if ( close_err < 0 ) {
                fprintf(stderr, "copy: Unable to close file %s: %s\n", dest, strerror(errno));
                exit(1);
            }
            return 1;
        }
        if (!read_ret) break; // we have reached the end of the file
        
        // try to write to destination file
        write_ret = write(dest_fd, buffer, read_ret);
        if ( write_ret < 0 ) {
            if (errno == EINTR) {} // if we get interrupted while trying to write, we try to finish the write in the code block below
            else
            {
                // if we get a fatal error, attempt to close the files and return
                fprintf(stderr, "copy: Unable to write to file %s: %s\n", dest, strerror(errno));
                int close_err = close(input_fd);
                if ( close_err < 0 ) {
                    fprintf(stderr, "copy: Unable to close file %s: %s\n", source, strerror(errno));
                    exit(1);
                }
                close_err = close(dest_fd);
                if ( close_err < 0 ) {
                    fprintf(stderr, "copy: Unable to close file %s: %s\n", dest, strerror(errno));
                    exit(1);
                }
                return 1;
            }
        }
        // if we are unable to write all the bytes, keep trying until we get a hard error
        int current_bytes_written = write_ret;
        if (current_bytes_written < 0) { current_bytes_written = 0; } // special case for EINTR: 0 bytes were written in that case
        while (current_bytes_written < read_ret)
        {
            write_ret = write(dest_fd, buffer, read_ret);
            if ( write_ret < 0 ) {
                if (errno == EINTR) { continue; } // if we encounter another EINTR, try again
                else // if we encounter a fatal error 
                {
                    // if we get a fatal error, attempt to close the files and return
                    fprintf(stderr, "copy: Unable to write to file %s: %s\n", source, strerror(errno));
                    int close_err = close(input_fd);
                    if ( close_err < 0 ) {
                        fprintf(stderr, "copy: Unable to close file %s: %s\n", source, strerror(errno));
                        exit(1);
                    }
                    close_err = close(dest_fd);
                    if ( close_err < 0 ) {
                        fprintf(stderr, "copy: Unable to close file %s: %s\n", dest, strerror(errno));
                        exit(1);
                    }
                    return 1;
                }
            }
            current_bytes_written += write_ret;
        }
        total_bytes_written += current_bytes_written;
    }

    // output when a successful copy occurs
    printf("%s -> %s\n", source, dest);

    // close source file
    int close_err = close(input_fd); // if we can't close a file something is seriously wrong
    if ( close_err < 0 ) {
        fprintf(stderr, "copy: Unable to close file %s: %s\n", source, strerror(errno));
	    close_err = close(dest_fd);
        if ( close_err < 0 ) {
            fprintf(stderr, "copy: Unable to close file %s: %s\n", dest, strerror(errno));
            exit(1);
        }
        exit(1);
    }

    // close destination file
    close_err = close(dest_fd);
    if ( close_err < 0 ) {
        fprintf(stderr, "copy: Unable to close file %s: %s\n", dest, strerror(errno));
	    exit(1);
    }
    // update info about files copied
    copy_info->num_bytes += total_bytes_written;
    copy_info->num_files++;
    return 0;
}

// takes in a directory path and then recursively calls itself for every directory in that directory
// also copies all the files in the directory
// same as filecopy above, if we get a system call error, attempt to free as many resources and return
// if there is an error freeing resources, something is seriously wrong and we quit
int recursive_directory_copy(const char *dirname, const char *destname, copy_info *copy_info)
{
    // attempt to open directory
    DIR *current_dir = opendir(dirname);
    if ( current_dir == 0 ) {
        fprintf(stderr, "copy: Unable to open directory %s: %s\n", dirname, strerror(errno));
        return 1;
    }
    // check for file permissions 
    struct stat stat_buffer;
    int stat_err = stat(dirname, &stat_buffer);
    if ( stat_err == -1 ) {
        fprintf(stderr, "copy: Unable to stat directory %s: %s\n", dirname, strerror(errno));
        int close_err = closedir(current_dir);
        if ( close_err == -1 ) {
            fprintf(stderr, "copy: Unable to close directory %s: %s\n", dirname, strerror(errno));
            exit(1);
        }
        return 1;
    }
    // create new directory with same permissions
    int mkdir_err = mkdir(destname, stat_buffer.st_mode);
    if (mkdir_err < 0)
    {
        fprintf(stderr, "copy: Unable to create directory %s: %s\n", destname, strerror(errno));
        int close_err = closedir(current_dir);
        if ( close_err == -1 ) {
            fprintf(stderr, "copy: Unable to close directory %s: %s\n", dirname, strerror(errno));
            exit(1);
        }
        return 1;
    }
    // display successful copy
    printf("%s -> %s\n", dirname, destname);
    copy_info->num_dir++;
    errno = 0; // set errno to be zero because readdir returns zero both if it errors out or reaches the end of the directory. 
    // readdir fails is errno is set to a nonzero value after the call
    struct dirent *dir_info = readdir(current_dir);
    if ( errno ) {
        fprintf(stderr, "copy: Unable to read directory %s: %s\n", dirname, strerror(errno));
        int close_err = closedir(current_dir);
        if ( close_err == -1 ) {
            fprintf(stderr, "copy: Unable to close directory %s: %s\n", dirname, strerror(errno));
            exit(1);
        }
        return 1;
    }
    while (dir_info) // loop while dir_info is not null and we haven't reached the last file entry
    {
        if (strcmp(dir_info->d_name, ".") && strcmp(dir_info->d_name, "..")) // skip the . and .. files
        {
            // create the current and new paths
            char *current_path = malloc(strlen(dirname) + strlen(dir_info->d_name) + 2);
            if (current_path == NULL)
            {
                fprintf(stderr, "copy: Unable to allocate memory: exiting program\n");
                int close_err = closedir(current_dir);
                if ( close_err == -1 ) {
                    fprintf(stderr, "copy: Unable to close directory %s: %s\n", dirname, strerror(errno));
                    exit(1);
                }
                return 1;
            }
            char *copy_to = malloc(strlen(destname) + strlen(dir_info->d_name) + 2);
            if (copy_to == NULL)
            {
                fprintf(stderr, "copy: Unable to allocate memory: exiting program\n");
                if (current_path != NULL) {free(current_path);}
                int close_err = closedir(current_dir);
                if ( close_err == -1 ) {
                    fprintf(stderr, "copy: Unable to close directory %s: %s\n", dirname, strerror(errno));
                    exit(1);
                }
                return 1;
            }
            sprintf(current_path, "%s/%s", dirname, dir_info->d_name);
            sprintf(copy_to, "%s/%s", destname, dir_info->d_name);
            if (dir_info->d_type == 4) // if the file in the directory is another directory, recursively copy from there
            {
                int dir_copy_ret = recursive_directory_copy(current_path, copy_to, copy_info);
                if (dir_copy_ret)
                {
                    int close_err = closedir(current_dir);
                    if ( close_err == -1 ) {
                        fprintf(stderr, "copy: Unable to close directory %s: %s\n", dirname, strerror(errno));
                        exit(1);
                    }
                    if (current_path != NULL) {free(current_path);}
                    if (copy_to != NULL) {free(copy_to);}
                    return 1;
                }
            }
            else if (dir_info->d_type == 8) // else if its a regular file, preform filecopy on it
            {
                int file_copy_ret = filecopy(current_path, copy_to, copy_info);
                if (file_copy_ret)
                {
                    int close_err = closedir(current_dir);
                    if ( close_err == -1 ) {
                        fprintf(stderr, "copy: Unable to close directory %s: %s\n", dirname, strerror(errno));
                        exit(1);
                    }
                    if (current_path != NULL) {free(current_path);}
                    if (copy_to != NULL) {free(copy_to);}
                    return 1;
                }
            }
            else // other file types should exit
            {
                fprintf(stderr, "copy: Unable to copy file %s: file is not a regular file or directory\n", current_path);
                int close_err = closedir(current_dir);
                if ( close_err == -1 ) {
                    fprintf(stderr, "copy: Unable to close directory %s: %s\n", dirname, strerror(errno));
                    exit(1);
                }
                if (current_path != NULL) {free(current_path);}
                if (copy_to != NULL) {free(copy_to);}
                return 1;
            }
            if (current_path != NULL) {free(current_path);}
            if (copy_to != NULL) {free(copy_to);}
        }
        // try to see if the dir contains more files
        errno = 0;
        dir_info = readdir(current_dir); 
        if ( errno ) {
            fprintf(stderr, "copy: Unable to read from directory %s: %s\n", dirname, strerror(errno));
            int close_err = closedir(current_dir);
            if ( close_err == -1 ) {
                fprintf(stderr, "copy: Unable to close directory %s: %s\n", dirname, strerror(errno));
                exit(1);
            }
            return 1;
        }
    }
    // close the directory
    int close_err = closedir(current_dir);
    if ( close_err == -1 ) {
        fprintf(stderr, "copy: Unable to close directory %s: %s\n", dirname, strerror(errno));
        exit(1);
    }
    return 0;
}
int treecopy(char *source_file, char *dest_file)
{
    copy_info copy_info = {0, 0, 0}; // struct used to store info on how much data was copied

    // read input to see if its a dir or a file or other
    struct stat stat_buffer;
    int stat_err = stat(source_file, &stat_buffer);
    if ( stat_err == -1 ) {
        fprintf(stderr, "copy: Unable to stat file %s: %s\n", source_file, strerror(errno));
        return 1;
    }
    if (!S_ISDIR(stat_buffer.st_mode)) // if its only a file just copy it
    {
        if (filecopy(source_file, dest_file, &copy_info)) {return 0;}
    }
    else //otherwise its a directory
    {
        if (source_file[strlen(source_file) - 1] == '/') // remove trailing / if it exists
        {
            char *dir_source = malloc(strlen(source_file) + 1); // allocate memory for string with trailing / removed
            if (dir_source == NULL)
            {
                fprintf(stderr, "copy: Unable to allocate memory: exiting program\n");
                exit(1);
            }
            strcpy(dir_source, source_file);
            dir_source[strlen(source_file) - 1] = '\0'; // remove /
            int copy_ret = recursive_directory_copy(dir_source, dest_file, &copy_info); // begin recursively copying the directory with removing trailing /
            if (dir_source != NULL) {free(dir_source);}
            if (copy_ret){return 1;} // recursivly bubble up error returns
        }
        else
        {
            int copy_ret = recursive_directory_copy(source_file, dest_file, &copy_info); // begin recursively copying the directory without having to change argv
            if (copy_ret){return 1;}
        } 
    }
    printf("copy: copied %d directories, %d files, and %d bytes from %s to %s\n",
            copy_info.num_dir, copy_info.num_files, copy_info.num_bytes, source_file, dest_file);
    return 0;
}

// *** End Code taken from treecopy.c 

int list_current_dir()
{
    // attempt to open .
    DIR *current_dir = opendir(".");
    if ( current_dir == 0 ) {
        fprintf(stderr, "list: Unable to open directory .: %s\n", strerror(errno));
        return 1;
    }
    errno = 0; // set errno to be zero because readdir returns zero both if it errors out or reaches the end of the directory. 
    // readdir fails is errno is set to a nonzero value after the call
    struct dirent *dir_info = readdir(current_dir);
    if ( errno ) {
        fprintf(stderr, "list: Unable to read directory .: %s\n", strerror(errno));
        // attempt to close directory. if it fails, we quit because something is wrong
        int close_err = closedir(current_dir);
        if ( close_err == -1 ) {
            fprintf(stderr, "list: Unable to close directory .: %s\n", strerror(errno));
            exit(1);
        }
        return 1;
    }
    printf("%s %13s\t\t%16s\n", "Type", "Filename", "Total Bytes"); // print header
    while (dir_info) // loop while dir_info is not null and we haven't reached the last file entry
    {
        if (strcmp(dir_info->d_name, ".") && strcmp(dir_info->d_name, "..")) // skip the . and .. files
        {
            // stat files in directory to get file size
            struct stat stat_buffer;
            int stat_err = stat(dir_info->d_name, &stat_buffer);
            if ( stat_err == -1 ) {
                fprintf(stderr, "list: Unable to stat file %s: %s\n", dir_info->d_name, strerror(errno));
                int close_err = closedir(current_dir);
                if ( close_err == -1 ) {
                    fprintf(stderr, "list: Unable to close directory .: %s\n", strerror(errno));
                    exit(1);
                }
                return 1;
            }
            // display executables in green
            if ( (stat_buffer.st_mode & 0100) && (dir_info->d_type != 4) )
            {
                printf("\033[0;32mF: %15s\033[0m \t\t%10ld bytes\n", dir_info->d_name, stat_buffer.st_size);
            }
            // display folders and files in red and yellow
            else if (dir_info->d_type == 4)
            {
                printf("\033[0;31mD: %15s\033[0m \t\t%10ld bytes\n", dir_info->d_name, stat_buffer.st_size);
            }
            else
            {
                printf("\033[0;33mF: %15s\033[0m \t\t%10ld bytes\n", dir_info->d_name, stat_buffer.st_size);
            }
            
        }
        // try to see if the dir contains more files
        errno = 0;
        dir_info = readdir(current_dir); 
        if ( errno ) {
            fprintf(stderr, "list: Unable to read from directory .: %s\n", strerror(errno));
            int close_err = closedir(current_dir);
            if ( close_err == -1 ) {
                fprintf(stderr, "list: Unable to close directory .: %s\n", strerror(errno));
                exit(1);
            }
            return 1;
        }
    }
    // close the directory
    int close_err = closedir(current_dir);
    if ( close_err == -1 ) {
        fprintf(stderr, "list: Unable to close directory .: %s\n", strerror(errno));
        exit(1);
    }
    return 0;
}

int change_dir(char *destination_path)
{
    // attempt to ask OS to change the directory
    int chdir_err = chdir(destination_path);
    if ( chdir_err < 0 ) {
        fprintf(stderr, "chdir: unable to change current working directory to %s: %s\n", destination_path, strerror(errno));
        exit(1);
    }
    return 0;
}

int print_working_directory()
{
    // ask OS for the name of the current directory
    char *curr_wd = NULL;
    curr_wd = getcwd(curr_wd, 0); // passing in a null string and 0 has getcwd automatically allocate buffer of correct size
    if (curr_wd == NULL) // check if malloc was successful
    {
        fprintf(stderr, "pwd: can not get current working directory: %s\n", strerror(errno));
        return 1;
    }
    printf("%s\n", curr_wd);
    free(curr_wd);
    return 0;
}

int start_process(char *words[128])
{
    pid_t pid = fork();
    if (pid < 0) 
    {
        fprintf(stderr, "myshell: unable to fork: %s\n", strerror(errno));
        exit(1);
    }
    else if (pid == 0) // child process
    {
        execvp(words[1], &words[1]);
        // if we reach this point, exec failed
        fprintf(stderr, "myshell: unable to execute %s: %s\n", words[0], strerror(errno));
        exit(1);
    }
    else // parent process
    {
        printf("myshell: process %d started\n", pid);
        return pid;
    }
}
// wait for any child to finish
int wait_for_process()
{
    int status;
    pid_t pid = wait(&status);
    if (pid < 0) // if we get -1, either there is no children (ECHILD errno) or we got a fatal error
    {
        if (errno == ECHILD)
        {
            printf("myshell: No children.\n");
            return 0;
        }
        else
        {
            fprintf(stderr, "myshell: unable to wait for any child: %s\n", strerror(errno));
            exit(1);
        }
    }
    else if (WIFEXITED(status)) // normal exit - display exit status
    {
        printf("myshell: process %d exited normally with status %d.\n", pid, WEXITSTATUS(status));
        return 0;
    } 
    else if (WIFSIGNALED(status)) // abnormal exit - display signal that caused termination
    {
        printf("myshell: process %d exited abnormally with signal %d: %s\n", pid, WTERMSIG(status), strsignal(WTERMSIG(status)));
        return 0;
    } 
    else
    {
        printf("myshell: process %d exited in unknown state\n", pid);
        return 1;
    }
}
// wait for specified child to finish: basically the same function as above but different system call and different error messages
int wait_for_specific_process(pid_t child_pid)
{
    int status;
    pid_t pid = waitpid(child_pid, &status, 0);
    if (pid < 0)
    {
        if (errno == ECHILD)
        {
            printf("myshell: no child with such PID.\n");
            return 0;
        }
        else
        {
            fprintf(stderr, "myshell: unable to wait for child with PID %d: %s\n", child_pid, strerror(errno));
            exit(1);
        }
    }
    else if (WIFEXITED(status)) 
    {
        printf("myshell: process %d exited normally with status %d.\n", pid, WEXITSTATUS(status));
        return 0;
    } 
    else if (WIFSIGNALED(status)) 
    {
        printf("myshell: process %d exited abnormally with signal %d: %s\n", pid, WTERMSIG(status), strsignal(WTERMSIG(status)));
        return 0;
    } 
    else
    {
        printf("myshell: process %d exited in unknown state\n", pid);
        return 1;
    }
}

int kill_process(pid_t pid)
{
    //send SIGKILL to process
    int kill_ret = kill(pid, 15);
    if (kill_ret < 0)
    {
        fprintf(stderr, "kill: unable to kill process %d: %s\n", pid, strerror(errno));
        return 1;
    }
    printf("kill: successfully able to kill process with PID %d\n", pid);
    return 0;
}

int main()
{
    while (1)
    {
        char input_buff[1024]; // buffer to store user command
        char *words[129]; // array of buffers to store individual arguments
        printf("\033[0;32mmyshell>\033[0;0m "); // print myshell prompt
        fflush(stdout);
        if (fgets(input_buff, 1024, stdin) == NULL) // if we have reached EOF
        {
            break;
        }
        if (!strcmp(input_buff, "\n")){continue;} // special case for when user types nothing and presses enter
        words[0] = strtok(input_buff, " \t\n"); // tokenize command to run
        int nwords = 0;
        while (words[nwords] != NULL) // keep tokenizing while there are args to parse
        {
            nwords++;
            if (nwords == 129) 
            {
                fprintf(stderr, "Error: too many arguments entered. Only accepting 128 arguments\n");
                exit(1);
            }
            words[nwords] = strtok(0, " \t\n");
        }
        // now we check for each command the user could have entered one after the other
        // we also check that the number of arguments they enter makes sense, and otherwise doesn't accept the command
        if (!strcmp(words[0], "list")) 
        {
            if (nwords > 1)
            {
                fprintf(stderr, "Error: list does not accept arguments\n");
                continue;
            }
            list_current_dir();
        }
        else if (!strcmp(words[0], "chdir"))
        {
            if ( (nwords > 2) || (nwords == 1))
            {
                fprintf(stderr, "Error: chdir only accepts one argument\n");
                continue;
            }
            chdir(words[1]);
        }
        else if (!strcmp(words[0], "pwd"))
        {
            if (nwords > 1)
            {
                fprintf(stderr, "Error: pwd does not accept arguments\n");
                continue;
            }
            print_working_directory();
        }
        else if (!strcmp(words[0], "copy"))
        {
            if (nwords != 3)
            {
                fprintf(stderr, "Error: copy only accepts two arguments\n");
                continue;
            }
            if(treecopy(words[1], words[2]))
            {
                fprintf(stderr, "copy unsuccessful\n");
            }
        }
        else if (!strcmp(words[0], "start"))
        {
            if (nwords < 2)
            {
                fprintf(stderr, "Error: start requires at least a program to run\n");
                continue;
            }
            start_process(words);
        }
        else if (!strcmp(words[0], "wait"))
        {
            if (nwords > 1)
            {
                fprintf(stderr, "Error: wait takes no arguments\n");
                continue;
            }
            wait_for_process();
        }
        else if (!strcmp(words[0], "waitfor"))
        {
            if (nwords != 2)
            {
                fprintf(stderr, "Error: waitfor takes exactly one argument\n");
                continue;
            }
            wait_for_specific_process(atoi(words[1]));
        }
        else if (!strcmp(words[0], "run"))
        {
            if (nwords < 2)
            {
                fprintf(stderr, "Error: run requires at least a program to run\n");
                continue;
            }
            wait_for_specific_process(start_process(words));
        }
        else if (!strcmp(words[0], "kill"))
        {
            if (nwords < 2)
            {
                fprintf(stderr, "Error: kill requires the pid of the target process\n");
                continue;
            }
            kill_process(atoi(words[1]));
        }
        else if (!strcmp(words[0], "quit") || !strcmp(words[0], "exit"))
        {
            exit(0);
        }
        else
        {
            printf("Unknown command: %s\n", words[0]);
        }
    }
    exit(0);
}  