/*
    Written by  Emil Karlström
                Blekinge Tekniska Högskola
                DVAMI19
    Code for Assignment 2 of course DV1580
    No steal code P L O X 
*/
#include <stdlib.h>
#include <stdbool.h> 
#include <stdio.h>

#include <ftw.h>
#include <openssl/md5.h>
#include <pthread.h>
#include <omp.h>

#define MAX_THREADS 2
#define MD5_READ_BYTES_SIZE 1024 * 1024
#define FTW_MAX_FILE_HANDLERS 20

#define HASH_TIME_OUTPUT

const char* IGNORED_EXTENSIONS[] = { ".cfg" };
const size_t IGNORED_EXTENSIONS_COUNT = sizeof(IGNORED_EXTENSIONS) / sizeof(char*);

pthread_t threads[MAX_THREADS];
volatile bool thread_working[MAX_THREADS];

int file_count;
char** files;

typedef struct {
    int thread_id;
    int file_id;
    unsigned char* md5_buffer;
} thread_data;

// Returns the index of a non-working thread from the threadpool
// If all threads are working then it returns -1
int get_non_working_thread()
{
    int index = -1;
    for(int i = 0; i < MAX_THREADS; i++)
    {
        if(!thread_working[i])
        {
            index = i;
            break;
        }
    }
    return index;
}

// Computes the MD5 hash of a file
void md5_of_file(const char* fpath, unsigned char c[])
{
    FILE* inFile = fopen(fpath, "rb");
    unsigned char data[MD5_READ_BYTES_SIZE];
    MD5_CTX mdContext;
    MD5_Init(&mdContext);

    int bytes;
    while ((bytes = fread(data, 1, MD5_READ_BYTES_SIZE, inFile)) != 0) 
        MD5_Update(&mdContext, data, bytes);
    MD5_Final(c, &mdContext);
    fclose(inFile);
}

// Computes the final MD5 hash of all hashes from the files.
void final_md5_combine(unsigned char** md5_hashes, unsigned char c[])
{
    MD5_CTX mdContext;
    MD5_Init(&mdContext);
    for(int i = 0; i < file_count; i++)
        MD5_Update(&mdContext, md5_hashes[i], MD5_DIGEST_LENGTH);
    MD5_Final(c, &mdContext);
}

// This is where the thread starts its execution
void* thread_entry(void* value) 
{
    thread_data *data = (thread_data*)value;
    int file_id = (data->file_id);
    int thread_id = (data->thread_id);

    char* file = files[file_id];
    unsigned char c[MD5_DIGEST_LENGTH]; 
    md5_of_file(file, c);
    
    memcpy(data->md5_buffer, c, MD5_DIGEST_LENGTH);
    thread_working[thread_id] = false;
    return 0;
}

// Maps the tree-file structure and saves it in `files`
int map_tree(const char* fpath, const struct stat *sb, int typeflag) 
{
    if (S_ISREG(sb->st_mode))
    {
        char* ext = strrchr(fpath, '.');
        if(ext != NULL)
        {
            for(int i = 0; i < IGNORED_EXTENSIONS_COUNT; i++)
                if(strcmp(IGNORED_EXTENSIONS[i], ext) == 0){
                    return 0; // Return if we want to ignore the extension
                }
        }

        files = realloc(files, (file_count + 1) * sizeof(char*));
        files[file_count] = (char *) malloc(strlen(fpath)+1);
        strcpy(files[file_count], fpath);
        file_count += 1;
    }
    return 0;
}

int main(int argc, char **argv) 
{
    if(argc < 3){
        printf("Invalid arguments. Example usage: %s file-tree <desired_md5>", argv[0]);
        return EXIT_FAILURE;
    }

    const char *dirpath = argv[1];
    const char *desired_md5 = argv[2];

    file_count = 0;
    double t0 = omp_get_wtime();

    // Find all interesting files
    ftw(dirpath, map_tree, FTW_MAX_FILE_HANDLERS);

    // Create some containers for data
    unsigned char** md5_hashes = malloc(file_count * sizeof(char*));     // MD5 hashes container
    thread_data **data_list = malloc(file_count * sizeof(thread_data*)); // Data for each thread and file container

    // Allocate data for all items in the containers
    for(int i = 0; i < file_count; i++)
    {
        md5_hashes[i] = (unsigned char*)malloc(MD5_DIGEST_LENGTH);
        data_list[i] = (thread_data*)malloc(sizeof(thread_data));
        data_list[i]->file_id=i;
        data_list[i]->md5_buffer=md5_hashes[i];
    }

    // Compute hashes in parallel using the threadpool
    bool found_thread = false;
    int thread_id;
    for(int i = 0; i < file_count; i++)
    {
        found_thread = false;
        while(!found_thread)
        {
            thread_id = get_non_working_thread();
            
            if(thread_id >= 0)
            {
                found_thread = true;
                data_list[i]->thread_id = thread_id;
                thread_working[thread_id] = true;
                pthread_create(&threads[thread_id], NULL, thread_entry, (void*)data_list[i]);
            }
        }
    }
    // Join all threads to make sure computing all hashes are done
    for(int i = 0; i < MAX_THREADS; i++)
        pthread_join(threads[i], NULL);

    // Compute final hash
    unsigned char c[MD5_DIGEST_LENGTH];
    final_md5_combine(md5_hashes, c);
    double seconds = omp_get_wtime() - t0;

    // Turn the final hash into a readable 33-char string
    char hash_md5[33];
    for(int i = 0; i < 16; i++)
        sprintf(&hash_md5[i*2], "%02x", c[i]);

    // for(int i = 0; i < file_count; i++)
    // {
    //     printf("%2d ", i);
    //     for(int j = 0; j < 16; j++)
    //         printf("%02x", md5_hashes[i][j]);
    //     printf("\n");
    // }

    // Print the true hash and the computed hash
    printf("True hash: %s\n", desired_md5);
    printf("Game hash: %s\n", hash_md5);
    if(strcmp(desired_md5, hash_md5) == 0)
        printf("Game files OK\n");
    else
        printf("Game files differ!\n");

#ifdef HASH_TIME_OUTPUT
    printf("Hash time: %f\n", seconds);
    printf("File counts: %i\n", file_count);
#endif

    // Free all containers
    for(int i = 0; i < file_count; i++){
        free(md5_hashes[i]);
        free(data_list[i]);
        free(files[i]);
    }
    free(md5_hashes);
    free(data_list);
    free(files);
    return EXIT_SUCCESS;
}