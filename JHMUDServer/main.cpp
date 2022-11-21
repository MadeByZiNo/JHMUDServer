#define RAPIDJSON_HAS_STDSTRING 1


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

#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

#define NOMINMAX
#pragma comment(lib, "Ws2_32.lib")
#include <WinSock2.h>
#include <WS2tcpip.h>

using namespace rapidjson;
using namespace std;

static const unsigned short SERVER_PORT = 27015;
static const int NUM_WORKER_THREADS = 10;

redisContext* c = redisConnect("127.0.0.1", 6379);
redisReply* reply;

list<shared_ptr<Slime>> slimes;



//클라이언트이자 유저
class Client {
public:
    SOCKET sock;  // 이 클라이언트의 active socket

    atomic<bool> doingRecv;

    Client(SOCKET sock) : sock(sock), doingRecv(false) {
    }

    ~Client() {
    }
    
    shared_ptr<User> user =NULL;
};


map<SOCKET, shared_ptr<Client> > activeClients;
mutex activeClientsMutex;

queue<shared_ptr<Client> > jobQueue;
mutex jobQueueMutex;
condition_variable jobQueueFilledCv;

//패시브 소켓을 만드는 함수
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

    int r = ::bind(passiveSock, (sockaddr*)&serverAddr, sizeof(serverAddr));
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
            cerr << "연결이 끊겼거나 유저의 캐릭터가 사망하였으므로 소켓에서 제외 " << WSAGetLastError() << endl;
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
            cerr << "연결이 끊겼거나 유저의 캐릭터가 사망하였으므로 소켓에서 제외 " << WSAGetLastError() << endl;
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

    shared_ptr<char> buf(new char[dataLen + 1]);
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

//객체를 메시지로 반환하는 함수.
string getJsonmessage(Document& d)
{
    StringBuffer buffer;
    Writer<StringBuffer> writer(buffer);
    d.Accept(writer);
    return buffer.GetString();
}

//슬라임 쓰레드
void slimeControler()
{
    int cnt = 0;
    while (true)
    {
        //60초마다 10마리씩 되게 생성을 한다.
        if (cnt == 0)
        {
            srand((unsigned int)time(NULL));
            while (slimes.size() < 10)
            {
                shared_ptr<Slime> tempSlime(new Slime());
                slimes.push_back(tempSlime);
            }
        }

        // 존재하는 슬라임들의 이터레이터를 돈다.
        list<shared_ptr<Slime>>::iterator siter;
        for (siter = slimes.begin(); siter != slimes.end(); siter++)
        {
            int sx = (*siter)->getX();
            int sy = (*siter)->getY();

            //유저들도 이터레이터를 돈다.

            map<SOCKET, shared_ptr<Client>>::iterator uiter;    
            lock_guard<mutex> lg(activeClientsMutex);
            for (uiter = activeClients.begin(); uiter != activeClients.end(); uiter++)
            {
                if (uiter->second->user == NULL) { continue; }
                int ux = uiter->second->user->getX();
                int uy = uiter->second->user->getY();

                //슬라임의 공격 범위 안에 있으면 실행한다.
                if ((*siter)->Attack(ux, uy) == true)
                {
                    bool status = uiter->second->user->UserDamaged((*siter)->getStr());

                    Document d;
                    d.SetObject();

                    Value hp, damaged, nickname;

                    hp.SetInt(uiter->second->user->getHp());
                    damaged.SetInt((*siter)->getStr());
                    nickname.SetString(uiter->second->user->getNickname().c_str(), d.GetAllocator());

                    d.AddMember("command", "slimeAttack", d.GetAllocator());
                    d.AddMember("nickname", nickname, d.GetAllocator());
                    d.AddMember("hp", hp, d.GetAllocator());
                    d.AddMember("damaged", damaged, d.GetAllocator());
                    if (status == true) { d.AddMember("status", "live", d.GetAllocator()); }
                    else { d.AddMember("status", "death", d.GetAllocator()); }
                    map<SOCKET, shared_ptr<Client>>::iterator uiter2;
                    //브로드캐스트 한다.
                    for (uiter2 = activeClients.begin(); uiter2 != activeClients.end(); uiter2++)
                    {
                        if (uiter->first == uiter2->first) { d.AddMember("who", "commander", d.GetAllocator()); }
                        else { d.AddMember("who", "receiver", d.GetAllocator()); }
                        string s = getJsonmessage(d);
                        const char* message = s.c_str();
                        sendMessage(message, uiter2->first);
                        d.RemoveMember("who");
                    }
                }
			}
		}   
        //60초마다 공격
        Sleep(1000);
        cnt += 5;
        if (cnt == 60) { cnt = 0; }
    }
}
   


