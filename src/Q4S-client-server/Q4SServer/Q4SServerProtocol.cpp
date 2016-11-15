#include "Q4SServerProtocol.h"

#include <stdio.h>
#include <vector>
#include <sstream>

#include "ETime.h"
#include "Q4SMathUtils.h"
#include "Q4SServerConfigFile.h"
#include "EKey.h"
#include "Q4SMessage.h"
#include "Q4SMessageTools.h"

#define     DEFAULT_CONN_ID     1

Q4SServerProtocol::Q4SServerProtocol ()
{
    clear();
}

Q4SServerProtocol::~Q4SServerProtocol ()
{
    done();
}

bool Q4SServerProtocol::init()
{
    // Prevention done call
    done();

    bool ok = true;

    if (ok)
    {
        mReceivedMessages.init( );
    }

    if (ok)
    {
        ok &= openConnectionListening();
    }

    if (ok)
    {
        ok &= mServerSocket.startAlertSender();
    }

    return ok;
}

void Q4SServerProtocol::done()
{
    bool closedConnectionAlertSender = mServerSocket.closeConnection(SOCK_DGRAM);
    if (!closedConnectionAlertSender)
    {
        printf( "Error closing sender socket connection.\n" );
    }

    closeConnectionListening();

    mReceivedMessages.done( );
}

void Q4SServerProtocol::clear()
{
    lastAlertTimeStamp = ULONG_MAX;
}

bool Q4SServerProtocol::openConnectionListening()
{
    bool ok = true;

    marrthrListenHandle[ 0 ] = CreateThread( 0, 0, ( LPTHREAD_START_ROUTINE )manageTcpConnectionsFn, ( void* ) this, 0, 0 );
    marrthrListenHandle[ 1 ] = CreateThread( 0, 0, ( LPTHREAD_START_ROUTINE )manageUdpReceivedDataFn, ( void* ) this, 0, 0 );
    
    return ok;
}

void Q4SServerProtocol::closeConnectionListening()
{
    bool ok = true;

    if( ok )
    {
        mServerSocket.stopWaiting( );
    }
    if( ok )
    {
        WaitForMultipleObjects( 2, marrthrListenHandle, true, INFINITE );
    }

    if( !ok )
    {
        //TODO: launch error
        printf( "Error closing connections.\n" );
    }
}

void Q4SServerProtocol::closeConnections()
{
    bool ok = true;

    if( ok )
    {
        ok &= mServerSocket.closeConnection( SOCK_STREAM );
        ok &= mServerSocket.closeConnection( SOCK_DGRAM );
        WaitForMultipleObjects( 2, marrthrDataHandle, true, INFINITE );
    }

    if( !ok )
    {
        //TODO: launch error
        printf( "Error closing connections.\n" );
    }
}


// State managing functions.

bool Q4SServerProtocol::begin()
{
    printf("WAITING FOR BEGIN\n");
    std::string message;

    bool ok = true;
    
    if ( ok ) 
    {
        // Wait for a message
        mReceivedMessages.readFirst( message );
    }

    if (ok)
    {
        std::string pattern;
        pattern.assign( "BEGIN" );
        if ( message.substr( 0, pattern.size( ) ).compare( pattern ) != 0)
        {
            ok = false;
        }
    }
    
    if( ok )
    {
        ok &= mServerSocket.sendTcpData( DEFAULT_CONN_ID, "200 OK" );
    }

    return ok;
}

bool Q4SServerProtocol::ready()
{
    printf("WAITING FOR READY\n");
    std::string message;

    bool ok = true;

    if ( ok ) 
    {
        ok &= mReceivedMessages.readFirst( message );
    }

    if (ok)
    {
        std::string pattern;
        pattern.assign( "READY" );
        if ( message.substr( 0, pattern.size( ) ).compare( pattern ) != 0)
        {
            ok = false;
        }
    }

    if( ok )
    {
        ok &= mServerSocket.sendTcpData( DEFAULT_CONN_ID, "200 OK" );
    }

    return ok;
}

