
#include <sys/socket.h>
#include <zconf.h>
#include <iostream>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include "whatsappio.h"

int initClient(char* clientName, char* serverAddress, unsigned short portNum){
    int clientSockfd;
    struct sockaddr_in sa;
    struct hostent *hp;

    char buffer[WA_MAX_INPUT];

    if ((hp = gethostbyname(serverAddress)) == nullptr) {
        print_error("gethostbyname", h_errno);
        exit(1);
    }

    memset(&sa,0,sizeof(struct sockaddr_in));    // fill this struct with zeros
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = INADDR_ANY;
    sa.sin_port = htons((u_short)portNum);

    if ((clientSockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        print_error("socket", errno);
        exit(1);
    }

    if (connect(clientSockfd, (struct sockaddr *)&sa , sizeof(sa)) < 0) {
        close(clientSockfd);
        print_error("connect", errno);
        exit(1);
    }

    //now we want to send our name to the server, and check if the connection succeed
    if (write(clientSockfd, clientName, strlen(clientName) + 1)<0){
        print_error("write", errno);
        exit(1);
    }

    if (read(clientSockfd, buffer, WA_MAX_INPUT) < 0){
        print_error("read", errno);
        exit(1);
    }

    if (strcmp(buffer, S_C_CONNECT_SUCCESS) == 0) {
        print_connection();
    }
    else if (strcmp(buffer, S_C_CONNECT_DUP) == 0){
        print_dup_connection();
        exit(1);
    }
    else{
        print_fail_connection();
        exit(1);
    }

    return clientSockfd;

}

int main(int argc, char* argv[]) {
    if (argc != 4) {
        print_client_usage();
        exit(1);
    }

    char* clientName = argv[1];
    char* serverAddress = argv[2];
    int serverPortNum = std::stoi(argv[3]);
    if (serverPortNum < 0){
        std::cout << "serverPort should be positive" << std::endl;
    }

    if (!isLettersOrDigits(clientName)){
        print_fail_connection();
        exit(1);
    }

    int clientSockfd = initClient(clientName, serverAddress, (unsigned short)serverPortNum);

    std::string clientNameStr = (clientName);
    fd_set clientsfds;  //all of the file descriptors (also those who not ready)
    fd_set readfds; // the file descriptors to be checked for being ready to read

    // init the set
    FD_ZERO(&clientsfds);
    FD_SET(clientSockfd, &clientsfds);
    FD_SET(STDIN_FILENO, &clientsfds);

    char buffer[WA_MAX_INPUT];
    bzero(buffer,WA_MAX_INPUT);
    std::vector<std::string> clientsVec;

    while (true) {
        readfds = clientsfds;
        if (select(3 + 1, &readfds, NULL, NULL, NULL) < 0) {
            print_error("select", errno);
            exit(1);
        }

        if (FD_ISSET(clientSockfd, &readfds)) {
            if (read(clientSockfd, buffer, WA_MAX_INPUT) < 0) {
                print_error("read", errno);
                exit(1);
            }

            //take care of EXIT from the server
            if (strcmp(buffer,"EXIT") == 0){
                close(clientSockfd);
                exit(1);
            }
            else {
                // the client got incoming messages
                printf("%s", buffer);
            }
            bzero(buffer,WA_MAX_INPUT);
        }

        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            if (read(STDIN_FILENO, buffer, WA_MAX_INPUT) < 0) {  //insert the msg into buf
                print_error("read", errno);
                exit(1);
            }

            std::string command = buffer;
            command = command.substr(0, command.size()-1);
            command_type  commandT;
            std::string name;
            std::string message;
            bool isValidName;

            parse_command(command, commandT, name, message, clientsVec);
            switch (commandT){
                case CREATE_GROUP:
                    if (((clientsVec.size() == 1) && (clientsVec.at(0) == clientNameStr)) || clientsVec.size() == 0){        // without members or less than 2 members. nothing is printed in server
                        print_create_group(false, false, "", name);
                        break;
                    }

                    if (!isLettersOrDigits(name)) {
                        print_create_group(false, false, "", name);
                        break;
                    }

                    // validate that the clients in clientsVec are valid:
                    isValidName = true;
                    for (const std::string &curClientName: clientsVec){
                        if (!isLettersOrDigits(curClientName)) {
                            print_create_group(false, false, "", name);
                            isValidName = false;
                            break;
                        }
                    }
                    if (!isValidName){
                        break;
                    }

                    if (write(clientSockfd, command.c_str(), command.size() + 1) < 0){
                        print_error("write", errno);
                        exit(1);
                    }
                    if (read(clientSockfd, buffer, WA_MAX_INPUT) < 0) {
                        print_error("read", errno);
                        exit(1);
                    }
                    print_create_group(false, strcmp(buffer, S_C_CREATE_GROUP_SUCCESS) == 0, "", name);
                    break;

                case SEND:
                    // the client send message by himself.
                    if (name == clientNameStr){ // can't send message to myself
                        print_send(false, false, "", "", "");
                        break;
                    }

                    if (write(clientSockfd, command.c_str(), command.size() + 1) < 0){
                        print_error("write", errno);
                        exit(1);
                    }
                    if (read(clientSockfd, buffer, WA_MAX_INPUT) < 0) {
                        print_error("read", errno);
                        exit(1);
                    }
                    print_send(false, strcmp(buffer, S_C_SEND_SUCCESS) == 0, "", "", "");
                    break;

                case WHO:
                    if (write(clientSockfd, command.c_str(), command.size() + 1) < 0){
                        print_error("write", errno);
                        exit(1);
                    }
                    if (read(clientSockfd, buffer, WA_MAX_INPUT) < 0) {
                        print_error("read", errno);
                        exit(1);
                    }
                    printf("%s",buffer);
                    break;

                case EXIT:
                    // Unregisters the client from the server and removes it from all groups.
                    if (write(clientSockfd, command.c_str(), command.size() + 1) < 0){
                        print_error("write", errno);
                        exit(1);
                    }
                    clientsVec.clear();
                    print_exit(false, "");
                    exit(0);

                case INVALID:
                    print_invalid_input();
                    break;
            }
            bzero(buffer,WA_MAX_INPUT);
        }

    }
    return 0;
}