//로그인 시도 하는 함수
bool doLogin(shared_ptr<Client> client, SOCKET activeSock,string buf) {
 
    //유저에게 결과값을 반환하기 위한 객체
    Document d;
    d.SetObject();

    //랜덤 생성을 위해 설정한다.
    srand((unsigned int)time(NULL));

    //생성되어있는지 체크한다.
    reply = (redisReply*)redisCommand(c, "EXISTS USER:%s", buf.c_str());

    if (reply->type == REDIS_REPLY_ERROR) {
        printf("error");
        return false;
    }
    //redis에 만료되었꺼나 처음로그인하는 아이디면 캐릭터를 새로 생성한다.
    if(reply->integer == 0) 
    {
        freeReplyObject(reply);
        d.AddMember("result", "new", d.GetAllocator());
        client->user = shared_ptr<User>(new User(buf));
    }                  
    else  //남아있는 데이터가 있을 시 그 데이터대로 초기화 한다.
    { 
        d.AddMember("result", "load", d.GetAllocator());
        freeReplyObject(reply);
       
        reply = (redisReply*)redisCommand(c, "GET USER:%s:X", buf.c_str());
        int _x = stoi(reply->str);
        freeReplyObject(reply);
        reply = (redisReply*)redisCommand(c, "GET USER:%s:Y", buf.c_str());
        int _y = stoi(reply->str);
        freeReplyObject(reply);
        reply = (redisReply*)redisCommand(c, "GET USER:%s:HP", buf.c_str());
        int _hp = stoi(reply->str);
        freeReplyObject(reply);
        reply = (redisReply*)redisCommand(c, "GET USER:%s:STR", buf.c_str());
        int _str = stoi(reply->str);
        freeReplyObject(reply);
        reply = (redisReply*)redisCommand(c, "HGET USER:%s:POTIONS HP", buf.c_str());
        int _numOfHpPotion = stoi(reply->str);
        freeReplyObject(reply);
        reply = (redisReply*)redisCommand(c, "HGET USER:%s:POTIONS STR", buf.c_str());
        int _numOfStrPotion = stoi(reply->str);
        freeReplyObject(reply);

        client->user = shared_ptr<User>(new User(buf,_x,_y,_hp,_str,_numOfHpPotion,_numOfStrPotion));

    }

    //redis에 만들어진 데이터를 등록한다.
    reply = (redisReply*)redisCommand(c, "SET USER:%s 1", buf.c_str());
    freeReplyObject(reply);
    reply = (redisReply*)redisCommand(c, "SET USER:%s:HP %d", buf.c_str(), client->user->getHp());
    freeReplyObject(reply);
    reply = (redisReply*)redisCommand(c, "SET USER:%s:STR %d", buf.c_str(), client->user->getStr());
    freeReplyObject(reply);
    reply = (redisReply*)redisCommand(c, "SET USER:%s:X %d", buf.c_str(), client->user->getX());
    freeReplyObject(reply);
    reply = (redisReply*)redisCommand(c, "SET USER:%s:Y %d", buf.c_str(), client->user->getY());
    freeReplyObject(reply);
    reply = (redisReply*)redisCommand(c, "HSET USER:%s:POTIONS HP %d", buf.c_str(), client->user->getNumOfHpPotion());
    freeReplyObject(reply);
    reply = (redisReply*)redisCommand(c, "HSET USER:%s:POTIONS STR %d", buf.c_str(), client->user->getNumOfStrPotion());
    freeReplyObject(reply);
    reply = (redisReply*)redisCommand(c, "PERSIST USER:%s", buf.c_str());
    freeReplyObject(reply);

    //캐릭터 생성정보를 전송하기 위해 객체를 만든다.
    Value inputNickname, x, y, hp, str, potionhp, potionstr;

    inputNickname.SetString(buf.c_str(),d.GetAllocator());
    x.SetInt(client->user->getX());
    y.SetInt(client->user->getY());
    hp.SetInt(client->user->getHp());
    str.SetInt(client->user->getStr());
    potionhp.SetInt(client->user->getNumOfHpPotion());
    potionstr.SetInt(client->user->getNumOfStrPotion());

    d.AddMember("command", "inputNickname", d.GetAllocator());
    d.AddMember("nickname", inputNickname, d.GetAllocator());
    d.AddMember("x", x, d.GetAllocator());
    d.AddMember("y", y, d.GetAllocator());
    d.AddMember("hp", hp, d.GetAllocator());
    d.AddMember("str", str, d.GetAllocator());
    d.AddMember("potionhp", potionhp, d.GetAllocator());
    d.AddMember("potionstr", potionstr, d.GetAllocator());

    string s = getJsonmessage(d);
    const char* message = s.c_str();

    //캐릭터 생성 정보를 보여준다.
    if (sendMessage(message, activeSock) == 1) { return false; }
 
    return true;
}