bool Q4SServerProtocol::measure(float maxLatency, float maxJitter, float minBandWith, float maxPacketLoss, float &latency, float &jitter)
{
    bool measureOk = true;

    printf("MEASURING\n");

    measureOk = Q4SServerProtocol::measureStage0(maxLatency, maxJitter, latency, jitter);
    if (measureOk)
    {
        measureOk = Q4SServerProtocol::measureStage1( minBandWith, maxPacketLoss);
    }

    // Check if CANCEL has been received
    if (mReceivedMessages.size() != 0)
    {
        Q4SMessageInfo message;
        std::string pattern;
        pattern.assign( "CANCEL" );
        bool cancelMessageReceived = mReceivedMessages.readMessage( pattern, message, false);
        if ( cancelMessageReceived)
        {
            printf("CANCEL received: %s\n", message.message.c_str());
            measureOk = false;
        }
    }

    return measureOk;
}

void Q4SServerProtocol::continuity(float maxLatency, float maxJitter, float minBandWith, float maxPacketLoss)
{
    bool stop = false;
    bool measureOk = true;

    while ( !stop )
    {
        float latency;
        float jitter;
        measureOk = measure(maxLatency, maxJitter, minBandWith, maxPacketLoss, latency, jitter);
        if (!measureOk)
        {
            //Alert
            std::string alertMessage;
            alertMessage= "Latency: " + std::to_string((long double)latency) + " Jitter: " + std::to_string((long double)jitter);
            alert(alertMessage);
        }
//        stop = !measureOk;
    }
}


void Q4SServerProtocol::bwidth()
{
    printf("METHOD: bwidth\n");
}

void Q4SServerProtocol::cancel()
{
    printf("METHOD: cancel\n");
}

void Q4SServerProtocol::alert(std::string alertMessage)
{
    unsigned long actualTime = ETime_getTime();
    unsigned long timeFromLastAlert = actualTime - lastAlertTimeStamp;
    if ( timeFromLastAlert > q4SServerConfigFile.timeBetweenAlerts)
    {
        lastAlertTimeStamp = actualTime;

        std::string message = "ALERT "+alertMessage;
        mServerSocket.sendAlertData(message.c_str());

        printf("METHOD: alert\n");
    }
}

void Q4SServerProtocol::end()
{
    closeConnections();
}

//--private:-------------------------------------------------------------------------------

bool Q4SServerProtocol::measureStage0(float maxLatency, float maxJitter, float &latency, float &jitter)
{
    bool ok = true;

    std::vector<unsigned long> arrSentPingTimestamps;

    if ( ok ) 
    {
        // Wait to recive the first Ping
        Q4SMessageInfo  messageInfo;
        ok &= mReceivedMessages.readPingMessage( 0, messageInfo, false );
    }

    if(!ok)
    {
        printf( "ERROR:PING 0 is not the first message.\n" );
    }

    if( ok )
    {
        // Send regular pings
        ok &= sendRegularPings(arrSentPingTimestamps);
    }

    if(!ok)
    {
        printf( "ERROR:sendUdpData PING.\n" );
    }

    if (ok)
    {
        // Wait the established time to start calculation
        Sleep( (DWORD)q4SServerConfigFile.timeStartCalc);

        // Calculate Latency
        calculateLatency(arrSentPingTimestamps, latency, q4SServerConfigFile.showMeasureInfo);
        printf( "MEASURING RESULT - Latency: %.3f\n", latency );

        // Calculate Jitter
        calculateJitter(jitter, true, q4SServerConfigFile.showMeasureInfo);
        printf( "MEASURING RESULT - Jitter: %.3f\n", jitter );

        // Check latency and jitter limits
        ok &= checkLatencyAndJitter(latency, jitter, maxLatency, maxJitter);
    }

    return ok;
}
bool Q4SServerProtocol::sendRegularPings(std::vector<unsigned long> &arrSentPingTimestamps)
{
    bool ok = true;

    Q4SMessage message;
    unsigned long timeStamp = 0;
    int pingIndex = 0;
    int pingMaxCount = 20;

    for( pingIndex = 0; pingIndex < pingMaxCount; pingIndex++ )
    {
        // Store the timestamp
        timeStamp = ETime_getTime( );
        arrSentPingTimestamps.push_back( timeStamp );

        // Prepare message and send
        message.init(Q4SMREQUESTORRESPOND_REQUEST, Q4SMTYPE_PING,"myIp", q4SServerConfigFile.defaultUDPPort, pingIndex, timeStamp);
        ok &= mServerSocket.sendUdpData( DEFAULT_CONN_ID, message.getMessageCChar() );

        // Wait the established time between pings
        Sleep( (DWORD)q4SServerConfigFile.timeBetweenPings );
    }

    return ok;
}

