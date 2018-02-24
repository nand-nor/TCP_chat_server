/*
 * Allie Clifford (Acliff01)
 * Comp 112 Homework 2
 * date created: 2/6/2018
 * last modified: 2/11/2018
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

#define type_m short int
typedef enum message_type {HELLO = 1, HELLO_ACK, LIST_REQUEST, CLIENT_LIST, CHAT, EXIT, ERROR_CAP, ERROR_CD} Type_M;

/*
 * packed struct for holding message data
 */
struct __attribute__((__packed__)) header_C {
        type_m type;
        char source[20];
        char dest[20];
        unsigned int len;
        unsigned int ID;
        //char data[300];
};

struct cli_ID {
        char *ID; //client username
        int sockfd; //cliend socket file descriptor
        int active; //boolean to indicate if client is active
        int mess_to_send; //indicates if that user has a message
        int mess_in_queue;
        struct header_C *header;
        struct cli_ID *next; //this is going to be a queue for now
                                //but I eventually want it to be a hash table
};      

struct message_C {
        char data[300];
        int sockfd;
        int ID;
        struct header_C *header;
        struct cli_ID *client;
        struct message_C *next;
};

struct cli_q {
        struct cli_ID *queue;
        int size;
};
struct mess_q {
        struct message_C *queue;
        struct message_C *end;
        int size;
};

struct mess_q *MESSAGE_QUEUE;
struct cli_q *CLIENTS;  

struct cli_ID **hashmap;

/*
 * Function call definitions
 */

void call_socket(int *sock, int port, struct sockaddr_in *sock_in); 
void call_bind(int sock, struct sockaddr *sock_in, int sock_size); 
void call_listen(int sock, int num_conn); 
int call_accept(int master_sock, int *cli_sock, struct sockaddr *cli_ID, socklen_t *add_size); 
int call_read(int sock, char *buffer, int size);
void call_sockopt(int sock, int conn);
void call_ioctl(int sock, int conn);
int call_select(int sock, fd_set *listen_set);
void call_write(int sock, char *response, int size);
int process_connections(int master_sock, int *client_sock, int *max_sock, 
                        fd_set *master_set, fd_set *listen_set, int ready); 
struct header_C *pack_header(char *buffer, char *desti, Type_M mess_type, 
                                unsigned int size, unsigned int ID);
int parse_resp(int sock, struct header_C *resp, char *buffer);
void return_clients(char *list);
struct cli_ID *client_check(char *client_ID);
void queue_message(struct header_C *resp, char *message, int sock, struct cli_ID *client);
struct cli_ID *check_for_message_in(int sock);
int check_for_message_out(int sock);
void rec_message(struct header_C *resp, int sock);
void put(struct cli_ID *new_cli);
struct cli_ID *get(char *key);
void init_queue();
void init_hashmap();
int hash_fun(char *key);

int main(int argc, char *argv[]){ 
        if(argc != 2){
                fprintf(stderr, "Usage: %s port", argv[0]);
                exit(EXIT_FAILURE);
        }        
        init_queue();
        init_hashmap();
        int master_socket, client_socket, max_socket, port_num, conn = 1, 
                num_ready, fd_ready;
        struct sockaddr_in server_address;   
        port_num = strtol(argv[1], NULL, 10); 
        fd_set master_set, listen_set;
        call_socket(&master_socket, port_num, &server_address);
        call_sockopt(master_socket, conn);
        call_ioctl(master_socket, conn);
        call_bind(master_socket, (struct sockaddr *)&server_address, 
                                sizeof(server_address)); 
        call_listen(master_socket, 10); 
        FD_ZERO(&master_set);
        max_socket = master_socket;
        FD_SET(master_socket, &master_set);
        int close_server = 0; 
        do {
                memcpy(&listen_set, &master_set, sizeof(master_set));
                printf("Selecting...\n");
                int num_ready = select(max_socket + 1, &listen_set, NULL,
                                        NULL, NULL);               
                if(num_ready < 0) {       
                        fprintf(stderr,"SELECT error %d\n", errno);          
                        break;
                } else if (num_ready == 0) {
                        printf("is this needed if no timeout?...\n");
                } else {
                        printf("there is activity!\n");
                        fd_ready = num_ready;   
                        close_server = process_connections(master_socket, 
                                        &client_socket, &max_socket,
                                         &master_set, &listen_set, fd_ready);   
                }
        } while(!close_server); 
        return 0; 
}              


