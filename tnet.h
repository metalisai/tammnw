// TODO: this won't work
#ifndef TNET_H
#define TNET_H

#include <stdlib.h>
#include <stdio.h>

#define TNET_PLATFORM_LINUX

//linux
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <time.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>
//TODO: remove
#include <assert.h>

#include <cstdint>

typedef int64_t tnet_i64;
typedef int32_t tnet_i32;
typedef int16_t tnet_i16;

typedef uint64_t tnet_u64;
typedef uint32_t tnet_u32;
typedef uint16_t tnet_u16;

#define tnet_Megabytes(Value) (Value*1024LL*1024LL)

#define TNET_SEND_DATA_FLAG_REQCON       1
#define TNET_SEND_DATA_FLAG_ACCCON       1<<1
#define TNET_SEND_DATA_FLAG_HEARTBEAT    1<<2
#define TNET_SEND_DATA_FLAG_DECLINE      1<<3
#define TNET_SEND_DATA_FLAG_PING         1<<4 // TODO: add to the packet forming code

#define TNET_MAX_PACKET_SIZE 512
#define TNET_CONFIRMED_PACKETS_HISTORY_SIZE  512

#define TNET_CONNECTION_TIMEOUT_MS   10000
#define TNET_PING_INTERVAL_MS           1000

#define TNET_PING_TYPE_PING 1
#define TNET_PING_TYPE_PINGBACK 2

#define TNET_PING_MESSSAGE_SIZE 3
#define TNET_PING_IS_RELIABLE   0

enum tnet_connection_state_t
{
    CEDisconnected,
    CEConnecting,
    CEConnected
};

#define TNET_RECEIVED_MESSAGE_HISTORY_SIZE   4096 // POWER OF 2 ONLY

enum tnet_ringqueue_state_t
{
    Empty,
    None,
    Full
};

struct tnet_ringqueue
{
    char* startAddr = NULL;
    size_t size = 0;
    char* dequeuePointer = NULL;
    char* queuePointer = NULL;
    tnet_ringqueue_state_t state = tnet_ringqueue_state_t::Empty;
};

#ifdef TNET_PLATFORM_LINUX
typedef timespec tnet_time_point;
#else
    #error tnet_time_point not defined on this platform!
#endif

struct tnet_connection_state
{
    tnet_i32 socket;
    tnet_u32 destIP;
    tnet_i32 seqId;
    tnet_u32 ack;
    tnet_i32 ackBits;
    tnet_u16 destPort;
    tnet_u16 messageId;
    tnet_u32 packetsSinceAck;
    tnet_u32 ping;
    tnet_connection_state_t state;
    tnet_time_point lastPacketReceived;
    tnet_time_point lastPacketSent;
    tnet_time_point lastPingRequest;
    tnet_u32 confirmedPackets[TNET_CONFIRMED_PACKETS_HISTORY_SIZE];
    tnet_u16 receivedMessages[TNET_RECEIVED_MESSAGE_HISTORY_SIZE];
    pthread_mutex_t conMutex;
};

struct tnet_host_settings
{
    bool keepConnectionsAlive;
    // TODO: implement
    tnet_u32 maxReceiveAllocPerConnection; // how many bytes from receive Queue can one connection allocate
};

struct tnet_host
{
    tnet_u32 maxConnections;
    tnet_i32 socket;
    bool keepConnectionsAlive;
    tnet_connection_state* conStates;
    // resend buffer (doesn't need mutex, accessed only from 1 thread)
    // holds SentMessages structs
    tnet_ringqueue resendBuffer;
    // receive buffer
    // holds ReceivedMessage structs
    pthread_mutex_t receiveBufMutex;
    tnet_ringqueue receiveBuffer;
    // send buffer
    // holds QueuedMessage structs
    tnet_ringqueue sendBuffer;
    pthread_mutex_t sendBufMutex;
    pthread_t recWorker;
    pthread_t sendWorker;
    volatile bool sendDone;
    pthread_mutex_t sendMut;
    pthread_cond_t sendCon;

};

enum tnet_host_event_t
{
    HENothing,
    HEConnect,
    HEDisconnect,
    HEData
};

// actual API
bool tnet_create_host(tnet_host* host, tnet_u16 port, tnet_i32 maxConnections);
void tnet_free_host(tnet_host* host);
tnet_host_event_t tnet_get_next_event(tnet_i32 connection, char* buf, int& received);
void tnet_queue_data(tnet_host* host, tnet_i32 connection, const char* data, const tnet_i32 dataSize, const bool reliable, tnet_u32 flags = 0);
void tnet_release_pending_data(tnet_host* host);
void tnet_disconnect(tnet_host* host, tnet_u32 connectionId);
tnet_i32 tnet_accept(tnet_host* host, tnet_i32 conId);
unsigned short tnet_get_ping(tnet_host* host, unsigned int connectionId);

//#ifdef TNET_IMPLEMENTATION

struct tnet_received_event
{
    tnet_host_event_t type;
    tnet_i32 connection;
    tnet_u32 size;
    char data[TNET_MAX_PACKET_SIZE];
};
struct tnet_queued_data
{
    bool reliable;
    tnet_i32 connection;
    tnet_i32 size;
    tnet_u32 flags;
    tnet_time_point queuedAt;
    char data[TNET_MAX_PACKET_SIZE];
};
struct tnet_sent_reliable_data
{
    tnet_time_point sendTime;
    tnet_u32 pId;
    tnet_u16 messageId;
    tnet_queued_data qData;
};

#define REL_HEADER_SIZE 18
#define UREL_HEADER_SIZE 2

#pragma pack(push,1)
struct tnet_urelbody
{
    unsigned char data[TNET_MAX_PACKET_SIZE];
};

