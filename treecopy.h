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

// struct used to store how many directory and files have been copied as well as the number of bytes copied
typedef struct copy_info
{
    int num_dir;
    int num_files;
    int num_bytes;
} copy_info;

// arguments are the source file path and the destination file path, and copies a single file,
// also updates copy_info
void filecopy(const char *source, const char *dest, copy_info *copy_info)
{
    // open file to copy
    int input_fd = open(source, O_RDONLY, 0);
    if ( input_fd < 0 ) {
        fprintf(stderr, "copy: Unable to open file %s: %s\n", source, strerror(errno));
	    exit(1);
    }

    // open file destination and copy permissions
    struct stat stat_buffer;
    int stat_err = stat(source, &stat_buffer);
    if ( stat_err == -1 ) {
        fprintf(stderr, "copy: Unable to stat file %s: %s\n", source, strerror(errno));
        exit(1);
    }
    // create destination file
    int dest_fd = open(dest, O_CREAT|O_WRONLY, stat_buffer.st_mode);
    if ( dest_fd < 0 ) {
        fprintf(stderr, "copy: Unable to create file %s: %s\n", dest, strerror(errno));
	    exit(1);
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
            fprintf(stderr, "copy: Unable to read from file %s: %s\n", source, strerror(errno));
            exit(1);
        }
        if (!read_ret) break; // we have reached the end of the file
        
        // try to write to destination file
        write_ret = write(dest_fd, buffer, read_ret);
        if ( write_ret < 0 ) {
            if (errno == EINTR) {} // if we get interrupted while trying to write, we try to finish the write in the code block below
            else
            {
                fprintf(stderr, "copy: Unable to write to file %s: %s\n", dest, strerror(errno));
                exit(1);
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
                else // if we encounter a fatal error just quit out
                {
                    fprintf(stderr, "copy: Unable to write to file %s: %s\n", source, strerror(errno));
                    exit(1);
                }
            }
            current_bytes_written += write_ret;
        }
        total_bytes_written += current_bytes_written;
    }

    // output when a successful copy occurs
    printf("%s -> %s\n", source, dest);

    // close source file
    int close_err = close(input_fd);
    if ( close_err < 0 ) {
        fprintf(stderr, "copy: Unable to close file %s: %s\n", source, strerror(errno));
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
}

// takes in a directory path and then recursively calls itself for every directory in that directory
// also copies all the files in the directory
void recursive_directory_copy(const char *dirname, const char *destname, copy_info *copy_info)
{
    // attempt to open directory
    DIR *current_dir = opendir(dirname);
    if ( current_dir == 0 ) {
        fprintf(stderr, "copy: Unable to open directory %s: %s\n", dirname, strerror(errno));
        exit(1);
    }
    // check for file permissions 
    struct stat stat_buffer;
    int stat_err = stat(dirname, &stat_buffer);
    if ( stat_err == -1 ) {
        fprintf(stderr, "copy: Unable to stat directory %s: %s\n", dirname, strerror(errno));
        exit(1);
    }
    // create new directory with same permissions
    int mkdir_err = mkdir(destname, stat_buffer.st_mode);
    if (mkdir_err < 0)
    {
        fprintf(stderr, "copy: Unable to create directory %s: %s\n", destname, strerror(errno));
        exit(1);
    }
    // display successful copy
    printf("%s -> %s\n", dirname, destname);
    copy_info->num_dir++;
    errno = 0; // set errno to be zero because readdir returns zero both if it errors out or reaches the end of the directory. 
    // readdir fails is errno is set to a nonzero value after the call
    struct dirent *dir_info = readdir(current_dir);
    if ( errno ) {
        fprintf(stderr, "copy: Unable to read directory %s: %s\n", dirname, strerror(errno));
        exit(1);
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
                exit(1);
            }
            char *copy_to = malloc(strlen(destname) + strlen(dir_info->d_name) + 2);
            if (copy_to == NULL)
            {
                fprintf(stderr, "copy: Unable to allocate memory: exiting program\n");
                exit(1);
            }
            sprintf(current_path, "%s/%s", dirname, dir_info->d_name);
            sprintf(copy_to, "%s/%s", destname, dir_info->d_name);
            if (dir_info->d_type == 4) // if the file in the directory is another directory, recursively copy from there
            {
                recursive_directory_copy(current_path, copy_to, copy_info);
            }
            else if (dir_info->d_type == 8) // else if its a regular file, preform filecopy on it
            {
                filecopy(current_path, copy_to, copy_info);
            }
            else // other file types should exit
            {
                fprintf(stderr, "copy: Unable to copy file %s: file is not a regular file or directory\n", current_path);
                exit(1);
            }
            free(current_path);
            free(copy_to);
        }
        // try to see if the dir contains more files
        errno = 0;
        dir_info = readdir(current_dir); 
        if ( errno ) {
            fprintf(stderr, "copy: Unable to read from directory %s: %s\n", dirname, strerror(errno));
            exit(1);
        }
    }
    // close the directory
    int close_err = closedir(current_dir);
    if ( close_err == -1 ) {
        fprintf(stderr, "copy: Unable to close directory %s: %s\n", dirname, strerror(errno));
        exit(1);
    }
        
}
void treecopy(char *source_file, char *dest_file)
{
    copy_info copy_info = {0, 0, 0}; // struct used to store info on how much data was copied

    // read input to see if its a dir or a file or other
    struct stat stat_buffer;
    int stat_err = stat(source_file, &stat_buffer);
    if ( stat_err == -1 ) {
        fprintf(stderr, "copy: Unable to stat file %s: %s\n", source_file, strerror(errno));
        exit(1);
    }
    if (!S_ISDIR(stat_buffer.st_mode)) // if its only a file just copy it
    {
        filecopy(source_file, dest_file, &copy_info);
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
            recursive_directory_copy(dir_source, dest_file, &copy_info); // begin recursively copying the directory with removing trailing /
            if (dir_source != NULL) {free(dir_source);}
        }
        else
        {
            recursive_directory_copy(source_file, dest_file, &copy_info); // begin recursively copying the directory without having to change argv
        } 
    }
    printf("copy: copied %d directories, %d files, and %d bytes from %s to %s\n",
            copy_info.num_dir, copy_info.num_files, copy_info.num_bytes, source_file, dest_file);
}