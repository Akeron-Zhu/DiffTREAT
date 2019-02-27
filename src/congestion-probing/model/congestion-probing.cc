/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#include "congestion-probing.h"

#include "congestion-probing-tag.h"

#include "ns3/log.h"
#include "ns3/uinteger.h"
#include "ns3/boolean.h"
#include "ns3/packet.h"
#include "ns3/socket.h"
#include "ns3/ipv4-raw-socket-factory.h"
#include "ns3/ipv4-header.h"
#include "ns3/node.h"
#include "ns3/simulator.h"
#include "ns3/ipv4-xpath-tag.h"

#include <sys/socket.h>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("CongestionProbing");

NS_OBJECT_ENSURE_REGISTERED (CongestionProbing);

//返回TypeId
TypeId
CongestionProbing::GetTypeId (void)
{
    static TypeId tid = TypeId ("ns3::CongestionProbing")
        .SetParent<Object> ()
        .SetGroupName ("Internet")
        .AddConstructor<CongestionProbing> ()
       .AddAttribute ("ProbingInterval",
                "Time interval between probings",
                TimeValue (MicroSeconds (100)),
                MakeTimeAccessor (&CongestionProbing::m_probeInterval),
                MakeTimeChecker ())
        .AddAttribute ("ProbeTimeout",
                "The timeout for probing event",
                TimeValue (Seconds (0.1)),
                MakeTimeAccessor (&CongestionProbing::m_probeTimeout),
                MakeTimeChecker ())
        .AddTraceSource ("Probing",
                "The probing event callback",
                MakeTraceSourceAccessor (&CongestionProbing::m_probingCallback),
                "ns3::CongestionProbing::ProbingCallback")
        .AddTraceSource ("ProbingTimeout",
                "The probing event timeout",
                MakeTraceSourceAccessor (&CongestionProbing::m_probingTimeoutCallback),
                "ns3::CongestionProbing::ProbingTimeoutCallback");

    return tid;
}

//产生一个介于min到max间的值
template<typename T>
T rand_range (T min, T max)
{
    return min + ((double)max - min) * rand () / RAND_MAX;
}

TypeId
CongestionProbing::GetInstanceTypeId () const
{
    return CongestionProbing::GetTypeId ();
}

CongestionProbing::CongestionProbing ()
    : m_probeEvent (),
      m_id (0),
      m_sourceAddress (Ipv4Address ("127.0.0.1")),
      m_probeAddress (Ipv4Address ("127.0.0.1")),
      m_pathId (0),
      m_node (),
      m_probeInterval (MicroSeconds (100)),
      m_probeTimeout (Seconds (0.1))
{
    NS_LOG_FUNCTION (this);
}

CongestionProbing::CongestionProbing (const CongestionProbing &other)
    : m_probeEvent (),
      m_id (0),
      m_sourceAddress (other.m_sourceAddress),
      m_probeAddress (other.m_probeAddress),
      m_pathId (other.m_pathId),
      m_node (),
      m_probeInterval (other.m_probeInterval),
      m_probeTimeout (other.m_probeTimeout),
      m_probingCallback (other.m_probingCallback),
      m_probingTimeoutCallback (other.m_probingTimeoutCallback)
{
    NS_LOG_FUNCTION (this);
}

CongestionProbing::~CongestionProbing ()
{
    NS_LOG_FUNCTION (this);
}

//取消探测事件
void
CongestionProbing::DoDispose ()
{
    m_probeEvent.Cancel ();
}

//设置源地址
void
CongestionProbing::SetSourceAddress (Ipv4Address address)
{
    m_sourceAddress = address;
}

//设置要探测的地址
void
CongestionProbing::SetProbeAddress (Ipv4Address address)
{
    m_probeAddress = address;
}

//设置路径ID
void
CongestionProbing::SetPathId (uint32_t pathId)
{
    m_pathId = pathId;
}

//设置节点
void
CongestionProbing::SetNode (Ptr<Node> node)
{
    m_node = node;
}

//开始探测，调用DoStartProbe
void
CongestionProbing::StartProbe ()
{
    Simulator::ScheduleNow (&CongestionProbing::DoStartProbe, this);
}

void
CongestionProbing::DoStartProbe ()
{
    NS_LOG_LOGIC (this << "Start probing with interval: " << m_probeInterval);

    m_socket = m_node->GetObject<Ipv4RawSocketFactory> ()->CreateSocket ();
    m_socket->SetRecvCallback (MakeCallback (&CongestionProbing::ReceivePacket, this));
    m_socket->Bind (InetSocketAddress (Ipv4Address ("0.0.0.0"), 0));
    m_socket->SetAttribute ("IpHeaderInclude", BooleanValue (true));

    m_probeEvent = Simulator::ScheduleNow (&CongestionProbing::ProbeEvent, this);
}

