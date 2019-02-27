/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#ifndef IPV4_LINK_PROBE_H
#define IPV4_LINK_PROBE_H

#include "link-probe.h"

#include "ns3/node.h"
#include "ns3/packet.h"
#include "ns3/ipv4-l3-protocol.h"
#include "ns3/event-id.h"
#include "ns3/data-rate.h"

#include "ipv4-queue-probe.h"

namespace ns3 {
//继承于LinkProbe,需要实现两个虚函数，Start与Stop
//因为是继承，所以自然含有一个std::map<uint32_t, std::vector<struct LinkStats> > m_stats;
//自然含有一个m_probeName
//出现在CONGA-simulation-large.cc中
class Ipv4LinkProbe : public LinkProbe
{
public:

  static TypeId GetTypeId (void);
  //
  Ipv4LinkProbe (Ptr<Node> node, Ptr<LinkMonitor> linkMonitor);

  void SetDataRateAll (DataRate dataRate);

  void SetCheckTime (Time checkTime);

  void TxLogger (Ptr<const Packet> packet, Ptr<Ipv4> ipv4, uint32_t interface);

  void DequeueLogger (Ptr<const Packet> packet, uint32_t interface);

  void PacketsInQueueLogger (uint32_t NPackets, uint32_t interface);

  void BytesInQueueLogger (uint32_t NBytes, uint32_t interface);

  void PacketsInQueueDiscLogger (uint32_t NPackets, uint32_t interface);

  void BytesInQueueDiscLogger (uint32_t NBytes, uint32_t interface);

  void CheckCurrentStatus ();

  void Start ();
  void Stop ();

private:

  double GetLinkUtility (uint32_t interface, uint64_t bytes, Time time);
  //检设置单位时间，即在多少时间段检查一次，比如100us内发送了多少字节除以最大能发送字节就是链路利用率
  //也即多长时间检测一次
  Time m_checkTime;
  //事件的ID。
  EventId m_checkEvent;
  //一个MAP，用于存储形式为<interface,对应属性的值>
  //m_queueProbe的作用是监视队列触动标准的调用时，然后通过它再调用其它函数传入不同的变量
  std::map<uint32_t, Ptr<Ipv4QueueProbe> > m_queueProbe;

  std::map<uint32_t, uint64_t> m_accumulatedTxBytes;
  std::map<uint32_t, uint64_t> m_accumulatedDequeueBytes;

  std::map<uint32_t, uint32_t> m_NPacketsInQueue;
  std::map<uint32_t, uint32_t> m_NBytesInQueue;

  std::map<uint32_t, uint32_t> m_NPacketsInQueueDisc;
  std::map<uint32_t, uint32_t> m_NBytesInQueueDisc;
  //一个映射用于设置<interface，Datarate>接口的速率
  std::map<uint32_t, DataRate> m_dataRate;

  Ptr<Ipv4L3Protocol> m_ipv4;
};

}

#endif /* IPV4_LINK_PROBE_H */

