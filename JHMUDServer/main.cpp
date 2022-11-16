#include <chrono>
#include <condition_variable>
#include <cstring>
#include <functional>
#include <hiredis/hiredis.h>
#include <iostream>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <stdlib.h>
#include <sstream>
#include <thread>

#include "Slime.h"
#include "User.h"


#define NOMINMAX
#pragma comment(lib, "Ws2_32.lib")
#include <WinSock2.h>
#include <WS2tcpip.h>


using namespace std;

static const unsigned short SERVER_PORT = 27015;
static const int NUM_WORKER_THREADS = 10;

redisContext* c = redisConnect("127.0.0.1", 6379);
redisReply* reply;

list<shared_ptr<Slime>> slimes;




class Client {
public:
    SOCKET sock;  // 이 클라이언트의 active socket

    atomic<bool> doingRecv;

    Client(SOCKET sock) : sock(sock), doingRecv(false) {
    }

    ~Client() {
        reply = (redisReply*)redisCommand(c, "EXPIRE USER:%s 300 ", user->getNickname());
        freeReplyObject(reply);
        cout << "Client destroyed. Socket: " << sock << endl;
    }
    
    shared_ptr<User> user =NULL;
};


map<SOCKET, shared_ptr<Client> > activeClients;
mutex activeClientsMutex;

queue<shared_ptr<Client> > jobQueue;
mutex jobQueueMutex;
condition_variable jobQueueFilledCv;

//send하는 함수
int sendMessage(const char* message, SOCKET sock)
{
    int r = 0;
    int dataLen = strlen(message);
    int dataLenNetByteOrder = htonl(dataLen);
    int offset = 0;
  
    while (offset < 4) {
        r = send(sock, ((char*)&dataLenNetByteOrder) + offset, 4 - offset, 0);
        if (r == SOCKET_ERROR) {
            std::cerr << "failed to send length: " << WSAGetLastError() << std::endl;
            return 1;
        }
        offset += r;
    }

    char data[1000];
    strcpy(data, message);
    offset = 0;
    while (offset < dataLen) {
        r = send(sock, data + offset, dataLen - offset, 0);
        if (r == SOCKET_ERROR) {
            std::cerr << "send failed with error " << WSAGetLastError() << std::endl;
            return 1;
        }
        std::cout << "Sent " << r << " bytes" << std::endl;
        offset += r;
    }
    return 0;
}

//recv하는 함수
shared_ptr<char> recvMessage(SOCKET sock)
{
    int dataLenNetByteOrder;
    int offset = 0;
    while (offset < 4) {
        int r = recv(sock, ((char*)&dataLenNetByteOrder) + offset, 4 - offset, 0);
        if (r == SOCKET_ERROR) {
            cerr << "recv failed with error " << WSAGetLastError() << endl;
            return nullptr;
        }
        else if (r == 0) {
            cerr << "Socket closed: " << sock << endl;
            return nullptr;
        }
        offset += r;
    }
    int dataLen = ntohl(dataLenNetByteOrder);
    cout << "[" << sock << "] Received length info: " << dataLen << endl;

    shared_ptr<char> buf(new char[dataLen+1]);
    offset = 0;
    while (offset < dataLen) {
        int r = recv(sock, buf.get() + offset, dataLen - offset, 0);
        if (r == SOCKET_ERROR) {
            cerr << "recv failed with error " << WSAGetLastError() << endl;
            return nullptr;
        }
        else if (r == 0) {
            return nullptr;
        }
        cout << "[" << sock << "] Received " << r << " bytes" << endl;
        offset += r;
    }
    buf.get()[dataLen] = '\0';
    return buf;
}