int process_connections(int master_sock, int *client_sock, int *max_sock, 
                        fd_set *master_set, fd_set *listen_set, int ready) {
        char buffer[300];
        bzero(buffer,300);
        printf("entering process connection...\n");
        int i, close_conn = 0, close_serv = 0, read_next = 0, check_read, 
                check_write, check_in = 0, new_cli; 
        for(i = 0; i <= *max_sock && ready > 0; ++i){ 
                if(FD_ISSET(i, listen_set)){
                        ready -= 1;
                        if(i == master_sock){
                                do {
                                        new_cli = accept(master_sock, NULL, 
                                                        NULL);
                                        if(new_cli < 0) {
                                                if(errno != EWOULDBLOCK){
                                                        fprintf(stderr,
                                                        "ACCEPT error %d\n",
                                                                errno);
                                                close_serv = 1;
                                                }
                                                break;
                                        }
                                        FD_SET(new_cli, master_set);
                                        if(new_cli > *max_sock) {
                                                *max_sock = new_cli;
                                        }
                                    } while(new_cli != -1); 
                        } else {
                                do {
                                        printf("starting reads/writes...\n");
                                        //first check to see if there are messages
                                        struct cli_ID *mess_in = check_for_message_in(i);
                                        if(mess_in != NULL){
                                               printf("mess in not null, entering rec_message...\n"); 
                                                rec_message(mess_in->header, i);                                
                                                
                                        }
                                        
                                        check_in = check_for_message_out(i);
                                        printf("finished checking for message out...\n");
                                        if(check_in || mess_in != NULL) {
                                                printf("we sent the message, so should we break and move on?\n");
                                                read_next = 1;
                                                continue;
                                        }
                                        
                                        printf("so the client has nothing coming in for which a header was already sent, and they have nothing going out. So now we read what kind of request they wanna make...\n");
                                        struct header_C *incoming;
                                        incoming = malloc(sizeof(*incoming));
                                        bzero(incoming, 
                                                sizeof(struct header_C));
                                        check_read = recv(i, 
                                                (char *)incoming, 
                                                sizeof(struct header_C),0);
                                        if(check_read < 0){
                                                if(errno != EWOULDBLOCK) {
                                                        fprintf(stderr, 
                                                        " RECV error \
                                                        handle ungraceful \
                                                        exit needs \
                                                        function\n");
                                                         close_conn = 1; 
                                                } 
                                                continue;
                                          } 
                                        if(check_read == 0){
                                                printf(" connection closed\n");
                                                close_conn = 1;
                                                read_next = 1;
                                                continue;
                                        }
                                        
                        printf(" %d bytes received\n", check_read);
                        printf("source: %s\n", incoming->source);
                        printf("destination %s\n", incoming->dest); 
                                     
                                    
                                        read_next = parse_resp(i, 
                                                        incoming, buffer); 
                                       if(read_next = 1) {
                                                continue;
                                        }
                                         //char *def = "Got the message\n";
                                        //check_write = send(i, 
                                        //        def, strlen(def), 0);
                                        
                                       // if(check_write < 0) {
                                       //         fprintf(stderr, 
                                       //                 "SEND() failed");
                                       //         close_conn = 1;
                                       //         break;
                                       // }
        
                                        if(read_next == 1){
                                                break;
                                        } else if(read_next == 2) {
                                                close_conn = 1;
                                                break;
                                        }
                                        //bzero(buffer,255);
                                } while (!read_next); 
                                        if(close_conn) {
                                                printf("closin the conn\n"); 
                                               close(i);
                                                FD_CLR(i, master_set);
                                                if(i == *max_sock) {
                                                        while(!FD_ISSET(*max_sock, master_set)){
                                                                *max_sock -= 1;
                                                        }
                                                }
                                        }
                                }
                        }
                }
        return close_serv;
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

void call_sockopt(int sock, int conn){
        int on = 1;
        int check = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char *)&on, sizeof(on));
        if(check < 0) {
                fprintf(stderr,"CALL SETSOCKOPT error %d \n", errno);
                close(sock);
                exit(EXIT_FAILURE);
        }

}