struct tnet_relbody
{
    tnet_i32 seqId; // 4
    tnet_i32 ack;   // 8
    tnet_i32 ackBits; // 12
    tnet_u16 size; // 14
    tnet_u16 messageId; // 16
    unsigned char data[TNET_MAX_PACKET_SIZE];
};

struct tnet_packet
{
    unsigned char hasRel : 1;
    unsigned char reqCon : 1;
    unsigned char acceptCon : 1;
    unsigned char heartbeat : 1;
    unsigned char decline : 1;
    unsigned char ping : 1;
    tnet_u16 size : 10;
    union
    {
        tnet_urelbody urelBody;
        tnet_relbody relBody;
    };
};
#pragma pack(pop)

// RINGQUEUE
bool tnet_ringqueue_initialize(tnet_ringqueue* dq, size_t size);
void tnet_ring_queue_free(tnet_ringqueue* dq);
void tnet_ringqueue_reset(tnet_ringqueue* dq);
bool tnet_ringqueue_queue(tnet_ringqueue* dq, const void* data, size_t size);
size_t tnet_ringqueue_dequeue(tnet_ringqueue* dq, void* data, unsigned int size);
size_t tnet_ringqueue_peek(tnet_ringqueue* dq, void* data, unsigned int size);
//tnet_ringqueue_state_t RingQueueGetState(tnet_ringqueue* dq);
bool tnet_ringqueue_drop(tnet_ringqueue* dq);

inline void net_get_time(tnet_time_point& to)
{
#ifdef TNET_PLATFORM_LINUX
    clock_gettime(CLOCK_MONOTONIC, &to);
#else
#error "net_get_time not defined on this platform"
#endif
}

inline tnet_i32 getDurationToNowMs(const tnet_time_point pastPoint)
{
#ifdef TNET_PLATFORM_LINUX
    tnet_time_point now;
    net_get_time(now);
    double dif = (now.tv_sec-pastPoint.tv_sec)*1000+(now.tv_nsec-pastPoint.tv_nsec)/1000000;
    return (tnet_i32)dif;
#else
#error getDurationToNowMs not defined
#endif
}

inline tnet_i32 getDurationMs(const tnet_time_point from, const tnet_time_point to)
{
#ifdef TNET_PLATFORM_LINUX
    double dif = (to.tv_sec-from.tv_sec)*1000+(to.tv_nsec-from.tv_nsec)/1000000;
    return (tnet_i32)dif;
#else
    #error getDuration not defined
#endif
}

void closeSocket(tnet_i32 socket)
{
    if(socket != -1)
    {
        close(socket);
        //shutdown(socket, SHUT_RDWR);
    }
    else
    {
        printf("trying to close an invalid socket!\n");
    }
}

tnet_i32 openSocket(tnet_u16 port)
{
    //create socket
    tnet_i32 nsock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if(nsock < 0)
    {
        printf("Creating socket failed!\n");
        return -1;
    }
    // bind
    sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons((unsigned short)port);
    if(bind(nsock, (const sockaddr*)&address, sizeof(sockaddr_in)) < 0)
    {
        printf("Binding socket failed!\n");
        return -1;
    }
    // set blocking mode
    tnet_u32 blocking = 1;
    tnet_u32 flags = fcntl(nsock, F_GETFL, 0);
    if (blocking)
        flags &= ~O_NONBLOCK;
    else
        flags |= O_NONBLOCK;
    if (fcntl(nsock, F_SETFL, flags) == -1)
    {
        printf("Failed to set socket blocking mode!\n");
        closeSocket(nsock);
        return -1;
    }
    return nsock;
}

tnet_i32 sendToSocket(tnet_i32 socket, void* data, tnet_u16 size, tnet_u32 destIp, tnet_u16 destPort)
{
    sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(destIp);
    address.sin_port = htons((unsigned short)destPort);
    tnet_i32 sentBytes = 1;
    // uncomment for 33% packetloss
    //if(random() % 3 != 1)
        sentBytes = sendto(socket, data, size, 0, (sockaddr*)&address, sizeof(sockaddr_in));
    if(sentBytes <= 0)
    {
        printf("sendToSocket failed!\n");
    }
    return sentBytes;
}

bool recvFromSocket(tnet_i32 socket, char* data, tnet_i32& received, tnet_u32& fromAddr, tnet_u16& fromPort)
{
    bool ret;
    sockaddr_in from;
    socklen_t fromLength = sizeof(from);
    tnet_i32 ss;
    ss = recvfrom(socket, (char*)data, TNET_MAX_PACKET_SIZE, 0, (sockaddr*)&from, &fromLength);
    if(ss == -1)
    {
        printf("recvfrom failed errno:%d\n",errno);
    }
    ret = ss > 0;
    if (ret)
    {
        received = (tnet_i32)ss;
        fromAddr = ntohl(from.sin_addr.s_addr);
        fromPort = ntohs(from.sin_port);
        return true;
    }
    return false;
}

void initConnectionState(tnet_connection_state& state)
{
    state.ack = 0;
    state.ackBits = 0;
    state.seqId = 0;
    state.destIP = 0;
    state.destPort = 0;
    state.socket = -1;
    state.state = tnet_connection_state_t::CEDisconnected;
    // TODO: make platform independent!
    state.conMutex = PTHREAD_MUTEX_INITIALIZER;
    state.messageId = 0;
    state.packetsSinceAck = 0;
    state.ping = 0xFFFFF;
    net_get_time(state.lastPacketReceived);
    state.lastPingRequest = state.lastPingRequest;
    for(int i=0; i< TNET_CONFIRMED_PACKETS_HISTORY_SIZE;i++)
    {
        state.confirmedPackets[i] = 0xFFFFFFFF;
    }
    for(int i=0; i< TNET_RECEIVED_MESSAGE_HISTORY_SIZE;i++)
    {
        state.receivedMessages[i] = 0xFFFF;
    }
}