//캐릭터 정보 전송 하는 함수
bool checkData(shared_ptr<Client> client, SOCKET activeSock) 
{
    Document d;
    d.SetObject();

    Value inputNickname, x, y, hp, str, potionhp, potionstr;

    inputNickname.SetString(client->user->getNickname().c_str(), d.GetAllocator());
    x.SetInt(client->user->getX());
    y.SetInt(client->user->getY());
    hp.SetInt(client->user->getHp());
    str.SetInt(client->user->getStr());
    potionhp.SetInt(client->user->getNumOfHpPotion());
    potionstr.SetInt(client->user->getNumOfStrPotion());

    d.AddMember("command", "checkdata", d.GetAllocator());
    d.AddMember("nickname", inputNickname, d.GetAllocator());
    d.AddMember("x", x, d.GetAllocator());
    d.AddMember("y", y, d.GetAllocator());
    d.AddMember("hp", hp, d.GetAllocator());
    d.AddMember("str", str, d.GetAllocator());
    d.AddMember("potionhp", potionhp, d.GetAllocator());
    d.AddMember("potionstr", potionstr, d.GetAllocator());

    string s = getJsonmessage(d);
    const char* message = s.c_str();

    //캐릭터 생성 정보를 보여준다.
    if (sendMessage(message, activeSock) == 1) { return false; }
    return true;
}

//캐릭터 이동 시도 하는 함수
bool doMove(shared_ptr<Client> client, SOCKET activeSock,int _x,int _y) {
    
    //클라이언트에게 이동 결과를 보내기 위한 객체생성
    Document d;
    d.SetObject();
    Value x,y;
    
    d.AddMember("command", "move", d.GetAllocator());
    //던전 범위를 벗어나서 false를 반환받으면 result 멤버를 fail로 한다.
    if (client->user->Move(_x, _y) == false)
    {
        d.AddMember("result", "fail", d.GetAllocator());
    }
    else  //잘 이동했을 시에는 success로 하며 이동 후 좌표까지 넣는다.
    {

        x.SetInt(client->user->getX());
        y.SetInt(client->user->getY());
        d.AddMember("result", "success", d.GetAllocator());
        d.AddMember("x", x, d.GetAllocator());
        d.AddMember("y", y, d.GetAllocator());

        
        //redis를 업데이트한다.
        reply = (redisReply*)redisCommand(c, "SET USER:%s:X %d", client->user->getNickname().c_str(), client->user->getX());
        freeReplyObject(reply);
        reply = (redisReply*)redisCommand(c, "SET USER:%s:Y %d", client->user->getNickname().c_str() , client->user->getY());
        freeReplyObject(reply);
    }

    string s = getJsonmessage(d);
    const char* message = s.c_str();

    //클라이언트에게 보낸다.
    if (sendMessage(message, activeSock) == 1) { return false; }
    return true;
}

