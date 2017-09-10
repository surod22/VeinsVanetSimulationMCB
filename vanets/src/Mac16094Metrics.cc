//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
// 
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see http://www.gnu.org/licenses/.
// 

#include "Mac16094Metrics.h"

#include <../../veins/src/veins/modules/phy/DeciderResult80211.h>
#include <../../veins/src/veins/base/phyLayer/PhyToMacControlInfo.h>
using namespace std;
#define DBG_MAC EV
Define_Module(Mac16094Metrics); 

void Mac16094Metrics::initialize(int i){
    cout<<setiosflags(ios::fixed)<<setprecision(10);

    metrics= new Metrics();
    statsReceivedPackets = 0;
    statsMbpsReceived = 0;
    statsControlMbpsReceived = 0;
    throughputMetricMac = 0;
    throughputMbps = 0;
    throughputControlMbps = 0;
    receivedFramesLowerMsg = 0;
    receivedBitsLowerPackets = 0;
    receivedBitsLowerWsm = 0;
    packetsNotForMe = 0;
    statsReceivedBits = 0 ;
    collisionsPktNonDecoded = 0;


    throughputSignalMac = registerSignal("throughputSignalMac");

    Mac1609_4::initialize(i);

    WATCH(throughputMetricMac);
    WATCH(throughputMbps);
    WATCH(throughputControlMbps);
    WATCH(collisionsPktNonDecoded);
}


void Mac16094Metrics::finish(){

    recordScalar("throughputMetricMac", throughputMetricMac);
    recordScalar("throughputMbps", throughputMbps);
    recordScalar("throughputControlMbps", throughputControlMbps);
    recordScalar("receivedFramesLowerMsg",receivedFramesLowerMsg);
    recordScalar("receivedBitsLowerPackets",receivedBitsLowerPackets);
    recordScalar("receivedBitsLoserWsm", receivedBitsLowerPackets);
    recordScalar("packetsNotForMe", packetsNotForMe);
    recordScalar("receivedTotalBits", statsReceivedBits);
    recordScalar("collisionsPktNonDecoded", collisionsPktNonDecoded);
    Mac1609_4::finish();
}

void Mac16094Metrics::handleLowerMsg(cMessage* message){

    Mac80211Pkt* macPkt = static_cast<Mac80211Pkt*>(message);
    ASSERT(macPkt);



    WaveShortMessage*  wsm =  dynamic_cast<WaveShortMessage*>(macPkt->decapsulate());
    receivedFramesLowerMsg++;


    double macPktBitLength = (macPkt->getBitLength());
    receivedBitsLowerPackets= receivedBitsLowerPackets + macPktBitLength;


    double tempBitLength = (wsm->getWsmLength());
    receivedBitsLowerWsm= receivedBitsLowerWsm + tempBitLength;

    //pass information about received frame to the upper layers
    DeciderResult80211 *macRes = dynamic_cast<DeciderResult80211 *>(PhyToMacControlInfo::getDeciderResult(message));
    ASSERT(macRes);
    DeciderResult80211 *res = new DeciderResult80211(*macRes);
    wsm->setControlInfo(new PhyToMacControlInfo(res));

    long dest = macPkt->getDestAddr();

    DBG_MAC << "Received frame name= " << macPkt->getName()
            << ", myState=" << " src=" << macPkt->getSrcAddr()
            << " dst=" << macPkt->getDestAddr() << " myAddr="
            << myMacAddress << std::endl;

    if (macPkt->getDestAddr() == myMacAddress) {
        DBG_MAC << "Received a data packet addressed to me." << std::endl;
        statsReceivedPackets++;

        double statsReceivedPacketsDbl = (double) statsReceivedPackets;
        double time = simTime().dbl();

        sendUp(wsm);
    }
    else if (dest == LAddress::L2BROADCAST()) {

        cout<<setiosflags(ios::fixed)<<setprecision(16);

        statsReceivedBroadcasts++;
        double statsReceivedBroadcastsDbl = (double) statsReceivedBroadcasts;
        double time = simTime().dbl();

        double messageBits = (double)wsm->getBitLength();
        statsReceivedBits = statsReceivedBits + messageBits;
        computeThroughput(metrics, statsReceivedBroadcastsDbl,time);
        computeThroughputMbps(metrics, messageBits, statsMbpsReceived, time);

        sendUp(wsm);
    }
    else {
        DBG_MAC << "Packet not for me, deleting..." << std::endl;
        packetsNotForMe++;
        delete wsm;
    }
    delete macPkt;
}