tnet_i32 findAndResetInactiveConnectionSlot(tnet_connection_state* connections, int maxConnections)
{
    tnet_i32 ret = -1;
    for(int i = 0; i < maxConnections; i++)
    {
        pthread_mutex_lock(&connections[i].conMutex);
        if(connections[i].state == tnet_connection_state_t::CEDisconnected)
        {
            initConnectionState(connections[i]);
            ret = i;
        }
        pthread_mutex_unlock(&connections[i].conMutex);
        if(ret != -1)
            break;
    }
    return ret;
}

tnet_i32 findActiveConnectionByDest(tnet_connection_state* connections, int maxConnections, tnet_u32 ip, tnet_u16 port)
{
	// TODO: hastable, binary search or something?
    tnet_i32 ret = -1;
    for(int i = 0; i < maxConnections; i++)
    {
        pthread_mutex_lock(&connections[i].conMutex);
        if(connections[i].state != tnet_connection_state_t::CEDisconnected && connections[i].destIP == ip && connections[i].destPort == port)
        {
            ret = i;
        }
        pthread_mutex_unlock(&connections[i].conMutex);
        if(ret != -1)
            break;
    }
    return ret;
}

// only used by receiveProc
void proccessRemoteAck(tnet_connection_state& connection, tnet_u32 ack, tnet_u32 ackBits)
{
    unsigned int cabits;
    // REMCONST
    for (int i = 0; i < 32; i++) // TODO: dumb solution like this cant be the best way?
    {
        cabits = ackBits;
        cabits >>= i; // shift the interested bit into bit 0
        cabits &= 1; // mask so that only bit 0 remains
        assert(cabits == 1 || cabits == 0);
        tnet_u32 remPacketId = ack - i; // the remoteSequenceId the packet represents
        if (cabits == 1)
        {
            if (connection.confirmedPackets[remPacketId%TNET_CONFIRMED_PACKETS_HISTORY_SIZE] == remPacketId) // already confirmed
                continue;
            connection.confirmedPackets[remPacketId%TNET_CONFIRMED_PACKETS_HISTORY_SIZE] = remPacketId;
            //connection.confirmedPacketCount++; // do we even use this anywhere??
        }
    }
}

// only used by receiveProc
void ackPacket(tnet_connection_state& connection, tnet_u32 remSeq)
{
    if (remSeq >= connection.ack) // newer packet
    {
        unsigned int difference = remSeq - connection.ack;
        if (difference < 32) // REMCONST
        {
            connection.ackBits <<= difference;
            connection.ackBits |= 1;
        }
        else
        {
            //assert(false); // extreme case that should never happen when testing (but our protocol should be able to deal with it anyways, possible bug catch place)
            connection.ackBits = 0;
        }
        connection.ack = remSeq; // only update if its more recent
    }
    else // older packet
    {
        unsigned int difference = connection.ack - remSeq;
        unsigned int mask = 1 << difference;
        connection.ackBits |= mask;
    }
}

// TODO: accept strings
tnet_i32 hostConnect(tnet_host* host, tnet_u32 destIp, tnet_u16 destPort)
{
    tnet_i32 conId = findAndResetInactiveConnectionSlot(host->conStates, host->maxConnections);
    if(conId >= 0)
    {
        pthread_mutex_lock(&host->conStates[conId].conMutex);
        host->conStates[conId].destIP = destIp;
        host->conStates[conId].destPort = destPort;
        host->conStates[conId].state = tnet_connection_state_t::CEConnecting;
        pthread_mutex_unlock(&host->conStates[conId].conMutex);
        tnet_queue_data(host, conId, (char*)0, 0, true, TNET_SEND_DATA_FLAG_REQCON);
    }
    else
    {
        printf("Tried to start new connection when out of slots!\n");
    }
    return conId;
}

tnet_i32 tnet_accept(tnet_host* host, tnet_i32 conId)
{
    if(conId >= 0)
    {
        pthread_mutex_lock(&host->conStates[conId].conMutex);
        host->conStates[conId].state = tnet_connection_state_t::CEConnected;
        pthread_mutex_unlock(&host->conStates[conId].conMutex);
        tnet_queue_data(host, conId, (char*)0, 0, true, TNET_SEND_DATA_FLAG_ACCCON);
    }
    else
    {
        printf("Tried to accept invalid connection!\n");
    }
    return conId;
}

void queue_ping_message(tnet_host* host, tnet_u32 connection)
{
    char data[3]; // bytes 1 and 2 will be added when sending (latency in ms between queueing and actual sending)
    data[0] = TNET_PING_TYPE_PING;
    tnet_queue_data(host, connection, data, TNET_PING_MESSSAGE_SIZE, TNET_PING_IS_RELIABLE, TNET_SEND_DATA_FLAG_PING);
}

void queue_pingback_message(tnet_host* host, tnet_u32 connection, tnet_u16 latency)
{
    char data[3]; // bytes 1 and 2 will be added when sending (latency in ms between queueing and actual sending)
    data[0] = TNET_PING_TYPE_PINGBACK;
    *((tnet_u16*)&data[1]) = latency;
    tnet_queue_data(host, connection, data, TNET_PING_MESSSAGE_SIZE, TNET_PING_IS_RELIABLE, TNET_SEND_DATA_FLAG_PING);
}

