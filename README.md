# Web Server

This is a single and multi-thread web-server capable of accepting GET and HEAD HTTP 1.1 requests coded in C for the networking course. It supports both IPv4 and IPv6. It is base on the provided basic server code, which I adapted to receive and respond to HTTP requests.

### More information

The files it retrieves are taken from the `html` folder, so for example requesting `GET / HTTP/1.1` from a server run from `../src/` will retrieve `../src/html/index.html`. This is defined at the top of `service_client_socket.c` as `base_path`, so it can be easily changed.

The server correctly fetches html and pdf files, images, text documents, etc. The `html` folder contains some example pages, a favicon, a pdf and a txt file to showcase that it works correctly.

If the request URI ends in `[folder]/`, the server will try to get the `index.html` in that folder. If it ends without a `/` symbol, the server will try to get a file. This works for any relative path inside the `html` folder.

The server supports byte range fetching of resources. If the request has the `Range` header containing a byte range, the server will try to fetch that range, giving a status code 206 if that is achievable or 416 if the range is not valid. The server advertises this support by sending the `Accept-Ranges: bytes` header in reponses that have status code 200 or 216.

The server responds with appropriate status codes if the request is not valid, too long, has an unsupported http version, has a method that is not supported or has a path that is too long or the resource in the path cannot be found (in which case it sends a custom 404.html).

The limit for requests is 8KB (8192 characters) and the limit for paths is 2KB (2048 characters). In the latter, the length includes the `/html` prefix and also `index.html` in the case that the resource from the path is a folder. There is no explicit size limit for the resources, but I did not check for files bigger than 900MB.

### Code

As the one provided in the course, the code contains:
* a function which reads from a socket, displays the data and then echoes it back where it came from (service_client_socket.c)
* a function which acquires a socket to listen on (get_listen_socket.c)
* a function which waits for incoming connections on that socket and processes them either one at a time (service_listen_socket.c) or for multiple connections in parallel (service_listen_socket_multithread.c)
* some helper functions, header files, a main function and other odds and ends

### Running instructions

To compile the code, run: ``$ make``

Once compiled, it can be run either as single-threaded or multi-threaded server using the following commands, specifying a port-number to listen on.

```bash
$ ./single_thread_server 1234
binding to port 1234
```

```bash
$ ./multi_thread_server 4444
binding to port 4444
```

You can now connect to the server either through the terminal or using an internet browser. For example:
```bash
$ wget localhost:4444
```

\* If you are using Solaris, you need to use gmake, as it uses some of the GNU extensions, notably $+.

### Running with your own html folder

If you want to run this with some other files, you need to make sure there is a `404.html` file so that the server has a text to send along with the 404 status code. At the very least, make an empty file.
