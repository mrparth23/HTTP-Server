#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/time.h>
#include <string>
#include <iostream>
#include <sstream>
#include <queue>
#include <time.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORTNO 8080
#define TOTAL_THREADS 20
#define BACKLOG 100

// queue for first come first serve request handling for clients
std::queue<int> clients;

pthread_t threads[TOTAL_THREADS];
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t condition_var = PTHREAD_COND_INITIALIZER;

void get_date(char *buffer, char *format);
void initThreads();
void *handle_thread(void *t_client_sockfd);
void handle_client(int client_sockfd);
std::string getHeader(int sockfd);
void parseHeader(int client_sockfd, std::string header);
int check_resource(std::string path);
void serve_request(int client_sockfd, int status, std::string stat_msg, std::string err_msg, std::string version, std::string path, int resource, std::string method);
void write_header(int client_sockfd, std::string header);
void send_resource(int client_sockfd, int resource);

// main driver code
int main(int argc, char *argv[])
{

    int server_sockfd, client_sockfd;            // Socket file descriptors
    struct sockaddr_in server_addr, client_addr; // Socket addressess
    socklen_t client_size;                       // length of the client address

    char date[128];

    // Creates a new socket, arg[1]: type of address domain, arg[2]: type of socket(here continuous stream, other datagram read as chunk), arg[3]: type of protocol
    server_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sockfd < 0)
    {
        printf("[-]Error in connection.\n");
        exit(1);
    }
    printf("[+]Server Socket is created.\n");

    // set sock option to reuse the same address for the socket
    setsockopt(server_sockfd, SOL_SOCKET, SO_REUSEADDR, (void *)1, sizeof((void *)1));

    // Initialize addr structs to zero
    memset(&server_addr, 0, sizeof(server_addr));
    memset(&client_addr, 0, sizeof(client_addr));

    server_addr.sin_family = AF_INET;                // Define internet address family (here IPv4)
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY); // Symbolic constant INADDR_ANY gets the ip address on which server is running
    server_addr.sin_port = htons(PORTNO);            // convert port to network byte order using the function htons()

    // Bind the socket and check the binding status
    // arg[1] : serverc socket file descriptor
    // arg[2] : server address from sockaddr structure
    // arg[3] : size of the server address
    if (bind(server_sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        printf("[-]Error in binding.\n");
        exit(1);
    }
    printf("[+]Bind to port %d\n", PORTNO);

    // listen for client and check the listen status
    // arg[1] : server socket file descriptor
    // arg[2] : total no of clients to listen
    if (listen(server_sockfd, BACKLOG) == 0)
        printf("[+]Listening....\n");
    else
        printf("[-]Error in listening.\n");

    // Size of client address
    client_size = sizeof(client_addr);

    // Initialize empty threads with thread handler
    initThreads();

    while (true)
    {
        // Initialize client address to 0
        memset(&client_addr, 0, sizeof(client_addr));

        // accept the connection request from client on server side
        // args[1] : server socket file descriptor
        // args[2] : client address from scokaddr structure
        // args[3] : size of the client address
        client_sockfd = accept(server_sockfd, (struct sockaddr *)&client_addr, &client_size);
        if (client_sockfd < 0)
            exit(1);

        // prints the date & time of the connection between server & client
        get_date(date, (char *)"%H:%M:%S %a %b %d %Y");
        printf("[%s] %s:%d \n", date, inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        // pointer to client socket file descriptor
        int client = client_sockfd;

        // to lock the shared data structure among the threads until the operations on it are executed
        pthread_mutex_lock(&mutex);
        clients.push(client);

        // signal all the threads that client socket file descriptor is available to work on
        pthread_cond_signal(&condition_var);

        // to unlock the shared data structure among the threads as the operations are executed
        pthread_mutex_unlock(&mutex);
    }
}

void get_date(char *buffer, char *format)
{
    // structure with seconds and microseconds
    struct timeval tv;
    time_t curtime;

    // Get the current time of the day
    gettimeofday(&tv, NULL);

    // set currentime equals to the total no of seconds of the day
    curtime = tv.tv_sec;

    // Get the string output of the date with long format
    strftime(buffer, 128, format, localtime(&curtime));
}

// Initialize empty threads with thread handler
void initThreads()
{
    for (int i = 0; i < TOTAL_THREADS; i++)
        pthread_create(&threads[i], NULL, handle_thread, NULL);
}

// Assign the client socket file descriptor to the thread
void *handle_thread(void *arg)
{
    while (true)
    {
        int t_client, sockfd = 0;

        // to lock the shared data structure among the threads until the operations on it are executed
        pthread_mutex_lock(&mutex);
        if (clients.empty() == true)
        {
            // Wait until the signal is initiated for the availability of the new client sccoket file descriptor
            pthread_cond_wait(&condition_var, &mutex);
            t_client = clients.front();
            clients.pop();
            sockfd = t_client;
        }

        // to unlock the shared data structure among the threads as the operations are executed
        pthread_mutex_unlock(&mutex);

        if (sockfd)
            handle_client(sockfd);
    }
}

// function to handle the request from the client
void handle_client(int client_sockfd)
{
    // fetch the header from the client socket
    std::string header = getHeader(client_sockfd);

    // parse the fetched header and based on the header send the response
    parseHeader(client_sockfd, header);
    return;
}

// fetch the header from the client socket
std::string getHeader(int sockfd)
{
    int n, SIZE = 4096;
    char buffer[SIZE];
    std::string header = "";

    // read from client socket until the end of header.
    while (1)
    {
        bzero(buffer, SIZE);
        n = recv(sockfd, buffer, SIZE, 0);
        if (n <= 0)
        {
            break;
        }
        header.append(buffer);

        // if recvd data is less than the buffer it means end of request, so return the header
        if (n < 4096)
            break;
    }
    return header;
}

// parse the fetched header and based on the header send the response
void parseHeader(int client_sockfd, std::string headers)
{
    // convert the string to a string stream for easy tokenization of the header recieved
    std::stringstream ss;
    ss.str(headers);
    std::string token;
    std::getline(ss, token);
    std::stringstream header(token);

    std::string method, stat_msg, err_msg, version, path="./files";
    int status, resource;

    // get first token, method and check if it is GET Request method as it is the only version supported else give status 400 bad request
    if (header >> token && (token == "GET"))
    {
        method = token;
    }
    else
    {
        status = 400;
        stat_msg = "Bad Request";
        err_msg = "The request line contained invalid characters following the request method string";
        serve_request(client_sockfd, status, stat_msg, err_msg, version, path, resource, method);
        return;
    }

    // get the second token as the path of the file requested or else give 400 error for bad request
    if (header >> token)
    {
        // give path as index.html if no path is given else give the original path
        if (token == "/")
        {
            path += "/index.html";
        }
        else
        {
            path += token;
        }

        // check if the requested file is available or not, if not give 404 error for not found.
        resource = check_resource(path);
        if (resource<0)
        {
            status = 404;
            stat_msg = "Not Found";
            err_msg = "The requested URL wasn't found";
            serve_request(client_sockfd, status, stat_msg, err_msg, version, path, resource, method);
            return;
        }
    }
    else
    {
        status = 400;
        stat_msg = "Bad Request";
        err_msg = "The request line contained invalid characters following the protocol string";
        serve_request(client_sockfd, status, stat_msg, err_msg, version, path, resource, method);
        return;
    }

    // Get HTTP version idf available else give error 400 as bad request back to client
    if (header >> token)
    {
        if (token == "HTTP/1.1")
        {
            version = "1.1";
        }
        else if (token == "HTTP/1.0")
        {
            version = "1.0";
        }
    }
    else
    {
        status = 400;
        stat_msg = "Bad Request";
        err_msg = "The request line contained invalid characters";
        serve_request(client_sockfd, status, stat_msg, err_msg, version, path, resource, method);
        return;
    }

    // if status do not contain any type of error code then assign 200 as status for OK
    if (status != 400 || status != 404 || status != 501 || status != 304)
    {
        status = 200;
        stat_msg = "OK";
    }

    // serve the request based on the data collected from the header, and display the proper messages
    serve_request(client_sockfd, status, stat_msg, err_msg, version, path, resource, method);
}

// check if the requested file is available or not
int check_resource(std::string path)
{
    int resource;
    resource = open(path.c_str(), O_RDONLY);
    return resource;
}

// serve the request based on the data collected from the header, and display the proper messages
void serve_request(int client_sockfd, int status, std::string stat_msg, std::string err_msg, std::string version, std::string path, int resource, std::string method)
{
    std::string body = "", header = "";
    char temp[400];

    // if status aint 200(OK) or 304(Not Modified) send the error message as the following body
    if (status != 200 && status != 304)
    {
        sprintf(temp, "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01//EN\"\"http://www.w3.org/TR/html4/strict.dtd\">\r\n"
                      "<HTML>\r\n"
                      " <HEAD><TITLE>%d %s</TITLE></HEAD>\r\n"
                      " <BODY>\r\n"
                      "  <H1>%s</H1>\r\n"
                      "  <P>%s</P>\r\n"
                      " </BODY>\r\n"
                      "</HTML>",
                status, stat_msg.c_str(), stat_msg.c_str(), err_msg.c_str());
        body.append(temp);
    }

    // append to the header with the http version of the server
    sprintf(temp, "HTTP/1.1 %d %s\r\n", status, stat_msg.c_str());
    header.append(temp);

    // append to the header with the Connection as close or keep alive
    header.append("Connection: close\r\n");

    // append Server name to the header
    header.append("Server: TeamDepth_5/1.0\r\n");

    // append date to the header
    time_t t = time(NULL);
    struct tm *tm = gmtime(&t);
    strftime(temp, 100, "Date: %a, %d %b %Y %H:%M:%S GMT\r\n", tm);
    header.append(temp);

    // append Content length of the body to be sent
    if(resource<0)
        sprintf(temp, "Content-Length: %lu\r\n", body.size());
    else{
        int size = lseek(resource, 0, SEEK_END);
        lseek(resource,0,SEEK_SET);
        sprintf(temp, "Content-Length: %d\r\n", size);
    }
    header.append(temp);

    // append Content type of the file to be sent, for now supports only html files
    sprintf(temp, "Content-Type: text/html; charset=UTF-8\r\n\r\n");
    header.append(temp);

    // if file is not modified than dont send the file again and just send the header else send the file
    if (status != 304)
    {
        if (status == 200)
        {
            write_header(client_sockfd, header);
            send_resource(client_sockfd, resource);
        }
        else
        {
            header.append(body);
            write_header(client_sockfd, header);
        }
    }
    else
    {
        write_header(client_sockfd, header);
    }
}

// send the response header to the client
void write_header(int client_sockfd, std::string header)
{
    size_t offset = 0, sent, nBytes = header.size();
    const char *buffer = header.c_str();

    // Send Bytes of Header to client until buffer gets empty
    while ((sent = send(client_sockfd, buffer + offset, nBytes, 0)) > 0 || (sent == -1 && errno == EINTR))
    {
        if (sent > 0)
        {
            offset += sent; // increment offset by sent data to point to next block of data
            nBytes -= sent; // decrement number of bytes left to send.
        }
    }
}

// send the files requested from the GET request
void send_resource(int client_sockfd, int resource)
{
    int nBytes;
    char buffer[4096];

    // Read blocks of bytes of size 4096 from file to be sent and get the number of read bytes
    while ((nBytes = read(resource, buffer, 4096)) > 0)
    {
        int offset = 0, sent;

        // Send Bytes of Data read from file to client until buffer gets empty
        while ((sent = write(client_sockfd, buffer + offset, nBytes)) > 0 || (sent == -1 && errno == EINTR))
        {
            if (sent > 0)
            {
                offset += sent; // increment offset by sent data to point to next block of data
                nBytes -= sent; // decrement number of bytes left to send.
            }
        }
    }
}