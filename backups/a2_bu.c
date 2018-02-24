/*
 * Allie Clifford (Acliff01)
 * Comp 112 Homework 2
 * date created: 2/6/2018
 * last modified: 2/19/2018
 *
 * a2.c
 *
 * C program to implement TCP-based chat application
 * using a c client/server model built on socket 
 * programming
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


#define type_m short int
typedef enum message_type {HELLO = 1, HELLO_ACK, LIST_REQUEST, CLIENT_LIST, CHAT, EXIT, ERROR_CAP, ERROR_CD} Type_M;

/*
 * packed struct for holding header data sent to the server
 */
struct __attribute__((__packed__)) header_C {
        type_m type;
        char source[20];
        char dest[20];
        unsigned int len;
        unsigned int ID;
      
};

/*
 * struct for storing client data. Includes the client ID (name), the sockfd,
 * and a queue for holding all messages that have been sent each cycle to the
 * client. queue keeps a count of how many messages are in the queue. The
 * pointer to the header acts as an indicator that this client has sent a header
 * message and the next message to read will be a buffer with chat data to be
 * tied to the header struct
 */
struct client_node {
        char *name; //client username
        int sockfd; //cliend socket file descriptor
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
 *Global data structures for storing client and socketfd information
 */
struct client_node **HASHMAP;
struct client_node *SOCKETS[30];
/*
 * Function call definitions
 */

void call_socket(int *sock, int port, struct sockaddr_in *sock_in); 
void call_bind(int sock, struct sockaddr *sock_in, int sock_size); 
void call_listen(int sock, int num_conn); 
//int call_accept(int master_sock, int *cli_sock, struct sockaddr *client_node, socklen_t *add_size); 
//int call_read(int sock, char *buffer, int size);
void call_sockopt(int sock);
void call_ioctl(int sock);
//void call_write(int sock, char *response, int size);
int process_connections(int master_sock, int *max_sock, 
                        fd_set *master_set, fd_set *listen_set, int ready); 
struct header_C *pack_header(char *buffer, char *desti, Type_M mess_type, 
                                unsigned int size, unsigned int ID);
int parse_resp(int sock, struct header_C *resp);
void return_clients(char *list);
int client_check(char *client_ID);
int queue_message(struct header_C *resp, char *message);
int check_new_client(int sock);
int check_for_message_in(int sock);
int check_for_message_out(int sock, int close);
int rec_message(int sock);
void put(struct client_node *new_cli);
struct client_node *get(char *key);
void init_queue();
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
        call_listen(master_socket, 10); 
        FD_ZERO(&master_set);
        FD_ZERO(&read_set); 

