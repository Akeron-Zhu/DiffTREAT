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

#include "flow-monitor.h"
#include "ns3/simulator.h"
#include "ns3/log.h"
#include "ns3/double.h"
#include <fstream>
#include <sstream>

#define INDENT(level)                            \
  for (int __xpto = 0; __xpto < level; __xpto++) \
    os << ' ';

#define PERIODIC_CHECK_INTERVAL (Seconds(1))

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("FlowMonitor");

NS_OBJECT_ENSURE_REGISTERED(FlowMonitor);

TypeId
FlowMonitor::GetTypeId(void)
{
  static TypeId tid = TypeId("ns3::FlowMonitor")
                          .SetParent<Object>()
                          .SetGroupName("FlowMonitor")
                          .AddConstructor<FlowMonitor>()
                          .AddAttribute("MaxPerHopDelay", ("The maximum per-hop delay that should be considered.  "
                                                           "Packets still not received after this delay are to be considered lost."),
                                        TimeValue(Seconds(10.0)),
                                        MakeTimeAccessor(&FlowMonitor::m_maxPerHopDelay),
                                        MakeTimeChecker())
                          .AddAttribute("StartTime", ("The time when the monitoring starts."),
                                        TimeValue(Seconds(0.0)),
                                        MakeTimeAccessor(&FlowMonitor::Start),
                                        MakeTimeChecker())
                          .AddAttribute("DelayBinWidth", ("The width used in the delay histogram."),
                                        DoubleValue(0.001),
                                        MakeDoubleAccessor(&FlowMonitor::m_delayBinWidth),
                                        MakeDoubleChecker<double>())
                          .AddAttribute("JitterBinWidth", ("The width used in the jitter histogram."),
                                        DoubleValue(0.001),
                                        MakeDoubleAccessor(&FlowMonitor::m_jitterBinWidth),
                                        MakeDoubleChecker<double>())
                          .AddAttribute("PacketSizeBinWidth", ("The width used in the packetSize histogram."),
                                        DoubleValue(20),
                                        MakeDoubleAccessor(&FlowMonitor::m_packetSizeBinWidth),
                                        MakeDoubleChecker<double>())
                          .AddAttribute("FlowInterruptionsBinWidth", ("The width used in the flowInterruptions histogram."),
                                        DoubleValue(0.250),
                                        MakeDoubleAccessor(&FlowMonitor::m_flowInterruptionsBinWidth),
                                        MakeDoubleChecker<double>())
                          .AddAttribute("FlowInterruptionsMinTime", ("The minimum inter-arrival time that is considered a flow interruption."),
                                        TimeValue(Seconds(0.5)),
                                        MakeTimeAccessor(&FlowMonitor::m_flowInterruptionsMinTime),
                                        MakeTimeChecker());
  return tid;
}

//得到TypeId
TypeId
FlowMonitor::GetInstanceTypeId(void) const
{
  return GetTypeId();
}

//初始化
FlowMonitor::FlowMonitor()
    : m_enabled(false)
{
  // m_histogramBinWidth=DEFAULT_BIN_WIDTH;
}

//关闭
void FlowMonitor::DoDispose(void)
{
  for (std::list<Ptr<FlowClassifier> >::iterator iter = m_classifiers.begin();
       iter != m_classifiers.end();
       iter++)
  {
    *iter = 0;
  }
  for (uint32_t i = 0; i < m_flowProbes.size(); i++)
  {
    m_flowProbes[i]->Dispose();
    m_flowProbes[i] = 0;
  }
  Object::DoDispose();
}

//返回一个流的状态，如果之前没有就初始化一个新的返回
inline FlowMonitor::FlowStats &
FlowMonitor::GetStatsForFlow(FlowId flowId)
{
  FlowStatsContainerI iter;
  iter = m_flowStats.find(flowId);
  if (iter == m_flowStats.end())
  {
    FlowMonitor::FlowStats &ref = m_flowStats[flowId];
    ref.delaySum = Seconds(0);
    ref.jitterSum = Seconds(0);
    ref.lastDelay = Seconds(0);
    ref.txBytes = 0;
    ref.rxBytes = 0;
    ref.txPackets = 0;
    ref.rxPackets = 0;
    ref.lostPackets = 0;
    ref.queueDiscLostPacket = 0;
    ref.sinkByte = 0;
    ref.rtoRank = -1;
    ref.timesForwarded = 0;
    ref.delayHistogram.SetDefaultBinWidth(m_delayBinWidth);
    ref.jitterHistogram.SetDefaultBinWidth(m_jitterBinWidth);
    ref.packetSizeHistogram.SetDefaultBinWidth(m_packetSizeBinWidth);
    ref.flowInterruptionsHistogram.SetDefaultBinWidth(m_flowInterruptionsBinWidth);
    return ref;
  }
  else
  {
    return iter->second;
  }
}

