#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>

#include <memory.h>
#include <string.h>

#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <assert.h>
#include <ctype.h>
#include <sys/fcntl.h>

#include "service_client_socket.h"

#define buffer_size 1024
#define max_path_size 200

char* str(off_t val){
    if(val==0)
        return "0\0";
	static char buf[32] = {0};
	int i = 30;
	for(; val && i ; --i, val /= 10)
		buf[i] = "0123456789abcdef"[val % 10];
	
	return &buf[i+1];
}

short str_is_digit(char* chr) {
    int i;
    for (i = 0; i < strlen(chr); i++)
        if (!isdigit(chr[i]))
            return 0;
    return 1;
}

enum method { GET, HEAD, OTHER };


int parse_request(char *buffer, enum method *method, char* path, int* fp) {

    char base_path[] = "html";
    char default_path[] = "index.html";
    char path_404[] = "/404.html";

    // Split the request into lines
    char *line, *saveptrline;
    line = strtok_r(buffer,"\r\n",&saveptrline);
    int line_count=0;
    char* first_line[3];
    while (line!=NULL)
    {

        // Split the line into words
        char *word="\0", *saveptrword="\0";
        word = strtok_r(line," ",&saveptrword);
        int word_count = 0;
        while (word!=NULL) {
            if(line_count==0) {
                if (word_count >= 3)
                    return 400;
                else first_line[word_count] = word;
            }

            word = strtok_r(NULL, " ",&saveptrword);
            word_count++;

        }

        if(line_count==0 && word_count!=3)
            return 400;

        line = strtok_r(NULL, "\r\n", &saveptrline);
        line_count++;
    }

    // parse Request-line

    // Parse http version
    char *version = first_line[2];
    if (!strncmp(version,"HTTP/", 5)) {
        // Check syntax
        char *dot = strchr(version, '.');
        if (!dot)
            return 400;

        char digit1[5], digit2[5];
        strncpy(digit1, version+ 5, dot - version - 5);
        char *end = digit1 + (dot - version - 5);
        *end = '\0';
        strcpy(digit2, dot + 1);
        if (!str_is_digit(digit1) || !str_is_digit(digit2))
            return 400;

        // Check version
        if (strcmp(version + 5, "1.1")!=0)
            return 505;
    }
    else return 400;

    // parse method
    char* meth = first_line[0];
    if (!strcmp(meth,"GET"))
        *method = GET;
    else if (!strcmp(meth,"HEAD"))
        *method = HEAD;
    else {
        *method = OTHER;
        return 501; // Not Implemented
    }

    // parse Request-URI
    char *uri = first_line[1];
    if (uri[0] != '/') // Request must start with '/'
        return 400;
    if (uri[strlen(uri) - 1] == '/') { // look for index.html in the specified folder
        strcpy(path, base_path);
        strcat(path, uri);
        strcat(path, default_path);
    } else { // look for file
        strcpy(path, base_path);
        strcat(path, uri);
    }
    printf("Request: %s\n", uri);

    // Try to get the resource
    *fp = open(path, O_RDONLY);
    struct stat statusbuffer;
    fstat(*fp, &statusbuffer);
    if(*fp<0 || S_ISDIR(statusbuffer.st_mode)) {
        perror("can't open file");
        errno = 0;

        strcpy(path, base_path);
        strcat(path, path_404);
        *fp = open(path, O_RDONLY);
        if(!*fp)
            return 500; // can't find 404.html, which should not happen
        return 404;
    }




    return 200;

}

int service_client_socket (const int s, const char *const tag) {

    char buffer[buffer_size];
    size_t bytes;

    printf("new connection from %s\n", tag);

    // repeatedly read a buffer load of bytes
    while ((bytes = read(s, buffer, buffer_size)) > 0) {

        #if (__SIZE_WIDTH__ == 64 || __SIZEOF_POINTER__ == 8)
        //printf("Got %ld bytes from %s:\n\"%s\"\n", bytes, tag, buffer);
        //printf ("Got %ld bytes from %s.\n", bytes, tag);
        #else
        //printf ("Got %d bytes from %s.\n", bytes, tag);
        //printf ("Got %d bytes from %s:\n\"%s\"\n", bytes, tag, buffer);
        #endif


        char path[max_path_size] = "\0";
        enum method method = OTHER;
        int fp;

        int code = parse_request(buffer, &method, path, &fp);

        // Prepare response
        char read_line[buffer_size];
        char response[buffer_size];

        switch (code) {

            case 200:
                // Prepare status-line and headers
                strcpy(response, "HTTP/1.1 200 OK\n"
                        //"Content-Type: text/html\n"
                        //"Connection: close\n"
                        );
                break;
            case 400:
                strcpy(response, "HTTP/1.1 400 Bad Request\n");
                break;
            case 404:
                strcpy(response, "HTTP/1.1 404 Not Found\n"
                        //"Connection: close\n"
                        //"Content-Type: text/html\n"
                        );
                break;
            case 500:
                strcpy(response, "HTTP/1.1 500 Internal Server Error\n");
                break;
            case 501:
                strcpy(response, "HTTP/1.1 501 Not Implemented\n");
                break;
            case 505:
                strcpy(response, "HTTP/1.1 505 HTTP Version Not Supported\n");
                break;

            default:
                printf("%d\n", code);
                return -1;

        }

        //if(path[strlen(path)-1]=='o')
            //strcat(response, "Content-Type: image/x-icon\n");
        if(path[strlen(path)-1]=='t')
            strcat(response, "Content-Type: text/plain\n");
        strcat(response, "Content-Length: ");
        off_t length = 0;

        if (method == GET) {


            length = lseek(fp, 0, SEEK_END);
            lseek(fp, 0, SEEK_SET);

            strcat(response, str(length));
            strcat(response, "\n\n");

            //printf("Content-length: %s\n", str(length));

            if (write(s, response, strlen(response)) != strlen(response)) {
                perror("write");
                return -1;
            }

            while((length = read(fp, read_line, buffer_size))>0) {
                if(write(s, read_line, length) != length) {
                    perror("write");
                    return -1;
                }
            }


        }
        

    }

    /* bytes == 0: orderly close; bytes < 0: something went wrong */
    if (bytes != 0) {
        perror("read");
        return -1;
    }
    printf("connection from %s closed\n", tag);
    close(s);
    return 0;

}

