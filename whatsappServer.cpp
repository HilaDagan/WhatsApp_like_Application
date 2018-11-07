
#include <sys/socket.h>
#include <zconf.h>
#include <iostream>
#include <netinet/in.h>
#include <netdb.h>
#include <map>
#include <algorithm>
#include <signal.h>
#include <csignal>
#include "whatsappio.h"


std::map<std::string, std::vector<std::string>> groupToClient, clientToGroup;
std::map<std::string, int> clientsTofd; // a dictionary with client names as keys and fd as values.
int serverSockfd;

int establish(unsigned short portNum) {
    struct sockaddr_in sa;

    //sockaddrr_in initlization
    memset(&sa, 0, sizeof(struct sockaddr_in));  // fill this struct with zeros
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = INADDR_ANY;
    sa.sin_port= htons((u_short)portNum);  // this is our port number

    if ((serverSockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {  // create socket
        print_error("socket", errno);
    }

    if (bind(serverSockfd, (struct sockaddr *)&sa, sizeof(struct sockaddr_in)) < 0){
        close(serverSockfd);
        print_error("bind", errno);
    }

    if (listen(serverSockfd, 10) < 0){  //max 10 requests can wait on the queue
        print_error("listen", errno);
    }
    return 0;
}

/**
 * Connect the server to a new client.
 * @param serverSockfd file descriptor
 * @return -1 if fails, otherwise the connectionSockfd.
 */
int connectNewClient(int serverSockfd) {
    int connectionSockfd;
    char clientName[WA_MAX_NAME + 1];
    bzero(clientName, WA_MAX_NAME);
    if((connectionSockfd = accept(serverSockfd, nullptr, nullptr)) < 0)
    {
        print_error("accept", errno);
    }

    if (read(connectionSockfd, clientName, WA_MAX_NAME) < 0) {  //insert the msg into buf
        print_error("read", errno);
    }

    // validate that the client name is not exist in the clients:
    if (clientsTofd.find(clientName) != clientsTofd.end()) {
        if (write(connectionSockfd, S_C_CONNECT_DUP, strlen(S_C_CONNECT_DUP)+1) == -1)
        {
            print_error("write", errno);
        }
        return -1;
    }

    // validate that the client name is not exist in the groups:
    if (groupToClient.find(clientName) != groupToClient.end()) {
        if (write(connectionSockfd, S_C_CONNECT_DUP, strlen(S_C_CONNECT_DUP)+1) == -1)
        {
            print_error("write", errno);
        }
        return -1;
    }

    // the name is legal so we need to update our databases
    // and write to the client
    if (write(connectionSockfd, S_C_CONNECT_SUCCESS, strlen(S_C_CONNECT_SUCCESS)+1) == -1)
    {
        print_error("write", errno);
            exit(1);
    }
    clientsTofd[clientName] = connectionSockfd;
    print_connection_server(clientName);
    return connectionSockfd;
}

/**
 * Handle the create_group command.
 * @param groupName the name of the new group.
 * @param curGroupClientsVec  the names of the clients in this group.
 * @return true if success, otherwise false.
 */
const char* handleCreateGroup(std::string& groupName, std::string& clientName,
                        std::vector<std::string>& curGroupClientsVec) {

    if ((groupToClient.find(groupName) != groupToClient.end()) ||
        clientsTofd.find(groupName) != clientsTofd.end()) {
        // the group name is already exist as a group or a client.
        print_create_group(true, false, clientName, groupName);
        return S_C_CREATE_GROUP_FAIL;
    }

    // validate that the clients in clientsVec are exist:
    for (const std::string &curClientName: curGroupClientsVec){
        if (clientsTofd.find(curClientName) == clientsTofd.end()) {
            print_create_group(true, false, clientName, groupName);
            return S_C_CREATE_GROUP_FAIL;
        }
    }

    // remove duplicates:
    std::sort( curGroupClientsVec.begin(), curGroupClientsVec.end());
    curGroupClientsVec.erase(std::unique(curGroupClientsVec.begin(), curGroupClientsVec.end()),
                             curGroupClientsVec.end() );

    // add the clients to the group.
    groupToClient[groupName] = curGroupClientsVec;
    // add the client that created the group to the group (if doesnt' exist already)
    if (std::find(curGroupClientsVec.begin(), curGroupClientsVec.end(), clientName) == curGroupClientsVec.end() ){
        groupToClient[groupName].push_back(clientName);
    }

    // add the group to each of its clients.
    for (const std::string &curClientName: groupToClient[groupName]){
        clientToGroup[curClientName].push_back(groupName);
    }
    print_create_group(true, true, clientName, groupName);
    return S_C_CREATE_GROUP_SUCCESS;
}

const char* handleSend(std::string& name, std::string& curClientName, std::string& message,
                       std::vector<std::string>& relevantGroupVec) {
    std::string res;
    if (clientsTofd.find(name) != clientsTofd.end()){ // name is a client name.
        int cur_fd = clientsTofd[name];
        res = curClientName;
        res.append(": ").append(message).append("\n");
        if (write(cur_fd, res.c_str(), res.size() + 1) < 0){
            print_error("write", errno);
        }
        print_send(true, true, curClientName, name, message);
        return S_C_SEND_SUCCESS;
    }

    else if (groupToClient.find(name) != groupToClient.end()) { // name is a group name.
        relevantGroupVec = groupToClient[name];
        // validate that the client name is a group member:
        if (std::find(relevantGroupVec.begin(), relevantGroupVec.end(), curClientName) == relevantGroupVec.end()) {
            print_send(true, false, curClientName, name, message);
            return S_C_SEND_FAIL;
        }
        // send message to all of the group friends (not to the sender
        for (const std::string &clientName: relevantGroupVec) {
            std::cout << clientName << std::endl;
            if (clientName != curClientName) {
                int cur_fd = clientsTofd[clientName];
                res = curClientName;
                res.append(": ").append(message).append("\n");
                if (write(cur_fd, res.c_str(), res.size() + 1) < 0) {
                    print_error("write", errno);
                }
            }
        }
        print_send(true, true, curClientName, name, message);
        return S_C_SEND_SUCCESS;
    }
    else{
        print_send(true, false, curClientName, name, message);
        return S_C_SEND_FAIL;
    }
}

/**
 * Close the server.
 * Called when EXIT is typed or when there is sig-interrupt.
 */
void serverExit() {
    for (std::pair<std::string, int> curClient: clientsTofd) {
        if (write(curClient.second, "EXIT", strlen("EXIT") + 1) < 0) {
            print_error("write", errno);
        }
    }

    // clear the containers:
    clientsTofd.clear();
    for (std::pair<std::string, std::vector<std::string>> curClient: clientToGroup) {
        curClient.second.clear();
        std::vector<std::string>().swap(curClient.second);
    }

    for (std::pair<std::string, std::vector<std::string>> curGroup: groupToClient) {
        curGroup.second.clear();
        std::vector<std::string>().swap(curGroup.second);
    }
    clientToGroup.clear();
    groupToClient.clear();
    close(serverSockfd);
    print_exit();
    exit(0);
}

/**
 * Send message of exit() to all the clients and releas and close the server.
 */
void exitAll(int signum){
    serverExit();
}

/**
 * The main function: creates the server and manage the sockets.
 */
int main(int argc, char* argv[]) {
    std::signal(SIGINT, exitAll);

    if (argc != 2) {
        print_server_usage();
        exit(1);
    }

    int portNum = std::stoi(argv[1]); // a port number that the server listens to.
    if (portNum < 0) {
        std::cout << "serverPort should be positive" << std::endl;
    }

    establish((unsigned short) portNum); // the socket the the server listens to.

    fd_set readfds; // the file descriptors to be checked for being ready to read
    std::vector<int>::iterator it;

    char buffer[WA_MAX_INPUT];
    bzero(buffer,WA_MAX_INPUT);
    int maxSockfd, curSockfd, curClientfd;
    std::string curClientName;

    while (true) {
        // initialize readfds
        FD_ZERO(&readfds);
        FD_SET(serverSockfd, &readfds);
        FD_SET(STDIN_FILENO, &readfds);
        maxSockfd = serverSockfd;

        for (std::pair<std::string, int> client : clientsTofd) {
            curSockfd = client.second;
            FD_SET(curSockfd, &readfds);

            if (curSockfd > maxSockfd)
                maxSockfd = curSockfd;
        }

        if (select(maxSockfd + 1, &readfds, NULL, NULL, NULL) < 0) {
            print_error("select", errno);
        }

        if (FD_ISSET(serverSockfd, &readfds)) {
            connectNewClient(serverSockfd); //this func do accept, read the connection msg protocol, write connection succeed or not msg to the client' and print the
        }

        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            if (read(STDIN_FILENO, buffer, WA_MAX_INPUT) < 0) {
                print_error("read", errno);
            }
            if (strcmp(buffer, "EXIT\n") == 0) {
                serverExit();
            }
            else {  //EXIT is the only valid input
                print_invalid_input();
            }
            bzero(buffer,WA_MAX_INPUT);
        }

        //will check each client if it’s in readfds
        //and then receive a message from him
        for (std::pair<std::string, int> client : clientsTofd) {
            curClientName = client.first;
            curClientfd = client.second;
            if (FD_ISSET(curClientfd, &readfds)) {
                if (read(curClientfd, buffer, WA_MAX_INPUT) < 0) {
                    print_error("read", errno);
                }

                std::string command = buffer;
                command = command.substr(0, command.size());
                command_type commandT;
                std::string name, message;
                std::vector<std::string> clientsVec, curGroupClientsVec;
                parse_command(command, commandT, name, message, clientsVec);

                switch (commandT) {
                    case CREATE_GROUP: {
                        const char* res = handleCreateGroup(name, curClientName, clientsVec);
                        // tell the client the result.
                        if (write(curClientfd, res, strlen(res) + 1) < 0) {
                            print_error("write", errno);
                        }
                    }
                        break;

                    case SEND: {
                        const char* res = handleSend(name, curClientName, message, clientsVec);
                        if (write(curClientfd, res, strlen(res) + 1) < 0) {
                            print_error("write", errno);
                        }
                    }
                        break;

                    case WHO: {
                        std::string clientList;
                        int j = 0;
                        for (std::map<std::string, int>::iterator it = clientsTofd.begin(); it != clientsTofd.end(); it++) {
                            if (j != 0) {
                                clientList.append(",");
                            }
                            clientList.append(it->first);
                            j++;
                        }
                        clientList.append("\n");

                        if (write(curClientfd, clientList.c_str(), clientList.size() + 1) < 0) {
                            print_error("write", errno);
                        }
                        print_who_server(curClientName);
                        break;
                    }

                    case EXIT: {
                         // Unregisters the client from the server and removes it from all groups.
                        // deletes the client from all his groups:
                        for (std::string groupName: clientToGroup[curClientName]){  //all the names of the groups of the deleted client
                            groupToClient[groupName].erase(std::remove(groupToClient[groupName].begin(),
                                                                       groupToClient[groupName].end(), curClientName),
                                                           groupToClient[groupName].end());

                            if (groupToClient[groupName].size() == 1){
                                std::string last_name = groupToClient[groupName].at(0);
                                groupToClient.erase(groupName); //delete the group of 1 member
                                std::vector<std::string> groupsVec = clientToGroup[last_name];
                                groupsVec.erase(std::remove(groupsVec.begin(), groupsVec.end(), groupName),
                                                groupsVec.end());   // delete the group from the list of groups of the last member
                            }
                        }
                        clientToGroup.erase(curClientName);
                        clientsTofd.erase(curClientName);
                        print_exit(true, curClientName);
                        break;

                    }
                    case INVALID:
                        // Shouldn't get here.
                        break;
                }
                bzero(buffer,WA_MAX_INPUT);
            }
        }
    }
}