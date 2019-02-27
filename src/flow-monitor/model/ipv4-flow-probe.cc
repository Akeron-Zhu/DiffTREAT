/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
//
// Copyright (c) 2009 INESC Porto
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License version 2 as
// published by the Free Software Foundation;
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//
// Author: Gustavo J. A. M. Carneiro  <gjc@inescporto.pt> <gjcarneiro@gmail.com>
//

#include "ns3/ipv4-flow-probe.h"
#include "ns3/ipv4-flow-classifier.h"
#include "ns3/node.h"
#include "ns3/packet.h"
#include "ns3/flow-monitor.h"
#include "ns3/log.h"
#include "ns3/pointer.h"
#include "ns3/config.h"
#include "ns3/flow-id-tag.h"
#include "ns3/queue-disc.h"


#include "ns3/tcp-header.h"
#include "ns3/rto-pri-tag.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("Ipv4FlowProbe");

//////////////////////////////////////
// Ipv4FlowProbeTag class implementation //
//////////////////////////////////////

/**
 * \ingroup flow-monitor
 *
 * \brief Tag used to allow a fast identification of the packet
 *
 * This tag is added by FlowMonitor when a packet is seen for
 * the first time, and it is then used to classify the packet in
 * the following hops.
 */
//用于快速鉴别这个包的标签
class Ipv4FlowProbeTag : public Tag
{
public:
  /**
   * \brief Get the type ID.
   * \return the object TypeId
   */
  static TypeId GetTypeId(void);
  virtual TypeId GetInstanceTypeId(void) const;
  virtual uint32_t GetSerializedSize(void) const;
  virtual void Serialize(TagBuffer buf) const;
  virtual void Deserialize(TagBuffer buf);
  virtual void Print(std::ostream &os) const;
  Ipv4FlowProbeTag();
  /**
   * \brief Consructor
   * \param flowId the flow identifier
   * \param packetId the packet identifier
   * \param packetSize the packet size
   * \param src packet source address
   * \param dst packet destination address
   */
  Ipv4FlowProbeTag(uint32_t flowId, uint32_t packetId, uint32_t packetSize, Ipv4Address src, Ipv4Address dst);
  /**
   * \brief Set the flow identifier
   * \param flowId the flow identifier
   */
  void SetFlowId(uint32_t flowId);
  /**
   * \brief Set the packet identifier
   * \param packetId the packet identifier
   */
  void SetPacketId(uint32_t packetId);
  /**
   * \brief Set the packet size
   * \param packetSize the packet size
   */
  void SetPacketSize(uint32_t packetSize);
  /**
   * \brief Set the flow identifier
   * \returns the flow identifier
   */
  uint32_t GetFlowId(void) const;
  /**
   * \brief Set the packet identifier
   * \returns the packet identifier
   */
  uint32_t GetPacketId(void) const;
  /**
   * \brief Get the packet size
   * \returns the packet size
   */
  uint32_t GetPacketSize(void) const;
  /**
   * \brief Checks if the addresses stored in tag are matching
   * the arguments.
   *
   * This check is important for IP over IP encapsulation.
   *
   * \returns True if the addresses are matching.
   */
  bool IsSrcDstValid(Ipv4Address src, Ipv4Address dst) const;

private:
  uint32_t m_flowId;     //!< flow identifier
  uint32_t m_packetId;   //!< packet identifier
  uint32_t m_packetSize; //!< packet size
  Ipv4Address m_src;     //!< IP source
  Ipv4Address m_dst;     //!< IP destination
};

//得到TypeId
TypeId
Ipv4FlowProbeTag::GetTypeId(void)
{
  static TypeId tid = TypeId("ns3::Ipv4FlowProbeTag")
                          .SetParent<Tag>()
                          .SetGroupName("FlowMonitor")
                          .AddConstructor<Ipv4FlowProbeTag>();
  return tid;
}
TypeId
Ipv4FlowProbeTag::GetInstanceTypeId(void) const
{
  return GetTypeId();
}