void call_ioctl(int sock, int conn) {
        //printf("sock check %ud\n", sock);
        int on = 1;
        int check = ioctl(sock, FIONBIO, (char *)&on);
        if(check < 0){
          
                fprintf(stderr, "CALL IOCTL error\n");
                close(sock);
                exit(EXIT_FAILURE);
        }
       // printf("ioctl return: check %d\n", check);
}
/*
 * call_read calls the read() function on the provided socket, which
 * writes te incoming bytestream to the provided buffer. It returns
 * the number of bytes written to the buffer. It performs the necesary
 * error handling and returns 0 if the bytestream is empty i.e. nothing
 * more to read
 */
int call_read(int sock, char *buffer, int size){
        int num_bytes = read(sock, buffer, size);
        if (num_bytes < 0) {
                close(sock);
                fprintf(stderr, "CALL READ error\n");
                exit(EXIT_FAILURE);
        }
        return num_bytes;        
}
/*
 * call_write calls the write() function and performs the necessary error
 * handling
 */
void call_write(int sock, char *response, int size){
        int num_bytes = write(sock, response, size);
        if(num_bytes < 0) { 
                close(sock);
                fprintf(stderr, "CALL WRITE error\n");
                exit(EXIT_FAILURE);
        }
}

struct header_C *pack_header(char *name_buffer, char *desti, Type_M mess_type, unsigned int size, unsigned int ID){ 
        struct header_C *header = malloc(sizeof(*header));
        header->type = htons(mess_type);
        memcpy(&header->source, name_buffer, strlen(name_buffer));
        //printf("source %s\n", header->source);
        memcpy(&header->dest, desti, strlen(desti));
        switch(mess_type) {          
                case 4: 
                       // printf("is a list resp\n");
                        //pack_list(header, size);  
                        header->len = htonl(size);
                        header->ID = htonl(0);
                        break;
                case 5:
                      //  printf("is a chat req\n");
                        header->len = htonl(size);
                        header->ID = htonl(ID);
                  //pack_chat(header, size, ID);
                        break;    
                case 8:
                      //  printf("is a cant deliver error\n");
                        header->ID = htonl(ID);
                        header->len = htonl(0);
                        //    pack_error(header, ID);
                        break;
                default:
                        header->len = htonl(0);
                        header->ID = htonl(0);
                }
        printf("test of pack_header functions: %d %d\n", header->len, header->ID);
        return header;
}       

