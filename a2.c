/*
 * Allie Clifford (Acliff01)
 * Comp 112 Homework 2
 * date created: 2/6/2018
 * last modified: 2/21/2018
 *
 * a2.c
 *
 * C program to implement TCP-based chat application
 * using a client/server model using sockets and select 
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <errno.h> //for debug
#include <poll.h>


typedef enum message_type {HELLO = 1, HELLO_ACK, LIST_REQUEST, CLIENT_LIST, CHAT, EXIT, ERROR_CAP, ERROR_CD} Type_M;

/*
 * packed struct for holding header data sent to the server
 */
struct __attribute__((__packed__)) header_C {
        short int type;
        char source[20];
        char dest[20];
        unsigned int len;
        unsigned int ID;
      
};

/*
 * struct for storing client data. Includes the client ID (name), the 
 * sockfd, and a queue for holding all messages that have been 
 * sent each cycle to the client. queue keeps a count of how many 
 * messages are in the queue. The pointer to a header struct acts 
 * as an indicator that this client has sent a header message and 
 * the next message to read will be a buffer with chat data to be 
 * tied to the header struct
 */
struct client_node {
        char name[20];
        int sockfd;
        int queue;
        struct header_C *header;
        struct message_C *messages; 
};      

/*
 * Struct for storing headers and chat data 
 */
struct message_C {
        struct header_C *header;  
        char data[400];
        int drop_data;
        struct message_C *next;
};

/*
 * Global data structures for storing client and socketfd information
 * The hashmap takes a client name as a key and returns a struct ptr
 * to that client's client_node info. The SOCKETS array takes a 
 * sockfd as an index and returns a pointer to the client currently
 * using that sockfd's client_node info
 */
struct client_node **HASHMAP;
struct client_node *SOCKETS[30];

/*
 * Function call definitions
 */
void call_socket(int *sock, int port, struct sockaddr_in *sock_in); 
void call_bind(int sock, struct sockaddr *sock_in, int sock_size); 
void call_listen(int sock, int num_conn); 
void call_sockopt(int sock);
void call_ioctl(int sock);
int process_connections(int master_sock, int *max_sock, 
                        fd_set *master_set, fd_set *listen_set, int ready); 
struct header_C *pack_header(char *buffer, char *desti, Type_M mess_type, 
                                unsigned int size, unsigned int ID);
int parse_resp(int sock, struct header_C *resp);
int return_clients(char *list);
int client_check(char *client_ID);
int queue_message(struct header_C *resp, char *message);
int check_new_client(int sock);
int check_for_message_in(int sock);
int check_for_message_out(int sock, int close);
int rec_message(int sock);
void put(struct client_node *new_cli);
struct client_node *get(char *key);
void init_hashmap();
int hash_fun(char *key);
int accept_and_set(int master_sockfd, int *max_sockfd, fd_set *master_set, 
        int *check);   
int run_client(int sockfd, int *check);
void cannot_deliver(struct message_C *message);
void remove_client_record(int sockfd);
void call_close(int sockfd);
int process_new_client(int sockfd, int *check);

int main(int argc, char *argv[]){ 
        if(argc != 2){
                fprintf(stderr, "Usage: %s port", argv[0]);
                exit(EXIT_FAILURE);
        }        
        init_hashmap();
        int master_socket, max_socket, port_num, num_ready, fd_ready;
        struct sockaddr_in server_address;   
        port_num = strtol(argv[1], NULL, 10); 
        fd_set master_set, read_set;
        call_socket(&master_socket, port_num, &server_address);
        call_sockopt(master_socket);
        call_ioctl(master_socket);
        call_bind(master_socket, (struct sockaddr *)&server_address, 
                                sizeof(server_address)); 
        call_listen(master_socket, 20); 
        FD_ZERO(&master_set);
        FD_ZERO(&read_set); 
        max_socket = master_socket;
        FD_SET(master_socket, &master_set);
        int close_check = 0; 
        do {
                read_set = master_set; 
                num_ready = select(max_socket +1, &read_set, 
                                NULL, NULL, NULL);
                if(num_ready < 0) {       
                        fprintf(stderr,"SELECT error %d\n", errno);
                        break;
                } else { 
                        fd_ready = num_ready;   
                        close_check = process_connections(master_socket, 
                                         &max_socket, &master_set, 
                                        &read_set, fd_ready);  
                }
        } while(!close_check); 
        return 0; 
}              

