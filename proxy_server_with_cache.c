#include "proxy_parse.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/wait.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>
#include <stdarg.h>

#define MAX_BYTES 4096    //max allowed size of request/response
#define MAX_CLIENTS 400     //max number of client requests served at a time
#define MAX_SIZE 200*(1<<20)     //size of the cache
#define MAX_ELEMENT_SIZE 10*(1<<20)     //max size of an element in cache
#define MAX_LOG_MESSAGE 2048   // Maximum size of log message

// Log levels
#define LOG_ERROR 0
#define LOG_WARN  1
#define LOG_INFO  2
#define LOG_DEBUG 3

typedef struct cache_element cache_element;

struct cache_element{
    char* data;         //data stores response
    int len;          //length of data i.e.. sizeof(data)...
    char* url;        //url stores the request
	time_t lru_time_track;    //lru_time_track stores the latest time the element is  accesed
    cache_element* next;    //pointer to next element
};

// Logger globals
int log_level = LOG_INFO;           // Default log level
FILE* log_file = NULL;              // Log file pointer
pthread_mutex_t log_mutex;          // Mutex for thread-safe logging

// Function prototypes
cache_element* find(char* url);
int add_cache_element(char* data,int size,char* url);
void remove_cache_element();
void log_message(int level, const char* format, ...);
void init_logger(const char* log_file_path);
void close_logger();
const char* get_log_level_str(int level);

int port_number = 8080;				// Default Port
int proxy_socketId;					// socket descriptor of proxy server
pthread_t tid[MAX_CLIENTS];         //array to store the thread ids of clients
sem_t seamaphore;	                //if client requests exceeds the max_clients this seamaphore puts the
                                    //waiting threads to sleep and wakes them when traffic on queue decreases
pthread_mutex_t lock;               //lock is used for locking the cache

cache_element* head;                //pointer to the cache
int cache_size;                     //cache_size denotes the current size of the cache

// Initialize the logger
void init_logger(const char* log_file_path) {
    pthread_mutex_init(&log_mutex, NULL);
    
    log_file = fopen(log_file_path, "a");
    if (log_file == NULL) {
        fprintf(stderr, "Error opening log file: %s\n", log_file_path);
        perror("fopen");
        // Continue without file logging
    }
    
    log_message(LOG_INFO, "Proxy server logging initialized");
}

// Close the logger
void close_logger() {
    if (log_file != NULL) {
        fclose(log_file);
    }
    pthread_mutex_destroy(&log_mutex);
}

// Convert log level to string
const char* get_log_level_str(int level) {
    switch (level) {
        case LOG_ERROR: return "ERROR";
        case LOG_WARN:  return "WARN";
        case LOG_INFO:  return "INFO";
        case LOG_DEBUG: return "DEBUG";
        default:        return "UNKNOWN";
    }
}

// Log a message with specified level
void log_message(int level, const char* format, ...) {
    if (level > log_level) {
        return;  // Skip if log level is higher than current setting
    }
    
    pthread_mutex_lock(&log_mutex);
    
    // Get current time
    time_t now = time(NULL);
    struct tm* time_info = localtime(&now);
    char timestamp[26];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", time_info);
    
    // Format the log message
    va_list args;
    va_start(args, format);
    
    char message[MAX_LOG_MESSAGE];
    vsnprintf(message, MAX_LOG_MESSAGE, format, args);
    
    // Log to console
    printf("[%s] [%s] %s\n", timestamp, get_log_level_str(level), message);
    
    // Log to file if available
    if (log_file != NULL) {
        fprintf(log_file, "[%s] [%s] %s\n", timestamp, get_log_level_str(level), message);
        fflush(log_file);  // Ensure it's written immediately
    }
    
    va_end(args);
    pthread_mutex_unlock(&log_mutex);
}

