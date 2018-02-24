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

//struct cli_q {
//        struct cli_ID *queue;
//        int size;
//};
struct mess_q {
        struct message_C *queue;
        struct message_C *end;
        int size;
};

struct mess_q *MESSAGE_QUEUE;
//struct cli_q *CLIENTS;  


struct cli_ID **hashmap;

struct socket {
        char client[20];
};        
struct socket socket_client_pairs[30];
struct client_node *SOCKETS[30];
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
int check_for_message_in(char *client_id);
int check_for_message_out(int sock);
void rec_message(char *client_id);
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
        call_listen(master_socket, 20); 
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
                check_write, check_in = 0, check_out = 0, new_cli; 
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
                                        struct socket *ptr = &socket_client_pairs[i];
                                        if (ptr != NULL) {
                                                printf("client %s\n", ptr->client);
                                                
                                                check_in = check_for_message_in(ptr->client);
                                                if(check_in) {
                                                
                                                        printf("we have a message to read from the client\n");        
                                                        rec_message(ptr->client);                                
                                                }
                                        }
                                        
                                        check_out = check_for_message_out(i);
                                        printf("finished checking for message out...\n");
                                        if(check_in == 1 ) {
                                                printf("server sent the messages in the queue for this socket...\n");
                                               read_next = 1;
                                                //close_conn = 1;
                                                break;
                                        }
                                        
                                        if(check_out == 1) {
                                                printf("we received a message and checked the queue for any outgoing messages, and sent them accordingly.\n");
                                                read_next = 1;
                                               break;
                                        }
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
                                                break;
                                          } 
                                        if(check_read == 0){
                                                printf(" connection closed\n");
                                                close_conn = 1;
                                                read_next = 1;
                                                break;
                                        }
                                        
                        printf(" %d bytes received\n", check_read);
                        printf("source: %s\n", incoming->source);
                        printf("destination %s\n", incoming->dest); 
                                     
                                    
                                read_next = parse_resp(i, 
                                                        incoming, buffer); 
                                       if(read_next = 1) {
                                                break;
                                        } 
        
                                        if(read_next == 1){
                                                break;
                                        } else if(read_next == 2) {
                                                close_conn = 1;
                                                break;
                                        } 
                                } while (read_next == 0 && close_conn == 0); 
                                        if(close_conn) {
                                                printf("closin the conn...removing name from hash table and from cli_sock pairs...could not free lets see what this does?!\n"); 
                                               close(i);
                                                struct socket *ptr = &socket_client_pairs[i];
                                                struct cli_ID *cli_ptr = get(ptr->client);
                                                ptr = NULL;
                                                cli_ptr = NULL;
                                                //free(cli_ptr);
                                                //free(ptr);
                                                
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

int parse_resp(int sock, struct header_C *resp, char *buffer) {
        Type_M type = ntohs(resp->type); 
        unsigned int size = ntohl(resp->len);
        unsigned int id = ntohl(resp->ID);
        int buff_size = 400, rec_sock = -1;
        char client_list[buff_size+1];        
        int ret_val = 0, n;
        switch(type) { 
                case 1:
                        printf("its a hello\n");
                        ret_val = 1; 
                        printf("name check: %s\n", resp->source);          
                        struct cli_ID *cli_ptr = client_check(resp->source);
                        if(cli_ptr != NULL){
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
        
                        put(new_cli);                        
                        struct socket *sock_ptr = &socket_client_pairs[sock];
                        memcpy(&sock_ptr->client, resp->source,strlen(resp->source));
                        
                       // if(CLIENTS->queue == NULL) {
                       //         CLIENTS->queue = new_cli;
                       //         ++CLIENTS->size;
                       // }else {
                       //         struct cli_ID *ptr = CLIENTS->queue;
                      //          new_cli->next = ptr;
                      //          CLIENTS->queue = new_cli;
                      //          ++CLIENTS->size; 
                       // }
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
                        printf("Its a list req\n");
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
                        printf("list serverside check: %s\n",client_list); 
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
                        if(size > 400) {
                                printf("we need error handling for data > 300\n");
                               ret_val = 2;
                                 break;
                        } 
                        
                        struct cli_ID *ptr_src = get(resp->source);
                        struct cli_ID *ptr_dest = get(resp->dest);

                        //CLIENTS->queue;
                        ret_val = 1;
                        if(ptr_src == NULL) {
                               // ret_val = 1;
                                printf("error handle for no such source\n");
                                ret_val = 2;
                                break;
                        } else if (ptr_dest == NULL) {
                                printf("error handle for no such dest\n");
                                ret_val = 2;
                                break;
                        }
                        
                        ++ptr_src->mess_to_send;                        
                        ptr_src->header = resp;
                        ++ptr_dest->mess_in_queue;
                        printf("dest ptr %s has %d messages in the queue\n", ptr_dest->ID, ptr_dest->mess_in_queue);
                        break;
                case 6:
                        printf("is a exit req\n");
                        ret_val = 2;
                        break;
                case 7:
                        printf("is a cli already here error\n");
                        //error function TO DO
                        ret_val = 2;
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
void rec_message(char *client_id) { 
        struct cli_ID *ptr_src = get(client_id);
        char message[400];
        bzero(message,400);
        int check_read;                        
        check_read = read(ptr_src->sockfd, message,400);  
        if(check_read < 0){
                if(errno != EWOULDBLOCK) {
                        fprintf(stderr, " RECV error handle ungraceful \
                                exit needs function\n");
                                                         //close_conn = 1; 
                } 
                return;
        } else if (check_read == 0) {
                printf("client is done...did they exit?\n");
                return;

        }
        printf("received message: %s\n", message);                        
        struct cli_ID *ptr_dest = get(ptr_src->header->dest);
        if(ptr_dest == NULL) {
                printf("rec_message no such client needs error handling\n");
                return; //should this be an error condition?
        } 
        printf("NOte to self: need error handling for message greater than 300 bytes in rec_mess\n");
        printf("source: %s dest %s\n", ptr_src->ID, ptr_dest->ID);
        queue_message(ptr_src->header,message,ptr_dest->sockfd, ptr_dest); 
}

void queue_message(struct header_C *resp, char *message, int sock, struct cli_ID *client){
        struct message_C *new_mess = malloc(sizeof(*new_mess));
        struct message_C *ptr; 

        printf("the message in queue_message... %s\n", message);        

        memcpy(&new_mess->data, message, strlen(message));
        new_mess->sockfd = sock; 
        memcpy(&new_mess->ID,client->ID,strlen(client->ID));
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
 * THIS IS GROSS!! Figure out how to get this to work with the hashmap
 * should the socketfd be the key, and not the client name???
 */
int check_for_message_in(char *client_id){
        printf("checking for in message...\n");
        struct cli_ID *ptr = get(client_id);
        if(ptr == NULL) {
                printf("its NULL, no incoming messages\n");
                return 0;
        } else {
                if(ptr->mess_to_send > 0) {
                        return 1;
                }else {
                        return 0;
                } 
        }
}
/*
 * Check for a message to send out from server 
 * to this client (from some other client)
 * THIS NEEDS WORK! It should go through the whole queue
 */
int check_for_message_out(int sock){
        printf("checking for out message....\n");
        if(MESSAGE_QUEUE->queue == NULL) {
                return 0;
        }
        struct socket *sock_ptr = &socket_client_pairs[sock];
        struct cli_ID *cli_ptr;
        if(sock_ptr != NULL){
                printf("client in socket struct: %s\n", sock_ptr->client);

                cli_ptr = get(sock_ptr->client);
                if(cli_ptr != NULL){
                        printf("checkin to see if client has message in queue... note to self we gotta check that this is appropriately set!\n");
                        if(cli_ptr->mess_in_queue != 0){
                                printf("How many messages? %d\n", cli_ptr->mess_in_queue);
        
        int i, j = MESSAGE_QUEUE->size, n, ret_val = 0;
        struct message_C *ptr = MESSAGE_QUEUE->queue;
        for(i = 0; i < j && ptr != NULL; ++i){
                if(ptr->sockfd == sock) {
                        printf("socket better be ready to write!\n");
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
                        --cli_ptr->mess_in_queue;
                        ret_val = 1;
                }
                ptr = ptr->next;
        }
        
        return ret_val;
                        } else {
                                printf("No messages\n");
                                return 0;
                        }
                } else {
                        return 0;
                }
        } else {
                return 0;
        }
      /*
        printf("This needs work-- it should go through the whole queue\n");
        if(sock == ptr->sockfd){
                printf("theres a message in the queue for this socket!\n");
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
                //ptr->client = NULL; //hope this does not drop the cli_ID
                MESSAGE_QUEUE->queue = MESSAGE_QUEUE->queue->next;
                //this might cause problems...
                free(ptr);
                //figure out how to free these after the message has been served,
                //without 
                return 1;
        }
        return 0;
*/
}


//isnt this  aduplicate of get??
struct cli_ID *client_check(char *client_ID) {
        struct cli_ID *ptr = get(client_ID);
       if(ptr != NULL) {
                return ptr;
        } else {
                return NULL;  
       }
}

void return_clients(char *cli_list) {
        int i = 1, j = 30, bytes = 0; 
        struct socket *ptr;
        for(i = 1; i <= j; ++i) {
                ptr = &socket_client_pairs[i];
                if(ptr != NULL) {
                        struct cli_ID *cli_ptr = get(ptr->client);
                        if(cli_ptr == NULL) {
                                continue;
                        } else {
                                printf("in loop %s\n",cli_ptr->ID);
                        }
                        if(cli_ptr->active) {
                                memcpy(&cli_list[bytes],ptr->client,strlen(ptr->client));
                                bytes += strlen(ptr->client);
                        }
                } else {
                        continue;
                }
        }

        printf("return clients test: %s\n", cli_list);
        return;
}       

void put(struct cli_ID *new_cli) {
        int index = hash_fun(new_cli->ID);
        if(hashmap[index] == NULL) {
                hashmap[index] = new_cli;
        } else {
                printf("WE have a collision problem to resolve :( %s\n", hashmap[index]->ID);
               // exit(1);
        }

}
struct cli_ID *get(char *key) {
        int index = hash_fun(key);
        struct cli_ID *ptr = hashmap[index];
        if(ptr == NULL) {
                //printf("hash table ... did we use the wrong key? %s %d\n", key, index);
                return NULL;
        }
        if(strcmp(ptr->ID,key) != 0) {
        //        printf("hash table GET  error key %s ptr->ID %s\n", key, ptr->ID);
                return NULL;
        }
        return ptr;
        
}
//think through prime num, hash func, and size of modulo and table...
int hash_fun(char *key) {
        int i, j = strlen(key);
        unsigned long prime_num = 131071; //mersenne prim 2^17 - 1
        for(i = 0; i < j; ++i){

                prime_num = ((prime_num << 1) + key[i] % 2) + j;
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

        //CLIENTS = malloc(sizeof(*CLIENTS));
        //CLIENTS->queue = NULL;
        //CLIENTS->size = 0;

        bzero(socket_client_pairs,(30* sizeof(struct socket)));

        //socket_client_pairs = malloc(sizeof(20 * struct socket));
}