void Q4SServerProtocol::calculateLatency(std::vector<unsigned long> &arrSentPingTimestamps, float &latency, bool showMeasureInfo)
{
    char messagePattern[ 256 ];
    std::string pattern;
    Q4SMessageInfo messageInfo;
    std::vector<float> arrPingLatencies;
    float actualPingLatency;
    int pingIndex = 0;
    int pingMaxCount = 20;


    // Prepare for Latency calculation
    for( pingIndex = 0; pingIndex < pingMaxCount; pingIndex++ )
    {
        // Generate pattern
        sprintf_s( messagePattern, "200 OK %d", pingIndex );
        pattern = messagePattern;

        if( mReceivedMessages.readMessage( pattern, messageInfo, true ) == true )
        {
            // Actual ping latency calculation
            actualPingLatency = (messageInfo.timeStamp - arrSentPingTimestamps[ pingIndex ])/ 2.0f;

            // Latency store
            arrPingLatencies.push_back( actualPingLatency );

            if (showMeasureInfo)
            {
                printf( "PING %d actual ping latency: %.3f\n", pingIndex, actualPingLatency );
            }
        }
        else
        {
                printf( "PING %d message lost\n", pingIndex);
        }
    }

    // Latency calculation
    latency = EMathUtils_median( arrPingLatencies );
}

void Q4SServerProtocol::calculateJitter(float &jitter, bool showResult, bool showMeasureInfo)
{
    int pingIndex = 0;
    int pingMaxCount = 20;
    Q4SMessageInfo messageInfo;
    std::vector<unsigned long> arrReceivedPingTimestamps;
    std::vector<unsigned long> arrPingJitters;
    unsigned long actualPingTimeWithPrevious;

    // Prepare for Jitter calculation
    for( pingIndex = 0; pingIndex < pingMaxCount; pingIndex++ )
    {
        if( mReceivedMessages.readPingMessage( pingIndex, messageInfo, true ) == true )
        {
            arrReceivedPingTimestamps.push_back( messageInfo.timeStamp );
            if( pingIndex > 0 )
            {
                // Actual time between this ping and previous calculation
                actualPingTimeWithPrevious = ( arrReceivedPingTimestamps[ pingIndex ] - arrReceivedPingTimestamps[ pingIndex - 1 ] );

                // Actual time between this ping and previous store
                arrPingJitters.push_back( actualPingTimeWithPrevious );

                if (q4SServerConfigFile.showMeasureInfo)
                {
                    printf( "PING %d ET: %.3f\n", pingIndex, actualPingTimeWithPrevious );
                }
            }
        }
    }

    // Jitter calculation
    float meanOfTimeWithPrevious = EMathUtils_mean( arrPingJitters );
    jitter = meanOfTimeWithPrevious - (float)q4SServerConfigFile.timeBetweenPings;

    if (showMeasureInfo)
    {
        printf( "Time With previous ping mean: %.3f\n", meanOfTimeWithPrevious );
    }
}

bool Q4SServerProtocol::checkLatencyAndJitter(float latency, float jitter, float maxLatency, float maxJitter)
{
    bool ok = true;

    if ( latency > maxLatency )
    {
        printf( "Lantecy limits not reached\n");
        ok = false;
    }

    if ( jitter > maxJitter)
    {
        printf( "Jitter limits not reached\n");
        ok = false;
    }

    return ok;
}

bool Q4SServerProtocol::measureStage1(float minBandWith, float maxPacketLoss)
{
    bool ok = true;

    // TODO measure bandwith and packet loss

    return ok;
}


// Incoming connection managing functions.

DWORD WINAPI Q4SServerProtocol::manageTcpConnectionsFn( LPVOID lpData )
{
    Q4SServerProtocol* q4sCP = ( Q4SServerProtocol* )lpData;
    return q4sCP->manageTcpConnection( );
}

//DWORD WINAPI Q4SServerProtocol::manageUdpConnectionsFn( LPVOID lpData )
//{
//    Q4SServerProtocol* q4sCP = ( Q4SServerProtocol* )lpData;
//    return q4sCP->manageUdpConnection( );
//}