/*
 * call_socket creates a new socket and performs the necessary 
 * error handling. It initializes the master_socket struct with
 * the appropriate inet, port, and address information. It then
 * returns the newly created TCP stream socket.
 */
void call_socket(int *master_sock, int port, struct sockaddr_in *sock_in){
        *master_sock = socket(AF_INET, SOCK_STREAM, 0);
        if(*master_sock < 0){
                fprintf(stderr,"CALL SOCKET error\n");
                exit(EXIT_FAILURE);
        }  
        memset(sock_in,0,sizeof(struct sockaddr_in));
        sock_in->sin_family = AF_INET;
        sock_in->sin_port = htons(port);
        sock_in->sin_addr.s_addr = htonl(INADDR_ANY);
}
/*
 * call_bind binds the provided socket to the socket address and performs
 * the necessary error handling 
 */
void call_bind(int sock, struct sockaddr *sock_in, int sock_size){
        int bound = bind(sock, sock_in, sock_size);
        if(bound < 0){             
                close(sock);
                fprintf(stderr,"CALL BIND error\n");
                exit(EXIT_FAILURE);
        }
} 
/*
 * call_listen sets the provided socket to listen and limits the
 * number of possible connections
 */
void call_listen(int sock, int num_conn) {
        int err = listen(sock, num_conn);
        if(err < 0){
                fprintf(stderr,"CALL LISTEN error\n");
                close(sock);
                exit(EXIT_FAILURE);
        }
}
/*
 * call_sockopt sets the socket to be reused
 */
void call_sockopt(int sock){
        int check, on = 1;
        check = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, 
                                (char *)&on, sizeof(on));
        if(check < 0) {
                fprintf(stderr,"CALL SETSOCKOPT error %d \n", errno);
                close(sock);
                exit(EXIT_FAILURE);
        }
}
/*
 * call_ioctl sets the socket to be non-blocking
 */
void call_ioctl(int sock) { 
        int on = 1;
        int check = ioctl(sock, FIONBIO, (char *)&on);
        if(check < 0){
                fprintf(stderr, "CALL IOCTL error\n");
                close(sock);
                exit(EXIT_FAILURE);
        } 
}

/*
 * process_connections is called by the loop that controls select() 
 * When there is activity registered in the set of connected sockets, 
 * process connections runs through the  sockfds. if its a new client 
 * that has not yet been processed, process_new_client() is called. 
 * If a returning client, prepare to read what the client is
 * attempting to write via a call to run_client(). After all active 
 * clients are handled, run through list of all connected sockfds, 
 * and send out any messages that may have accumulated in the client 
 * queues during the cycle. Maintains a state control
 * variable passed through all callee functions to determine: when socket 
 * should be closed and when server should be closed. Returns 1 if the
 * server should ever be closed.
 */
int process_connections(int master_sock, int *max_sock, 
                        fd_set *master_set, fd_set *read_set, int ready) {
        char buffer[400];
        bzero(buffer,400);
        struct client_node *ptr;
        int i, close_conn = 0, close_serv = 0, read_next = 0, new_cli;      
        for(i = 0; i <= *max_sock && ready > 0; ++i){ 
                if(FD_ISSET(i,read_set)){     
                        ready -= 1;
                        if(i == master_sock){
                                do { 
                                        new_cli = accept_and_set(master_sock,
                                                max_sock,master_set,
                                                &close_serv);
                                } while(new_cli != -1); 
                        } else { 
                                if(check_new_client(i)){ 
                                        read_next = process_new_client(i, 
                                                        &close_conn);
                                } else {
                                        read_next = run_client(i, &close_conn);
                                }
                        }
                        if(close_conn || errno == 9 || read_next == 2) { 
                                call_close(i);    
                                FD_CLR(i, master_set);
                                if(i == *max_sock) {
                                        while(!FD_ISSET(*max_sock, 
                                                master_set)){
                                                *max_sock -= 1;
                                        }
                                }
                        }
                }
        }
        for(i = 0; i <= *max_sock+1; ++i){ 
                ptr = SOCKETS[i];
                if(ptr != NULL){ 
                        check_for_message_out(i, 0);
                }
        }
        return close_serv;
} 


/*
 * New clients to be processed are parsed through process_new_client,
 * which includes error checking such as whether the client has closed the
 * connection, or if client is not sending a HELLO message. The
 * incoming data is first parsed to create a client record, and then
 * further parsed via a call to parse_resp for sending HELLOACK and CLIENT
 * LIST response messages. Throughout the function,  a check of
 * whether the connection should be closed is kept and passed back to
 * the calling function (process_connections) as a state maintainer
 */
