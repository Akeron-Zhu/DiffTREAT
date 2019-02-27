/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#ifndef IPV4_TLB_PROBING_H
#define IPV4_TLB_PROBING_H

#include "ns3/object.h"
#include "ns3/callback.h"
#include "ns3/traced-callback.h"
#include "ns3/ipv4-address.h"
#include "ns3/nstime.h"
#include "ns3/event-id.h"

#include <vector>
#include <map>

namespace ns3 {

class Packet;
class Socket;
class Node;
class Ipv4Header;

class Ipv4TLBProbing : public Object
{
public:

    static TypeId GetTypeId (void);
    virtual TypeId GetInstanceTypeId (void) const;

    Ipv4TLBProbing ();
    Ipv4TLBProbing (const Ipv4TLBProbing&);
    ~Ipv4TLBProbing ();
    virtual void DoDispose (void);

    void SetSourceAddress (Ipv4Address address);
    void SetProbeAddress (Ipv4Address address);

    void SetNode (Ptr<Node> node);

    void AddBroadCastAddress (Ipv4Address addr);

    void Init (void);

    void SendProbe (uint32_t path);

    void ReceivePacket (Ptr<Socket> socket);

    void ProbeEventTimeout (uint32_t id, uint32_t path);

    void StartProbe ();

    void StopProbe (Time stopTime);

private:

    void DoProbe ();
    void DoStop ();

    //void BroadcastBestPathTo (Ipv4Address addr);

    void ForwardPathInfoTo (Ipv4Address addr, uint32_t path, Time oneWayRtt, bool isCE);

    // Parameters
    //源地址与探测地址
    Ipv4Address m_sourceAddress;
    Ipv4Address m_probeAddress; // The flow destination

    //探测timeout的时间和探测间隔
    Time m_probeTimeout;
    Time m_probeInterval;
    //探测的id
    uint32_t m_id;

    //每次探测后将其id放入，等到timeout时清除id，用以判别是否超时
    std::map <uint32_t, EventId> m_probingTimeoutMap;

    /* Best path related */
    //是否有最好的路径和最好路径值
    bool m_hasBestPath;
    uint32_t m_bestPath;

    Time m_bestPathRtt;
    //bool m_bestPathECN;
    //uint32_t m_bestPathSize;
    /* ----------------- */
    //探测事件
    EventId m_probeEvent;
    //Socket
    Ptr<Socket> m_socket;
    //要去广播的地址
    std::vector<Ipv4Address> m_broadcastAddresses;
    //节点
    Ptr<Node> m_node;

};

}

#endif /* CONGESTION_PROBING_H */

