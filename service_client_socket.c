#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>

#include <memory.h>
#include <string.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <assert.h>

#include "service_client_socket.h"

#define buffer_size 1024
#define max_path_size 200

char* str(int val){
	static char buf[32] = {0};
	int i = 30;
	for(; val && i ; --i, val /= 10)
		buf[i] = "0123456789abcdef"[val % 10];
	
	return &buf[i+1];
}

enum method { GET, OTHER };

int service_client_socket (const int s, const char *const tag) {

 	char buffer[buffer_size];
 	size_t bytes;

 	char base_path[] = "html";
	char default_path[] = "/index.html";
	char path_404[] = "/404.html";

 	printf ("new connection from %s\n", tag);

 	// repeatedly read a buffer load of bytes
 	while ((bytes = read (s, buffer, buffer_size)) > 0) 
	{

	
		#if (__SIZE_WIDTH__ == 64 || __SIZEOF_POINTER__ == 8)
		printf ("Got %ld bytes from %s:\n\"%s\"\n", bytes, tag, buffer);
		#else
		printf ("Got %d bytes from %s:\n\"%s\"\n", bytes, tag, buffer);
		#endif
		
		enum method method;
		char path[max_path_size];

		// Split the request into lines
		char *line, *saveptrline;
		line = strtok_r(buffer,"\r\n",&saveptrline);
		int line_count=0;
		while (line!=NULL)
		{

			// Split the line into words
			char *word, *saveptrword;
			word = strtok_r(line," ",&saveptrword);
			int word_count = 0;
			while (word!=NULL) {
				
				if(line_count==0) {
				// expect Request-line
					
					switch(word_count) 
					{
						case 0: // expect Method
							if(!strcmp(word, "GET"))
								method = GET;
							else 
								method = OTHER;
							break;
						case 1: // expect Request-URI
							if(!strcmp(word, "/")) {
								strcpy(path, base_path);
								strcat(path, default_path);
							}
							else if(word[0]=='/') { // expect relative path
									strcpy(path, base_path);
									strcat(path, word);
							}
							break;
					}

				}	
				
				word = strtok_r(NULL, " ",&saveptrword);
				word_count++;
				
			}
			
			line = strtok_r(NULL, "\r\n", &saveptrline);
			line_count++;
		}   

		if(method == GET)
		{
			// Prepare message body
			char message_body[buffer_size] = "\0";
			char read_line[buffer_size];
			char response[buffer_size];


			FILE* fp;
			fp = fopen(path, "r");
			if(fp==NULL) {
				perror("can't open file");
				strcpy(response, "HTTP/1.1 404 Not Found\n"
								"Date: Sun, 18 Oct 2012 10:36:20 GMT\n"
								"Server: Apache/2.2.14 (Win32)\n"
								"Connection: Closed\n"
								"Content-Type: text/html; charset=iso-8859-1\n"
								"Content-Length: ");
				strcpy(path, base_path);
				strcat(path, path_404);
				fclose(fp);
				fp = fopen(path, "r");
			}
			else {

				// Prepare status-line and headers
				strcpy(response, 
					"HTTP/1.1 200 OK\n"
					//"Date: Thu, 19 Feb 2009 12:27:04 GMT\n"
					//"Server: Apache/2.2.3\n"
					//"Last-Modified: Wed, 18 Jun 2003 16:05:58 GMT\n"
					//"ETag: \"56d-9989200-1132c580\"\n"
					"Content-Type: text/html\n"
					"Accept-Ranges: bytes\n"
					"Connection: close\n"
					"Content-Length: ");				

			}

			perror(path);
			perror("ok, here");
			while (fgets(read_line, buffer_size, fp)) {
				strcat(message_body, read_line);
			}

			// Append content-length value to the header
			strcat(response, str(strlen(message_body)));
			strcat(response, "\n\n");

			// Append message body to the 
			strcat(response, message_body);

			if (write (s, response, strlen(response)) != bytes) {
				perror ("write");
				return -1;
			}
			
		}

	
	}


	/* bytes == 0: orderly close; bytes < 0: something went wrong */
	if (bytes != 0) {
		perror ("read");
		return -1;
	}
	printf ("connection from %s closed\n", tag);
	close (s);
	return 0;
}