int parse_resp(int sock, struct header_C *resp, char *buffer) {
        Type_M type = ntohs(resp->type);
        printf("type check: %d %d\n", type, resp->type);
        

        printf("check: client %s has socket %d\n", resp->source, sock);

        unsigned int size = ntohl(resp->len);
        unsigned int id = ntohl(resp->ID);
         
        int buff_size = CLIENTS->size * 20, rec_sock = -1;
        char client_list[buff_size+1];
        printf("size and ID: %u %u\n", size, id);        
        int ret_val = 0, n;
        switch(type) { 
                case 1:
                        printf("its a hello\n");
                        ret_val = 1;
                        //first check if they already there
                        //will write a new function for that
                        if(client_check(resp->source) != NULL){
                                printf("returning error 8\n");
                                struct header_C *err = pack_header(resp->dest,
                                                        resp->source, 
                                                        ERROR_CAP, 0,0);
                                n = write(sock,(char *)err,
                                        sizeof(struct header_C));
                                if(n < 0){
                                        fprintf(stderr, "ERROR_CAP error\n");
                                }
                                ret_val = 2;
                                break;
                        }

                        //pull this out into a function
                        struct cli_ID *new_cli = malloc(sizeof(*new_cli));
                        new_cli->ID = resp->source;
                        new_cli->active = 1;
                        new_cli->sockfd = sock;
                        new_cli->mess_in_queue = 0;
                        new_cli->mess_to_send = 0;
                        new_cli->header = resp;
                        new_cli->next = NULL;
                        if(CLIENTS->queue == NULL) {
                                CLIENTS->queue = new_cli;
                                ++CLIENTS->size;
                        }else {
                                struct cli_ID *ptr = CLIENTS->queue;
                                new_cli->next = ptr;
                                CLIENTS->queue = new_cli;
                                ++CLIENTS->size; 
                        }
                        struct header_C *ack = pack_header(resp->dest,
                                                resp->source, HELLO_ACK,
                                                0,0);
                        n = write(sock,(char *)ack,sizeof(struct header_C));
                        if(n < 0){
                                fprintf(stderr,"WRITE ACK error\n");
                        }
                        break;
                case 2:
                        printf("its a hay ack\n");
                        //this should happen-- error handle?
                        break;
                case 3:
                        printf("Its a list req, hope they ready to read what we writin\n");
                        //call list function
                        return_clients(client_list);
                        int list_len = strlen(client_list);
                        struct header_C *list = pack_header(resp->dest,
                                                resp->source, CLIENT_LIST,
                                        (unsigned int)buff_size,0);
                        n = write(sock,(char *)list,sizeof(struct header_C));
                        if(n < 0) {
                                fprintf(stderr,"WRITE 1 LIST error\n");
                        }

                        if(list_len > 300) {
                                printf("we need to send multiple write calls\n");
                        }
                printf("list: %s\n",client_list); 
                        n = write(sock,client_list,list_len);
                        if(n < 0){
                                fprintf(stderr,"WRITE LIST ERR\n");
                        } 
                        ret_val = 1;
                        break;
                case 4: 
                        printf("is a list resp wait what?\n");
                        //this should never happen-- error handle?
                        break;
                case 5:
                        printf("is a chat req\n");
                        if(size > 300) {
                                printf("we need error handling for data bigger than 300\n");
                        } 
                        
                        struct cli_ID *ptr = CLIENTS->queue;
                        ret_val = 1;
                        if(ptr == NULL) {
                               // ret_val = 1;
                                break;
                        }
                        
                        while(ptr != NULL){
                                if(ptr->sockfd == sock) {
                                        ptr->mess_to_send = 1;   
                                        ptr->header = resp;
                                        printf("we will rec mess on next loop for this socket\n");
                                        break;
                                }
                                ptr = ptr->next;
                        }
                       // ret_val = 1;
                        break;
                case 6:
                        printf("is a exit req\n");
                        ret_val = 1;
                        break;
                case 7:
                        printf("is a cli already here error\n");
                        //error function TO DO
                        ret_val = 1;
                        break;
                case 8:
                        printf("is a cant deliver error\n");
                        //error_function TO DO
                        ret_val = 1;
                        break;
                }
        //memcpy(buffer,resp,sizeof(struct header_C));
       
        return ret_val;
}
void rec_message(struct header_C *resp, int sock) {                        
        char message[300];
        bzero(message,300);
        int bytes, bytes_read = 0;                        
        //do {
        bytes = call_read(sock, message,300);  
        bytes_read += bytes;
                       // }while(bytes_read < size);
              //  ret_val = 1;
        printf("received message: %s\n", message);                        
        struct cli_ID *ptr = CLIENTS->queue;
        if(ptr == NULL) {
                printf("rec_message needs error handling\n");
                return; //should this be an error condition?
        }
        int dest_sock;
        while(ptr != NULL){
                if(strcmp(resp->dest,ptr->ID)==0){
                        dest_sock = ptr->sockfd;
                        printf("We found the sock number!\n");
                        printf("%s\n",ptr->ID);
                        ++ptr->mess_in_queue;
                        break;
                }
                ptr = ptr->next;
        }
        printf("we need error handling for if ptr == NULL in rec_mess\n");
        printf(" dest sock %d check \n", dest_sock);
                       // struct header_C *mess = pack_header(resp->source,
                        //                        resp->dest, CHAT, size, id);
                      //  n = write(rec_sock,(char *)resp, sizeof(struct header_C));
                        //if(n < 0){
                        //        fprintf(stderr, "CHAT WRITE 1 error\n");
                       // }
                        //n = write(rec_sock,message,strlen(message));
                        //if(n < 0) {
                        //        fprintf(stderr,"CHAT WRITE MESS error\n");
                       // }
        queue_message(resp,message,dest_sock, ptr); 
}

void queue_message(struct header_C *resp, char *message, int sock, struct cli_ID *client){
        struct message_C *new_mess = malloc(sizeof(*new_mess));
        struct message_C *ptr;
        //memcpy(&new_mess->source, resp->source, strlen(resp->source));;
        //memcpy(&new_mess->dest, resp->dest, strlen(resp->dest));
        memcpy(&new_mess->data, message, strlen(message));
        new_mess->sockfd = sock; 
        new_mess->ID = resp->ID;
        new_mess->header = resp;
        new_mess->next = NULL;
        new_mess->client = client;
        if(MESSAGE_QUEUE->queue == NULL) {
                MESSAGE_QUEUE->queue = new_mess;
                MESSAGE_QUEUE->end = new_mess;
                MESSAGE_QUEUE->size = 1;      
        } else {
                ptr = MESSAGE_QUEUE->end;
                ptr->next = new_mess;
                MESSAGE_QUEUE->end = new_mess;
                ++MESSAGE_QUEUE->size;
        }
}
/*
 * Check to see if the last interaction the server had with client 
 * indicates that a message payload is coming in
 *
 */