//每发送一个包时调用。更新包的追踪数据，更新流状态
void FlowMonitor::ReportFirstTx(Ptr<FlowProbe> probe, uint32_t flowId, uint32_t packetId, uint32_t packetSize, uint32_t interface,int16_t RtoRank)
{
  //如果没有开启，则直接返回
  if (!m_enabled)
  {
    return;
  }
  //得到当前的时间
  Time now = Simulator::Now();
  //得到追踪包的数据
  TrackedPacket &tracked = m_trackedPackets[std::make_pair(flowId, packetId)];
  //被probe探测到这个包的绝对时间
  tracked.firstSeenTime = now;
  //上次这个包被探测到的时间
  tracked.lastSeenTime = tracked.firstSeenTime;
  //转发的次数设为0
  tracked.timesForwarded = 0;
  //追踪了一个包
  NS_LOG_DEBUG("ReportFirstTx: adding tracked packet (flowId=" << flowId << ", packetId=" << packetId
                                                               << ").");
  //开始追踪一个包,设置delayFromFirstProbeSum为0
  probe->AddPacketStats(flowId, packetSize, Seconds(0));
  //得到流状态
  FlowStats &stats = GetStatsForFlow(flowId);
  //发送的字节变多
  stats.txBytes += packetSize;
  //发送的包数变多
  stats.txPackets++;
  //如果这是每一个包
  if (stats.txPackets == 1)
  {
    //这条流第一个发送包的时间就是现在，第一个包id就是这个，
    stats.timeFirstTxPacket = now;
    stats.firstPacketId = packetId;
    stats.ports.push_back(interface); //得到这个包走的端口
  }
  //最近一次看到这个流也是现在
  stats.timeLastTxPacket = now;
  /***********************************************************/
  if(stats.rtoRank < RtoRank) stats.rtoRank = RtoRank;
}

//转发包时调用
void FlowMonitor::ReportForwarding(Ptr<FlowProbe> probe, uint32_t flowId, uint32_t packetId, uint32_t packetSize, uint32_t interface)
{
  //如果没开启追踪，则直接返回
  if (!m_enabled)
  {
    return;
  }
  //get包追踪数据
  std::pair<FlowId, FlowPacketId> key(flowId, packetId);
  TrackedPacketMap::iterator tracked = m_trackedPackets.find(key);
  //如果没找到就返回错误
  if (tracked == m_trackedPackets.end())
  {
    NS_LOG_WARN("Received packet forward report (flowId=" << flowId << ", packetId=" << packetId
                                                          << ") but not known to be transmitted.");
    return;
  }
  //如果找到，就更新它的转发次数和最近一次看到的时间
  tracked->second.timesForwarded++;
  tracked->second.lastSeenTime = Simulator::Now();
  //得到现在为止已经delay了多长时间了
  Time delay = (Simulator::Now() - tracked->second.firstSeenTime);
  //添加到probe的包状态中，并将到现在的延加入
  probe->AddPacketStats(flowId, packetSize, delay);
  //get流状态，如是这个流的第一个包，则记录转发的接口
  FlowStats &stats = GetStatsForFlow(flowId);
  if (stats.firstPacketId == packetId)
  {
    stats.ports.push_back(interface);
  }
}
/***********************************************************************************************************************************************/
//转发包时调用
void FlowMonitor::ReportAppPacketSink(Ptr<FlowProbe> probe, uint32_t flowId, uint32_t packetId, uint32_t packetSize, const Address &address)
{
  //如果没开启追踪，则直接返回
  if (!m_enabled)
  {
    return;
  }
  FlowStatsContainerI iter;
  iter = m_flowStats.find(flowId);
  if (iter == m_flowStats.end())
   return;
  else 
   {
      FlowStats &stats = iter->second;
      //更新总时延
      stats.sinkByte += packetSize;
   }
  
}