bool Q4SServerProtocol::manageTcpConnection( )
{
    bool        ok = true;
    int         newConnId = 1;

    if( ok )
    {
        ok &= mServerSocket.startTcpListening( );
    }

    while( ok )
    {
        ok &= mServerSocket.waitForTcpConnection( newConnId );
        if( ok )
        {
            ManageTcpConnectionsFnInfo  fnInfo;
            fnInfo.pThis = this;
            fnInfo.connId = newConnId;
            marrthrDataHandle[ 0 ] = CreateThread( 0, 0, ( LPTHREAD_START_ROUTINE )manageTcpReceivedDataFn, ( void* ) &fnInfo, 0, 0 );
        }
        newConnId++;
    }

    return ok;
}

//bool Q4SServerProtocol::manageUdpConnection( )
//{
//    bool                ok = true;
//    
//    if( ok )
//    {
//        ok &= mServerSocket.waitForConnections( SOCK_DGRAM );
//    }
//    if( ok )
//    {
//        marrthrDataHandle[ 1 ] = CreateThread( 0, 0, ( LPTHREAD_START_ROUTINE )manageUdpReceivedDataFn, ( void* ) this, 0, 0 );
//    }
//
//    return ok;
//}


// Received data managing functions.

DWORD WINAPI Q4SServerProtocol::manageTcpReceivedDataFn( LPVOID lpData )
{
    ManageTcpConnectionsFnInfo* q4sCFI = ( ManageTcpConnectionsFnInfo* )lpData;
    return q4sCFI->pThis->manageTcpReceivedData( q4sCFI->connId );
}

DWORD WINAPI Q4SServerProtocol::manageUdpReceivedDataFn( LPVOID lpData )
{
    Q4SServerProtocol* q4sCP = ( Q4SServerProtocol* )lpData;
    return q4sCP->manageUdpReceivedData( );
}

bool Q4SServerProtocol::manageTcpReceivedData( int connId )
{
    bool                ok = true;
    char                buffer[ 65536 ];

    while( ok ) 
    {
        ok &= mServerSocket.receiveTcpData( connId, buffer, sizeof( buffer ) );
        if( ok )
        {
            std::string message = buffer;
            mReceivedMessages.addMessage ( message );
            printf( "Received: <%s>\n", buffer );
        }
    }
    
    return ok;
}

bool Q4SServerProtocol::manageUdpReceivedData( )
{
    bool                ok = true;
    char                udpBuffer[ 65536 ];
    int                 connId;

    mServerSocket.startUdpListening( );

    while ( ok )
    {
        ok &= mServerSocket.receiveUdpData( udpBuffer, sizeof( udpBuffer ), connId );

        if( ok )
        {
            unsigned long actualTimeStamp = ETime_getTime();

            std::string message = udpBuffer;

            int pingNumber = 0;
            unsigned long receivedTimeStamp = 0;

            // Comprobar que es un ping
            if ( Q4SMessageTools_isPingMessage(udpBuffer, &pingNumber, &receivedTimeStamp) )
            {
                if (q4SServerConfigFile.showReceivedPingInfo)
                {
                    printf( "Received Ping, number:%d, timeStamp: %d\n", pingNumber, receivedTimeStamp);
                }

                // mandar respuesta del ping
                char buffer[ 256 ];
                if (q4SServerConfigFile.showReceivedPingInfo)
                {
                    printf( "Ping responsed %d\n", pingNumber);
                }
                sprintf_s( buffer, "200 OK %d", pingNumber );
                ok &= mServerSocket.sendUdpData( connId, buffer );
            
                // encolar el ping y el timestamp para el calculo del jitter
                mReceivedMessages.addMessage(message, receivedTimeStamp);
            }
            else
            {
                // encolar el 200 ok y el timestamp actual para el calculo de la latencia
                mReceivedMessages.addMessage(message, actualTimeStamp);
            }

            if (q4SServerConfigFile.showReceivedPingInfo)
            {
                printf( "Received Udp: <%s>\n", udpBuffer );
            }
        }

        // Key management
        if (EKey_getKeyState(EK_A))
        {
            printf( "Alert Sended\n");
            std::string alertMessage;
            alertMessage= "Alert by keyboard";
            alert(alertMessage);
        }

    }

    return ok;
}