void QDataToPacket(tnet_queued_data& q, tnet_packet& p, tnet_u32 ack, tnet_u32 ackBits, tnet_u32 seqId, tnet_u16 messageId)
{
    p.hasRel = q.reliable;
    p.reqCon = (q.flags & TNET_SEND_DATA_FLAG_REQCON) != 0;
    p.acceptCon = (q.flags & TNET_SEND_DATA_FLAG_ACCCON) != 0;
    p.heartbeat = (q.flags & TNET_SEND_DATA_FLAG_HEARTBEAT) != 0;
    p.decline = (q.flags & TNET_SEND_DATA_FLAG_DECLINE) != 0;
    p.ping = (q.flags & TNET_SEND_DATA_FLAG_PING) != 0;
    if(q.reliable)
    {
        // REMCONST
        p.size = q.size + REL_HEADER_SIZE; // 16 = reliable packet header size
        p.relBody.ack = ack;
        p.relBody.ackBits = ackBits;
        p.relBody.seqId = seqId;
        p.relBody.messageId = messageId;
        p.relBody.size = q.size;
        memcpy(p.relBody.data, q.data, q.size);
    }
    else
    {
        p.size = q.size + UREL_HEADER_SIZE; // 2 = unreiliable packet header size
        memcpy(p.urelBody.data, q.data, q.size);
    }
}

inline bool flagSet(tnet_u32 flags, tnet_u32 flag)
{
    return (flags & flag) != 0;
}

// connection mutex must be locked !!
void sendPacket(tnet_host* host, tnet_packet& p, tnet_queued_data& q, tnet_u16 messageId, const tnet_time_point& now)
{
    tnet_connection_state* connections = host->conStates;
    if(connections[q.connection].state == tnet_connection_state_t::CEDisconnected &&    // if disconnected and not notifying the disconnect
            !flagSet(q.flags,TNET_SEND_DATA_FLAG_DECLINE))
        return;

    connections[q.connection].lastPacketSent = now;
    tnet_sent_reliable_data rdata;
    QDataToPacket(q, p, connections[q.connection].ack, connections[q.connection].ackBits, connections[q.connection].seqId++, messageId);

    if(p.ping) // if this is a ping packet, then add the send-queuedAt latency to it
    {
        assert(q.size == TNET_PING_MESSSAGE_SIZE);
        assert(q.reliable == TNET_PING_IS_RELIABLE);
#if TNET_PING_IS_RELIABLE
        unsigned char* data = p.relBody.data;
#else
        unsigned char* data= p.urelBody.data;
#endif
        if(data[0] == TNET_PING_TYPE_PING)
        {
            *((tnet_u16*)&data[1]) = (tnet_u16)getDurationMs(q.queuedAt,now);
        }
        else
        {
            assert(data[0] == TNET_PING_TYPE_PINGBACK);
            *((tnet_u16*)&data[1]) += (tnet_u16)getDurationMs(q.queuedAt,now);
        }
    }

    if(q.reliable)
    {
        net_get_time(rdata.sendTime);
        rdata.pId = p.relBody.seqId;
        rdata.messageId = messageId;
        connections[q.connection].packetsSinceAck = 0;

        memcpy(&rdata.qData,&q,sizeof(tnet_queued_data)-TNET_MAX_PACKET_SIZE+q.size);
        bool qd = tnet_ringqueue_queue(&host->resendBuffer, &rdata, sizeof(tnet_sent_reliable_data)-TNET_MAX_PACKET_SIZE+q.size);
        assert(qd);
    }

    // TODO: is the size correct?
    sendToSocket(host->socket, &p, p.size, connections[q.connection].destIP, connections[q.connection].destPort);
}

// lock connection mutex!
void disconnectConnection(tnet_host* host, tnet_u32 connectionId)  // internal version
{
    // TODO: send a disconnect message to the other side too, so it doesn't have to wait for timeout
    if(host->conStates[connectionId].state != CEDisconnected)
    {
        host->conStates[connectionId].state = CEDisconnected;
        pthread_mutex_lock(&host->receiveBufMutex);
        tnet_received_event buf;
        buf.connection = connectionId;
        buf.size = 0;
        buf.type = tnet_host_event_t::HEDisconnect;
        bool qd = tnet_ringqueue_queue(&host->receiveBuffer, &buf, sizeof(buf)-TNET_MAX_PACKET_SIZE+buf.size);
        assert(qd);
        pthread_mutex_unlock(&host->receiveBufMutex);

        tnet_queue_data(host, connectionId, 0, 0, false, TNET_SEND_DATA_FLAG_DECLINE);
    }
}

void tnet_disconnect(tnet_host* host, tnet_u32 connectionId) // API version
{
    pthread_mutex_lock(&host->conStates[connectionId].conMutex);
    disconnectConnection(host, connectionId);
    pthread_mutex_unlock(&host->conStates[connectionId].conMutex);
}

void checkConnectionStates(tnet_host* host)
{
    tnet_connection_state* connections = host->conStates;
    for(tnet_u32 i = 0; i < host->maxConnections; i++)
    {
        pthread_mutex_lock(&connections[i].conMutex);
        if(connections[i].state != tnet_connection_state_t::CEDisconnected)
        {
            if(getDurationToNowMs(connections[i].lastPacketReceived) > TNET_CONNECTION_TIMEOUT_MS)
            {
                disconnectConnection(host, i);
                printf("Disconnected\n");
            }
        }
        pthread_mutex_unlock(&connections[i].conMutex);
    }
}

unsigned short tnet_get_ping(tnet_host* host, unsigned int connectionId)
{
    // TODO: is this safe (another thread could be modifying the ping, but do we reallt care?)
    // TODO: what if connectionId is invalid
    return (unsigned short)host->conStates[connectionId].ping;
}

