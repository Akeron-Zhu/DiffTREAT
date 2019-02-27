/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#include "ipv4-tlb-probing.h"

#include "ns3/ipv4-tlb-probing-tag.h"

#include "ns3/log.h"
#include "ns3/uinteger.h"
#include "ns3/boolean.h"
#include "ns3/packet.h"
#include "ns3/socket.h"
#include "ns3/ipv4-raw-socket-factory.h"
#include "ns3/ipv4-header.h"
#include "ns3/node.h"
#include "ns3/simulator.h"
#include "ns3/ipv4-tlb.h"
#include "ns3/ipv4-xpath-tag.h"

#include <sys/socket.h>
#include <set>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("Ipv4TLBProbing");

NS_OBJECT_ENSURE_REGISTERED (Ipv4TLBProbing);

//返回TypeId
TypeId
Ipv4TLBProbing::GetTypeId (void)
{
    static TypeId tid = TypeId ("ns3::Ipv4TLBProbing")
        .SetParent<Object> ()
        .SetGroupName ("TLB")
        .AddConstructor<Ipv4TLBProbing> ()
        .AddAttribute ("ProbeInterval", "Probing Interval",
                      TimeValue (MicroSeconds (100)),
                      MakeTimeAccessor (&Ipv4TLBProbing::m_probeInterval),
                      MakeTimeChecker ())
    ;

    return tid;
}

TypeId
Ipv4TLBProbing::GetInstanceTypeId () const
{
    return Ipv4TLBProbing::GetTypeId ();
}

//初始化
Ipv4TLBProbing::Ipv4TLBProbing ()
    : m_sourceAddress (Ipv4Address ("127.0.0.1")),
      m_probeAddress (Ipv4Address ("127.0.0.1")),
      m_probeTimeout (Seconds (0.1)),
      m_probeInterval (MicroSeconds (100)),
      m_id (0),
      m_hasBestPath (false),
      m_bestPath (0),
      m_bestPathRtt (Seconds (666)),
      //m_bestPathECN (false),
      //m_bestPathSize (0),
      m_node ()
{
    NS_LOG_FUNCTION (this);
}

Ipv4TLBProbing::Ipv4TLBProbing (const Ipv4TLBProbing &other)
    : m_sourceAddress (other.m_sourceAddress),
      m_probeAddress (other.m_probeAddress),
      m_probeTimeout (other.m_probeTimeout),
      m_probeInterval (other.m_probeInterval),
      m_id (0),
      m_hasBestPath (false),
      m_bestPath (0),
      m_bestPathRtt (Seconds (666)),
      //m_bestPathECN (false),
      //m_bestPathSize (0),
      m_node ()
{
    NS_LOG_FUNCTION (this);
}

Ipv4TLBProbing::~Ipv4TLBProbing ()
{
    NS_LOG_FUNCTION (this);
}

//将m_probingTimeoutMap中的事件都取消
void
Ipv4TLBProbing::DoDispose ()
{
    std::map <uint32_t, EventId>::iterator itr =
        m_probingTimeoutMap.begin ();
    for ( ; itr != m_probingTimeoutMap.end (); ++itr)
    {
        (itr->second).Cancel ();
        m_probingTimeoutMap.erase (itr);
    }
}

//设置源地址
void
Ipv4TLBProbing::SetSourceAddress (Ipv4Address address)
{
    m_sourceAddress = address;
}

//设置探测地址
void
Ipv4TLBProbing::SetProbeAddress (Ipv4Address address)
{
    m_probeAddress = address;
}

//设置节点
void
Ipv4TLBProbing::SetNode (Ptr<Node> node)
{
    m_node = node;
}

//添加广播地址
void
Ipv4TLBProbing::AddBroadCastAddress (Ipv4Address addr)
{
    m_broadcastAddresses.push_back (addr);
}

//TLBProbing初始化，即初始化Socket
void
Ipv4TLBProbing::Init ()
{
    m_socket = m_node->GetObject<Ipv4RawSocketFactory> ()->CreateSocket ();
    m_socket->SetRecvCallback (MakeCallback (&Ipv4TLBProbing::ReceivePacket, this));
    m_socket->Bind (InetSocketAddress (Ipv4Address ("0.0.0.0"), 0));
    m_socket->SetAttribute ("IpHeaderInclude", BooleanValue (true));
}