//로그인 시도 하는 함수
bool doLogin(shared_ptr<Client> client, SOCKET activeSock,shared_ptr<char> buf) {
 
    srand((unsigned int)time(NULL));
    //redis에 만료되었꺼나 처음로그인하는 아이디면 캐릭터를 새로 생성한다.
    reply = (redisReply*)redisCommand(c, "EXISTS USER:%s", buf.get());
    if (reply->type == REDIS_REPLY_ERROR) {
        printf("error");
        return false;
    }
    if(reply->integer == 0) 
    {
        freeReplyObject(reply);
        client->user = shared_ptr<User>(new User(buf.get()));
    }                  
    else  //남아있는 데이터가 있을 시 그 데이터대로 초기화 한다.
    { 

        freeReplyObject(reply);
        char* _nickname = buf.get();
        reply = (redisReply*)redisCommand(c, "GET USER:%s:X", buf.get());
        int _x = stoi(reply->str);
        freeReplyObject(reply);
        reply = (redisReply*)redisCommand(c, "GET USER:%s:Y", buf.get());
        int _y = stoi(reply->str);
        freeReplyObject(reply);
        reply = (redisReply*)redisCommand(c, "GET USER:%s:HP", buf.get());
        int _hp = stoi(reply->str);
        freeReplyObject(reply);
        reply = (redisReply*)redisCommand(c, "GET USER:%s:STR", buf.get());
        int _str = stoi(reply->str);
        freeReplyObject(reply);
        reply = (redisReply*)redisCommand(c, "HGET USER:%s:POTIONS HP", buf.get());
        int _numOfHpPotion = stoi(reply->str);
        freeReplyObject(reply);
        reply = (redisReply*)redisCommand(c, "HGET USER:%s:POTIONS STR", buf.get());
        int _numOfStrPotion = stoi(reply->str);
        freeReplyObject(reply);

        client->user = shared_ptr<User>(new User(_nickname,_x,_y,_hp,_str,_numOfHpPotion,_numOfStrPotion));

    }
 
    //redis에 만들어진 데이터르 등록한다.
    reply = (redisReply*)redisCommand(c, "SET USER:%s 1", buf.get());
    freeReplyObject(reply);
    reply = (redisReply*)redisCommand(c, "SET USER:%s:HP %d", buf.get(), client->user->getHp());
    freeReplyObject(reply);
    reply = (redisReply*)redisCommand(c, "SET USER:%s:STR %d", buf.get(), client->user->getStr());
    freeReplyObject(reply);
    reply = (redisReply*)redisCommand(c, "SET USER:%s:X %d", buf.get(), client->user->getX());
    freeReplyObject(reply);
    reply = (redisReply*)redisCommand(c, "SET USER:%s:Y %d", buf.get(), client->user->getY());
    freeReplyObject(reply);
    reply = (redisReply*)redisCommand(c, "HSET USER:%s:POTIONS HP %d", buf.get(), client->user->getNumOfHpPotion());
    freeReplyObject(reply);
    reply = (redisReply*)redisCommand(c, "HSET USER:%s:POTIONS STR %d", buf.get(), client->user->getNumOfStrPotion());
    freeReplyObject(reply);
    reply = (redisReply*)redisCommand(c, "PERSIST USER:%s", buf.get());
    freeReplyObject(reply);

    //캐릭터 생성 정보를 보여준다.
    sendMessage(client->user->PrintUserInformation(), activeSock);

    return true;
}



SOCKET createPassiveSocket() {
    SOCKET passiveSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (passiveSock == INVALID_SOCKET) {
        cerr << "socket failed with error " << WSAGetLastError() << endl;
        return 1;
    }

    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(SERVER_PORT);
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);

    int r= ::bind(passiveSock, (sockaddr*)&serverAddr, sizeof(serverAddr));
    if (r == SOCKET_ERROR) {
        cerr << "bind failed with error " << WSAGetLastError() << endl;
        return 1;
    }

    r = listen(passiveSock, 10);
    if (r == SOCKET_ERROR) {
        cerr << "listen faijled with error " << WSAGetLastError() << endl;
        return 1;
    }

    return passiveSock;
}


bool processClient(shared_ptr<Client> client) {
    SOCKET activeSock = client->sock;
   
        while (true)
        {
            shared_ptr<char> buf;
            if ((buf=recvMessage(activeSock)) == nullptr) { return false; }

            if (client->user == NULL) {
                if (doLogin(client,activeSock,buf) == false) { return false; }
            }
        }
        return true;
}