void sendPackets(tnet_host* host)
{
    tnet_time_point now;
    net_get_time(now);
    // queue ping messages where needed
    for(tnet_u32 i = 0; i < host->maxConnections; i++)
    {
        if(host->conStates[i].state == CEDisconnected)
            continue;
        else if(getDurationMs(host->conStates[i].lastPingRequest, now) >= TNET_PING_INTERVAL_MS)
        {
            // TODO: mutex
            host->conStates[i].lastPingRequest = now;
            queue_ping_message(host, i);
        }
    }

    tnet_queued_data q;
    tnet_packet p;

    tnet_connection_state* connections = host->conStates;
    pthread_mutex_lock(&host->sendBufMutex);
    // TODO: what happens if buf too small?
    // TODO: can invalid connectionid get here?
    while(tnet_ringqueue_dequeue(&host->sendBuffer, &q, sizeof(q)) > 0)
    {
        pthread_mutex_lock(&connections[q.connection].conMutex);
        sendPacket(host, p, q, connections[q.connection].messageId, now);
        if(q.reliable)
            connections[q.connection].messageId++;
        pthread_mutex_unlock(&connections[q.connection].conMutex);
    }
    pthread_mutex_unlock(&host->sendBufMutex);
    // check if everything needs resend
    tnet_sent_reliable_data rdata;
    bool got = tnet_ringqueue_peek(&host->resendBuffer, &rdata, sizeof(rdata));
    // dif in msec
    tnet_i32 dif = getDurationToNowMs(rdata.sendTime); // REMCONST
    while(got && dif >= 200) // REMCONST
    {
        bool isReceived = host->conStates[rdata.qData.connection].confirmedPackets[rdata.pId%TNET_CONFIRMED_PACKETS_HISTORY_SIZE] == rdata.pId;
        if(!isReceived)
        {
            //printf("Message resent\n");

            sendPacket(host, p, rdata.qData, rdata.messageId, now);
        }
        tnet_ringqueue_drop(&host->resendBuffer);
        got = tnet_ringqueue_peek(&host->resendBuffer, &rdata, sizeof(rdata));
        if (got)
            dif = getDurationToNowMs(rdata.sendTime); // REMCONST
    }

    // keep connections alive
    if(host->keepConnectionsAlive)
    {
        for(tnet_u32 i = 0; i < host->maxConnections; i++)
        {
            if(host->conStates[i].state == CEDisconnected)
                continue;
            else if(getDurationMs(host->conStates[i].lastPacketSent, now) >= TNET_CONNECTION_TIMEOUT_MS/2)
            {
                tnet_queue_data(host, i, 0, 0, false, TNET_SEND_DATA_FLAG_HEARTBEAT);
            }
        }
    }
}

void receivePacket(tnet_host* host, tnet_u32 connectionId, tnet_connection_state& connection, tnet_packet& p, tnet_i32 received, tnet_ringqueue* recBuf, tnet_host_event_t event)
{
    tnet_i32 size = p.size;
    if(size != received) // packet size didn't match, corrupted or incomplete
    {
        printf("Corrupted packet 1\n");
        return;
    }
    if(p.hasRel)
    {
        if(p.relBody.size != received - REL_HEADER_SIZE) // REMCONST
        {
            printf("Corrupted packet 2\n");
            return;
        }
        pthread_mutex_lock(&connection.conMutex); // <------------ CONNECTION MUTEX LOCK
        net_get_time(connection.lastPacketReceived);

        connection.packetsSinceAck++;
        if(connection.packetsSinceAck >= 25) // REMCONST
        {
            tnet_queue_data(host, connectionId, 0, 0, true, TNET_SEND_DATA_FLAG_HEARTBEAT);
            printf("Heartbeat\n");
        }

        tnet_u16 messageId = p.relBody.messageId;
        bool shouldDrop = connection.receivedMessages[messageId%TNET_RECEIVED_MESSAGE_HISTORY_SIZE] == messageId;

        if(!shouldDrop)
        {
            // TODO: replace assert with a useful measure (dc?)
            tnet_i32 cur = connection.receivedMessages[messageId%TNET_RECEIVED_MESSAGE_HISTORY_SIZE];
            assert(cur == 0xFFFF || cur == (tnet_u16)(messageId-TNET_RECEIVED_MESSAGE_HISTORY_SIZE)); // if this fails, it means that either a reliable data was dropped or receivedMessages array overflowed
            connection.receivedMessages[messageId%TNET_RECEIVED_MESSAGE_HISTORY_SIZE] = messageId;
        }
        shouldDrop = shouldDrop || (p.heartbeat == 1); // drop messages that are heartbeat
        // confirm what the remote party has received
        proccessRemoteAck(connection, p.relBody.ack, p.relBody.ackBits);
        // acknowledge the packet we received
        ackPacket(connection, p.relBody.seqId);
        pthread_mutex_unlock(&connection.conMutex);  // <------------ CONNECTION MUTEX UNLOCK
        if(!shouldDrop) // only receive if not received before
        {
            // TODO: check buffer overflow?
            tnet_received_event buf;
            buf.connection = connectionId;
            buf.size = p.relBody.size;
            buf.type = event;
            memcpy(buf.data, p.relBody.data, p.relBody.size);
            bool qd = tnet_ringqueue_queue(recBuf, &buf, sizeof(buf)-TNET_MAX_PACKET_SIZE+p.relBody.size);
            assert(qd);
            //printf("reliable data: %s \n", (char*)p.relBody.data);
        }
    }
    else
    {
        tnet_received_event buf;
        buf.connection = connectionId;
        buf.size = p.size-UREL_HEADER_SIZE; // 2 is the size of main header TODO:if you ever change header size
        buf.type = event;
        memcpy(buf.data, p.urelBody.data, buf.size);
        bool qd = tnet_ringqueue_queue(recBuf, &buf, sizeof(buf)-TNET_MAX_PACKET_SIZE+buf.size);
        assert(qd);
        //printf("unreliable data: %s \n", (char*)p.urelBody.data);
    }
}