bool sendSlimePosition(shared_ptr<Client> client, SOCKET activeSock) {

    //클라이언트에게 이동 결과를 보내기 위한 객체생성
    Document d;
    d.SetObject();
   
    Value slimeList(kArrayType);
        
    // 존재하는 슬라임들의 이터레이터를 돈다.
    list<shared_ptr<Slime>>::iterator siter;
    for (siter = slimes.begin(); siter != slimes.end(); siter++)
    {
        Value slime(kObjectType);

        Value x, y;
        x.SetInt((*siter)->getX());
        y.SetInt((*siter)->getY());
        slime.AddMember("x", x,d.GetAllocator());
        slime.AddMember("y", y, d.GetAllocator());
        slimeList.PushBack(slime, d.GetAllocator());
    }

    d.AddMember("command", "slimePosition", d.GetAllocator());
    d.AddMember("slimeList", slimeList, d.GetAllocator());
    string s = getJsonmessage(d);
    const char* message = s.c_str();

    //클라이언트에게 보낸다.
    if (sendMessage(message, activeSock) == 1) { return false; }
    return true;
}

bool sendUserPosition(shared_ptr<Client> client, SOCKET activeSock) {

    //클라이언트에게 이동 결과를 보내기 위한 객체생성
    Document d;
    d.SetObject();

    Value userList(kArrayType);

    // 존재하는 슬라임들의 이터레이터를 돈다.
    map<SOCKET, shared_ptr<Client>>::iterator uiter;

    for (uiter = activeClients.begin(); uiter != activeClients.end(); uiter++)
    {
        Value user(kObjectType);

        Value x, y,nickname;
        x.SetInt(uiter->second->user->getX());
        y.SetInt(uiter->second->user->getY());
        nickname.SetString(uiter->second->user->getNickname().c_str(), d.GetAllocator());
        user.AddMember("x", x, d.GetAllocator());
        user.AddMember("y", y, d.GetAllocator());
        user.AddMember("nickname", nickname, d.GetAllocator());
        userList.PushBack(user, d.GetAllocator());
    }

    d.AddMember("command", "userPosition", d.GetAllocator());
    d.AddMember("userList",userList, d.GetAllocator());
    string s = getJsonmessage(d);
    const char* message = s.c_str();

    //클라이언트에게 보낸다.
    if (sendMessage(message, activeSock) == 1) { return false; }
    return true;
}

//프로세스가 작업을 꺼내서 클라이언트가 어떤 명령을 했는지 확인한다.
//확인한 후에 알맞은 command에 따라 value를 받는다.

bool processClient(shared_ptr<Client> client) {
    SOCKET activeSock = client->sock;
   
    //json으로 되어있는 문자열을 받고 역직렬화한다.
    shared_ptr<char> buf;
    if ((buf=recvMessage(activeSock)) == nullptr) { return false; }

    Document d;
    d.Parse(buf.get());


    //일단 command 를 받는다.
    Value& _command = d["Command"];
    const char* command = _command.GetString();

    
    //command에 따라 분기한다.

    if (strcmp(command,"Login")==0) 
    {
        Value& _nickname = d["Nickname"];
        string nickname = _nickname.GetString();
        if (doLogin(client, activeSock, nickname) == false) { return false; }
    }

    else if (strcmp(command, "Checkdata") == 0)
    {
        if (checkData(client, activeSock) == false) { return false; }
    }

    else if (strcmp(command, "Move")==0)
    {
        Value& _x = d["x"];
        Value& _y = d["y"];
        if (doMove(client, activeSock,_x.GetInt(),_y.GetInt()) == false) { return false; }
    }

    else if (strcmp(command, "SlimePosition") == 0)
    {
        if (sendSlimePosition(client, activeSock) == false) { return false; }
    }

    else if (strcmp(command, "UserPosition") == 0)
    {
        if (sendUserPosition(client, activeSock) == false) { return false; }
    }
    return true;
}

//아래는 쓰레드가 작업(클라이언트들의 요청)을 작업큐에서 빼서 행하는 과정이다.
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





//메인함수 
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

    shared_ptr<thread> slimeThread(new thread(slimeControler));
   
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

                reply = (redisReply*)redisCommand(c, "EXPIRE USER:%s 300 ", client->user->getNickname().c_str());
                freeReplyObject(reply);
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

    slimeThread->join();

    r = closesocket(passiveSock);
    if (r == SOCKET_ERROR) {
        cerr << "closesocket(passive) failed with error " << WSAGetLastError() << endl;
        return 1;
    }
    redisFree(c);
    WSACleanup();
    return 0;
}



