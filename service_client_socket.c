#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#include <memory.h>

#include <sys/stat.h>

#include <ctype.h>
#include <sys/fcntl.h>

#include "service_client_socket.h"

#define buffer_size 1024
#define max_path_size 2049
#define request_buffer_size 8193

#define base_path "html"
#define default_path "index.html"
#define path_404 "/404.html"

// Transforms an int into a string
char* str(off_t val){
    if(val==0)
        return "0\0";
	static char buf[32] = {0};
	int i = 30;
	for(; val && i ; --i, val /= 10)
		buf[i] = "0123456789abcdef"[val % 10];
	
	return &buf[i+1];
}

// Checks if a string is a number
short str_is_digit(char* chr) {
    int i;
    for (i = 0; i < strlen(chr); i++)
        if (!isdigit(chr[i]))
            return 0;
    return 1;
}

enum method { GET, HEAD, OTHER };

// Request information
struct request {
    enum method method;
    off_t file_length;
    char path[max_path_size];
    int fp;

    // for byte ranges
    short range;
    long int1, int2;
};

// Parses the request. Returns the status code and puts information into the request struct
int parse_request(char *buffer, struct request *req) {

    // Initialise the request information
    req->range = 0;
    req->file_length = 0;
    req->method = OTHER;

    char* first_line[3];

    short has_host = 0;

    // Split the request into lines
    char *line, *saveptrline;
    int line_count=0;
    line = strtok_r(buffer,"\r\n",&saveptrline);
    while (line!=NULL) {
        // Split the line into words and save the request-line words for later
        char *word, *saveptrword="\0";
        word = strtok_r(line," ",&saveptrword);
        int word_count = 0;
        while (word!=NULL) {
            // Request-line should have no more than 3 words
            if(line_count==0) {
                if (word_count >= 3)
                    return 400;
                else first_line[word_count] = word;
            }
                // check headers
            else if(word_count==0) {
                // Check for the range header
                if (strcmp(word, "Range:") == 0) {
                    // Get the header value
                    word = strtok_r(NULL, " ", &saveptrword);
                    word_count++;
                    if (word == NULL)
                        return 400;
                    // Check if it is valid. If it is, put the range in the request struct
                    if (strncmp(word, "bytes=", 6) == 0) {
                        char *dash = strchr(word, '-');
                        if (!dash)
                            return 400;
                        if (dash - word - 6 < 20 && strlen(dash + 1) < 20) {
                            char digit1[20], digit2[20];
                            strncpy(digit1, word + 6, dash - word - 6);
                            char *end = digit1 + (dash - word - 6);
                            *end = '\0';
                            strcpy(digit2, dash + 1);
                            if (!str_is_digit(digit1) || !str_is_digit(digit2))
                                return 400;
                            req->int1 = strtol(digit1, NULL, 10);
                            req->int2 = strtol(digit2, NULL, 10);
                            req->range = 1;
                        }
                    }
                } else if (strcmp(word, "Host:") == 0)
                    has_host = 1;
            }

            word = strtok_r(NULL, " ",&saveptrword);
            word_count++;
        }

        // Request-line must have 3 words
        if(line_count==0 && word_count!=3)
            return 400;

        line = strtok_r(NULL, "\r\n", &saveptrline);
        line_count++;
    }

    // Request must have host header
    if(has_host==0)
        return 400;

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
        strncpy(digit2, dot + 1, 4);
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
        req->method = GET;
    else if (!strcmp(meth,"HEAD"))
        req->method = HEAD;
    else {
        req->method = OTHER;
        return 501; // Not Implemented
    }

    // parse Request-URI
    char *uri = first_line[1];
    if (strlen(uri)>=max_path_size-strlen(base_path)-strlen(default_path))
        return 414;
    if (uri[0] != '/') // Request must start with '/'
        return 400;
    if (uri[strlen(uri) - 1] == '/') { // look for index.html in the specified folder
        strcpy(req->path, base_path);
        strcat(req->path, uri);
        strcat(req->path, default_path);
    } else { // look for file
        strcpy(req->path, base_path);
        strcat(req->path, uri);
    }
    printf("Request: %s\n", uri);

    // Try to get the resource
    req->fp = open(req->path, O_RDONLY);
    struct stat statusbuffer;
    fstat(req->fp, &statusbuffer);
    // if the resource can't be found or is not a file, get the 404.html instead
    if(req->fp<0 || S_ISDIR(statusbuffer.st_mode)) {
        perror("can't open file");
        errno = 0;

        strcpy(req->path, base_path);
        strcat(req->path, path_404);
        req->fp = open(req->path, O_RDONLY);
        if(!req->fp)
            return 500; // can't find 404.html, which should not happen
        req->file_length = lseek(req->fp, 0, SEEK_END);
        lseek(req->fp, 0, SEEK_SET);
        return 404;
    }

    // Put the length of the file in the request struct
    req->file_length = lseek(req->fp, 0, SEEK_END);
    lseek(req->fp, 0, SEEK_SET);

    // If getting a byte range, check if the range is valid
    if(req->range==1)
        if(req->int1<0 || req->int2<0 || req->int1>req->int2 || req->int2>req->file_length)
            return 416;
        else return 206;
    else return 200;
}