int sendErrorMessage(int socket, int status_code)
{
	char str[1024];
	char currentTime[50];
	time_t now = time(0);

	struct tm data = *gmtime(&now);
	strftime(currentTime, sizeof(currentTime), "%a, %d %b %Y %H:%M:%S %Z", &data);

	switch(status_code)
	{
		case 400: snprintf(str, sizeof(str), "HTTP/1.1 400 Bad Request\r\nContent-Length: 95\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>400 Bad Request</TITLE></HEAD>\n<BODY><H1>400 Bad Rqeuest</H1>\n</BODY></HTML>", currentTime);
				  log_message(LOG_ERROR, "400 Bad Request sent to client");
				  send(socket, str, strlen(str), 0);
				  break;

		case 403: snprintf(str, sizeof(str), "HTTP/1.1 403 Forbidden\r\nContent-Length: 112\r\nContent-Type: text/html\r\nConnection: keep-alive\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>403 Forbidden</TITLE></HEAD>\n<BODY><H1>403 Forbidden</H1><br>Permission Denied\n</BODY></HTML>", currentTime);
				  log_message(LOG_ERROR, "403 Forbidden sent to client");
				  send(socket, str, strlen(str), 0);
				  break;

		case 404: snprintf(str, sizeof(str), "HTTP/1.1 404 Not Found\r\nContent-Length: 91\r\nContent-Type: text/html\r\nConnection: keep-alive\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>404 Not Found</TITLE></HEAD>\n<BODY><H1>404 Not Found</H1>\n</BODY></HTML>", currentTime);
				  log_message(LOG_ERROR, "404 Not Found sent to client");
				  send(socket, str, strlen(str), 0);
				  break;

		case 500: snprintf(str, sizeof(str), "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 115\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>500 Internal Server Error</TITLE></HEAD>\n<BODY><H1>500 Internal Server Error</H1>\n</BODY></HTML>", currentTime);
				  log_message(LOG_ERROR, "500 Internal Server Error sent to client");
				  send(socket, str, strlen(str), 0);
				  break;

		case 501: snprintf(str, sizeof(str), "HTTP/1.1 501 Not Implemented\r\nContent-Length: 103\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>404 Not Implemented</TITLE></HEAD>\n<BODY><H1>501 Not Implemented</H1>\n</BODY></HTML>", currentTime);
				  log_message(LOG_ERROR, "501 Not Implemented sent to client");
				  send(socket, str, strlen(str), 0);
				  break;

		case 505: snprintf(str, sizeof(str), "HTTP/1.1 505 HTTP Version Not Supported\r\nContent-Length: 125\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>505 HTTP Version Not Supported</TITLE></HEAD>\n<BODY><H1>505 HTTP Version Not Supported</H1>\n</BODY></HTML>", currentTime);
				  log_message(LOG_ERROR, "505 HTTP Version Not Supported sent to client");
				  send(socket, str, strlen(str), 0);
				  break;

		default:  log_message(LOG_ERROR, "Invalid error code: %d", status_code);
				  return -1;
	}
	return 1;
}