int process_new_client(int sockfd, int *close_conn){
        int check_read, read_next = 0;
        struct header_C *incoming = malloc(sizeof(*incoming));
        bzero(incoming, sizeof(struct header_C));
        check_read = recv(sockfd, (char *)incoming, sizeof(struct header_C),0);
        if(check_read < 0){
                if(errno != EWOULDBLOCK) {
                        fprintf(stderr, " RECV error ungraceful \
                                        exit\n");
                        *close_conn = 1; 
                } 
                return 1;
        } 
        if((check_read == 0) || (htons(incoming->type) != HELLO)){ 
                *close_conn = 1;
                return 1;
        }       
        char *serv = "Server";
        struct header_C *err;
        if((client_check(incoming->source)) || 
                (strcmp(incoming->source,serv) == 0)){ 
                err = pack_header(serv, incoming->source, ERROR_CAP, 0,0);
                check_read = write(sockfd,(char *)err,sizeof(struct header_C));
                if(check_read < 0){
                        fprintf(stderr, "ERROR_CAP error %d\n", errno);
                }
                free(err);
                *close_conn = 1;
                return 1;        
        }
        struct client_node *new_cli = malloc(sizeof(*new_cli));
        bzero(new_cli->name,20);
        int len = strlen(incoming->source);   
        memcpy(&new_cli->name, incoming->source, len+1); 
        new_cli->name[len] = '\0'; 
        new_cli->sockfd = sockfd;
        new_cli->queue = 0;
        new_cli->header = NULL;
        new_cli->messages = NULL;
        SOCKETS[sockfd] = new_cli;
        put(new_cli);     
        read_next = parse_resp(sockfd, incoming); 
        if(read_next == 1){
                return 1;
        } else if(read_next == 2) { 
                *close_conn = 1;
                return 1;
        }                           
        return 0;
}
/*
 * pack_header takes in parameters for a new message and returns a pointer
 * to a newly allocated  header_C struct. It handles the conversions
 * of integers in the header ensuring  host to network byte ordering.
 */
struct header_C *pack_header(char *name_buffer, char *desti, 
                        Type_M mess_type, unsigned int size, unsigned int ID){
        struct header_C *header = malloc(sizeof(*header)); 
        header->type = htons(mess_type);
        memcpy(&header->source, name_buffer, strlen(name_buffer));
        memcpy(&header->dest, desti, strlen(desti));
        switch(mess_type) {          
                case 4: 
                        header->len = htonl(size);
                        header->ID = htonl(0);
                        break;
                case 5:
                        header->len = htonl(size);
                        header->ID = htonl(ID);
                        break;    
                case 8:
                        header->ID = htonl(ID);
                        header->len = htonl(0);
                        break;
                default:
                        header->len = htonl(0);
                        header->ID = htonl(0);
                } 
        return header;
}       

/*
 * parse_resp is called by two functions: if parsing data from a new client,
 * it is called by process new client, and runs through case 1. Otherwise,
 * it is called by run_client to parse data from a returning client. It takes 
 * in a socketfd and a header struct, and uses a switch case to process 
 * the data from the header. It ensures basic error checking and handling
 * for various error cases, and parses chat requests, list requests, 
 * and exit requests. It maintains and passes back the client connection
 * state variable.
 */