struct receiveProcArgs
{
    tnet_host* host;
};

void proccessPing(tnet_host* host, tnet_u32 connection, unsigned char* data)
{
    unsigned char type = data[0];
    tnet_u16 latency;
    switch(type)
    {
    case TNET_PING_TYPE_PING: // request to ping back
        queue_pingback_message(host,connection,*((tnet_u16*)&data[1]));
        break;
    case TNET_PING_TYPE_PINGBACK: // response to our (we assume latest) ping request
        pthread_mutex_lock(&host->conStates[connection].conMutex);
        latency = *((tnet_u16*)&data[1]);
        tnet_time_point now;
        net_get_time(now);
        host->conStates[connection].ping = getDurationMs(host->conStates[connection].lastPingRequest,now)-latency;
        pthread_mutex_unlock(&host->conStates[connection].conMutex);
        break;
    default:
        assert(false); // this should only happen with corrupt packets
        break;
    }
}
void* receiveProc(void* context)
{
    receiveProcArgs* args = (receiveProcArgs*)context;
    tnet_i32 socket = args->host->socket;
    tnet_connection_state* connections = args->host->conStates;
    tnet_u32 maxConnections = args->host->maxConnections;
    tnet_ringqueue* receiveBuf = &args->host->receiveBuffer;
    tnet_host* host = args->host;
    free(context);
    tnet_packet buf;
    tnet_i32 received;
    tnet_u32 fromAddr;
    tnet_u16 fromPort;
    // TODO: packet bigger than 512 ends receiver thread!
    while(recvFromSocket(socket, (char*)&buf, received, fromAddr, fromPort))
    {
        tnet_i32 conId = findActiveConnectionByDest(connections, maxConnections, fromAddr, fromPort);
        if(conId != -1) // connection exists and active
        {
            // TODO: locking and unlocking 2 times in  a row
            pthread_mutex_lock(&connections[conId].conMutex);
            tnet_connection_state_t cstate = host->conStates[conId].state;
            pthread_mutex_unlock(&connections[conId].conMutex);

            if((cstate == CEConnected || cstate == CEConnecting) && buf.decline) // disconnect signal
            {
                pthread_mutex_lock(&connections[conId].conMutex);
                disconnectConnection(host, conId);
                pthread_mutex_unlock(&connections[conId].conMutex);
            }
            // TODO: wat is this shit
            else if(cstate == tnet_connection_state_t::CEConnected
                    && !buf.reqCon
                    && !buf.acceptCon
                    && !buf.decline
                    && !buf.heartbeat
                    && !buf.ping) // regular data
            {
                pthread_mutex_lock(&host->receiveBufMutex);
                if(buf.relBody.size != 86 && buf.relBody.size != 4)
                {
                    printf("what");
                }
                receivePacket(host, conId, connections[conId],buf, received, receiveBuf, tnet_host_event_t::HEData);
                pthread_mutex_unlock(&host->receiveBufMutex);
            }
            // TODO: macro for packet size for reliable ping message
            else if(cstate == tnet_connection_state_t::CEConnected && buf.size-UREL_HEADER_SIZE == TNET_PING_MESSSAGE_SIZE) // ping packet
            {
                proccessPing(host, conId, buf.urelBody.data);
            }
            else if(cstate == tnet_connection_state_t::CEConnecting && buf.acceptCon)
            {
                pthread_mutex_lock(&connections[conId].conMutex);
                connections[conId].state = tnet_connection_state_t::CEConnected;
                pthread_mutex_unlock(&connections[conId].conMutex);

                pthread_mutex_lock(&host->receiveBufMutex);
                receivePacket(host, conId, connections[conId],buf, received, receiveBuf, tnet_host_event_t::HEConnect);
                pthread_mutex_unlock(&host->receiveBufMutex);
            }
        }
        else if(buf.reqCon) // inactive or didn't exist
        {
            tnet_i32 newConId = findAndResetInactiveConnectionSlot(connections, maxConnections);
            if(newConId != -1)
            {
                pthread_mutex_lock(&connections[newConId].conMutex);
                connections[newConId].state = tnet_connection_state_t::CEConnecting;
                connections[newConId].destIP = fromAddr;
                connections[newConId].destPort = fromPort;
                pthread_mutex_unlock(&connections[newConId].conMutex);

                pthread_mutex_lock(&host->receiveBufMutex);
                receivePacket(host, newConId, connections[newConId],buf, received, receiveBuf, tnet_host_event_t::HEConnect);
                pthread_mutex_unlock(&host->receiveBufMutex);
            }
            else
            {
                printf("New connection, but no slots left!\n");
            }
        }
        else
        {
            printf("weird packet? \n");
        }
    }
    printf("Receive worker closed!\n");
    return 0;
}

struct sendProcArgs
{
    tnet_host* host;
};

void* sendProc(void* context)
{
    printf("Send worker started!\n");
    sendProcArgs* args = (sendProcArgs*)context;
    tnet_host* host = args->host;
    free(context);
    pthread_mutex_lock (&host->sendMut);
    while(true)                      //if while loop with signal complete first don't wait
    {
        while(host->sendDone)
        {
            pthread_cond_wait(&host->sendCon, &host->sendMut);
        }
        checkConnectionStates(host);
        sendPackets(host);
        host->sendDone = true;
    }
    pthread_mutex_unlock (&host->sendMut);
    printf("Send worker closed!\n");
    return 0;
}

