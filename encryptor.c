#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <sys/shm.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <time.h>
#include <string.h>
#include <unistd.h>

void my_decrypt(char* text, int* permutation){
    int wlength = strlen(text);

    srand(time(NULL) ^ getpid());

    for(int i = wlength-1; i >= 0; i--){
        
        char temp = text[permutation[i]-1];
        text[permutation[i]-1] = text[i];
        text[i] = temp;
        printf("Debug, word after perm with %d is now: %s\n", permutation[i], text);
    }
}

int* my_crypt(char *text){
    int wlength = strlen(text);
    
    int* perm = (int*)malloc(wlength * sizeof(int));
    srand(time(NULL) ^ getpid());
    
    for(int i = 0; i < wlength; i++){
        int random = rand() % wlength;
        char temp = text[i];
        text[i] = text[random];
        text[random] = temp;
        perm[i] = random+1;
    }

    return perm;
}

int main(int argc, char* argv[]){
     if(argc == 2){
        char perms_file_name[] = "permutations.txt";
        
        char* text_file_name = argv[1];

        char write_file_name[] = "encrypted.txt";

        int text_fd = open(text_file_name, O_RDONLY | O_CREAT, S_IRUSR | S_IWUSR);
        if(text_fd == -1){
            perror("Error when opening the text file!");
            return 1;
        }

        int perms_fd = open(perms_file_name, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
        if(perms_fd == -1){
            perror("Error when opening the permutations file!");
            close(text_fd);
            return 1;
        }

        int write_fd = open(write_file_name, O_CREAT | O_RDWR | O_TRUNC, S_IRUSR | S_IWUSR);
        if (write_fd == -1) {
            perror("Error when opening the write file!");
            close(text_fd);
            close(perms_fd);
            return 1;
        }

        
        int page_size = getpagesize();
        int number_of_words = 0;
        char c;
        while(read(text_fd, &c, 1) > 0){
            if(c == ' '){
                number_of_words++;
            }
        }
        number_of_words++;
        lseek(text_fd, 0, SEEK_SET);

        char shm_name[] = "shm";
        int shm_fd = shm_open(shm_name, O_CREAT | O_RDWR, S_IRUSR|S_IWUSR|S_IXUSR);
        if(shm_fd < 0){
            perror("Error when creating shared memory!");
            close(text_fd);
            close(perms_fd);
            close(write_fd);
            return 1;
        }

        size_t shm_size = number_of_words*page_size;
        if(ftruncate(shm_fd, shm_size) == -1){
            perror("Error when truncating shared memory!");
            close(text_fd);
            close(perms_fd);
            close(write_fd);
            shm_unlink(shm_name);
            return 1;
        }

        for(int i = 0; i < number_of_words; i++){
            char* shm_ptr = mmap(0, page_size, PROT_WRITE, MAP_SHARED, shm_fd, page_size*i);
            if(shm_ptr == MAP_FAILED){
                perror("Error when mapping shared memory!");
                close(text_fd);
                close(perms_fd);
                close(write_fd);
                shm_unlink(shm_name);
                return 1;
            }

            char c;
            while(read(text_fd, &c, 1) > 0){
                if(c == ' ' || c == '\n'){
                    break;
                }
                *shm_ptr = c;
                shm_ptr++;
            }
            *shm_ptr = '\0';
        }

        for(int i = 0; i < number_of_words; i++){
            char* shm_ptr = mmap(0, page_size, PROT_READ, MAP_SHARED, shm_fd, page_size*i);
            pid_t pid = fork();
            if(pid < 0){
                perror("Error when creating child process!");
                close(text_fd);
                close(perms_fd);
                close(write_fd);
                shm_unlink(shm_name);
                return 1;
            }
            else if(pid == 0){
                char* word = (char*)malloc(strlen(shm_ptr) + 1);
                strcpy(word, shm_ptr);
                int* permutation = my_crypt(word);
                char buffer[100];
                for(int j = 0; j < strlen(shm_ptr); j++){
                    snprintf(buffer + strlen(buffer), sizeof(buffer) - strlen(buffer), "%d ", permutation[j]);
                }
                ssize_t bytes_written = write(perms_fd, buffer, strlen(buffer));
                if(bytes_written < 0){
                    perror("Error when writing to permutations file!");
                    free(permutation);
                    free(word);
                    munmap(shm_ptr, number_of_words * page_size);
                    return 1;
                }
                write(perms_fd, "\n", 1);
                bytes_written = write(write_fd, word, strlen(word));
                if(bytes_written < 0){
                    perror("Error when writing to permutations file!");
                    free(permutation);
                    free(word);
                    munmap(shm_ptr, number_of_words * page_size);
                    return 1;
                }
                write(write_fd, " ", 1);
                free(permutation);
                free(word);
                exit(0);
            }
            else{
                wait(NULL);
                munmap(shm_ptr, page_size);
            }
            
        }

        close(text_fd);
        close(perms_fd);
        close(write_fd);
        shm_unlink(shm_name);
        printf("Encryption done!\n");
    }
    else if(argc == 3){
        char* encrypted_file_name = argv[1];

        FILE* perms_read = fopen(argv[2], "r");
        if(perms_read == NULL){
            perror("Error when opening the permutations file");
            return 1;
        }

        char decrypted_file_name[] = "decrypted.txt";

        int encrypted_fd = open(encrypted_file_name, O_RDONLY | O_CREAT, S_IRUSR | S_IWUSR);
        if(encrypted_fd == -1){
            perror("Error when opening the encrypted file!");
            return 1;
        }

        int write_fd = open(decrypted_file_name, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);
        if (write_fd == -1) {
            perror("Error when opening the write file!");
            close(encrypted_fd);
            fclose(perms_read);
            return 1;
        }

        
        int page_size = getpagesize();
        int number_of_words = 0;
        char c;
        int word_length=0, max_word_length=0;
        while(read(encrypted_fd, &c, 1) > 0){
            if(c == ' '){
                number_of_words++;
                if(word_length > max_word_length){max_word_length = word_length;}
                word_length = 0;
            }else{word_length++;}
        }
        number_of_words++;
        lseek(encrypted_fd, 0, SEEK_SET); //what does this do

        //int perms[number_of_words][max_word_length];
        int** perms = (int**)malloc(number_of_words * sizeof(int*));
        for(int counter = 0; counter < number_of_words; counter++){
            perms[counter] = (int*)malloc(max_word_length*sizeof(int));
        }

        char shm_name[] = "shm";
        int shm_fd = shm_open(shm_name, O_CREAT | O_RDWR, S_IRUSR|S_IWUSR|S_IXUSR);
        if(shm_fd < 0){
            perror("Error when creating shared memory!");
            close(encrypted_fd);
            fclose(perms_read);
            close(write_fd);
            return 1;
        }

        size_t shm_size = number_of_words*page_size;
        if(ftruncate(shm_fd, shm_size) == -1){
            perror("Error when truncating shared memory!");
            close(encrypted_fd);
            fclose(perms_read);
            close(write_fd);
            shm_unlink(shm_name);
            return 1;
        }

        for(int i = 0; i < number_of_words; i++){
            char* shm_ptr = mmap(0, page_size, PROT_WRITE, MAP_SHARED, shm_fd, page_size*i);
            if(shm_ptr == MAP_FAILED){
                perror("Error when mapping shared memory!");
                close(encrypted_fd);
                fclose(perms_read);
                close(write_fd);
                shm_unlink(shm_name);
                return 1;
            }

            int word_length = 0;
            char c;
            while(read(encrypted_fd, &c, 1) > 0){
                if(c == ' ' || c == '\n'){
                    break;
                }
                *shm_ptr = c;
                shm_ptr++;
                word_length++;
            }
            *shm_ptr = '\0';

            for(int j = 0; j < word_length; j++){
                fscanf(perms_read, "%d", &perms[i][j]);
            }
        }

        for(int i = 0; i < number_of_words; i++){
            char* shm_ptr = mmap(0, page_size, PROT_READ, MAP_SHARED, shm_fd, page_size*i);
            pid_t pid = fork();
            if(pid < 0){
                perror("Error when creating child process!");
                close(encrypted_fd);
                fclose(perms_read);
                close(write_fd);
                shm_unlink(shm_name);
                return 1;
            }
            else if(pid == 0){
                char* word = (char*)malloc(strlen(shm_ptr) + 1);
                strcpy(word, shm_ptr);
                
                my_decrypt(word, perms[i]);

                ssize_t bytes_written;
                printf("debug, word: %s\n", word);
                bytes_written = write(write_fd, word, strlen(word));
                if(bytes_written < 0){
                    perror("Error when writing to decrypted file right here!");
                    free(word);
                    exit(1);
                }
                printf("debug reached here");
                write(write_fd, " ", 1);
                free(word);
                
                exit(0);
            }
            else{
                int status;
                pid_t wpid = wait(&status);
                if (wpid == -1) {
                    perror("Error waiting for child process");
                    return 1;
                }
                if (!WIFEXITED(status)) {
                    perror("exited with error");
                    return 1;
                }
                munmap(shm_ptr, page_size);
            }
        }
        close(encrypted_fd);
        fclose(perms_read);
        close(write_fd);
        shm_unlink(shm_name);
        printf("Decryption done!\n");
    
    }
    else{
        printf("Usage: %s [file_name] for encryption, %s [file_name] [permutations_file_name]", argv[0], argv[0]);  
    }
    return 0;
}
    