/***********************************************************************************************************************************************/

//收到包后调用
void FlowMonitor::ReportLastRx(Ptr<FlowProbe> probe, uint32_t flowId, uint32_t packetId, uint32_t packetSize)
{
  if (!m_enabled)
  {
    return;
  }
  //得到包的跟踪状态，如果没有返回错误
  TrackedPacketMap::iterator tracked = m_trackedPackets.find(std::make_pair(flowId, packetId));
  if (tracked == m_trackedPackets.end())
  {
    NS_LOG_WARN("Received packet last-tx report (flowId=" << flowId << ", packetId=" << packetId
                                                          << ") but not known to be transmitted.");
    return;
  }

  //如果有，就记录到现在为止的时延，并且添加到包状态中
  Time now = Simulator::Now();
  Time delay = (now - tracked->second.firstSeenTime);
  probe->AddPacketStats(flowId, packetSize, delay);
  //得到流状态
  FlowStats &stats = GetStatsForFlow(flowId);
  //更新总时延
  stats.delaySum += delay;
  //添加过了多少秒
  stats.delayHistogram.AddValue(delay.GetSeconds());
  //如果不是第一个收到的包，更新jitter
  if (stats.rxPackets > 0)
  {
    Time jitter = stats.lastDelay - delay;
    if (jitter > Seconds(0))
    {
      stats.jitterSum += jitter;
      stats.jitterHistogram.AddValue(jitter.GetSeconds());
    }
    else
    {
      stats.jitterSum -= jitter;
      stats.jitterHistogram.AddValue(-jitter.GetSeconds());
    }
  }
  //更新状态最近一个包的总时延
  stats.lastDelay = delay;
  //理更新收到的字节
  stats.rxBytes += packetSize;
  //添加收到了多少字节
  stats.packetSizeHistogram.AddValue((double)packetSize);
  //更新收到的包数
  stats.rxPackets++;
  //如果是第一个收到的包，更新时间timeFirstRxPacket
  if (stats.rxPackets == 1)
  {
    stats.timeFirstRxPacket = now;
  }
  else
  {
    // measure possible flow interruptions
    //如果不是第一个收到的包，得到从上次收到包到这次收到包的时间间隔
    Time interArrivalTime = now - stats.timeLastRxPacket;
    //如果间隔时间大于设定的阀值，则添加进图
    if (interArrivalTime > m_flowInterruptionsMinTime)
    {
      stats.flowInterruptionsHistogram.AddValue(interArrivalTime.GetSeconds());
    }
  }
  //更新最近一次收到包的时间
  stats.timeLastRxPacket = now;
  //流状态里的被转发次数再加上这个包的被转发次数
  stats.timesForwarded += tracked->second.timesForwarded;

  NS_LOG_DEBUG("ReportLastTx: removing tracked packet (flowId="
               << flowId << ", packetId=" << packetId << ").");
  //不再track所以清除数据
  m_trackedPackets.erase(tracked); // we don't need to track this packet anymore
}

//在丢包后调用
void FlowMonitor::ReportDrop(Ptr<FlowProbe> probe, uint32_t flowId, uint32_t packetId, uint32_t packetSize,
                             uint32_t reasonCode)
{
  if (!m_enabled)
  {
    return;
  }

  //在probe中添加丢包的状态和原因
  probe->AddPacketDropStats(flowId, packetSize, reasonCode);
  //得到流的状态
  FlowStats &stats = GetStatsForFlow(flowId);
  //丢包数加一
  stats.lostPackets++;
  /************************************************************************************/
  if(reasonCode == 4) stats.queueDiscLostPacket++;
  /************************************************************************************/
  //resize用于调整大小并用后面的值进行填充
  if (stats.packetsDropped.size() < reasonCode + 1)
  {
    stats.packetsDropped.resize(reasonCode + 1, 0);
    stats.bytesDropped.resize(reasonCode + 1, 0);
  }
  //更新因为这个原因丢了多少包和多少字节
  ++stats.packetsDropped[reasonCode];
  stats.bytesDropped[reasonCode] += packetSize;
  NS_LOG_DEBUG("++stats.packetsDropped[" << reasonCode << "]; // becomes: " << stats.packetsDropped[reasonCode]);

  // XXX It is event not true with QueueDisc Requeue
  /*
  TrackedPacketMap::iterator tracked = m_trackedPackets.find (std::make_pair (flowId, packetId));
  if (tracked != m_trackedPackets.end ())
    {
      // we don't need to track this packet anymore
      // FIXME: this will not necessarily be true with broadcast/multicast
      NS_LOG_DEBUG ("ReportDrop: removing tracked packet (flowId="
                    << flowId << ", packetId=" << packetId << ").");
      m_trackedPackets.erase (tracked);
    }
    */
}