struct cli_ID *check_for_message_in(int sock){
        printf("checking for in message...\n");
        if(CLIENTS->queue == NULL) {
                printf("its NULL\n");
                return NULL;
        } else {

                printf("blerghhh\n");
                struct cli_ID *ptr = CLIENTS->queue;
                while(ptr != NULL) {
                        printf("we in a while loop \n");
                        if(sock == ptr->sockfd) {
                                if(ptr->mess_to_send) {
                                        ptr->mess_to_send = 0;
                                        return ptr;
                                }
                        }
                        ptr = ptr->next;
                }
                return NULL;
        }
}
/*
 * Check for a message to send out from server 
 * to this client (from some other client)
 *
 */
int check_for_message_out(int sock){
        printf("checking for out message....\n");
        if(MESSAGE_QUEUE->queue == NULL) {
                return 0;
        }
        int n;
        struct message_C *ptr = MESSAGE_QUEUE->queue;
        
        if(sock == ptr->sockfd){
                printf("theres a message in the queue for you! Sending it now so you better be ready to read twice heck\n");
                struct header_C *send = ptr->header;
                n = write(sock,(char *)send,sizeof(struct header_C));
                if(n < 0){
                        fprintf(stderr,"MESSAGE HEADER WRITE error\n");
                        //more rror handling in case senderr
                        //wants to resend???
                }
                n = write(sock,ptr->data,strlen(ptr->data));
                if(n < 0){
                        fprintf(stderr,"MESSAGE BODY WRITE error\n");
                }
               
        
                --ptr->client->mess_in_queue;

                MESSAGE_QUEUE->queue = MESSAGE_QUEUE->queue->next;
                free(ptr);

                return 1;
        }
        return 0;
}

struct cli_ID *client_check(char *client_ID) {
       // int i = 1, j = CLIENTS->size;
       // struct cli_ID *ptr = get(client_ID);
        return get(client_ID);
       
        
}

void return_clients(char *cli_list) {
        int i = 1, j = CLIENTS->size, bytes = 0;
        struct cli_ID *ptr = CLIENTS->queue;
        if(ptr==NULL){
                char *empty_mess = "No currently connected clients\n";
                memcpy(cli_list, empty_mess, strlen(empty_mess));
                return;
        }
        while(i <= j && ptr != NULL) {
                if(ptr->active) {
                        memcpy(&cli_list[bytes],ptr->ID,strlen(ptr->ID));
                        bytes += strlen(ptr->ID);
                }
                ptr = ptr->next;
                ++i;
        }
        return;
}       

void put(struct cli_ID *new_cli) {
        int index = hash_fun(new_cli->ID);
        if(hashmap[index] == NULL) {
                hashmap[index] = new_cli;
        } else {
                printf("WE have a collision problem to resolve :(\n");
                exit(1);
        }

}
struct cli_ID *get(char *key) {
        int index = hash_fun(key);
        struct cli_ID *ptr = hashmap[index];
        if(ptr = NULL) {
                printf("hash table ... did we use the wrong key? %s %d\n", key, index);
                return NULL;
        
        }
        if(ptr->ID != key) {
                printf("hash table GET error\n");
                return NULL;
        }
        return ptr;
        
}
int hash_fun(char *key) {
        int i, j = strlen(key);
        unsigned long prime_num = 131071; //mersenne prim 2^17 - 1
        for(i = 0; i < j; ++i){
                prime_num += key[i];
                printf("%d\n", prime_num);
        }
        return (prime_num % 50);

}
void init_hashmap() {
        hashmap = (struct cli_ID **)calloc(50,sizeof(**hashmap));

}
void init_queue() {
        MESSAGE_QUEUE = malloc(sizeof(*MESSAGE_QUEUE));
        MESSAGE_QUEUE->queue = NULL;
        MESSAGE_QUEUE->size = 0;

        CLIENTS = malloc(sizeof(*CLIENTS));
        CLIENTS->queue = NULL;
        CLIENTS->size = 0;

}