bool tnet_create_host(tnet_host* host, tnet_u16 port, tnet_i32 maxConnections)
{
    host->maxConnections = maxConnections;
    // TODO: add to settings
    host->keepConnectionsAlive = true;
    //host->
    host->socket = openSocket(port);
    if(host->socket < 0)
        return false; // socket binding failed
    host->conStates = (tnet_connection_state*)malloc(maxConnections*sizeof(tnet_connection_state));
    if(host->conStates == 0)
        return false; // memory allocation for connection states failed
    for(int i=0; i<maxConnections;i++)
    {
        initConnectionState(host->conStates[i]);
        host->conStates[i].socket = host->socket;
    }
    // init buffer pointers to null so we dont crash when trying to free them
    host->resendBuffer.startAddr = 0;
    host->sendBuffer.startAddr = 0;
    host->receiveBuffer.startAddr = 0;
    // TODO: scale buffer size based on max connections?
    if(!tnet_ringqueue_initialize(&host->resendBuffer, tnet_Megabytes(10)))
    {
        tnet_free_host(host);
        return false; // memory allocation failed
    }
    if(!tnet_ringqueue_initialize(&host->sendBuffer, tnet_Megabytes(1)))
    {
        tnet_free_host(host);
        return false; // memory allocation failed
    }
    if(!tnet_ringqueue_initialize(&host->receiveBuffer, tnet_Megabytes(1)))
    {
        tnet_free_host(host);
        return false; // memory allocation failed
    }
    host->sendDone = true;
    host->sendMut=PTHREAD_MUTEX_INITIALIZER;
    host->sendCon=PTHREAD_COND_INITIALIZER;
    host->sendBufMutex=PTHREAD_MUTEX_INITIALIZER;
    host->receiveBufMutex=PTHREAD_MUTEX_INITIALIZER;
    receiveProcArgs* wargs = new receiveProcArgs;
    wargs->host = host;
    if(pthread_create(&host->recWorker, 0, receiveProc, wargs))
    {
        printf("Creating worker thread for host failed!\n");
        tnet_free_host(host);
        return false;
    }
    sendProcArgs* swargs = new sendProcArgs;
    swargs->host = host;
    if(pthread_create(&host->sendWorker, 0, sendProc, swargs))
    {
        printf("Creating worker thread for host failed!\n");
        tnet_free_host(host);
        return false;
    }
    return true;
}

void tnet_free_host(tnet_host* host)
{
    // TODO: end thread in a better way
    int canc = pthread_cancel(host->sendWorker);
    canc = pthread_cancel(host->recWorker);
    if(canc != 0)
    {
        printf("Canceling one of the threads failed!\n");
    }

    void *res;
    pthread_join(host->sendWorker,&res);
    pthread_join(host->recWorker,&res);

    //host->recWorker.join
    closeSocket(host->socket);
    free(host->conStates);
    tnet_ring_queue_free(&host->resendBuffer);
    tnet_ring_queue_free(&host->sendBuffer);
    tnet_ring_queue_free(&host->receiveBuffer);

    printf("Goodbye! o/\n");
}

tnet_host_event_t tnet_get_next_event(tnet_host* host, tnet_i32& connection, char* data, tnet_u32 size, tnet_i32& recSize)
{
    // TODO: 2 memcpys, remove 1
    // TODO: sure that no buffer overflow can happen?
    tnet_received_event event;
    pthread_mutex_lock(&host->receiveBufMutex);
    size_t result = tnet_ringqueue_dequeue(&host->receiveBuffer, &event, sizeof(event));
    pthread_mutex_unlock(&host->receiveBufMutex);
    if(result > 0)
    {
        connection = event.connection;
        if(event.type == tnet_host_event_t::HEData && size >= event.size)
        {
            memcpy(data, event.data, event.size);
            recSize = event.size;
        }
        return event.type;
    }
    else
    {
        return tnet_host_event_t::HENothing;
    }
}

void tnet_release_pending_data(tnet_host* host)
{
    pthread_mutex_lock (&host->sendMut);
    /*if(!host->sendDone)
        printf("main thread next iteration, but send thread not even started!\n");*/
    host->sendDone = false;
    //printf("signaling send worker\n");
    pthread_cond_signal(&host->sendCon);
    pthread_mutex_unlock (&host->sendMut);
    //pthread_yield(); // just in case
}

void tnet_queue_data(tnet_host* host, tnet_i32 connection, const char* data, const tnet_i32 dataSize, const bool reliable, tnet_u32 flags)
{
    // TODO: basically 2 memcpys, remove 1
    tnet_queued_data buf;
    buf.flags = flags;
    buf.connection = connection;
    buf.size = dataSize;
    buf.reliable = reliable;
    net_get_time(buf.queuedAt);
    memcpy(buf.data, data, dataSize);

    pthread_mutex_lock(&host->sendBufMutex);
    bool qd = tnet_ringqueue_queue(&host->sendBuffer, (char*)&buf, sizeof(tnet_queued_data)+dataSize-TNET_MAX_PACKET_SIZE);
    assert(qd);
    pthread_mutex_unlock(&host->sendBufMutex);
}

bool tnet_ringqueue_initialize(tnet_ringqueue* dq, size_t size)
{
    dq->startAddr = (char*)malloc(size);
    dq->dequeuePointer = dq->startAddr;
    dq->queuePointer = dq->startAddr;
    dq->state = tnet_ringqueue_state_t::Empty;
    dq->size = size;
    return dq->startAddr != NULL;
}

void tnet_ring_queue_free(tnet_ringqueue* dq)
{
    if(dq->startAddr != NULL)
        free(dq->startAddr);
}

// TODO: test before using this
bool tnet_ringqueue_doublesize(tnet_ringqueue* dq)
{
    char* newMem = (char*)malloc(dq->size*2);
    if(newMem == 0)
        return false;
    dq->dequeuePointer = newMem+(dq->dequeuePointer-dq->startAddr);
    dq->queuePointer = newMem+(dq->queuePointer-dq->startAddr);
    memmove(newMem,dq->startAddr,dq->size);
    dq->size *= 2;
    free(dq->startAddr);
    dq->startAddr = newMem;
    return true;
}