void
Ipv4TLBProbing::SendProbe (uint32_t path)
{
    Address to = InetSocketAddress (m_probeAddress, 0);
    //创建一个包
    Ptr<Packet> packet = Create<Packet> (0);
    Ipv4Header newHeader;
    newHeader.SetSource (m_sourceAddress);
    newHeader.SetDestination (m_probeAddress);
    newHeader.SetProtocol (0);
    newHeader.SetPayloadSize (packet->GetSize ());
    newHeader.SetEcn (Ipv4Header::ECN_ECT1);
    newHeader.SetTtl (255);
    packet->AddHeader (newHeader);

    // XPath tag
    // 添加XPath标签
    Ipv4XPathTag ipv4XPathTag;
    ipv4XPathTag.SetPathId (path);
    packet->AddPacketTag (ipv4XPathTag);

    // Probing tag
    // 添加探测Tag
    Ipv4TLBProbingTag probingTag;
    probingTag.SetId (m_id);
    probingTag.SetPath (path);
    probingTag.SetProbeAddress (m_probeAddress);
    probingTag.SetIsReply (0);
    probingTag.SetTime (Simulator::Now ());
    probingTag.SetIsCE (0);
    probingTag.SetIsBroadcast (0);
    packet->AddPacketTag (probingTag);
    //将这个包发送到要探测的地址
    m_socket->SendTo (packet, 0, to);
    m_id ++; //增加ID

    // Add timeout
    // m_probeTimeout后检测是否有超时了
    m_probingTimeoutMap[m_id] =
        Simulator::Schedule (m_probeTimeout, &Ipv4TLBProbing::ProbeEventTimeout, this, m_id, path);
    //调用Ipv4TLB中的ProbeSend来更新路径信息
    Ptr<Ipv4TLB> ipv4TLB = m_node->GetObject<Ipv4TLB> ();
    ipv4TLB->ProbeSend (m_probeAddress, path);
}

//包发送后检测是否有超时
void
Ipv4TLBProbing::ProbeEventTimeout (uint32_t id, uint32_t path)
{
    //从事件中去除，并调用Ipv4TLB中的函数检测是否timeout
    m_probingTimeoutMap.erase (id);
    Ptr<Ipv4TLB> ipv4TLB = m_node->GetObject<Ipv4TLB> ();
    ipv4TLB->ProbeTimeout (path, m_probeAddress);
}

//收到包的操作
void
Ipv4TLBProbing::ReceivePacket (Ptr<Socket> socket)
{
    //接收数据
    Ptr<Packet> packet = m_socket->Recv (std::numeric_limits<uint32_t>::max (), MSG_PEEK);
    
    //检测标签
    Ipv4TLBProbingTag probingTag;
    bool found = packet->RemovePacketTag (probingTag);

    if (!found)
    {
        return;
    }
    //得到ipv4Header头部
    Ipv4Header ipv4Header;
    found = packet->RemoveHeader (ipv4Header);

    //NS_LOG_LOGIC (this << " Ipv4 Header: " << ipv4Header);
    //如果不是回复就回复这个包
    if (probingTag.GetIsReply () == 0)
    {
        // Reply to this packet
        //创建一个新包
        Ptr<Packet> newPacket = Create<Packet> (0);
        Ipv4Header newHeader;
        newHeader.SetSource (m_sourceAddress);
        newHeader.SetDestination (ipv4Header.GetSource ());
        newHeader.SetProtocol (0);
        newHeader.SetPayloadSize (packet->GetSize ());
        newHeader.SetTtl (255);
        newPacket->AddHeader (newHeader);
        //创建回复用的ProbingTag,并将回复设置为1,设置一半的路径时间
        Ipv4TLBProbingTag replyProbingTag;
        replyProbingTag.SetId (probingTag.GetId ());
        replyProbingTag.SetPath (probingTag.GetPath ());
        replyProbingTag.SetProbeAddress (probingTag.GetProbeAddres ());
        replyProbingTag.SetIsReply (1);
        replyProbingTag.SetIsBroadcast (0);
        replyProbingTag.SetTime (Simulator::Now() - probingTag.GetTime ());
        //如果有标记ECN，则置设IsCE为1
        if (ipv4Header.GetEcn () == Ipv4Header::ECN_CE)
        {
            replyProbingTag.SetIsCE (1);
        }
        else
        {
            replyProbingTag.SetIsCE (0);
        }
        //将其添加到包中
        newPacket->AddPacketTag (replyProbingTag);
        //得到地址，并且发送
        Address to = InetSocketAddress (ipv4Header.GetSource (), 0);

        m_socket->SendTo (newPacket, 0, to);
    }
    else //如果是回复包
    {
        //先在m_probingTimeoutMap找，因为在timeout时间后，就会清楚Id,如果找不到说明超时
        std::map<uint32_t, EventId>::iterator itr =
                m_probingTimeoutMap.find (probingTag.GetId ());

        if (itr == m_probingTimeoutMap.end () && !probingTag.GetIsBroadcast ())
        {
            // The reply has incurred timeout
            return;
        }

        //如果不是广播，就取消它的超时事件
        if (!probingTag.GetIsBroadcast ())
        {
            // Cancel the probing timeout timer
            (itr->second).Cancel ();
            m_probingTimeoutMap.erase (itr);
        }

        //得到路径，得到单程RTT，得到是否CE和总大小
        uint32_t path = probingTag.GetPath ();
        Time oneWayRtt = probingTag.GetTime ();
        bool isCE = probingTag.GetIsCE () == 1 ? true : false;
        uint32_t size = packet->GetSize () + ipv4Header.GetSerializedSize ();

        if (oneWayRtt < m_bestPathRtt)
        {
            m_hasBestPath = true;
            m_bestPath = path;
            //m_bestPathRtt = oneWayRtt;
            //m_bestPathECN = isCE;
            //m_bestPathSize = size;
        }
        
        //调用节点收到探测包后的事件
        Ptr<Ipv4TLB> ipv4TLB = m_node->GetObject<Ipv4TLB> ();
        ipv4TLB->ProbeRecv (path, probingTag.GetProbeAddres (), size, isCE, oneWayRtt);
        //如果不是广播，就将得到的信息发送给同一叶节点下的其它服务器
        if (!probingTag.GetIsBroadcast ())
        {
            // Forward path information to servers under the same rack
            std::vector<Ipv4Address>::iterator broadcastItr = m_broadcastAddresses.begin ();
            for ( ; broadcastItr != m_broadcastAddresses.end (); broadcastItr ++)
            {
                Ipv4TLBProbing::ForwardPathInfoTo(*broadcastItr, path, oneWayRtt, isCE);
            }
        }
    }
}

