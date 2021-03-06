#ifndef _Q4SCLIENTCONFIGFILE_H_
#define _Q4SCLIENTCONFIGFILE_H_

#include "..\Q4SCommon\ConfigFile.h"

class Q4SClientConfigFile
{
public:

    Q4SClientConfigFile(const std::string &fName);

    std::string serverIP;
    std::string defaultTCPPort;
    std::string defaultUDPPort;
    unsigned long timeStartCalc;
    unsigned long timeEndApp;
    bool showMeasureInfo;
    bool showReceivedPingInfo;
    bool showSocketReceivedInfo;
	unsigned long serverConnectionRetryTimes;
	unsigned long serverConnectionMilisecondsBetweenTimes;
	bool waitForLaunchGANY;
};

extern Q4SClientConfigFile q4SClientConfigFile;

#endif //_Q4SCLIENTCONFIGFILE_H_