#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h> 
#include <sys/ioctl.h>
#include <sys/select.h>
#include <errno.h>

typedef enum message_type {HELLO = 1, HELLO_ACK, LIST_REQUEST, CLIENT_LIST, CHAT, EXIT, ERROR_CAP, ERROR_CD} Type_M;

struct __attribute__((__packed__)) message_C {
        short int type;
        char source[20];
        char dest[20];
        unsigned int len;
        unsigned int ID;
        //char *data;
};

struct message_C *pack_message(char *buffer, char *desti, Type_M mess_type, unsigned int size, unsigned int ID);
void parse_resp(struct message_C *resp, char *buffer, int sockfd);
void error(const char *msg);

int main(int argc, char *argv[])
{
        int sockfd, portno, n;
        struct sockaddr_in serv_addr;
        struct hostent *server;
        char name_buffer[256];
        char buffer[256];
        if (argc < 3) {
                fprintf(stderr,"usage %s hostname port\n", argv[0]);
                exit(0);
        }
        portno = atoi(argv[2]);
        sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) {
                error("ERROR opening socket");
        }
        //int on = 1;
       // int c;
       // c = setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR,(char *)&on,sizeof(on));
       // if(c < 0){
       //         fprintf(stderr,"SETSOCKOPT error: %d\n", errno);
       //         close(sockfd);
       //         exit(EXIT_FAILURE);
      //  }
      //  c = ioctl(sockfd,FIONBIO,(char *)&on);
      //  if(c<0){
        //        fprintf(stderr,"NONBLOCK error\n");
        //        close(sockfd);
       //         exit(EXIT_FAILURE);
       // }
        server = gethostbyname(argv[1]);
        if (server == NULL) {
                fprintf(stderr,"ERROR, no such host\n");
                exit(0);
        }
        bzero((char *) &serv_addr, sizeof(serv_addr));
        serv_addr.sin_family = AF_INET;
        bcopy((char *)server->h_addr, (char *)&serv_addr.sin_addr.s_addr, 
        server->h_length);
        serv_addr.sin_port = htons(portno);
        if (connect(sockfd,(struct sockaddr *) &serv_addr,sizeof(serv_addr)) 
                < 0) { 
                error("ERROR eeps connecting");
        }
        
        printf("Please enter your chat name: ");
        bzero(name_buffer,256);
        bzero(buffer,256);
        fgets(name_buffer,255,stdin);
        char *desti = "server";
        
        struct message_C *hi = pack_message(name_buffer, desti, HELLO, 0, 0); 
        n = write(sockfd,(char *)hi,sizeof(struct message_C));
        
        if (n < 0) {
                 error("ERROR writing to socket");
        }
        
        //n = read(sockfd,buffer,255);
        struct message_C *response = malloc(sizeof(*response));
        //if (n < 0) {
        //        error("ERROR reading from socket");
       // } 
        printf("%s\n",buffer);
        char send_to[20]; 
        char type[20];
        char *serv_check = "Server\n";
        char *type_check_chat = "CHAT\n";
        char *type_check_list = "LIST\n";
        char *type_check_exit = "EXIT\n";
        char wait[4];
        char *wait_check = "no\n";
        unsigned int id_curr = 1;
        char list_buffer[400];
        bzero(list_buffer,400);
        bzero(response,sizeof(struct message_C));
        bzero(buffer,255);
        n = read(sockfd,(char *)response,sizeof(struct message_C));
        parse_resp(response, buffer, sockfd);
        do {    
                struct message_C *gimme_list = pack_message(name_buffer, send_to, CHAT,0,0);
                n = write(sockfd,(char *)gimme_list,sizeof(struct message_C));                                
                if(n < 0) {
                        error("gimme list error\n");
                }
                bzero(response,sizeof(struct message_C));
                bzero(buffer,255);
                n = read(sockfd,(char *)response,sizeof(struct message_C));
                if(n < 0){
                        error("READ at end of while loop error\n");
                }
                if(n == 0) {
                        printf("Nothing to read, continue\n");
                        continue;
                } else {
                        parse_resp(response, buffer, sockfd);
                }
    } while(1);
        close(sockfd);
        return 0;
}

struct message_C *pack_message(char *name_buffer, char *desti, Type_M mess_type, unsigned int size, unsigned int ID){      
        struct message_C *header = malloc(sizeof(*header));
        header->type = htons(mess_type);
       // printf("message type %d %d\n", mess_type, htons(mess_type));
        memcpy(&header->source, name_buffer, strlen(name_buffer));
       // printf("source %s\n", header->source);
        memcpy(&header->dest, desti, strlen(desti));
      //  printf("destination %s\n", desti);
        switch(mess_type) {          
                case 4: 
              //          printf("is a list resp\n");
                        //pack_list(header, size);  
                        header->len = htonl(size);
                        header->ID = htonl(0);
                        break;
                case 5:
                //        printf("is a chat req\n");
                        header->len = htonl(size);
                        header->ID = htonl(ID);
                  //pack_chat(header, size, ID);
                        break;    
                case 8:
                  //      printf("is a cant deliver error\n");
                        header->ID = htonl(ID);
                        //    pack_error(header, ID);
                        break;
                default:
                        header->len = htonl(0);
                        header->ID = htonl(0);
                }
        //printf("test of functions: %d %d\n", header->len, header->ID);
        return header;
}

void parse_resp(struct message_C *resp, char *buffer, int sockfd) {
        Type_M type = ntohs(resp->type);
        printf("type check cli side: %d\n", type);
        bzero(buffer,255);
        int n;
        switch(type) {
                case 0:
                        printf("Server closed all connections\n");
                        //break;
                        exit(EXIT_FAILURE);
                case 1:
                        printf("its a hello\n");
                        break;
                case 2:
                        printf("its a hay ack\n");
                        break;
                case 3:
                        printf("Its a list req\n");
                        break;
                case 4: 
                        printf("is a list resp\n");
                        //bzero(buffer,255);
                        n = read(sockfd,buffer,resp->len);
                        printf("List: %s\n",buffer);
                        break;
                case 5:
                        printf("is a chat req\n");
                        n = read(sockfd,buffer,resp->len);
                        printf("Chat from %s: %s\n",resp->source, buffer);
                        break;
                case 6:
                        printf("is a exit req\n");
                        break;
                case 7:
                        printf("is a cli already here error\n");
                        exit(1);
                        break;
                case 8:
                        printf("is a cant deliver error\n");
                        break;
                }
        //memcpy(buffer,resp,sizeof(struct message_C));
       

}

void error(const char *msg)
{
    perror(msg);
        printf("errno %d\n", errno);
    exit(0);
}


