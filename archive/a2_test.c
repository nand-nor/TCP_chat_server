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

struct client_node {
        char *name; //client username
        int sockfd; //cliend socket file descriptor
        int active; //boolean to indicate if client is active
        int queue;
        struct header_C *header;
        struct message_C *messages;
        //struct client_node *next; //this is going to be a queue for now
                                //but I eventually want it to be a hash table
};      

struct message_C {
        struct header_C *header;  
        char data[300];
        struct message_C *next;
};

//struct cli_q {
//        struct client_node *queue;
//        int size;
//};

struct mess_q {
        struct message_C *queue;
        struct message_C *end;
        int size;
};

//struct mess_q *MESSAGE_QUEUE;
//struct cli_q *CLIENTS;  

struct client_node **hashmap;
struct client_node *SOCKETS[30];
/*
 * Function call definitions
 */

void call_socket(int *sock, int port, struct sockaddr_in *sock_in); 
void call_bind(int sock, struct sockaddr *sock_in, int sock_size); 
void call_listen(int sock, int num_conn); 
int call_accept(int master_sock, int *cli_sock, struct sockaddr *client_node, socklen_t *add_size); 
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
int client_check(char *client_ID);
void queue_message(struct header_C *resp, char *message, int sock, struct client_node *client);
int check_new_client(int sock);
int check_for_message_in(int sock);
int check_for_message_out(int sock);
void rec_message(int sock);
void put(struct client_node *new_cli);
struct client_node *get(char *key);
void init_queue();
void init_hashmap();
int hash_fun(char *key);

