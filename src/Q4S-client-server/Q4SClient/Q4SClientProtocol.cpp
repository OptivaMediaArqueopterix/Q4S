#include "Q4SClientProtocol.h"

#include <stdio.h>
#include <sstream>

#include "ETime.h"
#include "Q4SMathUtils.h"
#include "Q4SClientConfigFile.h"
#include "EKey.h"
#include "Q4SMessage.h"
#include "Q4SMessageTools.h"

Q4SClientProtocol::Q4SClientProtocol ()
{
    clear();
}

Q4SClientProtocol::~Q4SClientProtocol ()
{
    done();
}

bool Q4SClientProtocol::init()
{
    // Prevention done call
    done();

    bool ok = true;

    if (ok)
    {
        ok &= mReceivedMessages.init( );
    }

    if (ok)
    {
        ok &= openConnections();
    }

    return ok;
}

void Q4SClientProtocol::done()
{
    closeConnections();
    mReceivedMessages.done( );
}

void Q4SClientProtocol::clear()
{
}

bool Q4SClientProtocol::openConnections()
{
    bool ok = true;

    //open connections
    if( ok )
    {
        ok &= mClientSocket.openConnection( SOCK_STREAM );
        ok &= mClientSocket.openConnection( SOCK_DGRAM );
    }

    // launch received data managing threads.
    if( ok )
    {
        marrthrHandle[ 0 ] = CreateThread( 0, 0, manageUdpReceivedDataFn, ( LPVOID )this, 0, 0 );
        marrthrHandle[ 1 ] = CreateThread( 0, 0, manageTcpReceivedDataFn, ( LPVOID )this, 0, 0 );
    }

    return ok;
}

void Q4SClientProtocol::closeConnections()
{
    bool ok = true;

    if( ok )
    {
        ok &= mClientSocket.closeConnection( SOCK_STREAM );
        ok &= mClientSocket.closeConnection( SOCK_DGRAM );
        WaitForMultipleObjects( 2, marrthrHandle, true, INFINITE );
    }

    if( !ok )
    {
        //TODO: launch error
        printf( "Error closing connections.\n" );
    }
}


// State managing functions.

bool Q4SClientProtocol::begin()
{
    printf("METHOD: begin\n");

    bool ok = true;
    
    if( ok )
    {
        Q4SMessage message;
        message.init(Q4SMREQUESTORRESPOND_REQUEST, Q4SMTYPE_BEGIN, "myIP", q4SClientConfigFile.defaultTCPPort);
        ok &= mClientSocket.sendTcpData( message.getMessageCChar() );
    }

    if ( ok ) 
    {
        std::string message;
        mReceivedMessages.readFirst( message );
    }

    return ok;
}

bool Q4SClientProtocol::ready()
{
    printf("METHOD: ready\n");

    bool ok = true;
    
    if( ok )
    {
        Q4SMessage message;
        message.init(Q4SMREQUESTORRESPOND_REQUEST, Q4SMTYPE_READY, "myIP", q4SClientConfigFile.defaultTCPPort);
        ok &= mClientSocket.sendTcpData( message.getMessageCChar() );
    }

    if ( ok ) 
    {
        std::string message;
        mReceivedMessages.readFirst( message );
    }

    return ok;
}

bool Q4SClientProtocol::measure(Q4SMeasurementLimits limits, Q4SMeasurementResult &results)
{
    bool measureOk = true;

    printf("MEASURING\n");

    measureOk = Q4SClientProtocol::measureStage0(limits.stage0, results);
    if (measureOk)
    {
        measureOk = Q4SClientProtocol::measureStage1(limits.stage1, results);
    }

    return measureOk;
}

void Q4SClientProtocol::continuity(Q4SMeasurementLimits limits)
{
    bool stop = false;
    bool measureOk = true;

    while ( !stop )
    {
        Q4SMeasurementResult results;
        measureOk = measure(limits, results);
        // TODO notificar del error al Server
        //stop = !measureOk;
    }
}

void Q4SClientProtocol::bwidth()
{
    printf("METHOD: bwidth TODO\n");
}

void Q4SClientProtocol::cancel()
{
    printf("METHOD: cancel TODO\n");
    mClientSocket.sendTcpData( "CANCEL" );
}

//--private:-------------------------------------------------------------------------------

bool Q4SClientProtocol::measureStage0(Q4SMeasurementStage0Limits limits, Q4SMeasurementResult &results)
{
    bool ok = true;

    std::vector<unsigned long> arrSentPingTimestamps;

    if (ok)
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
        Sleep( (DWORD)q4SClientConfigFile.timeStartCalc);

        // Calculate Latency
        calculateLatency(arrSentPingTimestamps, results.values.latency, q4SClientConfigFile.showMeasureInfo);
        printf( "MEASURING RESULT - Latency: %.3f\n", results.values.latency );

        // Calculate Jitter
        calculateJitter(results.values.jitter, q4SClientConfigFile.showMeasureInfo);
        printf( "MEASURING RESULT - Jitter: %.3f\n", results.values.jitter );

        // Check latency and jitter limits
        ok &= checkStage0(limits, results);
    }
    
    return ok;
}