int parse_resp(int sock, struct header_C *resp) {
        Type_M type = ntohs(resp->type);  
        unsigned int size = ntohl(resp->len);
        unsigned int id = ntohl(resp->ID); 
        char client_list[400];
        char *serv = "Server";
        struct header_C *err, *ack;
        int ret_val = 0, n, bytes;
        switch(type) { 
                case 1:              
                        ack = pack_header(serv,resp->source, HELLO_ACK, 0,0);
                        n = write(sock,(char *)ack,sizeof(struct header_C));
                        if(n < 0){
                                fprintf(stderr,"WRITE ACK error %d\n", errno);
                        }        
                        free(ack);
                case 3:
                        bytes = return_clients(client_list);
                        struct header_C *list = pack_header(serv,
                                                resp->source, CLIENT_LIST,
                                        (unsigned int)bytes,0);
                        n = write(sock,(char *)list,sizeof(struct header_C));
                        if(n < 0) {
                                fprintf(stderr,"WRITE LIST error %d\n", errno);
                        }
                        n = write(sock,client_list,bytes);
                        if(n < 0){
                                fprintf(stderr,"WRITE LIST ERR %d\n", errno);
                        } 
                        ret_val = 1;
                        free(list);
                        break;
                case 5:
                        if(size > 400) { 
                                printf("Its bigger than 4hundo\n");
                                ret_val = 2;
                                break;
                        } 
                        struct client_node *ptr = SOCKETS[sock];
                        struct client_node *cli_ptr = get(resp->dest);
                        ret_val = 1;
                        if(ptr == NULL || cli_ptr == NULL) { 
                                err = pack_header(serv, resp->source, 
                                                        ERROR_CD, 0,id);
                                n = write(sock,(char *)err,
                                        sizeof(struct header_C));
                                if(n < 0){
                                        fprintf(stderr, "Error writing %d\n",
                                                errno);
                                }
                                free(err);
                                break;
                        }else if(strcmp(ptr->name, resp->dest) == 0) {
                      
                                ret_val = 2;
                                break;
                        } 
                        ptr->header = resp; 
                        break;
                case 6:
                        ret_val = 2;
                        struct client_node *err_ptr = SOCKETS[sock];
                        if(err_ptr->messages != NULL){
                                struct message_C *mess_ptr = err_ptr->messages;
                                while((mess_ptr != NULL) && 
                                        (err_ptr->queue != 0)){ 
                                        cannot_deliver(mess_ptr);             
                                        --err_ptr->queue;
                                        mess_ptr = mess_ptr->next;
                                }
                        }
                        break;
                default: 
                        ret_val = 2;
                }
        return ret_val;
}
/*
 * Run_client takes in a sockfd and a close connection check, and sets up the
 * behavior of the dat aparse flow based on if this specific client has
 * indicated via previously sent header message that it will be sending a chat
 * message payload. If client has sent a message header, run_client calls 
 * rec_message, which is responsible for receiving and queing the message.
 * Run_client maintains and returns a close connection state check
 */
int run_client(int sockfd, int *close_conn){
        int read_next = 0, check_read;
        if(check_for_message_in(sockfd)){ 
                check_read = rec_message(sockfd);
                if(check_read == 2) {
                        *close_conn = 1;
                }
                return 1;
        } else {
                struct header_C *incoming;
                incoming = malloc(sizeof(*incoming));
                bzero(incoming, sizeof(struct header_C));
                check_read = recv(sockfd, (char *)incoming, 
                                sizeof(struct header_C),0);
                if(check_read < 0){
                        if(errno != EWOULDBLOCK) {
                                fprintf(stderr, " RECV error \
                                        ungraceful client exit\n");
                                *close_conn = 1; 
                        } 
                        return 1;
                } 
                if(check_read == 0){ 
                        *close_conn = 1;
                        return 1;
                }
                read_next = parse_resp(sockfd, incoming);  
                if(read_next == 1){
                        return 1;
                } else if(read_next == 2) {
                        *close_conn = 1;
                        return 1;
                }
        }
        return 0;
}
/*
 * rec_message is called when a client is sending a chat data after having 
 * already sent a header message. Rec_message performs multiple error checks
 * such as a client sending to a nonexistent client, and others as specified
 * in the assignment specification. Once error checking has been handled,
 * the newly received chat data is then queued via a call to queue_message
 * It maintains the connection state check, returning with the appropriate 
 * value depending on whether any error handling was performed
 */
int rec_message(int sock) {                        
        char message[400];
        bzero(message,400);
        int bytes;                        
        char *serv = "Server"; 
        struct client_node *ptr = SOCKETS[sock];         
        struct header_C *resp = ptr->header;   
        int id = ntohl(resp->ID);
        int len = ntohl(resp->len);
        if((strcmp(ptr->name, resp->dest) == 0)|| 
                (strcmp(serv,resp->dest) == 0) || (resp->dest == NULL)){ 
                return 2;
        }        
        bytes = read(sock, message,len);   
        if(bytes < 0) {
                fprintf(stderr, "error reading on socket %d errno %d\n", 
                        sock, errno);
        } else if(bytes == 0){
                return 2;
        } 
        struct client_node *cli_ptr = get(resp->dest);
        if(cli_ptr == NULL){ 
                ptr->header = NULL;
                struct header_C *err = pack_header(serv, resp->source, 
                                        ERROR_CD, 0,id);
                bytes = write(ptr->sockfd,(char *)err,sizeof(struct header_C));
                if(bytes < 0){
                        fprintf(stderr, "ERROR_Cd error %d\n", errno);
                }
                free(err);
                return 1;
        }
        if(bytes != len){
                fprintf(stderr,"Incorrect message length errno %d\n", errno);
                return 2;
        }
        ptr->header = NULL;
        return queue_message(resp,message); 
        
}
/*
 * queue_message is called when a chat data payload has been received. It 
 * finds the destination client using the global client data structures and
 * it adds the message to the recipients individual message queue. It performs
 * error checking such as ensuring the data length is equal to the length
 * speicified by the header. It maintains the close connection state check
 * that is passed from all calling functions chaining from process_connections
 */