int service_client_socket (const int s, const char *const tag) {

    char buffer[request_buffer_size];
    size_t bytes;

    printf("New connection from %s\n", tag);

    // Read the request
    if ((bytes = read(s, buffer, request_buffer_size-1)) > 0) {

        #if (__SIZE_WIDTH__ == 64 || __SIZEOF_POINTER__ == 8)
        //printf("Got %ld bytes from %s:\n\"%s\"\n", bytes, tag, buffer);
        //printf ("Got %ld bytes from %s.\n", bytes, tag);
        #else
        //printf ("Got %d bytes from %s.\n", bytes, tag);
        //printf ("Got %d bytes from %s:\n\"%s\"\n", bytes, tag, buffer);
        #endif

        struct request req;
        int code;

        // If all the bytes were read, it means the request is too large
        // (there might be more to be read)
        buffer[bytes] = '\0';
        if(bytes == request_buffer_size-1)
            code = 413;
        else code = parse_request(buffer, &req);

        // Prepare response
        char read_line[buffer_size];
        char response[buffer_size];

        // Prepare status-line and headers
        long content_length = 0;
        switch (code) {
            case 200:
                strcpy(response, "HTTP/1.1 200 OK\n"
                        "Accept-Ranges: bytes\n");
                content_length = req.file_length;
                break;
            case 206:
                strcpy(response, "HTTP/1.1 206 Partial Content\n"
                        "Accept-Ranges: bytes\n"
                        "Content-Range: bytes ");
                strcat(response, str(req.int1));
                strcat(response, "-");
                strcat(response, str(req.int2));
                strcat(response, "/");
                strcat(response, str(req.file_length));
                strcat(response, "\n");

                // Get content-length
                content_length = req.int2-req.int1+1;

                break;
            case 400:
                strcpy(response, "HTTP/1.1 400 Bad Request\n");
                break;
            case 404:
                strcpy(response, "HTTP/1.1 404 Not Found\n");
                content_length = req.file_length;
                break;
            case 413:
                strcpy(response, "HTTP/1.1 413 Request Entity Too Large\n");
                break;
            case 414:
                strcpy(response, "HTTP/1.1 414 Request-URI Too Long\n");
                break;
            case 416:
                strcpy(response, "HTTP/1.1 416 Range Not Satisfiable\n");
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
                printf("Server error.\n");
                return -1;
        }

        // Add content-length
        strcat(response, "Content-Length: ");
        if(req.method==GET)
            strcat(response, str(content_length));
        else strcat(response, "0");
        strcat(response, "\n\n");

        // Send status line and headers
        if (write(s, response, strlen(response)) != strlen(response)) {
            perror("write");
            return -1;
        }

        // Send the file if applicable
        if(req.method==GET) {
            if (code == 206) {
                // Send the byte range
                lseek(req.fp, req.int1, SEEK_SET);
                while (content_length > 0 && (bytes = read(req.fp, read_line, buffer_size)) > 0) {
                    if (bytes > content_length)
                        bytes = content_length;
                    if (write(s, read_line, (size_t) bytes) != bytes) {
                        perror("write");
                        return -1;
                    }
                    content_length -= bytes;
                }
            } else if (code == 200 || code == 404) {
                // Send the whole file
                while ((bytes = read(req.fp, read_line, buffer_size)) > 0) {
                    if (write(s, read_line, (size_t) bytes) != bytes) {
                        perror("write");
                        return -1;
                    }
                }
            }
        }
    }

    printf("connection from %s closed\n", tag);
    close(s);
    return 0;
}