//调用开始探测事件
void
Ipv4TLBProbing::StartProbe ()
{
    m_probeEvent = Simulator::ScheduleNow (&Ipv4TLBProbing::DoProbe, this);
}

//停止探测
void
Ipv4TLBProbing::StopProbe (Time stopTime)
{
    Simulator::Schedule (stopTime, &Ipv4TLBProbing::DoStop, this);
}

//
void
Ipv4TLBProbing::DoProbe ()
{
    //开始探测探测的次数
    uint32_t probingCount = 3;

    std::set<uint32_t> pathSet;

    if (m_hasBestPath && m_bestPath != 0)
    {
        /*
        std::vector<Ipv4Address>::iterator itr = m_broadcastAddresses.begin ();
        for ( ; itr != m_broadcastAddresses.end (); itr ++)
        {
            Ipv4TLBProbing::BroadcastBestPathTo (*itr);
        }
        */

        Ipv4TLBProbing::SendProbe (m_bestPath);
        probingCount --;
        pathSet.insert (m_bestPath);
    }
    Ptr<Ipv4TLB> ipv4TLB = m_node->GetObject<Ipv4TLB> ();
    std::vector<uint32_t> availPaths = ipv4TLB->GetAvailPath (m_probeAddress);
    if (!availPaths.empty ())
    {
        for (uint32_t i = 0; i < 10; i++) // Try 8 times
        {
            //随机选择一条路径进行探测
            uint32_t path = availPaths[rand() % availPaths.size ()];
            if (pathSet.find (path) != pathSet.end ())
            {
                continue;
            }
            probingCount --;
            if (probingCount == 0)
            {
                break;
            }
            pathSet.insert (path);
            Ipv4TLBProbing::SendProbe (path);
        }
    }
    m_hasBestPath = false;
    m_bestPathRtt = Seconds (666);
    //固定周期进行探测
    m_probeEvent = Simulator::Schedule (m_probeInterval, &Ipv4TLBProbing::DoProbe, this);
}

void
Ipv4TLBProbing::DoStop ()
{
    //取消固定周期探测
    m_probeEvent.Cancel ();
}

/*
void
Ipv4TLBProbing::BroadcastBestPathTo (Ipv4Address addr)
{
    Address to = InetSocketAddress (addr, 0);

    Ptr<Packet> packet = Create<Packet> (0);
    Ipv4Header newHeader;
    newHeader.SetSource (m_sourceAddress);
    newHeader.SetDestination (addr);
    newHeader.SetProtocol (0);
    newHeader.SetPayloadSize (packet->GetSize ());
    newHeader.SetEcn (Ipv4Header::ECN_ECT1);
    newHeader.SetTtl (255);
    packet->AddHeader (newHeader);

    // Probing tag
    Ipv4TLBProbingTag probingTag;
    probingTag.SetId (0);
    probingTag.SetPath (m_bestPath);
    probingTag.SetProbeAddress (m_probeAddress);
    probingTag.SetIsReply (1);
    probingTag.SetTime (m_bestPathRtt);
    probingTag.SetIsCE (m_bestPathECN);
    probingTag.SetIsBroadcast (1);
    packet->AddPacketTag (probingTag);

    m_socket->SendTo (packet, 0, to);
}
*/

//转发信息到addr
void
Ipv4TLBProbing::ForwardPathInfoTo (Ipv4Address addr, uint32_t path, Time oneWayRtt, bool isCE)
{
    Address to = InetSocketAddress (addr, 0);

    Ptr<Packet> packet = Create<Packet> (0);
    Ipv4Header newHeader;
    newHeader.SetSource (m_sourceAddress);
    newHeader.SetDestination (addr);
    newHeader.SetProtocol (0);
    newHeader.SetPayloadSize (packet->GetSize ());
    newHeader.SetEcn (Ipv4Header::ECN_ECT1);
    newHeader.SetTtl (255);
    packet->AddHeader (newHeader);

    // Probing tag
    Ipv4TLBProbingTag probingTag;
    probingTag.SetId (0);
    probingTag.SetPath (path);
    probingTag.SetProbeAddress (m_probeAddress);
    probingTag.SetIsReply (1);
    probingTag.SetTime (oneWayRtt);
    probingTag.SetIsCE (isCE);
    probingTag.SetIsBroadcast (1);
    packet->AddPacketTag (probingTag);

    m_socket->SendTo (packet, 0, to);

}

}