//得到这个类的大小
uint32_t
Ipv4FlowProbeTag::GetSerializedSize(void) const
{
  return 4 + 4 + 4 + 8;
}
//将Tag写入buff
void Ipv4FlowProbeTag::Serialize(TagBuffer buf) const
{
  buf.WriteU32(m_flowId);
  buf.WriteU32(m_packetId);
  buf.WriteU32(m_packetSize);

  uint8_t tBuf[4];
  m_src.Serialize(tBuf);
  buf.Write(tBuf, 4);
  m_dst.Serialize(tBuf);
  buf.Write(tBuf, 4);
}
//从Buff得到Tag
void Ipv4FlowProbeTag::Deserialize(TagBuffer buf)
{
  m_flowId = buf.ReadU32();
  m_packetId = buf.ReadU32();
  m_packetSize = buf.ReadU32();

  uint8_t tBuf[4];
  buf.Read(tBuf, 4);
  m_src = Ipv4Address::Deserialize(tBuf);
  buf.Read(tBuf, 4);
  m_dst = Ipv4Address::Deserialize(tBuf);
}
//显示Tag的信息
void Ipv4FlowProbeTag::Print(std::ostream &os) const
{
  os << "FlowId=" << m_flowId;
  os << " PacketId=" << m_packetId;
  os << " PacketSize=" << m_packetSize;
}

//初始化
Ipv4FlowProbeTag::Ipv4FlowProbeTag()
    : Tag()
{
}

Ipv4FlowProbeTag::Ipv4FlowProbeTag(uint32_t flowId, uint32_t packetId, uint32_t packetSize, Ipv4Address src, Ipv4Address dst)
    : Tag(), m_flowId(flowId), m_packetId(packetId), m_packetSize(packetSize), m_src(src), m_dst(dst)
{
}

void Ipv4FlowProbeTag::SetFlowId(uint32_t id)
{
  m_flowId = id;
}
void Ipv4FlowProbeTag::SetPacketId(uint32_t id)
{
  m_packetId = id;
}
void Ipv4FlowProbeTag::SetPacketSize(uint32_t size)
{
  m_packetSize = size;
}
uint32_t
Ipv4FlowProbeTag::GetFlowId(void) const
{
  return m_flowId;
}
uint32_t
Ipv4FlowProbeTag::GetPacketId(void) const
{
  return m_packetId;
}
uint32_t
Ipv4FlowProbeTag::GetPacketSize(void) const
{
  return m_packetSize;
}
bool Ipv4FlowProbeTag::IsSrcDstValid(Ipv4Address src, Ipv4Address dst) const
{
  return ((m_src == src) && (m_dst == dst));
}

////////////////////////////////////////
// Ipv4FlowProbe class implementation //
////////////////////////////////////////
//初始化 会调用其父类中的初始化函数，即将AddProbe到monitor。也即当一个FlowProbe一创建，即与monitor关联
Ipv4FlowProbe::Ipv4FlowProbe(Ptr<FlowMonitor> monitor,
                             Ptr<Ipv4FlowClassifier> classifier,
                             Ptr<Node> node)
    : FlowProbe(monitor),
      m_classifier(classifier) //在这里将classfier赋值
{
  NS_LOG_FUNCTION(this << node->GetId());
  //得到这个节点的Ipv4L3Protocol,然后trace
  m_ipv4 = node->GetObject<Ipv4L3Protocol>();
  /**********************************Add************************************************/

  //当发送后调用SendOutgoingLogger
  if (!m_ipv4->TraceConnectWithoutContext("SendOutgoing",
                                          MakeCallback(&Ipv4FlowProbe::SendOutgoingLogger, Ptr<Ipv4FlowProbe>(this))))
  {
    NS_FATAL_ERROR("trace fail");
  }
  //当转发后调用ForwardLogger
  if (!m_ipv4->TraceConnectWithoutContext("UnicastForward",
                                          MakeCallback(&Ipv4FlowProbe::ForwardLogger, Ptr<Ipv4FlowProbe>(this))))
  {
    NS_FATAL_ERROR("trace fail");
  }
  //当收到包后调用ForwardUpLogger
  if (!m_ipv4->TraceConnectWithoutContext("LocalDeliver",
                                          MakeCallback(&Ipv4FlowProbe::ForwardUpLogger, Ptr<Ipv4FlowProbe>(this))))
  {
    NS_FATAL_ERROR("trace fail");
  }
  //当丢包后调用DropLogger
  if (!m_ipv4->TraceConnectWithoutContext("Drop",
                                          MakeCallback(&Ipv4FlowProbe::DropLogger, Ptr<Ipv4FlowProbe>(this))))
  {
    NS_FATAL_ERROR("trace fail");
  }

  // code copied from point-to-point-helper.cc
  //当队列丢包调用QueueDropLogger
  std::ostringstream oss;
  oss << "/NodeList/" << node->GetId() << "/DeviceList/*/TxQueue/Drop";
  Config::ConnectWithoutContext(oss.str(), MakeCallback(&Ipv4FlowProbe::QueueDropLogger, Ptr<Ipv4FlowProbe>(this)));

  std::ostringstream qd;
  qd << "/NodeList/" << node->GetId() << "/$ns3::TrafficControlLayer/RootQueueDiscList/*/Drop";
  Config::ConnectWithoutContext(qd.str(), MakeCallback(&Ipv4FlowProbe::QueueDiscDropLogger, Ptr<Ipv4FlowProbe>(this)));

  std::ostringstream appRec;
  appRec << "/NodeList/" << node->GetId() << "/ApplicationList/*/$ns3::PacketSink/Rx";
  Config::ConnectWithoutContext(appRec.str(), MakeCallback(&Ipv4FlowProbe::AppPacketReceiveLogger, Ptr<Ipv4FlowProbe>(this)));
}