//得到流的状态
const FlowMonitor::FlowStatsContainer &
FlowMonitor::GetFlowStats() const
{
  return m_flowStats;
}

//检查丢失的包
void FlowMonitor::CheckForLostPackets(Time maxDelay)
{
  Time now = Simulator::Now();

  //遍历所有的包
  for (TrackedPacketMap::iterator iter = m_trackedPackets.begin();
       iter != m_trackedPackets.end();)
  {
    //如果已经超时，则认此包已丢失
    if (now - iter->second.lastSeenTime >= maxDelay)
    {
      // packet is considered lost, add it to the loss statistics
      FlowStatsContainerI flow = m_flowStats.find(iter->first.first);
      NS_ASSERT(flow != m_flowStats.end());
      flow->second.lostPackets++; //得到流状态后，计丢包数加一

      // we won't track it anymore
      //不再进行跟踪
      m_trackedPackets.erase(iter++);
    }
    else
    {
      iter++; //否则继续
    }
  }
}
//根据初始值来检查是否丢包
void FlowMonitor::CheckForLostPackets()
{
  CheckForLostPackets(m_maxPerHopDelay);
}

//周期性进行检查是否丢包
void FlowMonitor::PeriodicCheckForLostPackets()
{
  CheckForLostPackets();
  Simulator::Schedule(PERIODIC_CHECK_INTERVAL, &FlowMonitor::PeriodicCheckForLostPackets, this);
}

//
void FlowMonitor::NotifyConstructionCompleted()
{
  Object::NotifyConstructionCompleted();
  Simulator::Schedule(PERIODIC_CHECK_INTERVAL, &FlowMonitor::PeriodicCheckForLostPackets, this);
}

//添加探测probe
void FlowMonitor::AddProbe(Ptr<FlowProbe> probe)
{
  m_flowProbes.push_back(probe);
}

//得到所有probe
const FlowMonitor::FlowProbeContainer &
FlowMonitor::GetAllProbes() const
{
  return m_flowProbes;
}

//从什么时间开始监视流
void FlowMonitor::Start(const Time &time)
{
  if (m_enabled)
  {
    return;
  }
  Simulator::Cancel(m_startEvent);
  m_startEvent = Simulator::Schedule(time, &FlowMonitor::StartRightNow, this);
}

//从什么时候开始停止监视流
void FlowMonitor::Stop(const Time &time)
{
  if (!m_enabled)
  {
    return;
  }
  Simulator::Cancel(m_stopEvent);
  m_stopEvent = Simulator::Schedule(time, &FlowMonitor::StopRightNow, this);
}

//立刻开始监视
void FlowMonitor::StartRightNow()
{
  if (m_enabled)
  {
    return;
  }
  m_enabled = true;
}

//立刻停止监视
void FlowMonitor::StopRightNow()
{
  if (!m_enabled)
  {
    return;
  }
  m_enabled = false;
  CheckForLostPackets();
}

//添加流分类器
void FlowMonitor::AddFlowClassifier(Ptr<FlowClassifier> classifier)
{
  m_classifiers.push_back(classifier);
}

