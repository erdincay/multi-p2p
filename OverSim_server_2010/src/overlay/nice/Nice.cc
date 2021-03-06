//
// Copyright (C) 2009 Institut fuer Telematik, Universitaet Karlsruhe (TH)
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
//

/**
 * @file Nice.cc
 * @author Christian Huebsch
 */

#include "Nice.h"
#include <GlobalStatistics.h>
#include <stdio.h>

#include "SimpleInfo.h"
#include "SimpleNodeEntry.h"
#include "SimpleUDP.h"
#include "GlobalNodeListAccess.h"

namespace oversim
{

/**
 * Define colors for layers in visualization
 */
const char *clustercolors[] = { "yellow",
                                "magenta",
                                "red",
                                "orange",
                                "green",
                                "aquamarine",
                                "cyan",
                                "blue",
                                "navy",
                                "yellow"
                              };

const char *clusterarrows[] = { "m=m,50,50,50,50;ls=yellow,2",
                                "m=m,50,50,50,50;ls=magenta,3",
                                "m=m,50,50,50,50;ls=red,4",
                                "m=m,50,50,50,50;ls=orange,5",
                                "m=m,50,50,50,50;ls=green,6",
                                "m=m,50,50,50,50;ls=aquamarine,7",
                                "m=m,50,50,50,50;ls=cyan,8",
                                "m=m,50,50,50,50;ls=blue,9",
                                "m=m,50,50,50,50;ls=navy,10",
                                "m=m,50,50,50,50;ls=yellow,11"
                              };

Define_Module(Nice);


/******************************************************************************
 * Constructor
 */
Nice::Nice() : numInconsistencies(0),
               numQueryTimeouts(0),
               numPeerTimeouts(0),
               numTempPeerTimeouts(0),
               numStructurePartitions(0),
               numOwnMessagesReceived(0),
               totalSCMinCompare(0),
               numJoins(0),
               totalForwardBytes(0),
               numReceived(0),
               totalReceivedBytes(0),
               numHeartbeat(0),
               totalHeartbeatBytes(0)
{

    /* do nothing at this point of time, OverSim calls initializeOverlay */

} // Nice


/******************************************************************************
 * Destructor
 */
Nice::~Nice()
{

    // destroy self timer messages
    cancelAndDelete(heartbeatTimer);
    cancelAndDelete(maintenanceTimer);
    cancelAndDelete(structureConnectionTimer);
    cancelAndDelete(rpPollTimer);
    cancelAndDelete(queryTimer);
    cancelAndDelete(visualizationTimer);

    std::map<TransportAddress, NicePeerInfo*>::iterator it = peerInfos.begin();

    for (; it != peerInfos.end(); it++) {

        delete it->second;

    }

} // ~NICE


/******************************************************************************
 * initializeOverlay
 * see BaseOverlay.h
 */
void Nice::initializeOverlay( int stage )
{

    /* Because of IPAddressResolver, we need to wait until interfaces
     * are registered, address auto-assignment takes place etc. */
    if (stage != MIN_STAGE_OVERLAY)
        return;

    /* Set appearance of node in visualization */
    getParentModule()->getParentModule()->getDisplayString().setTagArg("i", 0, "device/pc_vs");
    getParentModule()->getParentModule()->getDisplayString().setTagArg("i2", 0, "block/circle_vs");

    /* Initially clear all clusters */
    for (int i=0; i<maxLayers; i++) {

        clusters[i].clear();

    }

    /* Initialize Self-Messages */

    // Periodic Heartbeat Messages
    heartbeatInterval = par("heartbeatInterval");
    heartbeatTimer = new cMessage("heartbeatTimer");

    // Periodic Protocol Maintenance
    maintenanceInterval = par("maintenanceInterval");
    maintenanceTimer = new cMessage("maintenanceTimer");

    queryInterval = par("queryInterval");
    queryTimer = new cMessage("queryTimer");

    structureConnectionInterval = par("structureConnectionInterval");
    structureConnectionTimer = new cMessage("structureConnectionTimer");

    rpPollTimer = new cMessage("structureConnectionTimer");
    rpPollTimerInterval = par("rpPollTimerInterval");

    peerTimeoutInterval = par("peerTimeoutInterval");

    pimp = par("enhancedMode");

    isPollingRP = false;

    /* DEBUG */
    clusterrefinement = par("debug_clusterrefinement");
    debug_heartbeats = par("debug_heartbeats");
    debug_visualization = par("debug_visualization");
    debug_join = par("debug_join");
    debug_peertimeouts = par("debug_peertimeouts");
    debug_removes = par("debug_removes");
    debug_queries = par("debug_queries");

    visualizationTimer = new cMessage("visualizationTimer");

    /* Read cluster parameter k */
    k = par("k");

    CLUSTERLEADERBOUND = par("clusterLeaderBound");
    CLUSTERLEADERCOMPAREDIST = par("clusterLeaderCompareDist");
    SC_PROC_DISTANCE = par("scProcDistance");
    SC_MIN_OFFSET = par("scMinOffset");

    /* Add own node to peerInfos */
    NicePeerInfo* pi = new NicePeerInfo(this);
    pi->set_distance(0);
    peerInfos.insert(std::make_pair(thisNode, pi));

    /* Set evaluation layer to not specified */
    evalLayer = -1;
    joinLayer = -1;

    first_leader = TransportAddress::UNSPECIFIED_NODE;
    second_leader = TransportAddress::UNSPECIFIED_NODE;

    // add some watches
    WATCH(thisNode);
    WATCH_POINTER_MAP(peerInfos);
    WATCH(evalLayer);
    WATCH(query_start);
    WATCH(heartbeatTimer);
    WATCH_MAP(tempPeers);
    WATCH(RendevouzPoint);

    WATCH(numInconsistencies);
    WATCH(numQueryTimeouts);
    WATCH(numPeerTimeouts);
    WATCH(numTempPeerTimeouts);
    WATCH(numStructurePartitions);
    WATCH(numOwnMessagesReceived);
    WATCH(totalSCMinCompare);
    WATCH(numJoins);
    WATCH(totalForwardBytes);
    WATCH(numReceived);
    WATCH(totalReceivedBytes);
    WATCH(numHeartbeat);
    WATCH(totalHeartbeatBytes);

} // initializeOverlay


/******************************************************************************
 * joinOverlay
 * see BaseOverlay.h
 */
void Nice::joinOverlay()
{
    changeState(INIT);
    changeState(BOOTSTRAP);
} // joinOverlay


/******************************************************************************
 * changeState
 * see BaseOverlay.h
 */
void Nice::changeState( int toState )
{
    switch (toState) {

    case INIT:

        state = INIT;

        getParentModule()->getParentModule()->getDisplayString().setTagArg("i2", 1, "red");

        scheduleAt(simTime() + 1, visualizationTimer);

        break;

    case BOOTSTRAP:

        state = BOOTSTRAP;

        /* check wether there exists a Rendevouz Point */
        if (RendevouzPoint.isUnspecified()) {

            /* become Rendevouz Point */
            becomeRendevouzPoint();

            /* join cluster layer 0 as first node */
            clusters[0].add(thisNode);
            clusters[0].setLeader(thisNode);

            changeState(READY);

            return;

        }
        else {

            /* initiate NICE structure joining */
            BasicJoinLayer(-1);

            double offset = structureConnectionInterval.dbl() * heartbeatInterval.dbl();
            scheduleAt(simTime() + offset, structureConnectionTimer);

        }

        break;

    case READY:

        state = READY;

        cancelEvent(heartbeatTimer);
        scheduleAt(simTime() + heartbeatInterval, heartbeatTimer);
        cancelEvent(maintenanceTimer);
        scheduleAt(simTime() + maintenanceInterval, maintenanceTimer);

        getParentModule()->getParentModule()->getDisplayString().setTagArg
        ("i2", 1, clustercolors[getHighestLayer()]);

        setOverlayReady(true);

        //hoang
        cout << "node " << nodeID << " setOverlayReady(true)\n";
        FILE* f;
        f = fopen("ue_list.txt","r");
        int id;
        char ip[80];
        while(! feof(f)){
        	fscanf(f,"%d\t%s\n",&id,ip);
        	TransportAddress add = TransportAddress(IPvXAddress(ip),1024,TransportAddress::UNKNOWN_NAT);
    		NiceMessage * readyMsg = new NiceMessage("NICE_STATE_READY");

    		readyMsg->setCommand(NICE_STATE_READY);
    		readyMsg->setSrcNode(thisNode);
    		readyMsg->setNodeID(nodeID);

    		sendMessageToUDP(add, readyMsg);
        }
        fclose(f);

		//end of hoang
        break;

    }

} // changeState


/******************************************************************************
 * changeState
 * see BaseOverlay.h
 */
void Nice::handleTimerEvent( cMessage* msg )
{

    if (msg->isName("visualizationTimer")) {

        updateVisualization();
        scheduleAt(simTime() + 1, visualizationTimer);

    }
    else if (msg->isName("heartbeatTimer")) {

        sendHeartbeats();
        scheduleAt(simTime() + heartbeatInterval, heartbeatTimer);

    }
    else if (msg->isName("maintenanceTimer")) {

        maintenance();
        cancelEvent(maintenanceTimer);
        scheduleAt(simTime() + maintenanceInterval, maintenanceTimer);

    }
    else if (msg->isName("queryTimer")) {

        RECORD_STATS(++numInconsistencies; ++numQueryTimeouts);
        BasicJoinLayer(-1);

    }
    else if (msg->isName("structureConnectionTimer")) {

        if (RendevouzPoint == thisNode)
            return;

        RECORD_STATS(++numStructurePartitions; ++numInconsistencies);
        BasicJoinLayer(getHighestLayer());

    }
    else if (msg->isName("rpPollTimer")) {

        isPollingRP = false;

        if (RendevouzPoint == thisNode)
            return;

        becomeRendevouzPoint();

    }

} // handleTimerEvent


/******************************************************************************
 * handleUDPMessage
 * see BaseOverlay.h
 */
void Nice::handleUDPMessage(BaseOverlayMessage* msg)
{
	//hoang
	global->incNumReceivedAll();
	global->addToBitReceivedAll(msg->getBitLength());
	//end of hoang
    // try message cast to NICE base message
    if (dynamic_cast<NiceMessage*>(msg) != NULL) {

        NiceMessage* niceMsg = check_and_cast<NiceMessage*>(msg);

        // First of all, update activity information for sourcenode
        std::map<TransportAddress, NicePeerInfo*>::iterator it = peerInfos.find(niceMsg->getSrcNode());

        if (it != peerInfos.end()) {

            it->second->touch();

        }

        /* Dispatch message, possibly downcasting to a more concrete type */
        switch (niceMsg->getCommand()) {

            /* More concrete types, to which the message is cast if needed */
            NiceMemberMessage* queryRspMsg;
            NiceClusterMerge* mergeMsg;
            NiceMulticastMessage* multicastMsg;

            case NICE_QUERY:

                handleNiceQuery(niceMsg);

                break;

            case NICE_QUERY_RESPONSE:

                queryRspMsg = check_and_cast<NiceMemberMessage*>(niceMsg);
                handleNiceQueryResponse(queryRspMsg);

                break;

            case NICE_JOIN_CLUSTER:

                handleNiceJoinCluster(niceMsg);

                break;

            case NICE_POLL_RP:

                handleNicePollRp(niceMsg);

                break;

            case NICE_POLL_RP_RESPONSE:

                handleNicePollRpResponse(niceMsg);

                break;

            case NICE_HEARTBEAT:

                handleNiceHeartbeat(niceMsg);

                break;

            case NICE_LEADERHEARTBEAT:
            case NICE_LEADERTRANSFER:

                handleNiceLeaderHeartbeatOrTransfer(niceMsg);

                break;

            case NICE_JOINEVAL:

                handleNiceJoineval(niceMsg);

                break;

            case NICE_JOINEVAL_RESPONSE:

                handleNiceJoinevalResponse(niceMsg);

                break;

            case NICE_REMOVE:

                handleNiceRemove(niceMsg);

                break;

            case NICE_PEER_TEMPORARY:

                handleNicePeerTemporary(niceMsg);

                break;

            case NICE_PEER_TEMPORARY_RELEASE:

                handleNicePeerTemporaryRelease(niceMsg);

                break;

            case NICE_PING_PROBE:

                handleNicePingProbe(niceMsg);

                break;

            case NICE_PING_PROBE_RESPONSE:

                handleNicePingProbeResponse(niceMsg);

                break;

            case NICE_FORCE_MERGE:

                handleNiceForceMerge(niceMsg);

                break;

            case NICE_CLUSTER_MERGE_REQUEST:

                mergeMsg = check_and_cast<NiceClusterMerge*>(niceMsg);

                handleNiceClusterMergeRequest(mergeMsg);

                break;

            case NICE_MULTICAST:

                multicastMsg = check_and_cast<NiceMulticastMessage*>(msg);

                handleNiceMulticast(multicastMsg);

                break;
                //hoang
            case NICE_STATE_READY:
            	//update UE counter
            	cout << "node " << nodeID << " ip " << thisNode.getAddress() << " get a NICE_STATE_READY from " << niceMsg->getSrcNode() << endl;
            	global->incUEcounter();
//            	UEcounter++;
                //end of hoang
            default:
                
                delete niceMsg;
        }
    }
    //hoang
    else if(dynamic_cast<NiceMulticastMessage*>(msg) != NULL){

    	NiceMulticastMessage* multicastMsg;

    	multicastMsg = check_and_cast<NiceMulticastMessage*>(msg);

    	if(multicastMsg->getCommand() == NICE_MULTICAST){
    		handleNiceMulticast(multicastMsg);
    	}
    	else {
            delete multicastMsg;
    	}

    }
    //end of hoang
    else {
        delete msg;
    }
} // handleUDPMessage


/******************************************************************************
 * finishOverlay
 * see BaseOverlay.h
 */
void Nice::finishOverlay()
{

    simtime_t time = globalStatistics->calcMeasuredLifetime(creationTime);
    if (time < GlobalStatistics::MIN_MEASURED) return;

    globalStatistics->addStdDev("Nice: Inconsistencies/s", (double)numInconsistencies / time);
    globalStatistics->addStdDev("Nice: Query Timeouts/s", (double)numQueryTimeouts / time);
    globalStatistics->addStdDev("Nice: Peer Timeouts/s", (double)numPeerTimeouts / time);
    globalStatistics->addStdDev("Nice: Temporary Peer Timeouts/s", (double)numTempPeerTimeouts / time);
    globalStatistics->addStdDev("Nice: Structure Partitions/s", (double)numStructurePartitions / time);
    globalStatistics->addStdDev("Nice: Own Messages Received/s", (double)numOwnMessagesReceived / time);
    globalStatistics->addStdDev("Nice: SC Minimum Compare/s", (double)totalSCMinCompare / time);
    globalStatistics->addStdDev("Nice: Received JOIN Messages/s", (double)numJoins / time);
    globalStatistics->addStdDev("Nice: Forwarded Multicast Messages/s", (double)numForward / time);
    globalStatistics->addStdDev("Nice: Forwarded Multicast Bytes/s", (double)totalForwardBytes / time);
    globalStatistics->addStdDev("Nice: Received Multicast Messages/s (subscribed groups only)", (double)numReceived / time);
    globalStatistics->addStdDev("Nice: Received Multicast Bytes/s (subscribed groups only)", (double)totalReceivedBytes / time);
    globalStatistics->addStdDev("Nice: Send Heartbeat Messages/s", (double)numHeartbeat / time);
    globalStatistics->addStdDev("Nice: Send Heartbeat Bytes/s", (double)totalHeartbeatBytes / time);
    if( debug_join ) recordScalar("Nice: Total joins", (double)numJoins);

} // finishOverlay


/******************************************************************************
 * becomeRendevouzPoint
 */
void Nice::becomeRendevouzPoint()
{

    RendevouzPoint = thisNode;
    EV << simTime() << " : " << thisNode.getAddress() << " : Set RP to " << thisNode.getAddress() << endl;
    //hoang
//    const char * ip = par("externalHostIP");

    FILE* f;
    f = fopen("ue_list.txt","r");
    int id;
    char ip[80];
    while(! feof(f)){
    	fscanf(f,"%d\t%s\n",&id,ip);
    	TransportAddress add = TransportAddress(IPvXAddress(ip),1024,TransportAddress::UNKNOWN_NAT);
    	NiceMessage * msg = new NiceMessage("NICE_RP_NOTIFY");

		msg->setCommand(NICE_RP_NOTIFY);
		msg->setSrcNode(thisNode);

   		sendMessageToUDP(add, msg);

    }
    fclose(f);
/*
	TransportAddress add = TransportAddress(IPvXAddress(ip),1024,TransportAddress::UNKNOWN_NAT);
	TransportAddress add2 = TransportAddress(IPvXAddress("50.5.0.2"),1024,TransportAddress::UNKNOWN_NAT);
	TransportAddress add3 = TransportAddress(IPvXAddress("50.5.0.3"),1024,TransportAddress::UNKNOWN_NAT);
	TransportAddress add62 = TransportAddress(IPvXAddress("60.5.0.2"),1024,TransportAddress::UNKNOWN_NAT);
	TransportAddress add63 = TransportAddress(IPvXAddress("60.5.0.3"),1024,TransportAddress::UNKNOWN_NAT);
	TransportAddress add222 = TransportAddress(IPvXAddress("222.2.2.2"),1024,TransportAddress::UNKNOWN_NAT);

	NiceMessage * msg = new NiceMessage("NICE_RP_NOTIFY");

	msg->setCommand(NICE_RP_NOTIFY);
	msg->setSrcNode(thisNode);

	NiceMessage * dup2 = msg->dup();
	NiceMessage * dup3 = msg->dup();
	NiceMessage * dup62 = msg->dup();
	NiceMessage * dup63 = msg->dup();
	NiceMessage * dup222 = msg->dup();

	sendMessageToUDP(add, msg);
	sendMessageToUDP(add2, dup2);
	sendMessageToUDP(add3, dup3);
	sendMessageToUDP(add62, dup62);
	sendMessageToUDP(add63, dup63);
	sendMessageToUDP(add222, dup222);
*/
    //end of hoang

    /* Mark node as new RP (star symbol) */
    getParentModule()->getParentModule()->getDisplayString().
    setTagArg("i2", 0, "block/star_vs");

    //Become leader of all cluster layers
    for (int i=0; i<=getHighestLayer(); i++) {

        clusters[i].setLeader(thisNode);

    }

} // becomeRendevouzPoint


/******************************************************************************
 * BasicJoinLayer
 * Queries RendezvousPoint, sets targetLayer to the given layer
 * and peers temporarily with the RendezvousPoint.
 */
void Nice::BasicJoinLayer(short layer)
{

    // Cancel timers involved in structure refinement
    cancelEvent(maintenanceTimer);
    cancelEvent(heartbeatTimer);

    Query(RendevouzPoint, layer);

    if (layer > -1)
        targetLayer = layer;
    else
        targetLayer = 0;

    // Temporary peer with RP for faster data reception
    NiceMessage* msg = new NiceMessage("NICE_PEER_TEMPORARY");
    msg->setSrcNode(thisNode);
    msg->setCommand(NICE_PEER_TEMPORARY);
    msg->setLayer(-1);
    msg->setBitLength(NICEMESSAGE_L(msg));

    sendMessageToUDP(RendevouzPoint, msg);

    isTempPeered = true;

} // BasicJoinLayer


/******************************************************************************
 * Query
 * Sends a query message to destination.
 * Records query_start time.
 * Sets tempResolver to destination.
 * Sets joinLayer to layer.
 */
void Nice::Query(const TransportAddress& destination, short layer)
{
    if (debug_queries)
        EV << simTime() << " : " << thisNode.getAddress() << " : Query()" << endl;


    NiceMessage* msg = new NiceMessage("NICE_QUERY");
    msg->setSrcNode(thisNode);
    msg->setCommand(NICE_QUERY);
    msg->setLayer(layer);
    msg->setBitLength(NICEMESSAGE_L(msg));

    query_start = simTime();
    tempResolver = destination;

    cancelEvent(queryTimer);
    scheduleAt(simTime() + queryInterval, queryTimer);

    joinLayer = layer;

    sendMessageToUDP(destination, msg);

    if (debug_queries)
        EV << simTime() << " : " << thisNode.getAddress() << " : Query() finished." << endl;

} // Query

/* Functions handling NICE messages */

void Nice::handleNiceQuery(NiceMessage* queryMsg)
{

    if (debug_queries)
        EV << simTime() << " : " << thisNode.getAddress() << " : handleNiceQuery()" << endl;

    short layer = queryMsg->getLayer();

    if (debug_queries)
        EV << " layer before: " << layer << endl;

    if (layer > getHighestLeaderLayer()) {

        if (debug_queries)
            EV << " getHighestLeaderLayer(): " << getHighestLeaderLayer() << " ! Returning." << endl;

        delete queryMsg;
        return;

    }

    if (layer < 0) {

        if (RendevouzPoint == thisNode) {

            /* If layer is < 0, response with highest layer I am leader of */
            if (debug_queries)
                EV << " I am RP." << endl;
            layer = getHighestLeaderLayer();

        }
        else {

            if (debug_queries)
                EV << " I am not RP. Return." << endl;

            if (pimp) {

                /* forward to Rendevouz Point */
                NiceMessage* dup = static_cast<NiceMessage*>(queryMsg->dup());
                sendMessageToUDP(RendevouzPoint, dup);

            }

            delete queryMsg;
            return;

        }

    }

    if (debug_queries)
        EV << " layer after: " << layer << endl;

    if (!clusters[layer].getLeader().isUnspecified()) {

        if (clusters[layer].getLeader() != thisNode) {

            if (pimp) {

                NiceMessage* dup = static_cast<NiceMessage*>(queryMsg->dup());
                sendMessageToUDP(clusters[layer].getLeader(), dup);

            }

            if (debug_queries)
                EV << " I am not leader of this cluster. return." << endl;

            delete queryMsg;
            return;

        }

    }
    else {

        delete queryMsg;
        return;

    }

    NiceMemberMessage* response = new NiceMemberMessage("NICE_QUERY_RESPONSE");
    response->setSrcNode(thisNode);
    response->setCommand(NICE_QUERY_RESPONSE);
    response->setLayer(layer);

    /* Fill in current cluster members except me */
    response->setMembersArraySize(clusters[layer].getSize()-1);

    int j=0;

    for (int i = 0; i < clusters[layer].getSize(); i++) {

        if (clusters[layer].get(i) != thisNode) {

            response->setMembers(j, clusters[layer].get(i));
            if (debug_queries)
                EV << " Response: " << i << " : " << clusters[layer].get(i) << endl;
            j++;

        }

    }

    response->setBitLength(NICEMEMBERMESSAGE_L(response));

    sendMessageToUDP(queryMsg->getSrcNode(), response);

    if (debug_queries)
        EV << " Sent response to: " << queryMsg->getSrcNode() << endl;

    if (debug_queries)
        EV << simTime() << " : " << thisNode.getAddress() << " : handleNiceQuery() finished." << endl;

    delete queryMsg;
} // handleNiceQuery

void Nice::handleNiceClusterMergeRequest(NiceClusterMerge* mergeMsg)
{
    EV << simTime() << " : " << thisNode.getAddress() << " : NICE_CLUSTER_MERGE_REQUEST" << endl;

    short layer = mergeMsg->getLayer();

    // Only react if I am a leader of this cluster layer

    if (clusters[layer].getLeader().isUnspecified()) {

        EV << simTime() << " : " << thisNode.getAddress() << " : NO LEADER! BREAK. NICE_CLUSTER_MERGE_REQUEST finished" << endl;

        delete mergeMsg;

        return;

    }

    if (clusters[layer].getLeader() == thisNode) {

        clusters[layer+1].remove(mergeMsg->getSrcNode());
        deleteOverlayNeighborArrow(mergeMsg->getSrcNode());

        if (clusters[layer+1].getLeader() != thisNode)
            clusters[layer+1].setLeader(mergeMsg->getNewClusterLeader());

        // if I am alone now, become leader
        if (clusters[layer+1].getSize() == 1) {

            becomeRendevouzPoint();

            // cancel layer
            clusters[layer+1].clear();

            for (short i=0; i<maxLayers; i++) {

                if (clusters[i].getSize() > 0) {

                    if (clusters[i].contains(thisNode)) {

                        getParentModule()->getParentModule()->getDisplayString().setTagArg
                            ("i2", 1, clustercolors[i]);

                    }

                }

            }

        }

        for (unsigned int i=0; i<mergeMsg->getMembersArraySize(); i++) {

            /* Add new node to cluster */
            clusters[layer].add(mergeMsg->getMembers(i));

            /* Create peer context to joining node */
            /* Check if peer info already exists */
            std::map<TransportAddress, NicePeerInfo*>::iterator it = peerInfos.find(mergeMsg->getMembers(i));

            if (it != peerInfos.end()) { /* We already know this node */

            }
            else { /* Create PeerInfo object */

                NicePeerInfo* pi = new NicePeerInfo(this);

                pi->set_last_HB_arrival(simTime().dbl());

                peerInfos.insert(std::make_pair(mergeMsg->getMembers(i), pi));

            }

            /* Draw arrow to new member */
            showOverlayNeighborArrow(mergeMsg->getMembers(i), false, clusterarrows[layer]);

            EV << "getHighestLeaderLayer()].getSize(): " << clusters[getHighestLeaderLayer()].getSize() << endl;

            if (clusters[getHighestLeaderLayer()].getSize() < 2) {

                // cancel layer
                clusters[getHighestLeaderLayer()].clear();

                for (short i=0; i<maxLayers; i++) {

                    if (clusters[i].getSize() > 0) {

                        if (clusters[i].contains(thisNode)) {

                            getParentModule()->getParentModule()->getDisplayString().setTagArg
                                ("i2", 1, clustercolors[i]);

                        }

                    }

                }

            }

        }

    }
    else { // Forward to new cluster leader

        if (pimp) {

            NiceMemberMessage* dup = static_cast<NiceMemberMessage*>(mergeMsg->dup());
            sendMessageToUDP(clusters[layer].getLeader(), dup);
            delete mergeMsg;
            return;

        }

    }

    if (pimp)
        sendHeartbeats();

    delete mergeMsg;

    EV << simTime() << " : " << thisNode.getAddress() << " : NICE_CLUSTER_MERGE_REQUEST finished" << endl;
}

void Nice::handleNiceForceMerge(NiceMessage* msg)
{
    ClusterMergeRequest(msg->getSrcNode(), msg->getLayer());

    delete msg;
}

void Nice::handleNiceHeartbeat(NiceMessage* msg)
{
    if (debug_heartbeats)
        EV << simTime() << " : " << thisNode.getAddress() << " : handleHeartbeat()...  " << endl;


    // First, check if heartbeat is LeaderTransfer
    if (msg->getCommand() == NICE_LEADERTRANSFER) {

        if (debug_heartbeats)
            EV << simTime() << " : " << thisNode.getAddress() << " : NICE_LEADERTRANSFER from " << msg->getSrcNode() << " for " << msg->getLayer() << endl;

        if (!clusters[msg->getLayer()].getLeader().isUnspecified()) {

            /* React only if I am not already leader */
            if (clusters[msg->getLayer()].getLeader() != thisNode /*&& clusters[msg->getLayer()].getLeader() == msg->getSrcNode()*/) {

                if (debug_heartbeats)
                    EV << "I am not already leader of this cluster layer." << endl;

                NiceLeaderHeartbeat* hbMsg = check_and_cast<NiceLeaderHeartbeat*>(msg);

                clusters[hbMsg->getLayer()].clear();
                clusters[hbMsg->getLayer()].setLeader(thisNode);

                for (unsigned int i=0; i<hbMsg->getMembersArraySize(); i++) {

                    if (debug_heartbeats)
                        EV << "Adding: " << hbMsg->getMembers(i) << endl;

                    clusters[hbMsg->getLayer()].add(hbMsg->getMembers(i));
                    showOverlayNeighborArrow(hbMsg->getMembers(i), false, clusterarrows[hbMsg->getLayer()]);

                    std::map<TransportAddress, NicePeerInfo*>::iterator it = peerInfos.find(hbMsg->getMembers(i));

                    if (it != peerInfos.end()) {

                        /* We already know this node */
                        it->second->touch();

                    }
                    else {

                        //We don't know him yet

                    }

                }

                if (hbMsg->getSupercluster_membersArraySize() > 0) {

                    clusters[hbMsg->getLayer()+1].clear();

                    for (unsigned int i=0; i<hbMsg->getSupercluster_membersArraySize(); i++) {

                        clusters[hbMsg->getLayer()+1].add(hbMsg->getSupercluster_members(i));

                        std::map<TransportAddress, NicePeerInfo*>::iterator it = peerInfos.find(hbMsg->getSupercluster_members(i));

                        if (it != peerInfos.end()) {

                            /* We already know this node */
                            it->second->touch();

                        }
                        else {

                            //We don't know him yet

                        }

                    }

                    // experimental
                    clusters[hbMsg->getLayer()+1].add(thisNode);

                    clusters[hbMsg->getLayer()+1].setLeader(hbMsg->getSupercluster_leader());

                    if ((clusters[hbMsg->getLayer()+1].getLeader() == thisNode) && (clusters[hbMsg->getLayer()+2].getSize() == 0)) {

                        for (unsigned int i=0; i<hbMsg->getSupercluster_membersArraySize(); i++) {

                            showOverlayNeighborArrow(hbMsg->getSupercluster_members(i), false, clusterarrows[hbMsg->getLayer()+1]);

                        }

                        becomeRendevouzPoint();

                    }
                    else {

                        JoinCluster(hbMsg->getSupercluster_leader(), hbMsg->getLayer()+1);

                    }

                }
                else {

                    becomeRendevouzPoint();

                }

                for (int i=0; i<maxLayers; i++) {

                    if (clusters[i].contains(thisNode))
                        getParentModule()->getParentModule()->getDisplayString().setTagArg("i2", 1, clustercolors[i]);

                }

                clusters[hbMsg->getLayer()].set_Last_LT();

                delete hbMsg;

            }
            else {

                //Do nothing

            }

        }
        else {



        }

        if (pimp)
            sendHeartbeats();

        return;

    }
    else if (msg->getCommand() == NICE_HEARTBEAT) {

        NiceHeartbeat* hbMsg = check_and_cast<NiceHeartbeat*>(msg);

        if (debug_heartbeats)
            EV << simTime() << " : " << thisNode.getAddress() << " : NICE_HEARTBEAT from  " << hbMsg->getSrcNode() << endl;

        /* Update sequence number information and evaluate distance */
        std::map<TransportAddress, NicePeerInfo*>::iterator it = peerInfos.find(hbMsg->getSrcNode());

        if (it != peerInfos.end()) {

            /* We already know this node */
            // Collect subcluster infos
            it->second->setSubClusterMembers(hbMsg->getSublayermembers());

            it->second->set_last_HB_arrival(simTime().dbl());

            if (it->second->get_backHB(hbMsg->getSeqRspNo()) > 0) {

                /* Valid distance measurement, get value */
                double oldDistance = it->second->get_distance();

                /* Use Exponential Moving Average with factor 0.1 */
                double newDistance = (simTime().dbl() - it->second->get_backHB(hbMsg->getSeqRspNo()) - hbMsg->getHb_delay())/2.0;
                //hoang
                if(hoang_use_cost){
					newDistance = cost();
				}
                //end of hoang

                if (oldDistance > 0) {

                    it->second->set_distance((0.1 * newDistance) + (0.9 * oldDistance));

                }
                else {

                    it->second->set_distance(newDistance);

                }

            }

            it->second->set_last_recv_HB(hbMsg->getSeqNo());

        }

        it = peerInfos.find(hbMsg->getSrcNode());

        if (it != peerInfos.end()) {

            for (unsigned int i=0; i<hbMsg->getMembersArraySize(); i++) {

                it->second->updateDistance(hbMsg->getMembers(i), hbMsg->getDistances(i));

            }

        }

        delete hbMsg;

    }
    else if (msg->getCommand() == NICE_LEADERHEARTBEAT) {

        NiceLeaderHeartbeat* hbMsg = check_and_cast<NiceLeaderHeartbeat*>(msg);

        if (debug_heartbeats)
            EV << simTime() << " : " << thisNode.getAddress() << " : NICE_LEADERHEARTBEAT from  " << hbMsg->getSrcNode() << endl;

        //Alternative Detection
        leaderHeartbeats.push_back(std::make_pair(hbMsg->getSrcNode(), simTime()));

        if (leaderHeartbeats.size() > 3) {

            if (debug_heartbeats)
                EV << simTime() << "leaderHeartbeats.size() > 3 :  " << leaderHeartbeats.size() << endl;

            simtime_t predecessor =  leaderHeartbeats.at(leaderHeartbeats.size()-2).second;

            if (debug_heartbeats)
                EV << simTime() << "predecessor :  " << predecessor << endl;


            if (simTime() < (predecessor + heartbeatInterval)) {

                if (debug_heartbeats)
                    EV << simTime() << "simTime() < (predecessor + heartbeatInterval)" << endl;

                if (leaderHeartbeats.at(leaderHeartbeats.size()-2).first != hbMsg->getSrcNode()) {

                    if (debug_heartbeats) {
                        EV << simTime() << "(leaderHeartbeats.at(leaderHeartbeats.size()-2).first != hbMsg->getSrcNode())" << endl;
                        EV << "leaderHeartbeats.at(leaderHeartbeats.size()-2).first: " << leaderHeartbeats.at(leaderHeartbeats.size()-2).first << endl;
                    }

                    if (leaderHeartbeats.at(leaderHeartbeats.size()-3).first == hbMsg->getSrcNode()) {

                        if (debug_heartbeats) {
                            EV << simTime() << "(leaderHeartbeats.at(leaderHeartbeats.size()-3).first == hbMsg->getSrcNode())" << endl;
                            EV << "leaderHeartbeats.at(leaderHeartbeats.size()-3).first: " << leaderHeartbeats.at(leaderHeartbeats.size()-3).first << endl;
                            EV << "timestamp: " << leaderHeartbeats.at(leaderHeartbeats.size()-3).second << endl;
                        }

                        if (leaderHeartbeats.at(leaderHeartbeats.size()-4).first == leaderHeartbeats.at(leaderHeartbeats.size()-2).first) {

                            if (debug_heartbeats) {
                                EV << simTime() << "(leaderHeartbeats.at(leaderHeartbeats.size()-4).first == leaderHeartbeats.at(leaderHeartbeats.size()-2).first" << endl;
                                EV << "leaderHeartbeats.at(leaderHeartbeats.size()-4).first: " << leaderHeartbeats.at(leaderHeartbeats.size()-4).first << endl;
                                EV << "timestamp: " << leaderHeartbeats.at(leaderHeartbeats.size()-4).second << endl;

                            }

                            if (debug_heartbeats)
                                EV << simTime() << " : " << thisNode.getAddress() << " : CONFLICTING LEADERS!" << endl;

                            NiceMessage* removeMsg = new NiceMessage("NICE_REMOVE");
                            removeMsg->setSrcNode(thisNode);
                            removeMsg->setCommand(NICE_REMOVE);
                            removeMsg->setLayer(hbMsg->getLayer());

                            removeMsg->setBitLength(NICEMESSAGE_L(removeMsg));

                            sendMessageToUDP(leaderHeartbeats.at(leaderHeartbeats.size()-2).first, removeMsg);

                        }

                    }

                }
            }

        }


        /* Tidy up leaderheartbeats */
        if (leaderHeartbeats.size() > 4) {

            for (unsigned int i=0; i<(leaderHeartbeats.size()-4); i++) {

                leaderHeartbeats.erase(leaderHeartbeats.begin());

            }

        }

        /* Update sequence number information and evaluate distance */
        std::map<TransportAddress, NicePeerInfo*>::iterator it = peerInfos.find(hbMsg->getSrcNode());

        if (it != peerInfos.end()) { /* We already know this node */

            it->second->set_last_HB_arrival(simTime().dbl());

            if (it->second->get_backHB(hbMsg->getSeqRspNo()) > 0) {

                /* Valid distance measurement, get value */
                it->second->set_distance((simTime().dbl() - it->second->get_backHB(hbMsg->getSeqRspNo()) - hbMsg->getHb_delay())/2);
                //hoang
                if(hoang_use_cost){
					it->second->set_distance(cost());
				}
                //end of hoang

            }

            it->second->set_last_recv_HB(hbMsg->getSeqNo());

        }

        it = peerInfos.find(hbMsg->getSrcNode());

        if (it != peerInfos.end()) {

            for (unsigned int i=0; i<hbMsg->getMembersArraySize(); i++) {

                it->second->updateDistance(hbMsg->getMembers(i), hbMsg->getDistances(i));

            }

        }

        // Maintain cluster memberships

        if (!clusters[hbMsg->getLayer()].contains(thisNode)) {

            /* Node is not part of this cluster, remove it */
            NiceMessage* removeMsg = new NiceMessage("NICE_REMOVE");
            removeMsg->setSrcNode(thisNode);
            removeMsg->setCommand(NICE_REMOVE);
            removeMsg->setLayer(hbMsg->getLayer());

            removeMsg->setBitLength(NICEMESSAGE_L(removeMsg));

            if (debug_heartbeats)
                EV << "Node is not part of this cluster (" << hbMsg->getLayer() << "), removing it..." << endl;

            sendMessageToUDP(hbMsg->getSrcNode(), removeMsg);

            return;

        }

        if (clusters[hbMsg->getLayer()].getLeader() == thisNode) {

            if (simTime() < clusters[hbMsg->getLayer()].get_Last_LT() + 1.0) {

                if (debug_heartbeats)
                    EV << "Potential deprecated LeaderHeartbeat. Ignoring..." << endl;

                return;

            }

            if (debug_heartbeats)
                EV << "I am also Leader in this cluster. Conflicting!..." << endl;

            /* Winner: The one with minimum max distance*/
            bool allIn = true;

            //Check if we're talking about same cluster

            for (unsigned int u=0; u<hbMsg->getMembersArraySize(); u++) {

                if (!clusters[hbMsg->getLayer()].contains(hbMsg->getMembers(u))) {
                    allIn = false;

                    if (debug_heartbeats)
                        EV << hbMsg->getMembers(u) << " : Not in my cluster." << endl;

                }
                else {

                    if (debug_heartbeats)
                        EV << hbMsg->getMembers(u) << " : Check." << endl;

                }

            }

            if (allIn) {

                // Perform check for better cluster leader
                TaSet cl;
                for (int l=0; l<clusters[hbMsg->getLayer()].getSize(); l++) {

                    cl.insert(clusters[hbMsg->getLayer()].get(l));

                }

                simtime_t myDistance = getMaxDistance(thisNode, cl);
                simtime_t hisDistance = getMaxDistance(hbMsg->getSrcNode(), cl);


                if (myDistance > hisDistance) {

                    TaSet cl;
                    for (int i=0; i<clusters[hbMsg->getLayer()].getSize(); i++) {

                        cl.insert(clusters[hbMsg->getLayer()].get(i));

                        deleteOverlayNeighborArrow(clusters[hbMsg->getLayer()].get(i));

                    }
                    LeaderTransfer(hbMsg->getLayer(), hbMsg->getSrcNode(), cl, clusters[hbMsg->getLayer()+1].getLeader());

                    clusters[hbMsg->getLayer()].setLeader(hbMsg->getSrcNode());

                    gracefulLeave(hbMsg->getLayer());

                    /* Anyways, update cluster info */

                }
                else {

                    sendHeartbeatTo(hbMsg->getSrcNode(), hbMsg->getLayer());

                    return;

                }

            }
            else { // We have different children, simply leave other

                /* Remove it */
                NiceMessage* removeMsg = new NiceMessage("NICE_REMOVE");
                removeMsg->setSrcNode(thisNode);
                removeMsg->setCommand(NICE_REMOVE);
                removeMsg->setLayer(hbMsg->getLayer());

                removeMsg->setBitLength(NICEMESSAGE_L(removeMsg));

                sendMessageToUDP(hbMsg->getSrcNode(), removeMsg);

                return;

            }
        }

        /* Everything is in order. Process HB */

        for (int m=hbMsg->getLayer(); m<maxLayers; m++) {
            clusters[m].clear();
        }

        for (unsigned int i=0; i<hbMsg->getMembersArraySize(); i++) {

            //Check if member is already part of cluster

            /* Check if peer info already exists */
            std::map<TransportAddress, NicePeerInfo*>::iterator it = peerInfos.find(hbMsg->getMembers(i));

            if (it != peerInfos.end()) { /* We already know this node */

            }
            else { /* Create PeerInfo object */

                NicePeerInfo* pi = new NicePeerInfo(this);

                pi->set_last_HB_arrival(simTime().dbl());

                peerInfos.insert(std::make_pair(hbMsg->getMembers(i), pi));

            }

            clusters[hbMsg->getLayer()].add(hbMsg->getMembers(i));

        }

        // set cluster leadership
        clusters[hbMsg->getLayer()].setLeader(hbMsg->getSrcNode());

        if (hbMsg->getSupercluster_membersArraySize() > 0) {

            clusters[hbMsg->getLayer()+1].clear();

            for (unsigned int i=0; i<hbMsg->getSupercluster_membersArraySize(); i++) {

                /* Check if peer info already exists */
                std::map<TransportAddress, NicePeerInfo*>::iterator it = peerInfos.find(hbMsg->getSupercluster_members(i));

                if (it != peerInfos.end()) { /* We already know this node */

                }
                else { /* Create PeerInfo object */

                    NicePeerInfo* pi = new NicePeerInfo(this);

                    pi->set_last_HB_arrival(simTime().dbl());

                    peerInfos.insert(std::make_pair(hbMsg->getSupercluster_members(i), pi));

                }

                clusters[hbMsg->getLayer()+1].add(hbMsg->getSupercluster_members(i));

            }

            clusters[hbMsg->getLayer()+1].setLeader(hbMsg->getSupercluster_leader());

            it = peerInfos.find(hbMsg->getSrcNode());

            if (it != peerInfos.end()) {

                for (unsigned int k=0; k<hbMsg->getMembersArraySize(); k++) {

                    it->second->updateDistance(hbMsg->getMembers(k), hbMsg->getDistances(k));

                }

            }
            else {

                NicePeerInfo* pi = new NicePeerInfo(this);

                pi->set_last_HB_arrival(simTime().dbl());

                peerInfos.insert(std::make_pair(hbMsg->getSrcNode(), pi));

            }
        }

        delete hbMsg;

    }

    if (debug_heartbeats)
        EV << simTime() << " : " << thisNode.getAddress() << " : handleHeartbeat() finished.  " << endl;
}

void Nice::handleNiceJoinCluster(NiceMessage* joinMsg)
{

    if (debug_join)
        EV << simTime() << " : " << thisNode.getAddress() << " : handleNiceJoinCluster()" << endl;

    short layer = joinMsg->getLayer();

    if (debug_join)
        std::cout << " From : " << joinMsg->getSrcNode() << ", Layer:  " << layer << endl;

    if (!clusters[layer].getLeader().isUnspecified()) {

        if (clusters[layer].getLeader() != thisNode) {

            if (pimp) {

                NiceMessage* dup = static_cast<NiceMessage*>(joinMsg->dup());
                sendMessageToUDP(clusters[layer].getLeader(), dup);

            }

        }
        else {

            RECORD_STATS(++numJoins);

            /* Add new node to cluster */
            clusters[layer].add(joinMsg->getSrcNode());

            /* Create peer context to joining node */
            /* Check if peer info already exists */
            std::map<TransportAddress, NicePeerInfo*>::iterator it = peerInfos.find(joinMsg->getSrcNode());

            if (it != peerInfos.end()) { /* We already know this node */


            }
            else { /* Create PeerInfo object */

                NicePeerInfo* pi = new NicePeerInfo(this);

                peerInfos.insert(std::make_pair(joinMsg->getSrcNode(), pi));

            }

            /* Draw arrow to new member */
            showOverlayNeighborArrow(joinMsg->getSrcNode(), false, clusterarrows[layer]);

            if (pimp)
                sendHeartbeatTo(joinMsg->getSrcNode(), layer);
        }

    }
    else {

        if (debug_join)
            EV << "Leader unspecified. Ignoring request." << endl;

    }

    if (debug_join)
        EV << simTime() << " : " << thisNode.getAddress() << " : handleNiceJoinCluster() finished." << endl;

    delete joinMsg;
} // handleNiceJoinCluster

void Nice::handleNiceJoineval(NiceMessage* msg)
{
    NiceMessage* responseMsg = new NiceMessage("NICE_JOINEVAL_RESPONSE");
    responseMsg->setSrcNode(thisNode);
    responseMsg->setCommand(NICE_JOINEVAL_RESPONSE);
    responseMsg->setLayer(msg->getLayer());

    responseMsg->setBitLength(NICEMESSAGE_L(responseMsg));

    sendMessageToUDP(msg->getSrcNode(), responseMsg);

    delete msg;
}

void Nice::handleNiceJoinevalResponse(NiceMessage* msg)
{
    if (evalLayer > 0 && evalLayer == msg->getLayer()) {

        query_compare = simTime() - query_compare;

        if (query_compare < query_start) {

            Query(msg->getSrcNode(), msg->getLayer()-1);

        }
        else {

            Query(tempResolver, msg->getLayer() - 1);

        }

        evalLayer = -1;
    }

    delete msg;
}

void Nice::handleNiceLeaderHeartbeatOrTransfer(NiceMessage* msg)
{
            cancelEvent(structureConnectionTimer);
            double offset = structureConnectionInterval.dbl() * heartbeatInterval.dbl();
            scheduleAt(simTime() + offset, structureConnectionTimer);

            handleNiceHeartbeat(msg);
}

void Nice::handleNiceMulticast(NiceMulticastMessage* multicastMsg)
{
	//hoang
	global->incNumReceivedData();
	global->addToBitReceivedData(multicastMsg->getBitLength());
	//end of hoang
    RECORD_STATS(++numReceived; totalReceivedBytes += multicastMsg->getByteLength());

    /* If it is mine, count */
    if (multicastMsg->getSrcNode() == thisNode) {

        RECORD_STATS(++numOwnMessagesReceived);

    }
    else {

        unsigned int hopCount = multicastMsg->getHopCount();
        hopCount++;

        if (hopCount < 8) {

            RECORD_STATS(++numForward; totalForwardBytes += multicastMsg->getByteLength());

            NiceMulticastMessage* forOverlay = static_cast<NiceMulticastMessage*>(multicastMsg->dup());
            forOverlay->setHopCount(hopCount);
            sendDataToOverlay(forOverlay);
            //hoang
//            global->incNumForwardedData();
//            send(multicastMsg->decapsulate(), "appOut");

            global->recordALMhopcount(hopCount);
            //end of hoang

        }
    }

    delete multicastMsg;
}

void Nice::handleNicePeerTemporary(NiceMessage* msg)
{
    // Add node to tempPeers
    tempPeers.insert(std::make_pair(msg->getSrcNode(), simTime()));

    delete msg;
}

void Nice::handleNicePeerTemporaryRelease(NiceMessage* msg)
{
    // Remove node from tempPeers
    tempPeers.erase(msg->getSrcNode());

    delete msg;
}

void Nice::handleNicePingProbe(NiceMessage* msg)
{
    // Only answer if I am part of requested layer
    if (clusters[msg->getLayer()].contains(thisNode)) {

        NiceMessage* probe = new NiceMessage("NICE_PING_PROBE");
        probe->setSrcNode(thisNode);
        probe->setCommand(NICE_PING_PROBE_RESPONSE);
        probe->setLayer(msg->getLayer());

        probe->setBitLength(NICEMESSAGE_L(probe));

        sendMessageToUDP(msg->getSrcNode(), probe);

    }
    else {

        //Do nothing

    }

    delete msg;
}

void Nice::handleNicePingProbeResponse(NiceMessage* msg)
{
    //Only react if still in same cluster as when asked
    if (msg->getLayer() == getHighestLayer()+1) {

        std::map<TransportAddress, NicePeerInfo*>::iterator it = peerInfos.find(msg->getSrcNode());

        if (it != peerInfos.end()) {

            double distance = simTime().dbl() - it->second->getDES();
            //hoang
			if(hoang_use_cost){
				distance = cost();
			}
			//end of hoang

            it->second->set_distance(distance);
            it->second->touch();

        }

    }

    delete msg;
}

void Nice::handleNicePollRp(NiceMessage* msg)
{
    if (RendevouzPoint == thisNode) {

        NiceMessage* response = new NiceMessage("NICE_POLL_RP_RESPONSE");
        response->setSrcNode(thisNode);
        response->setCommand(NICE_POLL_RP_RESPONSE);
        response->setLayer(getHighestLeaderLayer());
        response->setBitLength(NICEMESSAGE_L(response));

        sendMessageToUDP(msg->getSrcNode(), response);

    }
    delete msg;
}

void Nice::handleNicePollRpResponse(NiceMessage* msg)
{
    if (isPollingRP) {

        if (msg->getLayer() < getHighestLayer()) {

            becomeRendevouzPoint();

        }
        else {

            //BasicJoinLayer(getHighestLayer());

        }

    }

    delete msg;
}

void Nice::handleNiceQueryResponse(NiceMemberMessage* queryRspMsg)
{

    cancelEvent(queryTimer);

    short layer = queryRspMsg->getLayer();

    /* Check layer response */
    if (layer == targetLayer) {

        /* Use member information for own cluster update */
        for (unsigned int i = 0; i < queryRspMsg->getMembersArraySize(); i++) {

            clusters[layer].add(queryRspMsg->getMembers(i));

        }

        clusters[layer].add(queryRspMsg->getSrcNode());

        /* Initiate joining of lowest layer */
        JoinCluster(queryRspMsg->getSrcNode(), layer);

        changeState(READY);

    }
    else {

        /* Evaluate RTT to queried node */
        query_start = simTime() - query_start;

        /* Find out who is nearest cluster member in response, if nodes are given */
        if (queryRspMsg->getMembersArraySize() > 0) {

            NiceMessage* joineval = new NiceMessage("NICE_JOINEVAL");
            joineval->setSrcNode(thisNode);
            joineval->setCommand(NICE_JOINEVAL);
            joineval->setLayer(layer);

            joineval->setBitLength(NICEMESSAGE_L(joineval));

            /* Initiate evaluation with all cluster members */
            for (unsigned int i = 0; i < queryRspMsg->getMembersArraySize(); i++) {

                NiceMessage* dup = static_cast<NiceMessage*>(joineval->dup());

                sendMessageToUDP(queryRspMsg->getMembers(i), dup);

            }

            delete joineval;

        }
        else { // Directly query same node again for lower layer

            Query(queryRspMsg->getSrcNode(), queryRspMsg->getLayer()-1);

        }

        evalLayer = layer;
        query_compare = simTime();

    }

    delete queryRspMsg;
} // handleNiceQueryResponse

void Nice::handleNiceRemove(NiceMessage* msg)
{
    if (debug_removes)
        EV << simTime() << " : " << thisNode.getAddress() << " : NICE_REMOVE" << endl;

    short layer = msg->getLayer();

    if (pimp) {
        if (!clusters[layer].getLeader().isUnspecified()) {
            if (clusters[layer].getLeader() != thisNode && (clusters[layer].getLeader() != msg->getSrcNode())) {

                NiceMessage* dup = static_cast<NiceMessage*>(msg->dup());
                sendMessageToUDP(clusters[layer].getLeader(), dup);
                delete msg;
                return;
            }
        }
    }

    if (debug_removes)
        EV << simTime() << " : " << thisNode.getAddress() << " : removing " << msg->getSrcNode() << " from layer " << layer << endl;

    if (!clusters[msg->getLayer()].getLeader().isUnspecified()) {

        if (clusters[msg->getLayer()].getLeader() == thisNode) {

            // check prevents visualization arrows to be deleted by error
            if (clusters[msg->getLayer()].contains(msg->getSrcNode())) {

                deleteOverlayNeighborArrow(msg->getSrcNode());
                clusters[msg->getLayer()].remove(msg->getSrcNode());
                updateVisualization();

            }

        }

        if (clusters[msg->getLayer()].getLeader() == msg->getSrcNode()) {


        }

    }

    if (debug_removes)
        EV << simTime() << " : " << thisNode.getAddress() << " : NICE_REMOVE finished." << endl;

    delete msg;
}

/* End handlers for NICE messages */

/******************************************************************************
 * getHighestLeaderLayer
 */
short Nice::getHighestLeaderLayer()
{

    short highest = -1;

    for (short i=0; i<maxLayers; i++) {

        if (!clusters[i].getLeader().isUnspecified())

            if (clusters[i].getLeader() == thisNode)
                highest = i;

    }

    return highest;

} // getHighestLeaderLayer

short Nice::getHighestLayer()
{

    short highest = -1;

    for (short i=0; i<maxLayers; i++) {

        if (clusters[i].contains(thisNode))

            highest = i;

    }

    return highest;

} // getHighestLayer

void Nice::JoinCluster(const TransportAddress& leader, short layer)
{

    if (debug_join)
        EV << simTime() << " : " << thisNode.getAddress() << " : JoinCluster()" << endl;

    NiceMessage* msg = new NiceMessage("NICE_JOIN_CLUSTER");
    msg->setSrcNode(thisNode);
    msg->setCommand(NICE_JOIN_CLUSTER);
    msg->setLayer(layer);
    msg->setBitLength(NICEMESSAGE_L(msg));

    sendMessageToUDP(leader, msg);

    /* Create peer context to leader */
    /* Check if peer info already exists */
    std::map<TransportAddress, NicePeerInfo*>::iterator it = peerInfos.find(leader);

    if (it != peerInfos.end()) { /* We already know this node */

    }
    else { /* Create PeerInfo object */

        NicePeerInfo* pi = new NicePeerInfo(this);

        peerInfos.insert(std::make_pair(leader, pi));

    }

    /* Locally add thisNode, too */
    clusters[layer].add(thisNode);

    /* Set leader for cluster */
    clusters[layer].setLeader(leader);

    for (short i=0; i<maxLayers; i++) {

        if (clusters[i].getSize() > 0) {

            if (clusters[i].contains(thisNode)) {

                getParentModule()->getParentModule()->getDisplayString().setTagArg
                ("i2", 1, clustercolors[i]);

            }

        }

    }

    // If not already running, schedule some timers
    if (!heartbeatTimer->isScheduled()) {

        scheduleAt(simTime() + heartbeatInterval, heartbeatTimer);

    }
    if (!maintenanceTimer->isScheduled()) {

        scheduleAt(simTime() + heartbeatInterval, maintenanceTimer);

    }

    if (isTempPeered) {

        // Release temporary peering
        NiceMessage* msg = new NiceMessage("NICE_PEER_TEMPORARY_RELEASE");
        msg->setSrcNode(thisNode);
        msg->setCommand(NICE_PEER_TEMPORARY_RELEASE);
        msg->setLayer(-1);
        msg->setBitLength(NICEMESSAGE_L(msg));

        sendMessageToUDP(RendevouzPoint, msg);

        isTempPeered = false;

    }

    if (debug_join)
        EV << simTime() << " : " << thisNode.getAddress() << " : JoinCluster() finished." << endl;

} // JoinCluster


/******************************************************************************
 * sendHeartbeats
 */
void Nice::sendHeartbeats()
{

    /* Go through all cluster layers from top to bottom */

    for (int i=getHighestLayer(); i >= 0; i--) {

        /* Determine if node is cluster leader in this layer */
        if (!clusters[i].getLeader().isUnspecified()) {

            if (clusters[i].getLeader() == thisNode) {

                /* Build heartbeat message with info on all current members */
                NiceLeaderHeartbeat* msg = new NiceLeaderHeartbeat("NICE_LEADERHEARTBEAT");
                msg->setSrcNode(thisNode);
                msg->setCommand(NICE_LEADERHEARTBEAT);
                msg->setLayer(i);
                msg->setOne_hop_distance(simTime().dbl());
                msg->setK(k);
                msg->setSc_tolerance(SC_PROC_DISTANCE);

                msg->setMembersArraySize(clusters[i].getSize());

                /* Fill in members */
                for (int j = 0; j < clusters[i].getSize(); j++) {

                    msg->setMembers(j, clusters[i].get(j));

                }

                /* Fill in distances to members */
                msg->setDistancesArraySize(clusters[i].getSize());

                for (int j = 0; j < clusters[i].getSize(); j++) {

                    std::map<TransportAddress, NicePeerInfo*>::iterator it = peerInfos.find(clusters[i].get(j));

                    if (it != peerInfos.end()) {

                        msg->setDistances(j, it->second->get_distance());

                    }
                    else {

                        msg->setDistances(j, -1);

                    }

                }

                /* Fill in Supercluster members, if existent */
                if (clusters[i+1].getSize() > 0) {

                    msg->setSupercluster_leader(clusters[i+1].getLeader());

                    msg->setSupercluster_membersArraySize(clusters[i+1].getSize());

                    for (int j = 0; j < clusters[i+1].getSize(); j++) {

                        msg->setSupercluster_members(j, clusters[i+1].get(j));

                    }

                }

                /* Send Heartbeat to all members in cluster, except me */
                for (int j = 0; j < clusters[i].getSize(); j++) {

                    if (clusters[i].get(j) != thisNode) {

                        NiceLeaderHeartbeat *copy = static_cast<NiceLeaderHeartbeat*>(msg->dup());

                        /* Get corresponding sequence numbers out of peerInfo */
                        std::map<TransportAddress, NicePeerInfo*>::iterator it = peerInfos.find(clusters[i].get(j));

                        if (it != peerInfos.end()) {

                            unsigned int seqNo = it->second->get_last_sent_HB();

                            copy->setSeqNo(++seqNo);

                            it->second->set_backHB(it->second->get_backHBPointer(), seqNo, simTime().dbl());
                            it->second->set_last_sent_HB(seqNo);
                            it->second->set_backHBPointer(!it->second->get_backHBPointer());

                            copy->setSeqRspNo(it->second->get_last_recv_HB());

                            if (it->second->get_last_HB_arrival() > 0) {

                                copy->setHb_delay(simTime().dbl() - it->second->get_last_HB_arrival());

                            }
                            else {

                                copy->setHb_delay(0.0);

                            }

                        }

                        copy->setBitLength(NICELEADERHEARTBEAT_L(msg));

                        RECORD_STATS(++numHeartbeat; totalHeartbeatBytes += copy->getByteLength());

                        sendMessageToUDP(clusters[i].get(j), copy);

                    }

                }

                delete msg;

            }
            else { // I am normal cluster member

                /* Build heartbeat message with info on all current members */
                NiceHeartbeat* msg = new NiceHeartbeat("NICE_HEARTBEAT");
                msg->setSrcNode(thisNode);
                msg->setCommand(NICE_HEARTBEAT);
                msg->setLayer(i);
                msg->setOne_hop_distance(simTime().dbl());

                msg->setSublayermembers(0);
                if (i>0) {
                    if (clusters[i-1].getLeader() == thisNode)
                        msg->setSublayermembers(clusters[i-1].getSize());

                }

                msg->setMembersArraySize(clusters[i].getSize());

                /* Fill in members */
                for (int j = 0; j < clusters[i].getSize(); j++) {

                    msg->setMembers(j, clusters[i].get(j));

                }

                /* Fill in distances to members */
                msg->setDistancesArraySize(clusters[i].getSize());

                for (int j = 0; j < clusters[i].getSize(); j++) {

                    std::map<TransportAddress, NicePeerInfo*>::iterator it = peerInfos.find(clusters[i].get(j));

                    if (it != peerInfos.end()) {

                        msg->setDistances(j, it->second->get_distance());

                    }
                    else {

                        msg->setDistances(j, -1);

                    }

                }

                /* Send Heartbeat to all members in cluster, except me */
                for (int j = 0; j < clusters[i].getSize(); j++) {

                    if (clusters[i].get(j) != thisNode) {

                        NiceHeartbeat *copy = static_cast<NiceHeartbeat*>(msg->dup());

                        /* Get corresponding sequence number out of peerInfo */
                        std::map<TransportAddress, NicePeerInfo*>::iterator it = peerInfos.find(clusters[i].get(j));

                        if (it != peerInfos.end()) {

                            unsigned int seqNo = it->second->get_last_sent_HB();

                            copy->setSeqNo(++seqNo);

                            it->second->set_backHB(it->second->get_backHBPointer(), seqNo, simTime().dbl());
                            it->second->set_backHBPointer(!it->second->get_backHBPointer());
                            it->second->set_last_sent_HB(seqNo);

                            copy->setSeqRspNo(it->second->get_last_recv_HB());

                            copy->setHb_delay(simTime().dbl() - it->second->get_last_HB_arrival());

                        }

                        copy->setBitLength(NICEHEARTBEAT_L(msg));

                        RECORD_STATS(++numHeartbeat; totalHeartbeatBytes += copy->getByteLength());

                        sendMessageToUDP(clusters[i].get(j), copy);

                    }

                }

                delete msg;

            }
        }

    }

    // Additionally, ping all supercluster members, if existent
    if (clusters[getHighestLayer()+1].getSize() > 0) {

        NiceMessage* msg = new NiceMessage("NICE_PING_PROBE");
        msg->setSrcNode(thisNode);
        msg->setCommand(NICE_PING_PROBE);
        msg->setLayer(getHighestLayer()+1);

        msg->setBitLength(NICEMESSAGE_L(msg));

        for (int i=0; i<clusters[getHighestLayer()+1].getSize(); i++) {

            if (clusters[getHighestLayer()+1].get(i) != clusters[getHighestLayer()].getLeader()) {

                NiceMessage* dup = static_cast<NiceMessage*>(msg->dup());

                sendMessageToUDP(clusters[getHighestLayer()+1].get(i), dup);

                std::map<TransportAddress, NicePeerInfo*>::iterator it = peerInfos.find(clusters[getHighestLayer()+1].get(i));

                if (it != peerInfos.end()) {

                    it->second->set_distance_estimation_start(simTime().dbl());

                }

            }

        }

        delete msg;

    }

} // sendHeartbeats


/******************************************************************************
 * sendHeartbeatTo
 */
void Nice::sendHeartbeatTo(const TransportAddress& node, int layer)
{

    if (clusters[layer].getLeader() == thisNode) {

        /* Build heartbeat message with info on all current members */
        NiceLeaderHeartbeat* msg = new NiceLeaderHeartbeat("NICE_LEADERHEARTBEAT");
        msg->setSrcNode(thisNode);
        msg->setCommand(NICE_LEADERHEARTBEAT);
        msg->setLayer(layer);

        msg->setMembersArraySize(clusters[layer].getSize());

        /* Fill in members */
        for (int j = 0; j < clusters[layer].getSize(); j++) {

            msg->setMembers(j, clusters[layer].get(j));

        }

        /* Fill in distances to members */
        msg->setDistancesArraySize(clusters[layer].getSize());

        for (int j = 0; j < clusters[layer].getSize(); j++) {

            std::map<TransportAddress, NicePeerInfo*>::iterator it = peerInfos.find(clusters[layer].get(j));

            if (it != peerInfos.end()) {

                msg->setDistances(j, it->second->get_distance());

            }
            else {

                msg->setDistances(j, -1);

            }

        }

        /* Fill in Supercluster members, if existent */
        if (clusters[layer+1].getSize() > 0) {

            msg->setSupercluster_leader(clusters[layer+1].getLeader());

            msg->setSupercluster_membersArraySize(clusters[layer+1].getSize());

            for (int j = 0; j < clusters[layer+1].getSize(); j++) {

                msg->setSupercluster_members(j, clusters[layer+1].get(j));

            }

        }

        /* Get corresponding sequence numbers out of peerInfo */
        std::map<TransportAddress, NicePeerInfo*>::iterator it = peerInfos.find(node);

        if (it != peerInfos.end()) {

            unsigned int seqNo = it->second->get_last_sent_HB();

            msg->setSeqNo(++seqNo);

            it->second->set_backHB(it->second->get_backHBPointer(), seqNo, simTime().dbl());
            it->second->set_last_sent_HB(seqNo);
            it->second->set_backHBPointer(!it->second->get_backHBPointer());

            msg->setSeqRspNo(it->second->get_last_recv_HB());

            msg->setHb_delay(simTime().dbl() - it->second->get_last_HB_arrival());

        }

        msg->setBitLength(NICELEADERHEARTBEAT_L(msg));

        RECORD_STATS(++numHeartbeat; totalHeartbeatBytes += msg->getByteLength());

        sendMessageToUDP(node, msg);

    }
    else {

        // build heartbeat message with info on all current members
        NiceHeartbeat* msg = new NiceHeartbeat("NICE_HEARTBEAT");
        msg->setSrcNode(thisNode);
        msg->setCommand(NICE_HEARTBEAT);
        msg->setLayer(layer);

        msg->setMembersArraySize(clusters[layer].getSize());

        // fill in members
        for (int j = 0; j < clusters[layer].getSize(); j++) {

            msg->setMembers(j, clusters[layer].get(j));

        }

        // fill in distances to members
        msg->setDistancesArraySize(clusters[layer].getSize());

        for (int j = 0; j < clusters[layer].getSize(); j++) {

            std::map<TransportAddress, NicePeerInfo*>::iterator it = peerInfos.find(clusters[layer].get(j));

            if (it != peerInfos.end()) {

                msg->setDistances(j, it->second->get_distance());

            }
            else if (clusters[layer].get(j) == thisNode) {

                msg->setDistances(j, 0);

            }
            else {

                msg->setDistances(j, -1);

            }

        }

        msg->setBitLength(NICEHEARTBEAT_L(msg));

        RECORD_STATS(++numHeartbeat; totalHeartbeatBytes += msg->getByteLength());

        sendMessageToUDP(node, msg);

    }

} // sendHeartbeatTo


/******************************************************************************
 * maintenance
 */
void Nice::maintenance()
{

    // care for structure connection timer
    if (!RendevouzPoint.isUnspecified()) {

        if (RendevouzPoint == thisNode) {

            cancelEvent(structureConnectionTimer);

        }
        else {

            if (!structureConnectionTimer->isScheduled()) {

                double offset = structureConnectionInterval.dbl() * heartbeatInterval.dbl();
                scheduleAt(simTime() + offset, structureConnectionTimer);

            }

        }

    }
    else {

        EV << "No RendevouzPoint! " << endl;
        becomeRendevouzPoint();

    }

    /* Delete deprecated tempPeers from map */

    bool deleted;

    do {

        deleted = false;

        std::map<TransportAddress, simtime_t>::iterator it = tempPeers.begin();

        while (it != tempPeers.end()) {

            if (simTime() > (it->second + 3*heartbeatInterval)) {

                RECORD_STATS(++numTempPeerTimeouts);

                tempPeers.erase(it->first);
                deleted = true;
                break;

            }

            it++;

        }

    } while (deleted);

    /* Delete nodes that haven't been active for too long autonomously */

    std::map<TransportAddress, NicePeerInfo*>::iterator it2 = peerInfos.begin();

    while (it2 != peerInfos.end()) {

        if (it2->first != thisNode) {

            double offset = peerTimeoutInterval.dbl()*heartbeatInterval.dbl();
            if (simTime() > (it2->second->getActivity() + offset)) {

                if (debug_peertimeouts) {
                    EV << simTime() << " : " << thisNode.getAddress() << " : PEER TIMED OUT! : " << it2->first << endl;
                    EV << "Activity : " << it2->second->getActivity() << endl;
                }

                RECORD_STATS(++numPeerTimeouts);

                // Delete node from all layer clusters
                for (int i=0; i<maxLayers; i++) {

                    if (clusters[i].contains(it2->first)) {

                        clusters[i].remove(it2->first);

                        /* If node was leader, elect new! */
                        if (!(clusters[i].getLeader().isUnspecified())) {

                            if (clusters[i].getLeader() == it2->first) {

                                if (debug_peertimeouts)
                                    EV << simTime() << " : " << thisNode.getAddress() << " : Need new Cluster Leader for Cluster : " << i << endl;

                                // Perform check for new cluster leader
                                TaSet cl;
                                for (int l=0; l<clusters[i].getSize(); l++) {

                                    cl.insert(clusters[i].get(l));

                                }

                                TransportAddress new_leader = findCenter(cl).first;

                                /* Remove old leader from supercluster */
                                clusters[i+1].remove(clusters[i].getLeader());

                                clusters[i].setLeader(new_leader);

                                if (new_leader == thisNode) {

                                    // I am new leader
                                    if (clusters[i+1].getSize() > 0) {

                                        /* TODO Experimental Hack: If old leader is also leader of sc -> basicJoinLayer*/
                                        if (clusters[i+1].getLeader() == clusters[i].getLeader()) {

                                            /* Locally add thisNode, too */
                                            clusters[i+1].add(thisNode);

                                            for (short j=0; j<maxLayers; j++) {

                                                if (clusters[j].getSize() > 0) {

                                                    if (clusters[j].contains(thisNode)) {

                                                        getParentModule()->getParentModule()->getDisplayString().setTagArg
                                                        ("i2", 1, clustercolors[j]);

                                                    }

                                                }

                                            }

                                            BasicJoinLayer(i+1);

                                        }
                                        else {

                                            JoinCluster(clusters[i+1].getLeader(), i+1);

                                        }

                                    }
                                    else {

                                        //TODO Experimental
                                        //Poll RP if existent
                                        //pollRP(getHighestLayer());
                                        //becomeRendevouzPoint();

                                    }

                                    for (int n=0; n<clusters[i].getSize(); n++) {

                                        if (clusters[i].get(n) != thisNode)
                                            showOverlayNeighborArrow(clusters[i].get(n), false, clusterarrows[i]);

                                    }

                                }

                            }

                        }

                    }

                }

                TransportAddress cand = it2->first;
                ++it2;
                peerInfos.erase(cand);
                continue;

            }
        }

        it2++;

    }

    // if node is cluster leader, check size bounds for every cluster
    for (int i=getHighestLayer(); i >= 0; i--) {

        //TODO Experimental Check for inconsistency: If I am node in cluster but not leader in subcluster, remove
        if (clusters[i].contains(thisNode) && (i > 0)) {

            if (clusters[i-1].getLeader() != thisNode) {

                Remove(i);
                return;

            }

        }

        if (!clusters[i].getLeader().isUnspecified()) {

            if (clusters[i].getLeader() == thisNode) {

                if (clusters[i].getSize() > (3*k-1)) {

                    ClusterSplit(i);

                    return;

                }


                if ((clusters[i].getSize() < k) && (clusters[i+1].getSize() > 1)) {

                    EV << simTime() << " : " << thisNode.getAddress()
                    << ": CLUSTER MERGE!: " << i << endl;

                    ClusterMerge(i);

                    return;

                }
                else if ((clusters[i].getSize() < k)) {


                }

            }

        }

    } // All Layers

    // if highest super cluster has more than one member
    if (clusters[getHighestLayer()+1].getSize() > 1) {

        if (clusterrefinement)
            EV << simTime() << " : " << thisNode.getAddress() << " : Look for better parent node in cluster : " << getHighestLayer()+1 << " ..."<< endl;

        std::map<TransportAddress, NicePeerInfo*>::iterator it = peerInfos.find(clusters[getHighestLayer()].getLeader());

        if (it != peerInfos.end()) {

            if (it->second->get_distance() > 0) {

                double distance = it->second->get_distance() - ((it->second->get_distance()/100.0) * SC_PROC_DISTANCE);

                double smallest = 10000.0;
                TransportAddress candidate = TransportAddress::UNSPECIFIED_NODE;

                for (int i=0; i < clusters[getHighestLayer()+1].getSize(); i++) {

                    if (clusters[getHighestLayer()+1].get(i) != clusters[getHighestLayer()].getLeader()) {

                        std::map<TransportAddress, NicePeerInfo*>::iterator it2 = peerInfos.find(clusters[getHighestLayer()+1].get(i));

                        if (it2 != peerInfos.end()) {

                            if ((it2->second->get_distance() < smallest) && (it2->second->get_distance() > 0)) {
                                smallest = it2->second->get_distance();
                                candidate = it2->first;
                            }

                        }

                    }

                }

                std::set<TransportAddress> clusterset;

                for (int m=0; m<clusters[getHighestLayer()+1].getSize(); m++) {

                    clusterset.insert(clusters[getHighestLayer()+1].get(m));

                }

                simtime_t meanDistance = getMeanDistance(clusterset);

                simtime_t minCompare = (meanDistance/100.0)*SC_MIN_OFFSET;

                RECORD_STATS(totalSCMinCompare += minCompare.dbl());

                if (minCompare < 0.005)
                    minCompare = 0.005;

                if ((smallest < distance) && ((distance - smallest) > minCompare.dbl())) { // change supercluster


                    if (clusterrefinement) {
                        EV << simTime() <<" : " << thisNode.getAddress() << ": Change SuperCluster! to " << candidate.getAddress() << endl;
                        EV << "Old distance ():  " << it->second->get_distance() << endl;
                        EV << "SC_PROC_DISTANCE:  " << SC_PROC_DISTANCE << endl;
                        EV << "Compare distance:  " << distance << endl;
                        EV << "New distance:  " << smallest << endl;
                        EV << "New SC_MIN_OFFSET:  " << SC_MIN_OFFSET << endl;
                    }

                    short highestLayer = getHighestLayer();

                    // leave old
                    Remove(highestLayer);

                    // join new
                    JoinCluster(candidate, highestLayer);

                    return;

                }
            }

        }
        else {

            //Do nothing

        }

    }

    //return;
    for (int i=getHighestLayer(); i >= 0; i--) {

        if (clusters[i].getSize() > 1 && clusters[i].getLeader() == thisNode) {

            bool allDistancesKnown = true;

            if (clusterrefinement)
                EV << simTime() << " : " << thisNode.getAddress() << " : Find better cluster leader in ..." << i << endl;

            /* Only make decisions if node has total distance knowledge in this cluster */
            for (int j=0; j<clusters[i].getSize(); j++) {

                /* Check if peer info already exists */
                std::map<TransportAddress, NicePeerInfo*>::iterator it = peerInfos.find(clusters[i].get(j));

                if (it != peerInfos.end()) { /* We already know this node */

                    simtime_t distance = it->second->get_distance();

                    //EV << "My distance to " << it->first << " : " << distance << endl;

                    if (distance < 0) {
                        allDistancesKnown = false;
                        continue;
                    }

                    for (int k=0; k<clusters[i].getSize(); k++) {

                        if ((it->first != thisNode) && (clusters[i].get(k) != it->first)) {

                            if (it->second->getDistanceTo(clusters[i].get(k)) < 0) {
                                allDistancesKnown = false;
                                break;
                            }
                        }

                    }

                }
                else { /* Create PeerInfo object */

                    allDistancesKnown = false;

                }

            }

            if (allDistancesKnown) {

                if (clusterrefinement)
                    EV << "Complete distance knowledge available." << endl;

                // Perform check for better cluster leader
                TaSet cl;
                for (int l=0; l<clusters[i].getSize(); l++) {

                    cl.insert(clusters[i].get(l));

                }

                TransportAddress new_leader = findCenter(cl).first;

                if (clusterrefinement)
                    EV << "NEW LEADER laut " << thisNode.getAddress() << " --> " << new_leader.getAddress() << endl;

                std::set<TransportAddress> clusterset;

                for (int m=0; m<clusters[i].getSize(); m++) {

                    clusterset.insert(clusters[i].get(m));

                }


                simtime_t meanDistance = getMeanDistance(clusterset);
                simtime_t oldDistance = getMaxDistance(clusters[i].getLeader(), clusterset);
                simtime_t newDistance = getMaxDistance(new_leader, clusterset);
                simtime_t compareDistance = (oldDistance - ((oldDistance/100.0)*CLUSTERLEADERCOMPAREDIST));

                simtime_t minCompare = (meanDistance/100.0)*CLUSTERLEADERBOUND;

                if (minCompare < 0.005)
                    minCompare = 0.005;

                if ((newDistance.dbl() < compareDistance.dbl()) && ((compareDistance.dbl() - newDistance.dbl()) > minCompare.dbl())) {

                    if (clusterrefinement)
                        EV << "CHANGE " << CLUSTERLEADERCOMPAREDIST << endl;

                    if (new_leader != thisNode) {

                        /* Check if I was leader*/
                        if (clusters[i].getLeader() == thisNode) {

                            for (int j=0; j<clusters[i].getSize(); j++) {

                                deleteOverlayNeighborArrow(clusters[i].get(j));

                            }

                            gracefulLeave(i);

                            LeaderTransfer(i, new_leader, cl, new_leader);

                            getParentModule()->getParentModule()
                            ->getDisplayString().setTagArg("i2", 0, "block/circle_vs");


                        }
                        else {

                            // Do nothing

                        }

                    }
                    else {

                        /* I am the new leader of this cluster */
                        /* Check if I already was*/
                        if (clusters[i].getLeader() == thisNode) {

                            // Do nothing

                        }
                        else {

                            /* Remove old leader from supercluster */
                            clusters[i+1].remove(clusters[i].getLeader());

                            // I am new leader
                            if (clusters[i+1].getSize() > 0) {

                                /* If old leader is also leader of sc -> basicJoinLayer*/
                                if (clusters[i+1].getLeader() == clusters[i].getLeader()) {

                                    /* Locally add thisNode, too */
                                    clusters[i+1].add(thisNode);

                                    for (short j=0; j<maxLayers; j++) {

                                        if (clusters[j].getSize() > 0) {

                                            if (clusters[j].contains(thisNode)) {

                                                getParentModule()->getParentModule()->getDisplayString().setTagArg
                                                ("i2", 1, clustercolors[j]);

                                            }

                                        }

                                    }

                                    BasicJoinLayer(i+1);

                                }
                                else {

                                    JoinCluster(clusters[i+1].getLeader(), i+1);

                                }

                            }
                            else {

                                becomeRendevouzPoint();

                            }

                            for (int n=0; n<clusters[i].getSize(); n++) {

                                if (clusters[i].get(n) != thisNode)
                                    showOverlayNeighborArrow(clusters[i].get(n), false, clusterarrows[i]);

                            }


                        }


                    }

                    // Set new leader for this cluster
                    clusters[i].setLeader(new_leader);

                }

                if (clusterrefinement) {
                    EV << "MaxDistance " << new_leader.getAddress() << " : " << getMaxDistance(new_leader, clusterset) << endl;
                    EV << "MaxDistance " << clusters[i].getLeader() << " : " << getMaxDistance(clusters[i].getLeader(), clusterset) << endl;
                    EV << "MaxDistance " << thisNode.getAddress() << " : " << getMaxDistance(thisNode, clusterset) << endl;
                }


            }
            else {

                //return;

            }

        } // getSize() > 1

    }

} // maintenance


/******************************************************************************
 * ClusterSplit
 */
void Nice::ClusterSplit(int layer)
{

    EV << simTime() << " : " << thisNode.getAddress() << " : ClusterSplit in Layer " << layer << endl;

    /* Get cluster to be splitted */
    NiceCluster cluster = clusters[layer];

    /* Introduce some helper structures */
    std::vector<TransportAddress> vec1;
    std::vector<TransportAddress> vec2;
    std::vector<TransportAddress> cl1;
    std::vector<TransportAddress> cl2;
    TransportAddress cl1_center = TransportAddress::UNSPECIFIED_NODE;
    TransportAddress cl2_center = TransportAddress::UNSPECIFIED_NODE;
    simtime_t min_delay = 999;

    for (int i=0; i<cluster.getSize(); i++) {

        /* Delete all arrows in visualization */
        deleteOverlayNeighborArrow(cluster.get(i));

        /* Put all members to first vector */
        vec1.push_back(cluster.get(i));
        //EV << "vec1.push_back(cluster.get(i)): " << cluster.get(i).getAddress() << endl;

        /* Put first half of members to second vector */
        if (i < cluster.getSize()/2) {
            vec2.push_back(cluster.get(i));
            //EV << "vec2.push_back(cluster.get(i)): " << cluster.get(i).getAddress() << endl;
        }

    }

    int combinations = 0;

    TaSet cl1set, cl2set, newClSet;
    TaSet::iterator sit;

    if (cluster.getSize() < /*(6*k-1)*/18) {

        /* Go through all combinations of clusters */
        do {

            combinations++;

            //EV << "combinations: " << combinations << endl;

            /* Introduce some helper structures */
            TransportAddress q1_center;
            TransportAddress q2_center;
            std::vector<TransportAddress> vec3;

            /* Determine elements that are in first set but not in second */
            std::set_difference(vec1.begin(), vec1.end(), vec2.begin(), vec2.end(), inserter(vec3, vec3.begin()));

            simtime_t min_q1_delay = 999;
            simtime_t min_q2_delay = 999;
            simtime_t max_delay = 0;

            q1_center = findCenter(vec2).first;

            //EV << "q1_center: " << q1_center.getAddress() << endl;

            std::map<TransportAddress, NicePeerInfo*>::iterator it = peerInfos.find(q1_center);

            if (it != peerInfos.end()) {

                min_q1_delay = it->second->get_distance();

            }
            else {

                min_q1_delay = 0;

            }

            q2_center = findCenter(vec3).first;

            //EV << "q2_center: " << q2_center.getAddress() << endl;

            it = peerInfos.find(q2_center);

            if (it != peerInfos.end()) {

                min_q2_delay = it->second->get_distance();

            }
            else {

                min_q2_delay = 0;

            }

            max_delay = std::max(min_q1_delay, min_q2_delay);

            if (min_delay == 0) min_delay = max_delay;

            if ((max_delay < min_delay) && !q1_center.isUnspecified() && !q2_center.isUnspecified()) {

                min_delay = max_delay;
                cl1 = vec2;
                cl2 = vec3;
                cl1_center = q1_center;
                cl2_center = q2_center;
            }

            //isplay(q1.begin(),q1.end());

        } while (next_combination(vec1.begin(), vec1.end(), vec2.begin(), vec2.end()));

        //EV << "COMBINATIONS: " << combinations << endl;

        //build sets
        std::vector<TransportAddress>::iterator vit;

        //EV << "cl1_center: " << cl1_center << endl;
        //EV << "cl2_center: " << cl2_center << endl;

        vit = cl1.begin();
        while (vit != cl1.end()) {
            cl1set.insert(*vit);
            vit++;
        }

        vit = cl2.begin();
        while (vit != cl2.end()) {
            cl2set.insert(*vit);
            vit++;
        }

    }
    else {

    }

    //if no valid split possible, split randomly
    if (cl1_center.isUnspecified() || cl2_center.isUnspecified()) {
        EV << thisNode.getAddress() << " RANDOM SPLIT" << endl;
        cl1set.clear();
        cl2set.clear();
        for (int i=0; i<cluster.getSize(); i++) {
            if (i < cluster.getSize()/2) {
                cl1set.insert(cluster.get(i));
            }
            else {
                cl2set.insert(cluster.get(i));
            }
        }
        cl1_center = findCenter(cl1set,true).first;
        cl2_center = findCenter(cl2set,true).first;
    }

    //find new neighbors
    TransportAddress newLeader, otherLeader;
    TaSet newCl;
    TaSet::iterator it = cl1set.begin();
    while (it != cl1set.end()) {
        if (*it == thisNode) {
            newCl = cl1set;
            newLeader = cl1_center;
            otherLeader = cl2_center;
        }
        it++;
    }

    it = cl2set.begin();
    while (it != cl2set.end()) {
        if (*it == thisNode) {
            newCl = cl2set;
            newLeader = cl2_center;
            otherLeader = cl1_center;
        }
        it++;

    }

    //####################################################################

    // Cluster split accomplished, now handling consequences

    // CASE 1: We lost all cluster leaderships
    // repair all cluster layer, top down
    if ((cl1_center != thisNode) && (cl2_center != thisNode)) {

        gracefulLeave(layer);

        if (clusters[layer+1].getSize() == 0) {

            clusters[layer+1].add(cl1_center);
            clusters[layer+1].add(cl2_center);
            clusters[layer+1].setLeader(cl1_center);
            RendevouzPoint = cl1_center;
            EV << simTime() << " : " << thisNode.getAddress() << " : Set RP to " << cl1_center.getAddress() << endl;


        }

        LeaderTransfer(layer, cl1_center, cl1set, cl1_center);
        LeaderTransfer(layer, cl2_center, cl2set, cl1_center);

        getParentModule()->getParentModule()
        ->getDisplayString().setTagArg("i2", 0, "block/circle_vs");

    } // CASE 1


    // CASE 2: We stay leader in one of the new clusters
    if ((cl1_center == thisNode) || (cl2_center == thisNode)) {

        if (clusters[layer+1].getSize() == 0) {

            clusters[layer+1].add(cl1_center);
            clusters[layer+1].add(cl2_center);

            clusters[layer+1].setLeader(thisNode);


        }

        if (cl1_center == thisNode) {

            clusters[layer+1].add(cl2_center);
            LeaderTransfer(layer, cl2_center, cl2set, cl1_center);

        }
        else {

            clusters[layer+1].add(cl1_center);
            LeaderTransfer(layer, cl1_center, cl1set, cl1_center);

        }


    } // CASE 2

    // update local cluster information for focussed layer
    TaSet::const_iterator cit = cl1set.begin();
    bool found = false;
    while (cit != cl1set.end()) {

        if (*cit == thisNode)
            found = true;
        cit++;
    }

    clusters[layer].clear();

    if (found) {

        clusters[layer].setLeader(cl1_center);

        cit = cl1set.begin();
        while (cit != cl1set.end()) {
            clusters[layer].add(*cit);
            cit++;
        }

    }
    else {

        clusters[layer].setLeader(cl2_center);

        cit = cl2set.begin();
        while (cit != cl2set.end()) {
            clusters[layer].add(*cit);
            cit++;
        }

    }

    //update arrows
    updateVisualization();

    if (pimp)
        sendHeartbeats();

} // ClusterSplit


/******************************************************************************
 * ClusterMerge
 */
void Nice::ClusterMerge(int layer)
{

    simtime_t min_delay = 999;

    TransportAddress min_node = TransportAddress::UNSPECIFIED_NODE;

    for (int i=0; i<clusters[layer+1].getSize(); i++) {

        TransportAddress node = clusters[layer+1].get(i);

        if (node != thisNode) {

            std::map<TransportAddress, NicePeerInfo*>::iterator it = peerInfos.find(node);

            if (it != peerInfos.end()) {
                simtime_t delay = it->second->get_distance();

                if ((delay > 0) && (delay < min_delay)) {

                    min_delay = delay;
                    min_node = node;

                }
            }
        }

    }

    if (!min_node.isUnspecified()) {

        // leave
        for (int i=maxLayers-1; i>layer; i--) {

            if (clusters[i].getSize() > 0) {

                if (clusters[i].contains(thisNode)) {

                    EV << "REPAIR: " << i << endl;

                    if (clusters[i].getLeader() == thisNode) {

                        EV << "1 remove from: " << i << endl;
                        Remove(i);

                        TaSet cl;
                        for (int j=0; j<clusters[i].getSize(); j++) {

                            cl.insert(clusters[i].get(j));

                            deleteOverlayNeighborArrow(clusters[i].get(j));

                        }

                        TransportAddress new_sc_center = findCenter(cl).first;

                        EV << "NEW LEADER (CM): " << i << " --> " << new_sc_center.getAddress() << endl;

                        clusters[i].setLeader(new_sc_center);

                        LeaderTransfer(i, new_sc_center, cl, new_sc_center);

                        // repair RP
                        if (i == maxLayers-1) {

                            RendevouzPoint = new_sc_center;
                            EV << simTime() << " : " << thisNode.getAddress() << " : Set RP to " << new_sc_center.getAddress() << endl;

                        }
                        else if (clusters[i+1].getSize() == 0) {

                            RendevouzPoint = new_sc_center;
                            EV << simTime() << " : " << thisNode.getAddress() << " : Set RP to " << new_sc_center.getAddress() << endl;

                        }

                    }
                    else {

                        // simply leave cluster
                        EV << "2 remove from: " << i << endl;
                        Remove(i);
                        clusters[i].remove(thisNode);

                    }

                }

            }

        }

        clusters[layer+1].remove(thisNode);

        for (int j=0; j<clusters[layer+1].getSize(); j++) {

            deleteOverlayNeighborArrow(clusters[layer+1].get(j));

        }

        for (short i=0; i<maxLayers; i++) {

            if (clusters[i].getSize() > 0) {

                if (clusters[i].contains(thisNode)) {

                    getParentModule()->getParentModule()->getDisplayString().setTagArg
                    ("i2", 1, clustercolors[i]);

                }

            }

        }

        // send merge request
        ClusterMergeRequest(min_node, layer);

    }
    else {

        EV << thisNode.getAddress() << " no suitable cluster found";

    }

} // ClusterMerge


/******************************************************************************
 * ClusterMergeRequest
 */
void Nice::ClusterMergeRequest(const TransportAddress& node, int layer)
{

    NiceClusterMerge* msg = new NiceClusterMerge("NICE_CLUSTER_MERGE_REQUEST");
    msg->setSrcNode(thisNode);
    msg->setCommand(NICE_CLUSTER_MERGE_REQUEST);
    msg->setLayer(layer);

    msg->setMembersArraySize(clusters[layer].getSize());

    /* Fill in members */
    for (int j = 0; j < clusters[layer].getSize(); j++) {

        msg->setMembers(j, clusters[layer].get(j));

        deleteOverlayNeighborArrow(clusters[layer].get(j));

    }

    msg->setNewClusterLeader(clusters[layer+1].getLeader());

    msg->setBitLength(NICECLUSTERMERGE_L(msg));

    clusters[layer].setLeader(node);

    getParentModule()->getParentModule()
    ->getDisplayString().setTagArg("i2", 0, "block/circle_vs");

    sendMessageToUDP(node, msg);

} // ClusterMergeRequest


/******************************************************************************
 * findClusterLeader
 */
TransportAddress Nice::findClusterLeader(NiceCluster cluster)
{
    // Function not implemented. Do not call.
    fprintf(stderr, "Called unimplemented Nice.findClusterLeader\n");
    fflush(stderr);
    throw 0;

    // Determine if there is a more fitting cluster leader than me

    // First, get own maxDistance for this Cluster
    std::set<TransportAddress> clusterset;

    for (int i=0; i<cluster.getSize(); i++) {

        clusterset.insert(cluster.get(i));
        EV << simTime() << " : " << thisNode.getAddress() << " : MaxDistance: "
        << cluster.get(i).getAddress() << " : " << getMaxDistance(cluster.get(i), clusterset) << endl;

    }

    //simtime_t myDistance = getMaxDistance(thisNode, clusterset);

    //EV << simTime() << " : " << thisNode.getAddress() << " : myMaxDistance: " << myDistance << endl;


} // findClusterLeader


/******************************************************************************
 * findCenter
 */
std::pair<TransportAddress,simtime_t> Nice::findCenter(TaSet cluster, bool allowRandom)
{

    TaSet::const_iterator it = cluster.begin();
    TransportAddress center = TransportAddress::UNSPECIFIED_NODE;
    simtime_t min_delay = 1000;

    if (cluster.size() > 1) {

        while (it != cluster.end()) {

            simtime_t delay = getMaxDistance(*it, cluster);

            if ((delay > 0) && (delay < min_delay)) {

                min_delay = delay;
                center = *it;

            }

            it++;
        }

    }

    if (center.isUnspecified()) {
        center = *(cluster.begin());
    }
    //EV << "center: " << center << endl;
    return std::make_pair(center, min_delay);

} // findCenter


/******************************************************************************
 * findCenter
 */
std::pair<TransportAddress, simtime_t> Nice::findCenter(std::vector<TransportAddress> cluster, bool allowRandom)
{
    TaSet clusterset;
    std::vector<TransportAddress>::const_iterator it = cluster.begin();

    //EV << "cluster.size(): " << cluster.size() << endl;

    while (it != cluster.end()) {
        clusterset.insert(*it);
        it++;
    }
    return findCenter(clusterset, allowRandom);

} // findCenter


/******************************************************************************
 * getMaxDistance
 */
simtime_t Nice::getMaxDistance(TransportAddress member, std::set<TransportAddress> neighbors)
{
    simtime_t maxDelay = 0;
    simtime_t delay = 0;

    if (member == thisNode) {

        std::set<TransportAddress>::iterator it = neighbors.begin();

        while (it != neighbors.end()) {

            std::map<TransportAddress, NicePeerInfo*>::iterator it2 = peerInfos.find(*it);

            if (it2 != peerInfos.end()) {

                delay = it2->second->get_distance();
                maxDelay = std::max(delay, maxDelay);

            }

            it++;

        }

    }
    else {

        std::map<TransportAddress, NicePeerInfo*>::iterator it = peerInfos.find(member);

        if (it != peerInfos.end()) {

            std::set<TransportAddress>::iterator it2 = neighbors.begin();

            while (it2 != neighbors.end()) {

                //EV << "getDistanceTo " << *it2 << endl;
                delay = it->second->getDistanceTo(*it2);
                //EV << thisNode.getAddress() << " : Distance to " << it2->getAddress() << " : " << delay << endl;
                maxDelay = std::max(delay, maxDelay);

                it2++;

            }

        }

    }

    return maxDelay;

} // getMaxDistance

/******************************************************************************
 * getMeanDistance
 */
simtime_t Nice::getMeanDistance(std::set<TransportAddress> neighbors)
{
    simtime_t meanDelay = 0;
    simtime_t delay = 0;
    unsigned int number = 0;

    std::set<TransportAddress>::iterator it = neighbors.begin();

    while (it != neighbors.end()) {

        if (*it != thisNode) {

            std::map<TransportAddress, NicePeerInfo*>::iterator it2 = peerInfos.find(*it);

            if (it2 != peerInfos.end()) {

                delay = it2->second->get_distance();
                //EV << "delay to " << *it << " : " << delay << endl;

                if  (delay > 0.0) {

                    meanDelay += delay;
                    number++;

                }

            }

        }

        it++;

    }

    if (number > 0) {

        return meanDelay/number;

    }
    else {

        return 0;

    }

} // getMeanDistance


/******************************************************************************
 * LeaderTransfer
 */
void Nice::LeaderTransfer(int layer, TransportAddress leader, TaSet cluster, TransportAddress sc_leader)
{

    NiceLeaderHeartbeat* msg = new NiceLeaderHeartbeat("NICE_LEADERTRANSFER");
    msg->setSrcNode(thisNode);
    msg->setCommand(NICE_LEADERTRANSFER);
    msg->setLayer(layer);

    msg->setMembersArraySize(cluster.size());

    // fill in members
    TaSet::iterator it = cluster.begin();
    int i = 0;
    while (it != cluster.end()) {
        msg->setMembers(i++, *it);
        it++;
    }

    // fill in supercluster members, if existent
    if (clusters[layer+1].getSize() > 0) {

        msg->setSupercluster_leader(clusters[layer+1].getLeader());

        msg->setSupercluster_membersArraySize(clusters[layer+1].getSize());

        for (int j = 0; j < clusters[layer+1].getSize(); j++) {

            msg->setSupercluster_members(j, clusters[layer+1].get(j));

        }

    }
    else {

        msg->setSupercluster_leader(sc_leader);

    }

    msg->setBitLength(NICELEADERHEARTBEAT_L(msg));

    sendMessageToUDP(leader, msg);

} // LeaderTransfer


/******************************************************************************
 * Remove
 */
void Nice::Remove(int layer)
{
    if (debug_removes)
        EV << simTime() << " : " << thisNode.getAddress() << " : Remove()" << endl;

    NiceMessage* msg = new NiceMessage("NICE_REMOVE");
    msg->setSrcNode(thisNode);
    msg->setCommand(NICE_REMOVE);
    msg->setLayer(layer);

    msg->setBitLength(NICEMESSAGE_L(msg));

    sendMessageToUDP(clusters[layer].getLeader(), msg);

    clusters[layer].remove(thisNode);

    for (short i=0; i<maxLayers; i++) {

        if (clusters[i].getSize() > 0) {

            if (clusters[i].contains(thisNode)) {

                getParentModule()->getParentModule()->getDisplayString().setTagArg
                ("i2", 1, clustercolors[i]);

            }

        }

    }

    if (debug_removes)
        EV << simTime() << " : " << thisNode.getAddress() << " : Remove() finished." << endl;


} // Remove


/******************************************************************************
 * gracefulLeave
 */
void Nice::gracefulLeave(short bottomLayer)
{
    EV << simTime() << " : " << thisNode.getAddress() << " : gracefulLeave()" << endl;

    for (int i=maxLayers-1; i>bottomLayer; i--) {

        if (clusters[i].getSize() > 0) {

            if (clusters[i].contains(thisNode)) {

                EV << "REPAIR: " << i << endl;

                if (clusters[i].getLeader() == thisNode) {

                    EV << "remove from: " << i << endl;
                    Remove(i);

                    if (clusters[i].getSize() > 0) {

                        TaSet cl;
                        for (int j=0; j<clusters[i].getSize(); j++) {

                            cl.insert(clusters[i].get(j));

                            EV << "rest: " << clusters[i].get(j).getAddress() << endl;

                            deleteOverlayNeighborArrow(clusters[i].get(j));

                        }

                        TransportAddress new_sc_center = findCenter(cl).first;

                        EV << "NEW LEADER (GL): " << i << " --> " << new_sc_center.getAddress() << endl;

                        if (new_sc_center.isUnspecified()) {

                            new_sc_center = clusters[i].get(0);

                            EV << "UNSPECIFIED! instead choose: " << new_sc_center.getAddress() << endl;

                        }

                        clusters[i].setLeader(new_sc_center);

                        LeaderTransfer(i, new_sc_center, cl, new_sc_center);

                        // repair RP
                        if (i == maxLayers-1) {

                            RendevouzPoint = new_sc_center;
                            EV << simTime() << " : " << thisNode.getAddress() << " : Set RP to " << new_sc_center.getAddress() << endl;

                        }
                        else if (clusters[i+1].getSize() == 0) {

                            RendevouzPoint = new_sc_center;
                            EV << simTime() << " : " << thisNode.getAddress() << " : Set RP to " << new_sc_center.getAddress() << endl;

                        }

                    }

                }
                else {

                    // simply leave cluster
                    EV << "removing " << thisNode.getAddress() << " from " << i << endl;
                    Remove(i);
                    clusters[i].remove(thisNode);

                }

            }

        }

    }

    EV << simTime() << " : " << thisNode.getAddress() << " : gracefulLeave() finished." << endl;


} // gracefulLeave


/******************************************************************************
 * handleAppMessage
 */
void Nice::handleAppMessage(cMessage* msg)
{
    if ( ALMAnycastMessage* anycastMsg = dynamic_cast<ALMAnycastMessage*>(msg) ) {
        EV << "[Nice::handleAppMessage() @ " << overlay->getThisNode().getAddress()
           << " (" << overlay->getThisNode().getKey().toString(16) << ")]\n"
           << "    Anycast message for group " << anycastMsg->getGroupId() << "\n"
           << "    ignored: Not implemented yet!"
           << endl;
    }
    else if ( ALMCreateMessage* createMsg = dynamic_cast<ALMCreateMessage*>(msg) ) {
        EV << "[Nice::handleAppMessage() @ " << overlay->getThisNode().getAddress()
           << " (" << overlay->getThisNode().getKey().toString(16) << ")]\n"
           << "    Create message for group " << createMsg->getGroupId() << "\n"
           << "    ignored: Not implemented yet!"
           << endl;
    }
    else if ( ALMDeleteMessage* deleteMsg = dynamic_cast<ALMDeleteMessage*>(msg) ) {
        EV << "[Nice::handleAppMessage() @ " << overlay->getThisNode().getAddress()
           << " (" << overlay->getThisNode().getKey().toString(16) << ")]\n"
           << "    Delete message for group " << deleteMsg->getGroupId() << "\n"
           << "    ignored: Not implemented yet!"
           << endl;
    }
    else if ( ALMLeaveMessage* leaveMsg = dynamic_cast<ALMLeaveMessage*>(msg) ) {
        EV << "[Nice::handleAppMessage() @ " << overlay->getThisNode().getAddress()
           << " (" << overlay->getThisNode().getKey().toString(16) << ")]\n"
           << "    Leave message for group " << leaveMsg->getGroupId() << "\n"
           << "    ignored: Not implemented yet!"
           << endl;
    }
    else if ( ALMMulticastMessage* multicastMsg = dynamic_cast<ALMMulticastMessage*>(msg) ) {
        NiceMulticastMessage *niceMsg = new NiceMulticastMessage("NICE_MULTICAST");
        niceMsg->setCommand(NICE_MULTICAST);
        niceMsg->setLayer(-1);
        niceMsg->setSrcNode(thisNode);
        niceMsg->setLastHop(thisNode);
        niceMsg->setHopCount(0);
//        niceMsg->setBitLength(NICEMULTICAST_L(niceMsg));//hoang disabled
        //hoang
//        niceMsg->setBitLength(multicastMsg->getBitLength());
//        niceMsg->setSendTime(simTime().dbl());
        niceMsg->setSendTime(global->getRealTime());
        niceMsg->setBitLength(720);
        niceMsg->setSenderID(nodeID);
        niceMsg->setLastHopID(nodeID);
        niceMsg->setSeqNo(numAppMsg++);
//        niceMsg->setXw(multicastMsg->getXw());
        delete multicastMsg;
        //end of hoang

//        niceMsg->encapsulate(multicastMsg); //hoang disabled
        sendDataToOverlay(niceMsg);

        // otherwise msg gets deleted later

        msg = NULL;
    }
    else if ( ALMSubscribeMessage* subscribeMsg = dynamic_cast<ALMSubscribeMessage*>(msg) ) {
        EV << "[Nice::handleAppMessage() @ " << overlay->getThisNode().getAddress()
           << " (" << overlay->getThisNode().getKey().toString(16) << ")]\n"
           << "    Subscribe message for group " << subscribeMsg->getGroupId() << "\n"
           << "    ignored: Not implemented yet!"
           << endl;
    }

    // Delete msg if not already deleted
    if ( msg ) {
        delete msg;
    }

} // handleAppMessage


/******************************************************************************
 * sendDataToOverlay
 */
void Nice::sendDataToOverlay(NiceMulticastMessage *appMsg)
{

    for (int layer=0; clusters[layer].contains(thisNode); layer++) {

        if ( appMsg->getLayer() != layer ) {

            for (int j=0; j<clusters[layer].getSize(); j++) {

                if (!(clusters[layer].contains(appMsg->getLastHop())) || appMsg->getSrcNode() == thisNode) {

                    const TransportAddress& member = clusters[layer].get(j);

                    if (!(member == thisNode)) {

                        NiceMulticastMessage* dup = static_cast<NiceMulticastMessage*>(appMsg->dup());

                        dup->setLayer( layer );
                        dup->setLastHop(thisNode);

                        //hoang
                        dup->setLastHopID(nodeID);
                        global->recordOut(nodeID,dup->getSenderID(),dup->getSeqNo(),global->getNodeIDofAddress(member.getAddress()), dup->getBitLength());
                        global->incNumSentData();
                        global->addToBitSentData(dup->getBitLength());
                        //end of hoang
                        sendMessageToUDP(member, dup);

                    }

                }

            } // for

        }

    }

    // Also forward data to temporary peers
    std::map<TransportAddress, simtime_t>::iterator it = tempPeers.begin();

    while (it != tempPeers.end()) {

        NiceMulticastMessage* dup = static_cast<NiceMulticastMessage*>(appMsg->dup());

        dup->setSrcNode(thisNode);

        //hoang
        dup->setLastHopID(nodeID);
        //FIXME: record sid=0 in case of streaming (only one sender=0)
        global->recordOut(nodeID,dup->getSenderID(),dup->getSeqNo(),global->getNodeIDofAddress((it->first).getAddress()), dup->getBitLength());
        global->incNumSentData();
		global->addToBitSentData(dup->getBitLength());
        //end of hoang
        sendMessageToUDP(it->first, dup);

        it++;

    }

    delete appMsg;

} // sendDataToOverlay


/******************************************************************************
 * updateVisualization
 */
void Nice::updateVisualization()
{

    if (debug_visualization)
        EV << simTime() << " : " << thisNode.getAddress() << " : updateVisualization" << endl;

    /* Update node symbol */
    getParentModule()->getParentModule()
    ->getDisplayString().setTagArg("i2", 0, "block/circle_vs");

    if (!RendevouzPoint.isUnspecified()) {

        if (RendevouzPoint == thisNode) {

            getParentModule()->getParentModule()
            ->getDisplayString().setTagArg("i2", 0, "block/star_vs");

        }

    }

    /* Update node color */
    if (debug_visualization)
        EV << "getHighestLayer(): " << getHighestLayer() << endl;

    getParentModule()->getParentModule()
    ->getDisplayString().setTagArg("i2", 1, clustercolors[getHighestLayer()]);

    //redraw
    for (int i=0; clusters[i].contains(thisNode); i++) {

        if (!(clusters[i].getLeader().isUnspecified())) {

            if (clusters[i].getLeader() == thisNode) {

                for (int j=0; j<clusters[i].getSize();j++) {

                    if (debug_visualization)
                        EV << "draw to: " << clusters[i].get(j) << endl;

                    showOverlayNeighborArrow(clusters[i].get(j), false, clusterarrows[i]);

                }

            }

        }
    }


} // updateVisualization

void Nice::pollRP(int layer)
{

    if (debug_queries)
        EV << simTime() << " : " << thisNode.getAddress() << " : pollRP()" << endl;

    NiceMessage* msg = new NiceMessage("NICE_POLL_RP");
    msg->setSrcNode(thisNode);
    msg->setCommand(NICE_POLL_RP);
    msg->setLayer(layer);
    msg->setBitLength(NICEMESSAGE_L(msg));

    cancelEvent(rpPollTimer);
    scheduleAt(simTime() + rpPollTimerInterval, rpPollTimer);

    sendMessageToUDP(RendevouzPoint, msg);

    isPollingRP = true;

    if (debug_queries)
        EV << simTime() << " : " << thisNode.getAddress() << " : pollRP() finished." << endl;

} // pollRP

/*
 * Hoang
 */

double Nice::cost()
{
	double cost, kw_var , xw_var;

	kw_var = getKw();
	xw_var = getXw();

//	cModule* thisOverlayTerminal = check_and_cast<cModule*>(getParentModule()->getParentModule());
//	cCompoundModule* appModule = check_and_cast<cCompoundModule*> (thisOverlayTerminal->getSubmodule("tier1"));
//	NiceTestApp* app = check_and_cast<NiceTestApp*> (appModule->getSubmodule("nicetestapp"));

//	double xd = app->getXd();
//	double kd = lastHopKd;

/*
	if(kd < 1e-10){
		bool isSender = par("isSender");
		if(!isSender){
			std::cout << thisNode.getAddress() <<" kd " << kd << " at " << simTime() << endl;
		}
	}

	if(xd < kd){
		xd = kd + 1e-3;
	}
*/
//	cost = sqrt( (kd/(xd-kd)) * (xw/(kw_var-xw)) );
	cost = xw_var / (kw_var - xw_var);


	if(hoang_debug_cost){
		std::cout << "xw=" << xw_var << " kw=" << kw_var << " || cost=" << cost << endl;
	}

	/*if(cost > SimTime::getMaxTime().dbl()){
		std::cout << "xd=" << xd << " kd=" << kd << " || xw=" << xw << " kw=" << kw_var << " || cost=" << cost << endl;
		cost = 1;
		globalStatistics->recordOutVector("Infinity cost times",1);
	}
*/

//	globalStatistics->recordOutVector("1 Cost",cost);
	return cost;
}

}; //namespace
