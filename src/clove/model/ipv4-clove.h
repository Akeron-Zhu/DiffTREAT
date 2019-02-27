/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#ifndef IPV4_CLOVE_H
#define IPV4_CLOVE_H

#include "ns3/object.h"
#include "ns3/nstime.h"
#include "ns3/ipv4-address.h"

#include <vector>
#include <map>
#include <utility>

#define CLOVE_RUNMODE_EDGE_FLOWLET 0
#define CLOVE_RUNMODE_ECN 1
#define CLOVE_RUNMODE_INT 2

namespace ns3 {

//Clove的flowlet路由表
struct CloveFlowlet {
    Time lastSeen;
    uint32_t path;
};

class Ipv4Clove : public Object {

public:
    Ipv4Clove ();
    Ipv4Clove (const Ipv4Clove&);

    static TypeId GetTypeId (void);

    void AddAddressWithTor (Ipv4Address address, uint32_t torId);
    void AddAvailPath (uint32_t destTor, uint32_t path);

    uint32_t GetPath (uint32_t flowId, Ipv4Address saddr, Ipv4Address daddr);

    void FlowRecv (uint32_t path, Ipv4Address daddr, bool withECN);

    bool FindTorId (Ipv4Address daddr, uint32_t &torId);

private:
    uint32_t CalPath (uint32_t destTor);

    Time m_flowletTimeout;
    uint32_t m_runMode;

    //可以到达某个地址的路径
    std::map<uint32_t, std::vector<uint32_t> > m_availablePath; 
    //从IP到Tor的映射
    std::map<Ipv4Address, uint32_t> m_ipTorMap;
    std::map<uint32_t, CloveFlowlet> m_flowletMap;

    // Clove ECN
    //过了半个RTT还没再次见到ECN则认为这个路径不再拥塞
    Time m_halfRTT; 
    //如果设置为True则只给不拥塞的路径分配权重，否则给所有路径分配
    bool m_disToUncongestedPath;
    //路径权重查询
    std::map<std::pair<uint32_t, uint32_t>, double> m_pathWeight; 
    //看到过ECN的路径及其时间
    std::map<std::pair<uint32_t, uint32_t>, Time> m_pathECNSeen;
};

}

#endif /* IPV4_CLOVE_H */