void Mac16094Metrics::handleUpperMsg(cMessage* message){
    Mac1609_4::handleUpperMsg(message);
}

void Mac16094Metrics::handleSelfMsg(cMessage* message){
    Mac1609_4::handleSelfMsg(message);
}

void Mac16094Metrics::handleLowerControl(cMessage* message){
    if (message->getKind() == MacToPhyInterface::TX_OVER) {

        DBG_MAC << "Successfully transmitted a packet on " << lastAC << std::endl;

        phy->setRadioState(Radio::RX);

        //message was sent
        //update EDCA queue. go into post-transmit backoff and set cwCur to cwMin
        myEDCA[activeChannel]->postTransmit(lastAC);
        //channel just turned idle.
        //don't set the chan to idle. the PHY layer decides, not us.

        if (guardActive()) {
            throw cRuntimeError("We shouldnt have sent a packet in guard!");
        }
    }
    else if (message->getKind() == Mac80211pToPhy11pInterface::CHANNEL_BUSY) {
        channelBusy();
    }
    else if (message->getKind() == Mac80211pToPhy11pInterface::CHANNEL_IDLE) {
        channelIdle();
    }
    else if (message->getKind() == Decider80211p::BITERROR || message->getKind() == Decider80211p::COLLISION) {
        statsSNIRLostPackets++;
        DBG_MAC << "A packet was not received due to biterrors" << std::endl;
    }
    else if (message->getKind() == Decider80211p::NOT_DECODED){
        collisionsPktNonDecoded++;
        DBG_MAC << "A packet was not received due to NOT_DECODED" << std::endl;

    }
    else if (message->getKind() == Decider80211p::RECWHILESEND) {
        statsTXRXLostPackets++;
        DBG_MAC << "A packet was not received because we were sending while receiving" << std::endl;
    }
    else if (message->getKind() == MacToPhyInterface::RADIO_SWITCHING_OVER) {
        DBG_MAC << "Phylayer said radio switching is done" << std::endl;
    }
    else if (message->getKind() == BaseDecider::PACKET_DROPPED) {
        phy->setRadioState(Radio::RX);
        DBG_MAC << "Phylayer said packet was dropped" << std::endl;
    }
    else {
        DBG_MAC << "Invalid control message type (type=NOTHING) : name=" << message->getName() << " modulesrc=" << message->getSenderModule()->getFullPath() << "." << std::endl;
        assert(false);
    }

    if (message->getKind() == Decider80211p::COLLISION) {
        emit(sigCollision, true);
    }

    delete message;

}

void Mac16094Metrics::handleUpperControl(cMessage* message){
    Mac1609_4::handleUpperControl(message);
}


void Mac16094Metrics::computeThroughput(Metrics* metrics, double receivedPackets, double currentSimulationTime){
  throughputMetricMac = metrics->computeThroughput(receivedPackets, currentSimulationTime);
  emit(throughputSignalMac, throughputMetricMac);
  metrics->throughputMetric = throughputMetricMac;
}

void Mac16094Metrics::computeThroughputMbps(Metrics* metrics, double messageBits, double currentMbs, double currentTime){
  //cout<<setiosflags(ios::fixed)<<setprecision(16);


  double messageMbs = (messageBits)/1000000;
  statsMbpsReceived = currentMbs + messageMbs;

  throughputMbps = metrics->computeThroughput(statsMbpsReceived, currentTime);
}

double Mac16094Metrics::getThroughputMbps(){
    return throughputMbps;
}

double Mac16094Metrics::getCollisionsPktNotDecoded(){
    return collisionsPktNonDecoded;
}

double Mac16094Metrics::getThroughputMetricMac(){
    return throughputMetricMac;
}

Mac16094Metrics::~Mac16094Metrics(){

}