Ipv4FlowProbe::~Ipv4FlowProbe()
{
}

/* static */
TypeId
Ipv4FlowProbe::GetTypeId(void)
{
  static TypeId tid = TypeId("ns3::Ipv4FlowProbe")
                          .SetParent<FlowProbe>()
                          .SetGroupName("FlowMonitor")
      // No AddConstructor because this class has no default constructor.
      ;

  return tid;
}

void Ipv4FlowProbe::DoDispose()
{
  m_ipv4 = 0;
  m_classifier = 0;
  FlowProbe::DoDispose();
}

//发包后调用,会报告给m_flowMonitor
void Ipv4FlowProbe::SendOutgoingLogger(const Ipv4Header &ipHeader, Ptr<const Packet> ipPayload, uint32_t interface)
{
  int16_t RtoRank=-1;
  FlowId flowId;
  FlowPacketId packetId;
  //检查目的地址是否是单播，如果不是则返回
  if (!m_ipv4->IsUnicast(ipHeader.GetDestination()))
  {
    // we are not prepared to handle broadcast yet
    return;
  }
  //检查是否有Ipv4FlowProbeTag
  Ipv4FlowProbeTag fTag;
  //Finds the first tag matching the parameter Tag type.
  //寻找第一个与参数类型相同的Tag

  bool found = ipPayload->FindFirstMatchingByteTag(fTag);
  if (found)
  {
    return;
  }
  //得到流Id与这个包的id
  if (m_classifier->Classify(ipHeader, ipPayload, &flowId, &packetId))
  {
    //uint32_t size = (ipPayload->GetSize () + ipHeader.GetSerializedSize ());
    uint32_t size = ipPayload->GetSize();
    /***************************************************************************/
    TcpHeader tcpHeader;
    uint32_t bytesRemoved = ipPayload->PeekHeader(tcpHeader);
    RtoPriTag rtoPriTag;
    bool found = ipPayload->PeekPacketTag(rtoPriTag);
    if(found) 
    {
      RtoRank=rtoPriTag.GetRtoRank();
      //std::cout<<RtoRank<<'\n';
    }
    /***************************************************************************/
    NS_LOG_DEBUG("ReportFirstTx (" << this << ", " << flowId << ", " << packetId << ", " << size << "); "
                                   << ipHeader << *ipPayload);
    m_flowMonitor->ReportFirstTx(this, flowId, packetId, size-bytesRemoved, interface,RtoRank);

    // tag the packet with the flow id and packet id, so that the packet can be identified even
    // when Ipv4Header is not accessible at some non-IPv4 protocol layer
    Ipv4FlowProbeTag fTag(flowId, packetId, size-bytesRemoved, ipHeader.GetSource(), ipHeader.GetDestination());
    ipPayload->AddByteTag(fTag);
  }
}