// sets to empty state
void tnet_ringqueue_reset(tnet_ringqueue* dq)
{
    dq->dequeuePointer = dq->startAddr;
    dq->queuePointer = dq->startAddr;
    dq->state = tnet_ringqueue_state_t::Empty;
}

// a reset with zeroing
void RingQueueZeroMemory(tnet_ringqueue* dq)
{
    memset(dq->startAddr, 0, dq->size);
    tnet_ringqueue_reset(dq);
}

// queues data to the buffer and returns true on success
bool tnet_ringqueue_queue(tnet_ringqueue* dq, const void* data, size_t size)
{
    if (size <= 0)
        return false;
    char* endaddr = dq->startAddr + dq->size;
    char* pointer = dq->queuePointer;
    char* next = (pointer + sizeof(size_t) + size);
    if (next >= endaddr) // back to start if not enough room
    {
        if (pointer + sizeof(size_t) <= endaddr)
            *(size_t*)pointer = 0;				// write 0, so the reader knows we went back to 0

        pointer = dq->startAddr;
        next = (pointer + sizeof(size_t) + size);

        if (dq->dequeuePointer == pointer)
        {
            return false;
        }
    }
    if ((pointer < dq->dequeuePointer && next >= dq->dequeuePointer) /*|| // buffer overflow would happen                                                                                                                             (pointer >= dq->dequeuePointer && next <= dq->dequeuePointer)*/)
    {
        dq->state = tnet_ringqueue_state_t::Full;
        return false;
    }
    *(size_t*)pointer = size;
    pointer += sizeof(size_t);
    memcpy(pointer, data, size);
    pointer += size;
    dq->queuePointer = pointer;
    if (dq->state == tnet_ringqueue_state_t::Empty)
    {
        dq->state = tnet_ringqueue_state_t::None;
    }
    return true;
}

// dequeues data from the buffer and returns number of bytes read
size_t tnet_ringqueue_dequeue(tnet_ringqueue* dq, void* data, unsigned int size)
{
    if (dq->state == tnet_ringqueue_state_t::Empty) // if its empty then its empty...
    {
        return 0;
    }
    char* pointer = dq->dequeuePointer;
    size_t cursize;
    if ((pointer + sizeof(size_t)) <= (dq->startAddr + dq->size)) // make sure we don't read from outside of the buffer
    {
        cursize = *(size_t*)pointer; // read the size
        if (cursize == 0) // size can only be 0 if the producer went bant back to start
        {
            pointer = dq->startAddr;
            cursize = *(size_t*)pointer; // read the size
        }
    }
    else // we would have read from outside the buffer bounds, back to start
    {
        pointer = dq->startAddr;
        cursize = *(size_t*)pointer; // read the size
    }
    assert(cursize > 0);
    if (cursize > size) // the buffer we were told to put the data was too small
    {
        return 0;
    }
    pointer += sizeof(size_t);
    memcpy(data, pointer, cursize);
    dq->dequeuePointer = pointer + cursize;
    if (dq->queuePointer == dq->dequeuePointer)
        dq->state = tnet_ringqueue_state_t::Empty;
    return cursize > 0;
}

// does the same as dequeuedata, but doesnt remove the data from queue
size_t tnet_ringqueue_peek(tnet_ringqueue* dq, void* data, unsigned int size)
{
    if (dq->state == tnet_ringqueue_state_t::Empty) // if its empty then its empty...
    {
        return 0;
    }
    char* pointer = dq->dequeuePointer;
    size_t cursize;
    if ((pointer + sizeof(size_t)) <= (dq->startAddr + dq->size)) // make sure we don't read from outside of the buffer
    {
        cursize = *(size_t*)pointer; // read the size
        if (cursize == 0) // size can only be 0 if the producer went bant back to start
        {
            pointer = dq->startAddr;
            cursize = *(size_t*)pointer; // read the size
        }
    }
    else // we would have read from outside the buffer bounds, back to start
    {
        pointer = dq->startAddr;
        cursize = *(size_t*)pointer; // read the size
    }
    assert(cursize > 0);
    if (cursize > size) // the buffer we were told to put the data was too small
    {
        return 0;
    }
    pointer += sizeof(size_t);
    memcpy(data, pointer, cursize);
    return cursize > 0;
}

tnet_ringqueue_state_t RingQueueGetState(tnet_ringqueue* dq)
{
    tnet_ringqueue_state_t state;
    state = dq->state;
    return state;
}

bool tnet_ringqueue_drop(tnet_ringqueue* dq)
{
    if (dq->state == tnet_ringqueue_state_t::Empty) // if its empty then its empty...
    {
        return 0;
    }
    char* pointer = dq->dequeuePointer;
    size_t cursize;
    if ((pointer + sizeof(size_t)) <= (dq->startAddr + dq->size)) // make sure we don't read from outside of the buffer
    {
        cursize = *(size_t*)pointer; // read the size
        if (cursize == 0) // size can only be 0 if the producer went bant back to start
        {
            pointer = dq->startAddr;
            cursize = *(size_t*)pointer; // read the size
        }
    }
    else // we would have read from outside the buffer bounds, back to start
    {
        pointer = dq->startAddr;
        cursize = *(size_t*)pointer; // read the size
    }
    assert(cursize > 0);
    pointer += sizeof(size_t);
    dq->dequeuePointer = pointer + cursize;
    if (dq->queuePointer == dq->dequeuePointer)
    dq->state = tnet_ringqueue_state_t::Empty;
    return cursize > 0;
}
//#endif // TNET_IMPLEMENTATION
#endif // TNET_H