int queue_message(struct header_C *resp, char *message){ 
        struct message_C *new_mess = malloc(sizeof(*new_mess));
        struct message_C *ptr; 
        memcpy(&new_mess->data, message, ntohl(resp->len)); 
        new_mess->header = resp;
        new_mess->drop_data = 0;
        new_mess->next = NULL; 
        struct client_node *cli_ptr = get(resp->dest);
        if(cli_ptr->messages == NULL){ 
                cli_ptr->messages = new_mess;
                cli_ptr->queue = 1;
        } else { 
                ptr = cli_ptr->messages;
                while(ptr->next != NULL) { 
                        ptr = ptr->next;
                }
                ptr->next = new_mess;
                ++cli_ptr->queue;
        }
        return 0;
}
/*
 * Check to see if the last interaction the server had with client 
 * indicates that a message payload is coming in
 */
int check_for_message_in(int sock){
        struct client_node *ptr = SOCKETS[sock];
        if(ptr == NULL) { 
                return 0;
        } else {
                if(ptr->header == NULL){
                        return 0;
                } else {
                        return 1;
                }
        }
}
/*
 * Check for a message to send out to specified client. If check flag is
 * set to true, then the client has recently closed its connection, and
 * all messages in it's queue will need to be returned to the sender. To
 * do this, cannot_deliver() is called. 
 */
int check_for_message_out(int sock, int check){ 
        if(check){
                struct client_node *ptr = SOCKETS[sock];
                if(ptr == NULL) {
                        return 0;
                }
                if(ptr->messages != NULL){ 
                        struct message_C *mess_ptr = ptr->messages;
                        while(mess_ptr != NULL && ptr->queue != 0){
                                 cannot_deliver(mess_ptr);             
                                --ptr->queue;
                                mess_ptr = mess_ptr->next;
                        }
                } else {
                        return 0;
                }
        }
        int n, ret_error = 0;
        struct client_node *ptr = SOCKETS[sock];
        if(ptr->messages != NULL) { 
                struct message_C *mess_ptr = ptr->messages;
                while(mess_ptr != NULL && ptr->queue != 0){
                        n = write(sock,(char *)mess_ptr->header,
                                sizeof(struct header_C));
                        if(n < 0) {
                                fprintf(stderr, "Error writing message \
                                        to %s, errno: %d\n", 
                                        ptr->name, errno);
                        }
                        if(!mess_ptr->drop_data) { 
                                n = write(sock, mess_ptr->data,
                                                 ntohl(mess_ptr->header->len));
                                if(n < 0 || n == 0) { 
                                        ret_error = 1;
                                }    
                        }
                        if(ret_error) {
                                cannot_deliver(mess_ptr);
                        }
                        --ptr->queue;
                        mess_ptr = mess_ptr->next;
                }
                ptr->messages = NULL;
                return 1;
        } else {
                return 0;
        }

}
/*
 * check_new_lient acts as a boolean check, where, given the sockfd, makes sure
 * that the client actually exists. This is the trigger for 
 * procesing_new_client. It is used when only the sockfd is known
 *
 */
int check_new_client(int sock) {
        struct client_node *ptr = SOCKETS[sock];
        if(ptr == NULL) {
                return 1;
        } else {
                return 0;
        }
}
/*
 * client_check performs similarly to check_new_client, however, it
 * taks as it's argument the client ID instead of the sockfd. It is used
 * for routing incoming chat messages from clients who do not know the 
 * sockfd of the client they are attempting to contact
 */