        max_socket = master_socket;
        FD_SET(master_socket, &master_set);
        int close_check = 0; 
        do {
                read_set = master_set;
                printf("Selecting...\n");   
                num_ready = select(max_socket +1, &read_set, NULL, NULL, NULL);
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
 * call_read calls the read() function on the provided socket, which
 * writes te incoming bytestream to the provided buffer. It returns
 * the number of bytes written to the buffer. It performs the necesary
 * error handling and returns 0 if the bytestream is empty i.e. nothing
 * more to read
 
int call_read(int sock, char *buffer, int size){
        int num_bytes = read(sock, buffer, size);
        if (num_bytes < 0) {
                close(sock);
                fprintf(stderr, "CALL READ error\n");
                exit(EXIT_FAILURE);
        }
        return num_bytes;        
}

 * call_write calls the write() function and performs the necessary error
 * handling
 
void call_write(int sock, char *response, int size){
        int num_bytes = write(sock, response, size);
        if(num_bytes < 0) { 
                close(sock);
                fprintf(stderr, "CALL WRITE error\n");
                exit(EXIT_FAILURE);
        }
}
*/


/*
 * process_connections is called by select() When there is activity registered
 * in the set of connected sockets, process connections runs through the  
 * sockfds and processes the connect: if its activity on th emaster sock, 
 * accept. Else, if its a new client that has not yet been processed, process 
 * the new client. If a returning client, prepare to read what the client is
 * attempting to write via a call to run_client(). After all active clients are
 * handled, run through list of all connected sockfds, and check to see if
 * they have any messages in their queues. If yes, send them out until each
 * queue is empty
 */
int process_connections(int master_sock, int *max_sock, 
                        fd_set *master_set, fd_set *read_set, int ready) {
        char buffer[400];
        bzero(buffer,400);
        int i, close_conn = 0, close_serv = 0, read_next = 0,  new_cli;      
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
                                        read_next = process_new_client(i, &close_conn);
                                } else {
                                        read_next = run_client(i, &close_conn);
                                }
                        }
                        if(close_conn || errno == 9 || read_next == 2) {
                                printf("closin the conn\n"); 
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
        struct client_node *ptr;
        for(i = 0; i <= *max_sock+1; ++i){
                printf("checkin for message out to socket %d\n", i);
                ptr = SOCKETS[i];
                if(ptr != NULL){
                        printf("we have a client...\n");
                        check_for_message_out(i, 0);
                }
        }
        
        return close_serv;
} 


/*
 * New clients to be processed are parsed through process_new_client,
 * which includes error checking such as whether or not a clientID is 
 * available, or if a new client is not sending a HELLO message. The
 * incoming data is parsed via a call to parse_resp, and a check of
 * whether the connection should be closed is kept
 */
int process_new_client(int sockfd, int *close_conn){
        int check_read, read_next = 0;
        struct header_C *incoming = malloc(sizeof(*incoming));
        bzero(incoming, sizeof(struct header_C));
        check_read = recv(sockfd, (char *)incoming, sizeof(struct header_C),0);
        if(check_read < 0){
                if(errno != EWOULDBLOCK) {
                        fprintf(stderr, " RECV error handle ungraceful \
                                        exit needs \function\n");
                        *close_conn = 1; 
                } 
                return 1;
        } 
        if(check_read == 0){ 
                *close_conn = 1;
                return 1;
        }
        //printf(" %d bytes received\n", check_read);
        //printf("source: %s\n", incoming->source);
        //printf("destination %s\n", incoming->dest); 
        //printf("type??? %d\n", htons(incoming->type));
        char *serv = "Server";
        if(htons(incoming->type) != HELLO) {
        //        printf("Its not a hello?\n");
                struct header_C *err = pack_header(serv,incoming->source, 
                                                ERROR_CD, 0,0);
                check_read = write(sockfd,(char *)err, sizeof(struct header_C));
                if(check_read < 0){
                        fprintf(stderr, "ERROR_CAP error\n");
                }
               *close_conn = 1;
                return 1;
        }

        read_next = parse_resp(sockfd, incoming); 
        if(read_next == 1){
                return 1;
        } else if(read_next == 2) {
          //      printf("\nClose conn should be set\ndo we check for unsendable messages??\n");
                *close_conn = 1;
                return 1;
        }                           
        return 0;
}
/*
 * pack_header takes in parameters for a new message and returns a pointer
 * to a newly allocated-via-malloc header_C struct. It handles the conversions
 * of integers in the header from host to network byte ordering.
 *
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
 * parse_resp takes in a socket fd and a header struct, and uses a switch case
 * to determine how to handle the parsing of the header. It performs error
 * checking for various error cases and parses chat requests, list requests, 
 * and exit requests. It also handles creating new clients and adding them
 * to the client hashtable and socketfd table, the two main data structures
 * that hold client information.
 */
int parse_resp(int sock, struct header_C *resp) {
        Type_M type = ntohs(resp->type);
        //printf("parse_resp type check: %d %d\n", type, resp->type);
        //printf("check: client %s has socket %d\n", resp->source, sock);
        unsigned int size = ntohl(resp->len);
        unsigned int id = ntohl(resp->ID); 
        char client_list[400];
        char *serv = "Server";
        //printf("size and ID: %u %u\n", size, id);        
        int ret_val = 0, n;
        switch(type) { 
                case 1:
                        //printf("its a hello\n");
                        ret_val = 1;             
                        if((client_check(resp->source)) || 
                                (strcmp(resp->source,serv) == 0)){
                                printf("returning error 7\n");
                                struct header_C *err = pack_header(serv,
                                                        resp->source, 
                                                        ERROR_CAP, 0,0);
                                n = write(sock,(char *)err,
                                        sizeof(struct header_C));
                                if(n < 0){
                                        fprintf(stderr, "ERROR_CAP error\n");
                                }
                                call_close(sock);
                                free(err);
                                ret_val = 2;
                                break;
                        }
                        struct client_node *new_cli = malloc(sizeof(*new_cli));
                        new_cli->name = resp->source; 
                        new_cli->sockfd = sock;
                        new_cli->queue = 0;
                        new_cli->header = NULL;
                        new_cli->messages = NULL;
                        SOCKETS[sock] = new_cli;
                        put(new_cli);                        
                        struct header_C *ack = pack_header(serv,
                                                resp->source, HELLO_ACK, 0,0);
                        n = write(sock,(char *)ack,sizeof(struct header_C));
                        if(n < 0){
                                fprintf(stderr,"WRITE ACK error\n");
                        }        
                case 3:
                        printf("Its a list req\n"); 
                        return_clients(client_list);
                        int list_len = strlen(client_list);
                        struct header_C *list = pack_header(serv,
                                                resp->source, CLIENT_LIST,
                                        (unsigned int)list_len,0);
                        n = write(sock,(char *)list,sizeof(struct header_C));
                        if(n < 0) {
                                fprintf(stderr,"WRITE 1 LIST error\n");
                        }
                        printf("serverside list check: %s\n",client_list); 
                        n = write(sock,client_list,list_len);
                        if(n < 0){
                                fprintf(stderr,"WRITE LIST ERR\n");
                        } 
                        ret_val = 1;
                        free(list);
                        free(ack);
                        break;
                case 2:
                        printf("its a hay ack error handle\n");
                        //this should happen-- error handle?
                        break;
                case 4: 
                        printf("is a list resp wait what? NEED TO ERROR HANDLE\n");
                        //this should never happen-- error handle?
                        break;
                case 5:
                        printf("is a chat req\n");
                        if(size > 400) { 
                                struct header_C *err_len = pack_header(serv,
                                                        resp->source, 
                                                        ERROR_CD, 0,id);
                                n = write(sock,(char *)err_len,sizeof(struct header_C));
                                if(n < 0){
                                        printf("eRROR writing to client handle\n");
                                }
                                free(err_len);
                        } 
                        struct client_node *ptr = SOCKETS[sock];
                        struct client_node *cli_ptr = get(resp->dest);
                        ret_val = 1;
                        if(ptr == NULL || cli_ptr == NULL) {
                                printf("Error handling for NO SUCH CLIENT\n");
                                if(ptr == NULL) {
                                        printf("SOurce not here weird\n");
                                }else if(cli_ptr == NULL){
                                        printf("Get returned a null client ptr,,,\n");
                                }

                                struct header_C *err = pack_header(serv,
                                                        resp->source, 
                                                        ERROR_CD, 0,id);
                                n = write(sock,(char *)err,sizeof(struct header_C));
                                if(n < 0){
                                        printf("eRROR writing to client handle\n");
                                }
                                free(err);
                        }else if(strcmp(ptr->name, resp->dest) == 0) {
                                  struct header_C *err_name = pack_header(serv,
                                                        resp->source, 
                                                        ERROR_CD, 0,id);
                                n = write(ptr->sockfd,(char *)err_name,sizeof(struct header_C));
                                if(n < 0){
                                        printf("Error message how to handle?\n");
                                }
                                free(err_name);
                        } else {
                                printf("%s HAS A CHAT REQUEST COMING UP\n", ptr->name);
                        }
                        ptr->header = resp;
                        //rec_message(sock);                        
                        break;
                case 6:
                        printf("is a exit req\n");
        
                        ret_val = 2;
                        struct client_node *err_ptr = SOCKETS[sock];
                   
                        if(err_ptr->messages != NULL){
         //                       printf("Bc errno is set to 11 we dont deliver it...\n");
                                struct message_C *mess_ptr = err_ptr->messages;
                                while(mess_ptr != NULL && err_ptr->queue != 0){
                                        printf("Calling cannot_deliver, returning to sender!\n");
                                        cannot_deliver(mess_ptr);             
                                        --err_ptr->queue;
                                        mess_ptr = mess_ptr->next;
                                }
                        }
  
                        break;
                case 7:
                        printf("is a cli already here error ERROR HANDLE\n");
                        ret_val = 2;
                        break;
               // ERROR_CD:
                case 8:
                        printf("is a cant deliver error ERROR HANDLE\n");
                        ret_val = 2;
                        break;
                }
        return ret_val;
}
/*
 * Run_client takes in a sockfd and a close connection check, and sets up the
 * behavior of the server according to whether this specific client has
 * indicated that it will be sending a chat message in a previous cycle. If
 * the client has sent a message header, then run_client calls rec_message,
 * which will call read and queue the incoming message. Run_client maintains a
 * and returns a close connection state check
 */
int run_client(int sockfd, int *close_conn){
        int read_next = 0, check_read;
       
        printf("Its a returning client lookin to write\n");
        if(check_for_message_in(sockfd)){ 
                printf("Client indicated incoming message\n");
                return rec_message(sockfd);
        } else {
               
 
                printf("server is waiting to read...\n");
                struct header_C *incoming;
                incoming = malloc(sizeof(*incoming));
                bzero(incoming, sizeof(struct header_C));
                check_read = recv(sockfd, (char *)incoming, 
                                sizeof(struct header_C),0);
                if(check_read < 0){
                        if(errno != EWOULDBLOCK) {
                                fprintf(stderr, " RECV error \
                                        handle ungraceful \
                                        exit needs \
                                        function\n");
                                *close_conn = 1; 
                        } 
                        return 1;
                } 
                if(check_read == 0){
                        printf(" connection closed?? errno %d\n", errno);
                        *close_conn = 1;
                        return 1;
                }
                                        
                printf(" %d bytes received\n", check_read);
                printf("source: %s\n", incoming->source);
                printf("destination %s\n", incoming->dest);                           
                read_next = parse_resp(sockfd, incoming); 
                printf("THINK THIS THRU-- DO WE NEED THIS CHECK FOR RETURNING CLIENTS?\n");
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
 * the newly received chat data is then queues via a call to queue_message
 *
 */
int rec_message(int sock) {                        
        char message[400];
        bzero(message,400);
        int bytes;                        
        char *serv = "Server"; 
        bytes = read(sock, message,400);  
        if(bytes == 0){
                printf("client is done\n");
        }else if(bytes < 0) {
                printf("error handle-- reading on socket %d resulted in %d\n", sock, errno);
        }  
        struct client_node *ptr = SOCKETS[sock];         
        struct header_C *resp = ptr->header;   
        int id = ntohl(resp->ID);
        //person trying to chat with themselves
        if(strcmp(ptr->name, resp->dest) == 0) {
                printf("error client sending message to self\n");
                struct header_C *err = pack_header(serv, resp->source, ERROR_CD, 0,id);
                bytes = write(ptr->sockfd,(char *)err,sizeof(struct header_C));
                if(bytes < 0){
                        fprintf(stderr, "ERROR_Cd error\n");
                }
                ptr->header = NULL;
                free(err);
                return 1;
        }        
        printf("received message: %s\n", message);                        
        if(ptr == NULL) {
                printf("rec_message needs error handling -- sockets not saving client ppinters\n");
                return 1; //should this be an error condition?
        }
        //int dest_sock;
        struct client_node *cli_ptr = get(resp->dest);
        
        if(cli_ptr == NULL){
                printf("no such client? ERROR HANDLing...\n");
                ptr->header = NULL;
                struct header_C *err = pack_header(serv, resp->source, 
                                        ERROR_CD, 0,id);
                bytes = write(ptr->sockfd,(char *)err,sizeof(struct header_C));
                if(bytes < 0){
                        fprintf(stderr, "ERROR_Cd error\n");
                }
                free(err);
                return 1;
        }
        if(strlen(message) > resp->len){
                ptr->header = NULL;
                struct header_C *err = pack_header(serv, resp->source, 
                                        ERROR_CD, 0,id);
                bytes = write(ptr->sockfd,(char *)err,sizeof(struct header_C));
                if(bytes < 0){
                        fprintf(stderr, "ERROR_Cd error\n");
                }
                free(err);
                return 1;
        }

        ptr->header = NULL;
        return queue_message(resp,message); 
}

/*
 * queue_message is called when a chat data payload has been received. It 
 * finds the destination client using the global client data structures and
 * it adds the message to the recipients individual message queue. It performs
 * error checking such as ensuring the data length is equal to the length
 * speicified by the header
 *
 */
int queue_message(struct header_C *resp, char *message){
        printf("we are queueing the message: %s\n", message);
        printf("the message is from %s and destined for: %s\n", resp->source, resp->dest);
        struct message_C *new_mess = malloc(sizeof(*new_mess));
        struct message_C *ptr;
        memcpy(&new_mess->data, message, strlen(message)); 
        new_mess->header = resp;
        new_mess->next = NULL;
        struct client_node *cli_ptr = get(resp->dest);
        printf("queue message test: %s %d\n", cli_ptr->name, cli_ptr->queue);

        if(resp->len != strlen(message)){
                printf("data sizes are not equal: %d %d\n NEED ERROR HANDLING\n", resp->len, strlen(message));
        }

        if(cli_ptr->messages == NULL){
                printf("1 setting new message for %s's queue\n", cli_ptr->name);
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
 * IS THIS FUNCTION EVEN USED??? DOUBLE CHECK!
 */
int check_for_message_in(int sock){
        printf("checking for in message...\n");
        struct client_node *ptr = SOCKETS[sock];
        if(ptr == NULL) {
                printf("its NULL\n");
                return 0;
        } else {
                printf("client name: %s\n", ptr->name);
                if(ptr->header == NULL){
                        printf("No header, movin along...\n");
                        return 0;
                } else {
                        printf("THeres a header here\n");
                        return 1;
                }
        }
}
/*
 * Check for a message to send out to specified client. If check flag is
 * set to true, then the client has recently closed its connection, and
 * all messages in it's queue will need to be returned to the sender. To
 * do this, cannot_deliver() is called. 
 *
 */
int check_for_message_out(int sock, int check){
        printf("Checking for messages...\n");
        if(check){
                printf("Check is set\n");
                struct client_node *ptr = SOCKETS[sock];
                if(ptr == NULL) {
                        printf("revisit wipe strategy..its NULL\n");
                        return 0;
                }
                if(ptr->messages != NULL){
                        printf("Calling cannot_deliver...ptr queue size: %d\n", ptr->queue);
                        struct message_C *mess_ptr = ptr->messages;
                        while(mess_ptr != NULL && ptr->queue != 0){
                                 cannot_deliver(mess_ptr);             
                                --ptr->queue;
                                mess_ptr = mess_ptr->next;
                        }
                } else {
                        printf("Ptr->messages is null?\n");
                }
                return 0;
        }
        int n, ret_error = 0;
        struct client_node *ptr = SOCKETS[sock];
        if(ptr == NULL) {
                printf("message out ERROR handle\n");
                return 0;
        } 
        printf("Client to check for outgoing messages: %s\n", ptr->name);
        if(ptr->messages != NULL) { 
                struct message_C *mess_ptr = ptr->messages;
                while(mess_ptr != NULL && ptr->queue != 0){
                       printf("%d Messages to go out!\n", ptr->queue);
                        printf("Message: %s\n", mess_ptr->data);
                        printf("name at socket: %s socket %d socket check %d\n", ptr->name, ptr->sockfd, sock);
                        n = write(sock,(char *)mess_ptr->header,
                                sizeof(struct header_C));
                        if(n < 0) {
                                printf("error writing message to client %s\n", ptr->name);
                                //break;
                        }
                        if(n == 0) {
                                printf("Client done reading??\n");
                                //break;
                        }
                        if(!mess_ptr->drop_data) {
                                printf("drop_data flag not set...\n");
                                n = write(sock, mess_ptr->data, strlen(mess_ptr->data));
                                if(n < 0) {
                                        printf("error writing message to client %s\n", ptr->name);
                                        ret_error = 1;
                                        // break;
                                }
                                if(n == 0) {
                                        printf("Client done reading??\n");
                                        ret_error = 1;
                                        //break;
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
                printf("No messages for this socket to receive\n");
                return 0;
        }

}
/*
 * check_new_lient acts as a boolean check, where, given the sockfd, makes sure
 * that the client actually exists. This is the trigger for procesing_new_client
 *
 */
int check_new_client(int sock) {
        struct client_node *ptr = SOCKETS[sock];
        if(ptr == NULL) {
                return 1;
        } else {
                printf("Client name: %s\n", ptr->name);
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
 * with the names of each active client, using memcpy
 *
 */
void return_clients(char *cli_list) {
        bzero(cli_list,400);
        int i, bytes = 0;
        struct client_node *ptr; // = CLIENTS->queue;
        char space[] = "\0";
        for(i = 3; i < 30; ++i) {
                ptr = SOCKETS[i];
                if(ptr != NULL) { 
                        printf("name %s\n", ptr->name);
                        memcpy(&cli_list[bytes],ptr->name,strlen(ptr->name));
                        bytes = (strlen(cli_list)); 
                        //memcpy(&cli_list[bytes],space,strlen(space)+1);
                }       
        }
        return;
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
        if(ptr == NULL) {
               // printf("hash table ... did we use the wrong key? %s %d\n", key, index);
                return NULL;
        
        }
        if(strcmp(ptr->name,key) != 0) {
               // printf("hash table GET error\n");
                return NULL;
        }
        printf("Insert check here against the current name associated with the socket!\n");
        return ptr;
        
}

/*
 * hash_fun calculates the index of a client given the name length, 
 * some bit shifting, and mod 50 (hash table size)
 */
int hash_fun(char *key) {
        int i, j = strlen(key);
        unsigned long prime_num = 131071; //mersenne prim 2^17 - 1
        for(i = 0; i < j; ++i){
                prime_num = (prime_num << 3) + key[i] + j;
                //printf("%d\n", prime_num);
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
int accept_and_set(int master_socket, int *max_socket, fd_set *master_set, int *check) {
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
 * sent the message
 */
void cannot_deliver(struct message_C *message) {
       printf("Entering cannot deliver...\n"); 
        char *serv = "Server";
        struct header_C *head_ptr = message->header;
        printf("Cannot deliver message from %s\n", head_ptr->source);
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
                //struct client_node *hash_ptr = get(sock_ptr->name);
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
        printf("calling check mess out, should trigger cannot_deliver\n");
        check_for_message_out(sockfd, close_conn);

        close(sockfd);

        remove_client_record(sockfd);
       // FD_CLR(sockfd, &master_set);
       // if(sockfd == max_socket) {
       //         while(!FD_ISSET(max_socket, &master_set)){
       //                 max_socket -= 1;
       //         }
      //  }
}