int main(int argc, char *argv[]){ 
        if(argc != 2){
                fprintf(stderr, "Usage: %s port", argv[0]);
                exit(EXIT_FAILURE);
        }        
        //init_queue();
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
                                        
                                
                                        if(!check_new_client(i)) {
                                               check_in = check_for_message_in(i);
                                                if(check_in){
                                                        printf("mess in not null, entering rec_message...\n"); 
                                                        rec_message(i);                                
                                                }
                                        
                                        check_out = check_for_message_out(i);
                                        printf("finished checking for message out...\n");
                                        if(check_out == 1 || check_in== 1) {
                                                printf("we sent the message, so continue\n");
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
                                        } else {
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
                                        if(read_next == 1){
                                                break;
                                        } else if(read_next == 2) {
                                                close_conn = 1;
                                                break;
                                        }
                                        //bzero(buffer,255);
                                }
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
        printf("test of pack_header functions: header type server side %d\n", ntohs(header->type));
        return header;
}       

int parse_resp(int sock, struct header_C *resp, char *buffer) {
        Type_M type = ntohs(resp->type);
        printf("parse_resp type check: %d %d\n", type, resp->type);
        printf("check: client %s has socket %d\n", resp->source, sock);

        unsigned int size = ntohl(resp->len);
        unsigned int id = ntohl(resp->ID);
         
        int rec_sock = -1;
        char client_list[400];
        printf("size and ID: %u %u\n", size, id);        
        int ret_val = 0, n;
        switch(type) { 
                case 1:
                        printf("its a hello\n");
                        ret_val = 1;             
                        if(client_check(resp->source)){
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
                        struct client_node *new_cli = malloc(sizeof(*new_cli));
                        new_cli->name = resp->source;
                        new_cli->active = 1;
                        new_cli->sockfd = sock;
                        new_cli->queue = 0;
                        new_cli->header = NULL;
                        new_cli->messages = NULL;
                        SOCKETS[sock] = new_cli;
                        put(new_cli);                        
                        
                        struct header_C *ack = pack_header(resp->dest,
                                                resp->source, HELLO_ACK,
                                                0,0);
                        n = write(sock,(char *)ack,sizeof(struct header_C));
                        if(n < 0){
                                fprintf(stderr,"WRITE ACK error\n");
                        }
                        break;
                case 2:
                        printf("its a hay ack error handle\n");
                        //this should happen-- error handle?
                        break;
                case 3:
                        printf("Its a list req\n");
                        //call list function
                        return_clients(client_list);
                        int list_len = strlen(client_list);
                        struct header_C *list = pack_header(resp->dest,
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
                        break;
                case 4: 
                        printf("is a list resp wait what? NEED TO ERROR HANDLE\n");
                        //this should never happen-- error handle?
                        break;
                case 5:
                        printf("is a chat req\n");
                        if(size > 400) {
                                printf("we need error handling for data bigger than 400\n");
                        } 
                        struct client_node *ptr = SOCKETS[sock];
                        ret_val = 1;
                        if(ptr == NULL) {
                                printf("Need error handling for NO SUCH CLIENT\n");
                                break;
                        }else {
                                printf("%s HAS A CHAT REQUEST COMING UP\n", ptr->name);
                        }
                        ptr->header = resp;                        
                        break;
                case 6:
                        printf("is a exit req\n");
                        ret_val = 2;
                        break;
                case 7:
                        printf("is a cli already here error ERROR HANDLE\n");
                        ret_val = 2;
                        break;
                case 8:
                        printf("is a cant deliver error ERROR HANDLE\n");
                        ret_val = 1;
                        break;
                }
        return ret_val;
}
void rec_message(int sock) {                        
        char message[400];
        bzero(message,400);
        int bytes;                        
        
        bytes = read(sock, message,400);  
        if(bytes == 0){
                printf("client is done\n");
        }else if(bytes < 0) {
                printf("error handle-- reading on socket %d resulted in %d\n", sock, errno);
        } 
        printf("received message: %s\n", message);                        
        struct client_node *ptr = SOCKETS[sock];       
        if(ptr == NULL) {
                printf("rec_message needs error handling -- sockets not saving client ppinters\n");
                return; //should this be an error condition?
        }
        struct header_C *resp = ptr->header;
        
        int dest_sock;
        struct client_node *cli_ptr = get(resp->dest);
        if(cli_ptr == NULL){
                printf("no such client? ERROR HANDLE!\n");
                return;
        }
        if(strcmp(resp->dest,cli_ptr->name)==0){
                dest_sock = cli_ptr->sockfd;
                printf("We found the sock number!\n");
                printf("%s sent a message to %s\n",ptr->name, cli_ptr->name); 
        }else {
                printf("rec_message error getting ptr to client_node\n");
                printf("name 1: %s name 2: %s\n", resp->dest, cli_ptr->name); 
       } 
        printf(" dest sock %d check \n", dest_sock);
        ptr->header = NULL;
        queue_message(resp,message,dest_sock, cli_ptr); 
}

void queue_message(struct header_C *resp, char *message, int sock, struct client_node *client){
        printf("we are queueing the message: %s\n", message);
        printf("the message is from %s and destined for: %s\n", resp->source, client->name);
        struct message_C new_mess;// = malloc(sizeof(*new_mess));
        struct message_C *ptr;
        memcpy(new_mess.data, message, strlen(message)); 
        new_mess.header = resp;
        new_mess.next = NULL;
        struct client_node *cli_ptr = get(resp->dest);
        printf("queue message test: %s %d\n", cli_ptr->name, cli_ptr->queue);
        if(cli_ptr->messages == NULL){
                printf("1 setting new message for %s's queue\n", cli_ptr->name);
                cli_ptr->messages = &new_mess;
                cli_ptr->queue = 1;

                if(cli_ptr->messages != NULL) {
                        printf("1Message check: %s\n", cli_ptr->messages->data);
                }
        } else {
                printf("1there is already a message in the queue for this client %s \n", client->name);
                ptr = cli_ptr->messages;
                while(ptr->next != NULL) {
                        printf("1updating message queue pointers...\n");
                        ptr = ptr->next;
                }
                ptr->next = &new_mess;
                ++cli_ptr->queue;
        }
}
/*
 * Check to see if the last interaction the server had with client 
 * indicates that a message payload is coming in
 *
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
                        return 0;
                } else {
                        return 1;
                }
        }
}
/*
 * Check for a message to send out from server 
 * to this client (from some other client)
 *
 */
int check_for_message_out(int sock){
        int n;
        struct client_node *ptr = SOCKETS[sock];
        if(ptr == NULL) {
                printf("message out ERROR handle\n");
                return 0;
        } else {
                printf("Client to check for outgoing messages: %s\n", ptr->name);
                if(ptr->messages != NULL) {
                        struct message_C *mess_ptr = ptr->messages;
                        while(mess_ptr != NULL && ptr->queue != 0){
                                printf("Messages to go out!\n");
                                printf("Message: %s\n", mess_ptr->data);
                                printf("name at socket: %s socket %d socket check %d\n", ptr->name, ptr->sockfd, sock);
                                n = write(sock,(char *)mess_ptr->header,sizeof(struct header_C));
                                n = write(sock, mess_ptr->data, strlen(mess_ptr->data));
                                if(n < 0) {
                                        printf("error writing message queue to connected client %s\n", ptr->name);
                                        break;
                                }
                                if(n == 0) {
                                        printf("Client done writing\n");
                                        break;
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
}

int check_new_client(int sock) {
        struct client_node *ptr = SOCKETS[sock];
        if(ptr == NULL) {
                return 1;
        } else {
                printf("Client name: %s\n", ptr->name);
                return 0;
        }
}

int client_check(char *client_ID) {
  //      int i;
    //    struct client_node *ptr;
        struct client_node *ptr = get(client_ID);
        if(ptr == NULL){

                return 0;
        } else {
                return 1;
        }
}

void return_clients(char *cli_list) {
        int i, bytes = 0;
        struct client_node *ptr; // = CLIENTS->queue;

        for(i = 3; i < 30; ++i) {
                ptr = SOCKETS[i];
                if(ptr != NULL) {
                        printf("socket i %d %s\n",i, ptr->name);
                        if(ptr->active) {
                                memcpy(&cli_list[bytes],ptr->name,strlen(ptr->name));
                                bytes += strlen(ptr->name);
                        }
                }       
        }
        return;
}       

void put(struct client_node *new_cli) {
        int index = hash_fun(new_cli->name);
        if(hashmap[index] == NULL) {
                hashmap[index] = new_cli;
        } else {
                printf("WE have a collision problem to resolve :(, %s\n", hashmap[index]->name );
                //exit(1);
        }

}


struct client_node *get(char *key) {
        int index = hash_fun(key);
        struct client_node *ptr = hashmap[index];
        if(ptr == NULL) {
                printf("hash table ... did we use the wrong key? %s %d\n", key, index);
                return NULL;
        
        }
        if(strcmp(ptr->name,key) != 0) {
                printf("hash table GET error\n");
                return NULL;
        }
        return ptr;
        
}
int hash_fun(char *key) {
        int i, j = strlen(key);
        unsigned long prime_num = 131071; //mersenne prim 2^17 - 1
        for(i = 0; i < j; ++i){
                prime_num = (prime_num << 3) + key[i] + j;
                printf("%d\n", prime_num);
        }
        return (prime_num % 50);

}

void init_hashmap() {
        hashmap = (struct client_node **)calloc(50,sizeof(**hashmap));

}