bool Q4SClientProtocol::sendRegularPings(std::vector<unsigned long> &arrSentPingTimestamps)
{
    bool ok = true;

    Q4SMessage message;
    unsigned long timeStamp = 0;
    int pingNumber = 0;
    int pingNumberToSend = 20;

    for ( pingNumber = 0; pingNumber < pingNumberToSend; pingNumber++ )
    {
        // Store the timestamp
        timeStamp = ETime_getTime( );
        arrSentPingTimestamps.push_back( timeStamp );

        // Prepare message and send
        message.init(Q4SMREQUESTORRESPOND_REQUEST, Q4SMTYPE_PING,"myIp", q4SClientConfigFile.defaultUDPPort, pingNumber, timeStamp);
        ok &= mClientSocket.sendUdpData( message.getMessageCChar() );

        // Wait the established time between pings
        Sleep( (DWORD)q4SClientConfigFile.timeBetweenPings );
    }

    return ok;
}

void Q4SClientProtocol::calculateLatency(std::vector<unsigned long> &arrSentPingTimestamps, float &latency, bool showMeasureInfo)
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
                printf( "PING %d actual ping latency: %d\n", pingIndex, actualPingLatency );
            }
        }
        else
        {
                printf( "PING %d message lost\n", pingIndex);
        }
    }

    // Latency calculation
    latency = EMathUtils_median( arrPingLatencies ) ;
}

void Q4SClientProtocol::calculateJitter(float &jitter, bool showMeasureInfo)
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

                if (showMeasureInfo)
                {
                    printf( "PING %d ET: %d\n", pingIndex, actualPingTimeWithPrevious );
                }
            }
        }
    }

    // Jitter calculation
    float meanOfTimeWithPrevious = EMathUtils_mean( arrPingJitters );
    jitter = meanOfTimeWithPrevious - (float)q4SClientConfigFile.timeBetweenPings;

    if (showMeasureInfo)
    {
        printf( "Time With previous ping mean: %.3f\n", meanOfTimeWithPrevious );
    }
}

bool Q4SClientProtocol::checkStage0(Q4SMeasurementStage0Limits limits, Q4SMeasurementResult &results)
{
    bool ok = true;

    if ( results.values.latency > limits.maxLatency )
    {
        results.latency = true;
        printf( "Lantecy limits not reached\n");
        ok = false;
    }

    if ( results.values.jitter > limits.maxJitter)
    {
        results.jitter = true;
        printf( "Jitter limits not reached\n");
        ok = false;
    }

    return ok;
}

bool Q4SClientProtocol::measureStage1(Q4SMeasurementStage1Limits limits, Q4SMeasurementResult &results)
{
    bool ok = true;

    // TODO measure bandwith and packet loss


    return ok;
}

// Received data managing functions.

DWORD WINAPI Q4SClientProtocol::manageTcpReceivedDataFn( LPVOID lpData )
{
    Q4SClientProtocol* q4sCP = ( Q4SClientProtocol* )lpData;
    bool ret = q4sCP->manageTcpReceivedData( );
    return ret;
}

DWORD WINAPI Q4SClientProtocol::manageUdpReceivedDataFn( LPVOID lpData )
{
    Q4SClientProtocol* q4sCP = ( Q4SClientProtocol* )lpData;
    bool ret = q4sCP->manageUdpReceivedData( );
    return ret;
}

bool Q4SClientProtocol::manageTcpReceivedData( )
{
    bool                ok = true;
    char                buffer[ 65536 ];
    
    while (ok) 
    {
        ok &= mClientSocket.receiveTcpData( buffer, sizeof( buffer ) );
        if( ok )
        {
            std::string message = buffer;
            mReceivedMessages.addMessage ( message );
            printf( "Received Tcp: <%s>\n", buffer );
        }
    }

    return ok;
}

bool Q4SClientProtocol::manageUdpReceivedData( )
{
    bool                ok = true;
    char                udpBuffer[ 65536 ];

    while ( ok )
    {
        ok &= mClientSocket.receiveUdpData( udpBuffer, sizeof( udpBuffer ) );

        if( ok )
        {
            unsigned long actualTimeStamp = ETime_getTime();

            std::string message = udpBuffer;

            int pingNumber = 0;
            unsigned long receivedTimeStamp = 0;

            // Comprobar que es un ping
            if ( Q4SMessageTools_isPingMessage(udpBuffer, &pingNumber, &receivedTimeStamp) )
            {
                if (q4SClientConfigFile.showReceivedPingInfo)
                {
                    printf( "Received Ping, number:%d, timeStamp: %d\n", pingNumber, receivedTimeStamp);
                }

                // mandar respuesta del ping
                char buffer[ 256 ];
                if (q4SClientConfigFile.showReceivedPingInfo)
                {
                    printf( "Ping responsed %d\n", pingNumber);
                }
                sprintf_s( buffer, "200 OK %d", pingNumber );
                ok &= mClientSocket.sendUdpData( buffer );
            
                // encolar el ping y el timestamp para el calculo del jitter
                mReceivedMessages.addMessage(message, actualTimeStamp);
            }
            else
            {
                // encolar el 200 ok y el timestamp actual para el calculo de la latencia
                mReceivedMessages.addMessage(message, actualTimeStamp);
            }

            if (q4SClientConfigFile.showReceivedPingInfo)
            {
                printf( "Received Udp: <%s>\n", udpBuffer );
            }
        }

        // Key management
        if (EKey_getKeyState(EK_C))
        {
            printf( "CANCEL key pressed\n");
            cancel();
            ok = false;
        }
    }

    return ok;
}