//转发的时候调用，会报告给m_flowMonitor
void Ipv4FlowProbe::ForwardLogger(const Ipv4Header &ipHeader, Ptr<const Packet> ipPayload, uint32_t interface)
{
  Ipv4FlowProbeTag fTag;
  bool found = ipPayload->FindFirstMatchingByteTag(fTag);

  if (found)
  {
    if (!ipHeader.IsLastFragment() || ipHeader.GetFragmentOffset() != 0)
    {
      NS_LOG_WARN("Not counting fragmented packets");
      return;
    }
    if (!fTag.IsSrcDstValid(ipHeader.GetSource(), ipHeader.GetDestination()))
    {
      NS_LOG_LOGIC("Not reporting encapsulated packet");
      return;
    }

    FlowId flowId = fTag.GetFlowId();
    FlowPacketId packetId = fTag.GetPacketId();

    // uint32_t size = (ipPayload->GetSize () + ipHeader.GetSerializedSize ());
    uint32_t size = ipPayload->GetSize();
    NS_LOG_DEBUG("ReportForwarding (" << this << ", " << flowId << ", " << packetId << ", " << size << ");");
    m_flowMonitor->ReportForwarding(this, flowId, packetId, size, interface);
  }
}

//收到包后调用，会报告给m_flowMonitor
void Ipv4FlowProbe::ForwardUpLogger(const Ipv4Header &ipHeader, Ptr<const Packet> ipPayload, uint32_t interface)
{
  Ipv4FlowProbeTag fTag;
  bool found = ipPayload->FindFirstMatchingByteTag(fTag);

  if (found)
  {
    if (!fTag.IsSrcDstValid(ipHeader.GetSource(), ipHeader.GetDestination()))
    {
      NS_LOG_LOGIC("Not reporting encapsulated packet");
      return;
    }

    FlowId flowId = fTag.GetFlowId();
    FlowPacketId packetId = fTag.GetPacketId();
    uint32_t size = fTag.GetPacketSize();

    //uint32_t size = (ipPayload->GetSize () + ipHeader.GetSerializedSize ());
    NS_LOG_DEBUG("ReportLastRx (" << this << ", " << flowId << ", " << packetId << ", " << size << "); "
                                  << ipHeader << *ipPayload);
    m_flowMonitor->ReportLastRx(this, flowId, packetId, size);
  }
}

//丢包后调用，并报告给m_flowMonitor
void Ipv4FlowProbe::DropLogger(const Ipv4Header &ipHeader, Ptr<const Packet> ipPayload,
                               Ipv4L3Protocol::DropReason reason, Ptr<Ipv4> ipv4, uint32_t ifIndex)
{
#if 0
  switch (reason)
    {
    case Ipv4L3Protocol::DROP_NO_ROUTE:
      break;

    case Ipv4L3Protocol::DROP_TTL_EXPIRED:
    case Ipv4L3Protocol::DROP_BAD_CHECKSUM:
      Ipv4Address addri = m_ipv4->GetAddress (ifIndex);
      Ipv4Mask maski = m_ipv4->GetNetworkMask (ifIndex);
      Ipv4Address bcast = addri.GetSubnetDirectedBroadcast (maski);
      if (ipHeader.GetDestination () == bcast) // we don't want broadcast packets
        {
          return;
        }
    }
#endif

  Ipv4FlowProbeTag fTag;
  bool found = ipPayload->FindFirstMatchingByteTag(fTag);

  if (found)
  {
    FlowId flowId = fTag.GetFlowId();
    FlowPacketId packetId = fTag.GetPacketId();

    uint32_t size = (ipPayload->GetSize() + ipHeader.GetSerializedSize());
    NS_LOG_DEBUG("Drop (" << this << ", " << flowId << ", " << packetId << ", " << size << ", " << reason
                          << ", destIp=" << ipHeader.GetDestination() << "); "
                          << "HDR: " << ipHeader << " PKT: " << *ipPayload);

    DropReason myReason;

    switch (reason)
    {
    case Ipv4L3Protocol::DROP_TTL_EXPIRED:
      myReason = DROP_TTL_EXPIRE;
      NS_LOG_DEBUG("DROP_TTL_EXPIRE");
      break;
    case Ipv4L3Protocol::DROP_NO_ROUTE:
      myReason = DROP_NO_ROUTE;
      NS_LOG_DEBUG("DROP_NO_ROUTE");
      break;
    case Ipv4L3Protocol::DROP_BAD_CHECKSUM:
      myReason = DROP_BAD_CHECKSUM;
      NS_LOG_DEBUG("DROP_BAD_CHECKSUM");
      break;
    case Ipv4L3Protocol::DROP_INTERFACE_DOWN:
      myReason = DROP_INTERFACE_DOWN;
      NS_LOG_DEBUG("DROP_INTERFACE_DOWN");
      break;
    case Ipv4L3Protocol::DROP_ROUTE_ERROR:
      myReason = DROP_ROUTE_ERROR;
      NS_LOG_DEBUG("DROP_ROUTE_ERROR");
      break;
    case Ipv4L3Protocol::DROP_FRAGMENT_TIMEOUT:
      myReason = DROP_FRAGMENT_TIMEOUT;
      NS_LOG_DEBUG("DROP_FRAGMENT_TIMEOUT");
      break;

    default:
      myReason = DROP_INVALID_REASON;
      NS_FATAL_ERROR("Unexpected drop reason code " << reason);
    }

    m_flowMonitor->ReportDrop(this, flowId, packetId, size, myReason);
  }
}
//队列丢包后调用，并报告给m_flowMonitor
void Ipv4FlowProbe::QueueDropLogger(Ptr<const Packet> ipPayload)
{
  Ipv4FlowProbeTag fTag;
  bool tagFound = ipPayload->FindFirstMatchingByteTag(fTag);

  if (!tagFound)
  {
    return;
  }

  FlowId flowId = fTag.GetFlowId();
  FlowPacketId packetId = fTag.GetPacketId();
  uint32_t size = fTag.GetPacketSize();

  NS_LOG_DEBUG("Drop (" << this << ", " << flowId << ", " << packetId << ", " << size << ", " << DROP_QUEUE
                        << "); ");

  m_flowMonitor->ReportDrop(this, flowId, packetId, size, DROP_QUEUE);
}