int connectRemoteServer(char* host_addr, int port_num)
{
	// Creating Socket for remote server ---------------------------
	log_message(LOG_DEBUG, "Connecting to remote server %s:%d", host_addr, port_num);
	
	int remoteSocket = socket(AF_INET, SOCK_STREAM, 0);

	if(remoteSocket < 0)
	{
		log_message(LOG_ERROR, "Error creating socket for remote server: %s", strerror(errno));
		return -1;
	}
	
	// Get host by the name or ip address provided
	struct hostent *host = gethostbyname(host_addr);	
	if(host == NULL)
	{
		log_message(LOG_ERROR, "No such host exists: %s", host_addr);
		return -1;
	}

	// inserts ip address and port number of host in struct `server_addr`
	struct sockaddr_in server_addr;

	bzero((char*)&server_addr, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(port_num);

	bcopy((char *)host->h_addr, (char *)&server_addr.sin_addr.s_addr, host->h_length);

	// Connect to Remote server ----------------------------------------------------
	if(connect(remoteSocket, (struct sockaddr*)&server_addr, (socklen_t)sizeof(server_addr)) < 0)
	{
		log_message(LOG_ERROR, "Error connecting to remote server %s:%d: %s", 
		            host_addr, port_num, strerror(errno));
		return -1;
	}
	
	log_message(LOG_INFO, "Successfully connected to remote server %s:%d", host_addr, port_num);
	return remoteSocket;
}

int handle_request(int clientSocket, ParsedRequest *request, char *tempReq)
{
	log_message(LOG_INFO, "Handling request for %s%s", request->host, request->path);
	
	char *buf = (char*)malloc(sizeof(char) * MAX_BYTES);
	strcpy(buf, "GET ");
	strcat(buf, request->path);
	strcat(buf, " ");
	strcat(buf, request->version);
	strcat(buf, "\r\n");

	size_t len = strlen(buf);

	if (ParsedHeader_set(request, "Connection", "close") < 0) {
		log_message(LOG_ERROR, "Failed to set Connection header");
	}

	if(ParsedHeader_get(request, "Host") == NULL)
	{
		if(ParsedHeader_set(request, "Host", request->host) < 0) {
			log_message(LOG_ERROR, "Failed to set Host header");
		}
	}

	if (ParsedRequest_unparse_headers(request, buf + len, (size_t)MAX_BYTES - len) < 0) {
		log_message(LOG_ERROR, "Failed to unparse request headers");
	}

	int server_port = 80;				// Default Remote Server Port
	if(request->port != NULL)
		server_port = atoi(request->port);

	int remoteSocketID = connectRemoteServer(request->host, server_port);

	if(remoteSocketID < 0) {
		log_message(LOG_ERROR, "Failed to connect to remote server");
		free(buf);
		return -1;
	}

	int bytes_send = send(remoteSocketID, buf, strlen(buf), 0);
	log_message(LOG_DEBUG, "Sent %d bytes to remote server", bytes_send);

	bzero(buf, MAX_BYTES);

	bytes_send = recv(remoteSocketID, buf, MAX_BYTES-1, 0);
	log_message(LOG_DEBUG, "Received %d bytes from remote server", bytes_send);
	
	char *temp_buffer = (char*)malloc(sizeof(char) * MAX_BYTES);
	int temp_buffer_size = MAX_BYTES;
	int temp_buffer_index = 0;
	int total_bytes = 0;

	while(bytes_send > 0)
	{
		int sent = send(clientSocket, buf, bytes_send, 0);
		if(sent < 0) {
			log_message(LOG_ERROR, "Error sending data to client: %s", strerror(errno));
			break;
		}
		log_message(LOG_DEBUG, "Sent %d bytes to client", sent);
		total_bytes += sent;
		
		for(int i = 0; i < bytes_send/sizeof(char); i++) {
			temp_buffer[temp_buffer_index] = buf[i];
			temp_buffer_index++;
		}
		
		if(temp_buffer_index >= temp_buffer_size - MAX_BYTES) {
			temp_buffer_size += MAX_BYTES;
			temp_buffer = (char*)realloc(temp_buffer, temp_buffer_size);
			if(!temp_buffer) {
				log_message(LOG_ERROR, "Memory allocation failed during response buffering");
				free(buf);
				return -1;
			}
		}

		bzero(buf, MAX_BYTES);
		bytes_send = recv(remoteSocketID, buf, MAX_BYTES-1, 0);
	} 
	
	temp_buffer[temp_buffer_index] = '\0';
	log_message(LOG_INFO, "Transfer complete. Total bytes: %d", total_bytes);
	
	free(buf);
	
	// Add to cache
	if(add_cache_element(temp_buffer, strlen(temp_buffer), tempReq)) {
		log_message(LOG_INFO, "Response cached successfully for %s%s", request->host, request->path);
	} else {
		log_message(LOG_WARN, "Failed to cache response for %s%s", request->host, request->path);
	}
	
	free(temp_buffer);
	close(remoteSocketID);
	return 0;
}

int checkHTTPversion(char *msg)
{
	int version = -1;

	if(strncmp(msg, "HTTP/1.1", 8) == 0)
	{
		version = 1;
	}
	else if(strncmp(msg, "HTTP/1.0", 8) == 0)			
	{
		version = 1;	// Handling this similar to version 1.1
	}
	else
		version = -1;

	return version;
}

void* thread_fn(void* socketNew)
{
	sem_wait(&seamaphore); 
	int p;
	sem_getvalue(&seamaphore, &p);
	log_message(LOG_DEBUG, "Thread started. Semaphore value: %d", p);
	
    int* t= (int*)(socketNew);
	int socket=*t;           // Socket is socket descriptor of the connected Client
	int bytes_send_client, len;	  // Bytes Transferred

	char *buffer = (char*)calloc(MAX_BYTES, sizeof(char));	// Creating buffer of 4kb for a client
	
	bzero(buffer, MAX_BYTES);								// Making buffer zero
	bytes_send_client = recv(socket, buffer, MAX_BYTES, 0); // Receiving the Request of client by proxy server
	
	while(bytes_send_client > 0)
	{
		len = strlen(buffer);
        //loop until u find "\r\n\r\n" in the buffer
		if(strstr(buffer, "\r\n\r\n") == NULL)
		{	
			bytes_send_client = recv(socket, buffer + len, MAX_BYTES - len, 0);
		}
		else{
			break;
		}
	}

	if(bytes_send_client <= 0) {
		if(bytes_send_client < 0) {
			log_message(LOG_ERROR, "Error receiving from client: %s", strerror(errno));
		} else {
			log_message(LOG_INFO, "Client disconnected");
		}
		free(buffer);
		close(socket);
		sem_post(&seamaphore);
		return NULL;
	}
	
	log_message(LOG_DEBUG, "Received request of %d bytes", strlen(buffer));
	
	char *tempReq = (char*)malloc(strlen(buffer)*sizeof(char)+1);
    //tempReq, buffer both store the http request sent by client
	strcpy(tempReq, buffer);
	
	//checking for the request in cache 
	log_message(LOG_DEBUG, "Checking cache for request");
	struct cache_element* temp = find(tempReq);

	if(temp != NULL) {
        //request found in cache, so sending the response to client from proxy's cache
		log_message(LOG_INFO, "Cache HIT - Serving response from cache");
		int size = temp->len/sizeof(char);
		int pos = 0;
		char response[MAX_BYTES];
		int total_sent = 0;
		
		while(pos < size) {
			bzero(response, MAX_BYTES);
			int chunk_size = (size - pos < MAX_BYTES) ? (size - pos) : MAX_BYTES;
			
			for(int i = 0; i < chunk_size; i++) {
				response[i] = temp->data[pos];
				pos++;
			}
			
			int sent = send(socket, response, chunk_size, 0);
			if(sent < 0) {
				log_message(LOG_ERROR, "Error sending cached response: %s", strerror(errno));
				break;
			}
			total_sent += sent;
		}
		
		log_message(LOG_INFO, "Sent %d bytes from cache to client", total_sent);
	}
	else {
		log_message(LOG_INFO, "Cache MISS - Forwarding request to origin server");
		
		//Parsing the request
		ParsedRequest* request = ParsedRequest_create();
		
        //ParsedRequest_parse returns 0 on success and -1 on failure
		if (ParsedRequest_parse(request, buffer, len) < 0) {
		   	log_message(LOG_ERROR, "Request parsing failed");
			sendErrorMessage(socket, 400);
		}
		else {	
			bzero(buffer, MAX_BYTES);
			if(!strcmp(request->method, "GET")) {
                log_message(LOG_DEBUG, "Processing GET request for %s%s", 
				            request->host ? request->host : "unknown", 
							request->path ? request->path : "unknown");
				
				if(request->host && request->path && (checkHTTPversion(request->version) == 1)) {
					bytes_send_client = handle_request(socket, request, tempReq);
					if(bytes_send_client == -1) {	
						log_message(LOG_ERROR, "Failed to handle request");
						sendErrorMessage(socket, 500);
					}
				}
				else {
					log_message(LOG_ERROR, "Invalid request parameters");
					sendErrorMessage(socket, 500);
				}
			}
            else {
                log_message(LOG_WARN, "Unsupported method: %s", request->method);
				sendErrorMessage(socket, 501);
            }
		}
        //freeing up the request pointer
		ParsedRequest_destroy(request);
	}

	log_message(LOG_DEBUG, "Closing client connection");
	shutdown(socket, SHUT_RDWR);
	close(socket);
	free(buffer);
	free(tempReq);
	
	sem_post(&seamaphore);	
	sem_getvalue(&seamaphore, &p);
	log_message(LOG_DEBUG, "Thread finished. Semaphore value: %d", p);
	
	return NULL;
}

cache_element* find(char* url) {
    // Checks for url in the cache if found returns pointer to the respective cache element or else returns NULL
    cache_element* site = NULL;
    int temp_lock_val = pthread_mutex_lock(&lock);
	log_message(LOG_DEBUG, "Cache search lock acquired: %d", temp_lock_val); 
	
    if(head != NULL) {
        site = head;
        while(site != NULL) {
            if(!strcmp(site->url, url)) {
                log_message(LOG_DEBUG, "Cache hit - URL found in cache");
				// Updating the time_track
				site->lru_time_track = time(NULL);
				break;
            }
            site = site->next;
        }       
    }
	
	if(site == NULL) {
        log_message(LOG_DEBUG, "Cache miss - URL not found in cache");
	}
	
    temp_lock_val = pthread_mutex_unlock(&lock);
	log_message(LOG_DEBUG, "Cache search lock released: %d", temp_lock_val); 
    return site;
}

void remove_cache_element() {
    // If cache is not empty searches for the node which has the least lru_time_track and deletes it
    cache_element* p;  	// Cache_element Pointer (Prev. Pointer)
	cache_element* q;	// Cache_element Pointer (Next Pointer)
	cache_element* temp;	// Cache element to remove
	
    int temp_lock_val = pthread_mutex_lock(&lock);
	log_message(LOG_DEBUG, "Remove cache element lock acquired: %d", temp_lock_val); 
	
	if(head != NULL) { // Cache != empty
		for (q = head, p = head, temp = head; q->next != NULL; q = q->next) { 
			// Iterate through entire cache and search for oldest time track
			if(((q->next)->lru_time_track) < (temp->lru_time_track)) {
				temp = q->next;
				p = q;
			}
		}
		
		if(temp == head) { 
			head = head->next; /*Handle the base case*/
		} else {
			p->next = temp->next;	
		}
		
		int removed_size = (temp->len) + sizeof(cache_element) + strlen(temp->url) + 1;
		cache_size = cache_size - removed_size;
		
		log_message(LOG_INFO, "Removed element from cache, size: %d bytes", removed_size);
		
		free(temp->data);     		
		free(temp->url); // Free the removed element 
		free(temp);
	} 
	
    temp_lock_val = pthread_mutex_unlock(&lock);
	log_message(LOG_DEBUG, "Remove cache element lock released: %d", temp_lock_val); 
}

int add_cache_element(char* data, int size, char* url) {
    // Adds element to the cache
    int temp_lock_val = pthread_mutex_lock(&lock);
	log_message(LOG_DEBUG, "Add cache element lock acquired: %d", temp_lock_val);
	
    int element_size = size + 1 + strlen(url) + sizeof(cache_element); // Size of the new element
    
    if(element_size > MAX_ELEMENT_SIZE) {
        // If element size is greater than MAX_ELEMENT_SIZE we don't add the element to the cache
        temp_lock_val = pthread_mutex_unlock(&lock);
		log_message(LOG_WARN, "Element too large for cache: %d bytes (max: %d)", 
		            element_size, MAX_ELEMENT_SIZE);
		return 0;
    }
    else {   
        while(cache_size + element_size > MAX_SIZE) {
            // We keep removing elements from cache until we get enough space to add the element
            log_message(LOG_INFO, "Cache full, removing LRU element. Current size: %d, Max: %d", 
                      cache_size, MAX_SIZE);
            remove_cache_element();
        }
        
        cache_element* element = (cache_element*)malloc(sizeof(cache_element));
        element->data = (char*)malloc(size + 1);
        strcpy(element->data, data); 
        
        element->url = (char*)malloc(1 + (strlen(url) * sizeof(char)));
        strcpy(element->url, url);
        
        element->lru_time_track = time(NULL);
        element->next = head; 
        element->len = size;
        head = element;
        cache_size += element_size;
        
        log_message(LOG_INFO, "Added element to cache, size: %d bytes, new total: %d bytes", 
                  element_size, cache_size);
        
        temp_lock_val = pthread_mutex_unlock(&lock);
		log_message(LOG_DEBUG, "Add cache element lock released: %d", temp_lock_val);
        return 1;
    }
    return 0;
}

void set_log_level(const char* level_str) {
    if(strcasecmp(level_str, "ERROR") == 0) {
        log_level = LOG_ERROR;
    } else if(strcasecmp(level_str, "WARN") == 0) {
        log_level = LOG_WARN;
    } else if(strcasecmp(level_str, "INFO") == 0) {
        log_level = LOG_INFO;
    } else if(strcasecmp(level_str, "DEBUG") == 0) {
        log_level = LOG_DEBUG;
    } else {
        fprintf(stderr, "Unknown log level: %s. Using default (INFO).\n", level_str);
    }
}

int main(int argc, char * argv[]) {
    // Initialize logger first
    init_logger("proxy_server.log");
    
    int client_socketId, client_len; // client_socketId == to store the client socket id
    struct sockaddr_in server_addr, client_addr; // Address of client and server to be assigned

    log_message(LOG_INFO, "Proxy server starting up");
    
    // Process command line arguments
    if(argc >= 2) {
        port_number = atoi(argv[1]);
        log_message(LOG_INFO, "Setting port number to %d", port_number);
    } else {
        log_message(LOG_ERROR, "Too few arguments. Usage: %s <port_number> [log_level]", argv[0]);
        exit(1);
    }
    
    // Set log level if provided
    if(argc >= 3) {
        set_log_level(argv[2]);
        log_message(LOG_INFO, "Log level set to %s", get_log_level_str(log_level));
    }

    // Initialize semaphore and mutex
    sem_init(&seamaphore, 0, MAX_CLIENTS);
    pthread_mutex_init(&lock, NULL);
    
    log_message(LOG_INFO, "Creating proxy server socket");
    //creating the proxy socket
    proxy_socketId = socket(AF_INET, SOCK_STREAM, 0);

    if(proxy_socketId < 0) {
        log_message(LOG_ERROR, "Failed to create socket: %s", strerror(errno));
        exit(1);
    }

    int reuse = 1;
    if(setsockopt(proxy_socketId, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse)) < 0) {
        log_message(LOG_ERROR, "setsockopt(SO_REUSEADDR) failed: %s", strerror(errno));
    }

    bzero((char*)&server_addr, sizeof(server_addr));  
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_number);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    // Binding the socket
    if(bind(proxy_socketId, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        log_message(LOG_ERROR, "Failed to bind to port %d: %s", port_number, strerror(errno));
        exit(1);
    }
    log_message(LOG_INFO, "Successfully bound to port %d", port_number);

    // Proxy socket listening to the requests
    int listen_status = listen(proxy_socketId, MAX_CLIENTS);

    if(listen_status < 0) {
        log_message(LOG_ERROR, "Error while listening: %s", strerror(errno));
        exit(1);
    }
    log_message(LOG_INFO, "Server listening for connections");

    int i = 0; // Iterator for thread_id (tid) and Accepted Client_Socket for each thread
    int Connected_socketId[MAX_CLIENTS];   // Array stores socket descriptors of connected clients

    // Initialize cache stats
    cache_size = 0;
    head = NULL;
    
    log_message(LOG_INFO, "Cache initialized: Max size=%d bytes, Max element size=%d bytes", 
              MAX_SIZE, MAX_ELEMENT_SIZE);

    // Infinite Loop for accepting connections
    while(1) {
        bzero((char*)&client_addr, sizeof(client_addr));
        client_len = sizeof(client_addr); 

        // Accepting the connections
        client_socketId = accept(proxy_socketId, (struct sockaddr*)&client_addr, (socklen_t*)&client_len);
        if(client_socketId < 0) {
            log_message(LOG_ERROR, "Error accepting connection: %s", strerror(errno));
            continue;
        }
        
        Connected_socketId[i] = client_socketId;

		// Getting IP address and port number of client
        struct sockaddr_in* client_pt = (struct sockaddr_in*)&client_addr;
        struct in_addr ip_addr = client_pt->sin_addr;
        char str[INET_ADDRSTRLEN];                                              // INET_ADDRSTRLEN: Default ip address size
        inet_ntop(AF_INET, &ip_addr, str, INET_ADDRSTRLEN);
        
        log_message(LOG_INFO, "Client connected from %s:%d (socket=%d)", 
                  str, ntohs(client_addr.sin_port), client_socketId);
        
        // Create a thread for each client accepted
        if (pthread_create(&tid[i], NULL, thread_fn, (void*)&Connected_socketId[i]) != 0) {
            log_message(LOG_ERROR, "Failed to create thread: %s", strerror(errno));
            close(client_socketId);
            continue;
        }
        
        // Detach thread so resources are freed automatically when thread finishes
        pthread_detach(tid[i]);
        
        i = (i + 1) % MAX_CLIENTS; // Cycle through the thread array
    }
    
    // Clean up - This part will never execute in the current implementation
    // but included for completeness
    close(proxy_socketId);
    sem_destroy(&seamaphore);
    pthread_mutex_destroy(&lock);
    close_logger();
    
    return 0;
}