void
CongestionProbing::DoStopProbe ()
{
    m_probeEvent.Cancel ();
}

//取消探测
void
CongestionProbing::StopProbe (Time time)
{
    Simulator::Schedule (time, &CongestionProbing::DoStopProbe, this);
}

//探测
void
CongestionProbing::ProbeEvent ()
{
    Address to = InetSocketAddress (m_probeAddress, 0);

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
    //添加XPath标签
    Ipv4XPathTag ipv4XPathTag;
    ipv4XPathTag.SetPathId (m_pathId);
    packet->AddPacketTag (ipv4XPathTag);

    // Probing tag
    // 添加CongestionProbing标签
    CongestionProbingTag probingTag;
    probingTag.SetId (m_id);
    probingTag.SetIsReply (0);
    probingTag.SetTime (Simulator::Now ());
    probingTag.SetIsCE (0);
    packet->AddPacketTag (probingTag);

    m_socket->SendTo (packet, 0, to);
    m_id ++;

    // Add timeout
    // 加入探测ID，用于检测是否超时
    m_probingTimeoutMap[m_id] = Simulator::Schedule (m_probeTimeout, &CongestionProbing::ProbeEventTimeout, this, m_id);
    
    //加入噪声后的探测时间，根据噪声时间调用探测事件
    double noise = rand_range (0.0, m_probeTimeout.GetSeconds ());
    Time noiseTime = Seconds (noise);

    m_probeEvent = Simulator::Schedule (m_probeInterval + noiseTime, &CongestionProbing::ProbeEvent, this);
}

void
CongestionProbing::ProbeEventTimeout (uint32_t id)
{
    //到时间检测是否超时
    m_probingTimeoutMap.erase (id);
    m_probingTimeoutCallback (m_pathId);
}

//收到包后的操作
void
CongestionProbing::ReceivePacket (Ptr<Socket> socket)
{
    //得到包
    Ptr<Packet> packet = m_socket->Recv (std::numeric_limits<uint32_t>::max (), MSG_PEEK);
    //取得CongestionProbingTag
    CongestionProbingTag probingTag;
    bool found = packet->RemovePacketTag (probingTag);

    if (!found)
    {
        return;
    }
    //得到Ipv4Header
    Ipv4Header ipv4Header;
    found = packet->RemoveHeader (ipv4Header);

    NS_LOG_LOGIC (this << "Ipv4 Header: " << ipv4Header);
    
    //如果不是回复包，则对个包进行回复
    if (probingTag.GetIsReply () == 0)
    {
        // Reply to this packet
        Ptr<Packet> newPacket = Create<Packet> (0);
        Ipv4Header newHeader;
        newHeader.SetSource (m_sourceAddress);
        newHeader.SetDestination (ipv4Header.GetSource ());
        newHeader.SetProtocol (0);
        newHeader.SetPayloadSize (packet->GetSize ());
        newHeader.SetTtl (255);
        newPacket->AddHeader (newHeader);

        CongestionProbingTag replyProbingTag;
        replyProbingTag.SetId (probingTag.GetId ());
        replyProbingTag.SetIsReply (1);
        replyProbingTag.SetTime (Simulator::Now() - probingTag.GetTime ());
        if (ipv4Header.GetEcn () == Ipv4Header::ECN_CE)
        {
            replyProbingTag.SetIsCE (1);
        }
        else
        {
            replyProbingTag.SetIsCE (0);
        }
        newPacket->AddPacketTag (replyProbingTag);

        Address to = InetSocketAddress (ipv4Header.GetSource (), 0);

        m_socket->SendTo (newPacket, 0, to);
    }
    else //如果是回复包
    {
        //检查是否超时
        std::map<uint32_t, EventId>::iterator itr =
                m_probingTimeoutMap.find (probingTag.GetId ());

        if (itr == m_probingTimeoutMap.end ())
        {
            // The reply has incurred timeout
            return;
        }
        //如果未超时，取消超时事件
        // Cancel the timeout timer
        (itr->second).Cancel ();
        m_probingTimeoutMap.erase (itr);

        // Raise an event
        //构建事件
        Time oneWayRtt = probingTag.GetTime ();
        bool isCE = probingTag.GetIsCE () == 1 ? true : false;
        m_probingCallback (m_pathId, packet, ipv4Header, oneWayRtt, isCE);
    }
}

}