//以XML流的形式输出
void FlowMonitor::SerializeToXmlStream(std::ostream &os, int indent, bool enableHistograms, bool enableProbes)
{
  //先检查是否有丢包
  CheckForLostPackets();
  //输出空格
  INDENT(indent);
  os << "<FlowMonitor>\n";
  indent += 2;
  INDENT(indent);
  os << "<FlowStats>\n";
  indent += 2;
  //输出所有流的状态
  for (FlowStatsContainerCI flowI = m_flowStats.begin();
       flowI != m_flowStats.end(); flowI++)
  {

    INDENT(indent);
#define ATTRIB(name) << " " #name "=\"" << flowI->second.name << "\""
    os << "<Flow flowId=\"" << flowI->first << "\"" 
      ATTRIB(rtoRank)
      ATTRIB(timeFirstTxPacket) 
      ATTRIB(timeFirstRxPacket) 
      ATTRIB(timeLastTxPacket) 
      ATTRIB(timeLastRxPacket) 
      ATTRIB(txBytes) 
      ATTRIB(rxBytes) 
      ATTRIB(txPackets) 
      ATTRIB(rxPackets) 
      ATTRIB(lostPackets) 
      ATTRIB(queueDiscLostPacket) //ADD BY MYSELF
      ATTRIB(sinkByte)
      ATTRIB(timesForwarded)
      ATTRIB(delaySum) 
      ATTRIB(jitterSum) 
      ATTRIB(lastDelay) 
       << ">\n";
#undef ATTRIB

    indent += 2;
    for (uint32_t reasonCode = 0; reasonCode < flowI->second.packetsDropped.size(); reasonCode++)
    {
      INDENT(indent);
      os << "<packetsDropped reasonCode=\"" << reasonCode << "\""
         << " number=\"" << flowI->second.packetsDropped[reasonCode]
         << "\" />\n";
    }
    for (uint32_t reasonCode = 0; reasonCode < flowI->second.bytesDropped.size(); reasonCode++)
    {
      INDENT(indent);
      os << "<bytesDropped reasonCode=\"" << reasonCode << "\""
         << " bytes=\"" << flowI->second.bytesDropped[reasonCode]
         << "\" />\n";
    }
    for (uint32_t portIndex = 0; portIndex < flowI->second.ports.size(); portIndex++)
    {
      INDENT(indent);
      os << "<path time=\"" << portIndex << "\""
         << " port=\"" << flowI->second.ports[portIndex]
         << "\" />\n";
    }
    if (enableHistograms)
    {
      flowI->second.delayHistogram.SerializeToXmlStream(os, indent, "delayHistogram");
      flowI->second.jitterHistogram.SerializeToXmlStream(os, indent, "jitterHistogram");
      flowI->second.packetSizeHistogram.SerializeToXmlStream(os, indent, "packetSizeHistogram");
      flowI->second.flowInterruptionsHistogram.SerializeToXmlStream(os, indent, "flowInterruptionsHistogram");
    }
    indent -= 2;

    INDENT(indent);
    os << "</Flow>\n";
  }
  indent -= 2;
  INDENT(indent);
  os << "</FlowStats>\n";

  for (std::list<Ptr<FlowClassifier> >::iterator iter = m_classifiers.begin();
       iter != m_classifiers.end();
       iter++)
  {
    (*iter)->SerializeToXmlStream(os, indent);
  }

  if (enableProbes)
  {
    INDENT(indent);
    os << "<FlowProbes>\n";
    indent += 2;
    for (uint32_t i = 0; i < m_flowProbes.size(); i++)
    {
      m_flowProbes[i]->SerializeToXmlStream(os, indent, i);
    }
    indent -= 2;
    INDENT(indent);
    os << "</FlowProbes>\n";
  }

  indent -= 2;
  INDENT(indent);
  os << "</FlowMonitor>\n";
}

std::string
FlowMonitor::SerializeToXmlString(int indent, bool enableHistograms, bool enableProbes)
{
  std::ostringstream os;
  SerializeToXmlStream(os, indent, enableHistograms, enableProbes);
  return os.str();
}

void FlowMonitor::SerializeToXmlFile(std::string fileName, bool enableHistograms, bool enableProbes)
{
  std::ofstream os(fileName.c_str(), std::ios::out | std::ios::binary);
  os << "<?xml version=\"1.0\" ?>\n";
  SerializeToXmlStream(os, 0, enableHistograms, enableProbes);
  os.close();
}

} // namespace ns3