void Ipv4FlowProbe::QueueDiscDropLogger(Ptr<const Packet> ipPayload)
{
  Ipv4FlowProbeTag fTag;
  bool tagFound = ipPayload->FindFirstMatchingByteTag(fTag);

  if (!tagFound)
  {
    return;
  }

  FlowId flowId = fTag.GetFlowId();
  FlowPacketId packetId = fTag.GetPacketId();
  uint32_t size = fTag.GetPacketSize();

  NS_LOG_DEBUG("Drop (" << this << ", " << flowId << ", " << packetId << ", " << size << ", " << DROP_QUEUE_DISC
                        << "); ");

  m_flowMonitor->ReportDrop(this, flowId, packetId, size, DROP_QUEUE_DISC);
}

void Ipv4FlowProbe::AppPacketReceiveLogger(Ptr<const Packet> ipPayload, const Address &address, const Address &local)
{

  /*std::cout << InetSocketAddress::ConvertFrom(address).GetIpv4 ()
                       << " port " << InetSocketAddress::ConvertFrom (address).GetPort ()<<'\n'
                       << InetSocketAddress::ConvertFrom(local).GetIpv4 ()
                       << " port " << InetSocketAddress::ConvertFrom (local).GetPort ()<<'\n';//*/

  Ipv4FlowClassifier::FiveTuple fiveTuple;
  fiveTuple.sourceAddress = InetSocketAddress::ConvertFrom(address).GetIpv4 ();
  fiveTuple.destinationAddress = InetSocketAddress::ConvertFrom(local).GetIpv4 (); 
  fiveTuple.sourcePort = InetSocketAddress::ConvertFrom (address).GetPort ();
  fiveTuple.destinationPort = InetSocketAddress::ConvertFrom (local).GetPort ();
  fiveTuple.protocol = uint8_t(6);
  //std::cout<<fiveTuple.sourceAddress<<' '<<fiveTuple.destinationAddress<<' '<<fiveTuple.sourcePort<<' '<<fiveTuple.destinationPort<<'\n';
  FlowId flowId = m_classifier->FindFlowId(fiveTuple);
  if(flowId == -1) 
  {
    std::cout<<"Ipv4FlowProbe: Can not find id!\n";
    return;
  }
  //else std::cout<<flowId<<'\n';
  

  
  FlowPacketId packetId = 0;
  uint32_t size = ipPayload->GetSize(); //fTag.GetPacketSize ();//
  //uint32_t size = (ipPayload->GetSize () + 20);
  NS_LOG_DEBUG("Drop (" << this << ", " << flowId << ", " << packetId << ", " << size << ", " << address
                        << "); ");
  m_flowMonitor->ReportAppPacketSink(this, flowId, packetId, size, address);
}



} // namespace ns3