int client_check(char *client_ID) {
        struct client_node *ptr = get(client_ID);
        if(ptr == NULL){
                return 0;
        } else {
                return 1;
        }
}
/*
 * return clients populates the char array provided as a function argument
 * with the names of each active client, using memcpy. Client names are
 * separated by null terminators ('\0') and must use a specifal function to
 * print fully to the screen. Returns the total numbe rof bytes to the 
 * calling function, which uses that to make a call to write() when returning
 * the client list
 */
int return_clients(char *cli_list) {
        memset(cli_list,'\0',400);
        int i, bytes = 0;
        struct client_node *ptr; 
        for(i = 0; i < 30; ++i) {
                ptr = SOCKETS[i];
                if(ptr != NULL) { 
                        memcpy(&cli_list[bytes],ptr->name,strlen(ptr->name)+1);
                        bytes += strlen(ptr->name)+1;    
                }       
        }
        return bytes;
}       
/*
 * put places a new client_node structr into the HASHTABLE data structure
 */
void put(struct client_node *new_cli) {
        int index = hash_fun(new_cli->name);
        HASHMAP[index] = new_cli; 
}
/*
 * put returns a pointer to a given index in the HASHTABLE data structure.
 * Returns NULL if the client is not there
 */
struct client_node *get(char *key) {
        int index = hash_fun(key);
        struct client_node *ptr = HASHMAP[index];
        if((ptr == NULL) || (strcmp(ptr->name,key) != 0)) {
                return NULL;
        } else {
                return ptr;
        }
}
/*
 * hash_fun calculates the index of a client given the name length, the value
 * of each char, some prime number bit shifting, and mod 50 (hash table size)
 * the sum of the values
 */
int hash_fun(char *key) {
        int i, j = strlen(key);
        unsigned long prime_num = 131071; //mersenne prime 2^17 - 1
        for(i = 0; i < j; ++i){
                prime_num = (prime_num << 3) + key[i] + j; 
        }
        return (prime_num % 50);
}
/*
 * Init the HASHTABLE data structure
 */
void init_hashmap() {
        HASHMAP = (struct client_node **)calloc(50,sizeof(**HASHMAP));
}
/*
 * Accept new connections on the master socket, add new sockfd to master set
 */
int accept_and_set(int master_socket, int *max_socket, fd_set *master_set, 
                int *check) {
        int new_cli;
        new_cli = accept(master_socket, NULL, NULL);
        if(new_cli < 0) {
                if(errno != EWOULDBLOCK){
                        fprintf(stderr, "ACCEPT error %d\n", errno);
                        *check = 1;
                }
                return new_cli;
        }
        FD_SET(new_cli, master_set);
        if(new_cli > *max_socket) {
                *max_socket = new_cli;
        }
        return new_cli;
}
/*
 * cannot_deliver returns undeliverable messages to the client who originally
 * sent the message. It repackages the message and header data in a struct 
 * that gets queued in th esenders message queue. In such cases the data 
 * part of the message is dropped
 */
void cannot_deliver(struct message_C *message) { 
        char *serv = "Server";
        struct header_C *head_ptr = message->header; 
        struct header_C *err = pack_header(serv, head_ptr->source, 
                                ERROR_CD, 0, head_ptr->ID);
        struct client_node *cli_ptr = get(head_ptr->source);
        if(cli_ptr == NULL) {
                free(err);
                free(message);
                return;
        }
        struct message_C *ptr;
        message->drop_data = 1;
        message->header = err;
        if(cli_ptr->messages == NULL){
                cli_ptr->messages = message;
                cli_ptr->queue = 1;
        } else { 
                ptr = cli_ptr->messages;
                while(ptr->next != NULL) { 
                        ptr = ptr->next;
                }
                ptr->next = message;
                ++cli_ptr->queue;
        }       
}   
/*
 * remove_client_record is called when a connection is closed, either via
 * graceful or ungraceful exit
 */
void remove_client_record(int sockfd){
        if(SOCKETS[sockfd] != NULL) {
                struct client_node *sock_ptr = SOCKETS[sockfd]; 
                int index = hash_fun(sock_ptr->name);
                SOCKETS[sockfd] = NULL;
                HASHMAP[index] = NULL;
        }
}
/*
 * call_close handles the return of outgoing messages to the connection to
 * be closed, closed the sockfd, and calls remove_client_record
 */
void call_close(int sockfd){
        int close_conn = 1;  
        check_for_message_out(sockfd, close_conn);
        close(sockfd);
        remove_client_record(sockfd);    
}