void workerThreadProc(int workerId) {

    while (true) {
        shared_ptr<Client> client;
        {
            unique_lock<mutex> ul(jobQueueMutex);

            while (jobQueue.empty()) {
                jobQueueFilledCv.wait(ul);
            }

            client = jobQueue.front();
            jobQueue.pop();

        }

        if (client) {
            SOCKET activeSock = client->sock;
            bool successful = processClient(client);
            if (successful == false) {
                closesocket(activeSock);
                {
                    lock_guard<mutex> lg(activeClientsMutex);

                    activeClients.erase(activeSock);
                }
            }
            else {

                client->doingRecv.store(false);
            }
        }
    }
}






int main()
{

    int r = 0;

    
    WSADATA wsaData;
    r = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (r != NO_ERROR) {
        cerr << "WSAStartup failed with error " << r << endl;
        return 1;
    }

  
    SOCKET passiveSock = createPassiveSocket();

    list<shared_ptr<thread> > threads;
    for (int i = 0; i < NUM_WORKER_THREADS; ++i) {
        shared_ptr<thread> workerThread(new thread(workerThreadProc, i));
        threads.push_back(workerThread);
    }

  
    while (true) {

        fd_set readSet, exceptionSet;

        FD_ZERO(&readSet);
        FD_ZERO(&exceptionSet);

        SOCKET maxSock = -1;

        FD_SET(passiveSock, &readSet);
        FD_SET(passiveSock, &exceptionSet);
        maxSock = max(maxSock, passiveSock);

        for (auto& entry : activeClients) {
            SOCKET activeSock = entry.first;
            shared_ptr<Client> client = entry.second;

            if (client->doingRecv.load() == false) {
                FD_SET(activeSock, &readSet);
                FD_SET(activeSock, &exceptionSet);
                maxSock = max(maxSock, activeSock);
            }
        }

        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 100;
        r = select(maxSock + 1, &readSet, NULL, &exceptionSet, &timeout);

        if (r == SOCKET_ERROR) {
            cerr << "select failed: " << WSAGetLastError() << endl;
            break;
        }

        if (r == 0) {
            continue;
        }

   
        if (FD_ISSET(passiveSock, &readSet)) {
            cout << "Waiting for a connection" << endl;
            struct sockaddr_in clientAddr;
            int clientAddrSize = sizeof(clientAddr);
            SOCKET activeSock = accept(passiveSock, (sockaddr*)&clientAddr, &clientAddrSize);

            if (activeSock == INVALID_SOCKET) {
                cerr << "accept failed with error " << WSAGetLastError() << endl;
                return 1;
            }
            else {
                shared_ptr<Client> newClient(new Client(activeSock));

                activeClients.insert(make_pair(activeSock, newClient));

                char strBuf[1024];
                inet_ntop(AF_INET, &(clientAddr.sin_addr), strBuf, sizeof(strBuf));
                cout << "New client from " << strBuf << ":" << ntohs(clientAddr.sin_port) << ". "
                    << "Socket: " << activeSock << endl;
            }
        }

        list<SOCKET> toDelete;
        for (auto& entry : activeClients) {
            SOCKET activeSock = entry.first;
            shared_ptr<Client> client = entry.second;

            if (FD_ISSET(activeSock, &exceptionSet)) {
                cerr << "Exception on socket " << activeSock << endl;

                closesocket(activeSock);

                toDelete.push_back(activeSock);

                continue;
            }

            if (FD_ISSET(activeSock, &readSet)) {
                
                client->doingRecv.store(true);

                {
                    lock_guard<mutex> lg(jobQueueMutex);

                    bool wasEmpty = jobQueue.empty();
                    jobQueue.push(client);

                    if (wasEmpty) {
                        jobQueueFilledCv.notify_one();
                    }

                }
            }
        }

        for (auto& closedSock : toDelete) {
            activeClients.erase(closedSock);
        }
    }

    for (shared_ptr<thread>& workerThread : threads) {
        workerThread->join();
    }

    r = closesocket(passiveSock);
    if (r == SOCKET_ERROR) {
        cerr << "closesocket(passive) failed with error " << WSAGetLastError() << endl;
        return 1;
    }
    redisFree(c);
    WSACleanup();
    return 0;
}



