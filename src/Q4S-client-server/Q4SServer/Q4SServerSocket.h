#ifndef _Q4SSERVERSOCKET_H_
#define _Q4SSERVERSOCKET_H_

#include "Q4SSocket.h"

#include <stdio.h>

class Q4SServerSocket
{
public:

    // Constructor-Destructor
    Q4SServerSocket( );
    ~Q4SServerSocket( );

    // Init-Done
    bool    init( );
    void    done( );

    bool    waitForConnections( int socketType );
    bool    stopWaiting( );
    bool    closeConnection( int socketType );
    bool    sendTcpData( const char* sendBuffer );
    bool    receiveTcpData( char* receiveBuffer, int receiveBufferSize );
    bool    sendUdpData( const char* sendBuffer );
    bool    receiveUdpData( char* receiveBuffer, int receiveBufferSize );

private:

    void    clear( );

    bool    initializeSockets( );
    bool    createListenSocket( int socketType );
    bool    bindListenSocket( int socketType );
    bool    startListen( );
    bool    acceptClientConnection( Q4SSocket* q4sSocket );
    bool    closeListenSocket( );

    SOCKET              mListenSocket;
    SOCKET              mUdpSocket;
    struct addrinfo*    mpAddrInfoResultTcp; 
    struct addrinfo*    mpAddrInfoResultUdp; 

    Q4SSocket           mq4sTcpSocket;
    Q4SSocket           mq4sUdpSocket;
};

#endif  // _Q4SSERVERSOCKET_H_