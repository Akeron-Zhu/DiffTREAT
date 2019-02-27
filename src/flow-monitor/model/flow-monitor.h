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

#ifndef FLOW_MONITOR_H
#define FLOW_MONITOR_H

#include <vector>
#include <map>

#include "ns3/ptr.h"
#include "ns3/object.h"
#include "ns3/address.h"
#include "ns3/flow-probe.h"
#include "ns3/flow-classifier.h"
#include "ns3/histogram.h"
#include "ns3/nstime.h"
#include "ns3/event-id.h"
#include "ns3/tcp-header.h"

namespace ns3 {

/**
 * \defgroup flow-monitor Flow Monitor
 * \brief  Collect and store performance data from a simulation
 */

/**
 * \ingroup flow-monitor
 * \brief An object that monitors and reports back packet flows observed during a simulation
 *
 * The FlowMonitor class is responsible for coordinating efforts
 * regarding probes, and collects end-to-end flow statistics.
 *
 */
//继承于Object类
//FlowMonitor是从流的角度来观察，而FlowProbe是从节点角度来观察
//FlowMointor只要一个实例就够了
class FlowMonitor : public Object
{
public:

  /// \brief Structure that represents the measured metrics of an individual packet flow
  //构建一个用于描述流状态的类
  struct FlowStats
  {
    /// Contains the absolute time when the first packet in the flow
    /// was transmitted, i.e. the time when the flow transmission
    /// starts
    //发送第一个包的时间 
    Time     timeFirstTxPacket;

    /// Contains the absolute time when the first packet in the flow
    /// was received by an end node, i.e. the time when the flow
    /// reception starts
    //收到第一个包的时间
    Time     timeFirstRxPacket;

    /// Contains the absolute time when the last packet in the flow
    /// was transmitted, i.e. the time when the flow transmission
    /// ends
    //最近一次这个流发包的时间
    Time     timeLastTxPacket;

    /// Contains the absolute time when the last packet in the flow
    /// was received, i.e. the time when the flow reception ends
    //最近一次这个流收到包的时间
    Time     timeLastRxPacket;

    /// Contains the sum of all end-to-end delays for all received
    /// packets of the flow.
    //所有包端到端时延的和
    Time     delaySum; // delayCount == rxPackets

    /// Contains the sum of all end-to-end delay jitter (delay
    /// variation) values for all received packets of the flow.  Here
    /// we define _jitter_ of a packet as the delay variation
    /// relatively to the last packet of the stream,
    /// i.e. \f$Jitter\left\{P_N\right\} = \left|Delay\left\{P_N\right\} - Delay\left\{P_{N-1}\right\}\right|\f$.
    /// This definition is in accordance with the Type-P-One-way-ipdv
    /// as defined in IETF \RFC{3393}.
    //每两次delay相差绝对值的和
    Time     jitterSum; // jitterCount == rxPackets - 1

    /// Contains the last measured delay of a packet
    /// It is stored to measure the packet's Jitter
    //上一个包端到端的时延
    Time     lastDelay;

    /// Total number of transmitted bytes for the flow
    //这条流发送包的总字节数
    uint64_t txBytes;
    /// Total number of received bytes for the flow
    //收到的总字节数
    uint64_t rxBytes;
    /// Total number of transmitted packets for the flow
    //发送的包的数量
    uint32_t txPackets;
    /// Total number of received packets for the flow
    //接收到包的数量
    uint32_t rxPackets;

    /// Total number of packets that are assumed to be lost,
    /// i.e. those that were transmitted but have not been reportedly
    /// received or forwarded for a long time.  By default, packets
    /// missing for a period of over 10 seconds are assumed to be
    /// lost, although this value can be easily configured in runtime
    //丢包的总数
    uint32_t lostPackets;

    /********************************************************************************/
    uint32_t queueDiscLostPacket;    
    uint32_t sinkByte;
    int16_t rtoRank;
    /********************************************************************************/

    /// Contains the number of times a packet has been reportedly
    /// forwarded, summed for all received packets in the flow
    //转发的次数
    uint32_t timesForwarded;

    /// Histogram of the packet delays
    //分布图
    Histogram delayHistogram;
    /// Histogram of the packet jitters
    Histogram jitterHistogram;
    /// Histogram of the packet sizes
    Histogram packetSizeHistogram;

    /// This attribute also tracks the number of lost packets and
    /// bytes, but discriminates the losses by a _reason code_.  This
    /// reason code is usually an enumeration defined by the concrete
    /// FlowProbe class, and for each reason code there may be a
    /// vector entry indexed by that code and whose value is the
    /// number of packets or bytes lost due to this reason.  For
    /// instance, in the Ipv4FlowProbe case the following reasons are
    /// currently defined: DROP_NO_ROUTE (no IPv4 route found for a
    /// packet), DROP_TTL_EXPIRE (a packet was dropped due to an IPv4
    /// TTL field decremented and reaching zero), and
    /// DROP_BAD_CHECKSUM (a packet had bad IPv4 header checksum and
    /// had to be dropped).
    //因为这个reason丢掉了几个包
    std::vector<uint32_t> packetsDropped; // packetsDropped[reasonCode] => number of dropped packets

    /// This attribute also tracks the number of lost bytes.  See also
    /// comment in attribute packetsDropped.
    ///因为这个reason丢掉了多少字节
    std::vector<uint64_t> bytesDropped; // bytesDropped[reasonCode] => number of dropped bytes
    Histogram flowInterruptionsHistogram; //!< histogram of durations of flow interruptions

    // The following code is used to track the path of one flow
    //用于根据一条流的路径
    uint32_t firstPacketId;
    std::vector<uint32_t> ports;
  };

  // --- basic methods ---
  /**
   * \brief Get the type ID.
   * \return the object TypeId
   */
  static TypeId GetTypeId ();
  TypeId GetInstanceTypeId () const;
  FlowMonitor ();

  /// Add a FlowClassifier to be used by the flow monitor.
  /// \param classifier the FlowClassifier
  //添加流分类器
  void AddFlowClassifier (Ptr<FlowClassifier> classifier);

  /// Set the time, counting from the current time, from which to start monitoring flows.
  /// \param time delta time to start
  //开始的时间和结束的时间
  void Start (const Time &time);
  /// Set the time, counting from the current time, from which to stop monitoring flows.
  /// \param time delta time to stop
  void Stop (const Time &time);
  /// Begin monitoring flows *right now*
  //立刻开始监控
  void StartRightNow ();
  //立刻停止监控
  /// End monitoring flows *right now*
  void StopRightNow ();

  // --- methods to be used by the FlowMonitorProbe's only ---
  /// Register a new FlowProbe that will begin monitoring and report
  /// events to this monitor.  This method is normally only used by
  /// FlowProbe implementations.
  /// \param probe the probe to add
  //添加FlowMonitorProbe
  void AddProbe (Ptr<FlowProbe> probe);

  /// FlowProbe implementations are supposed to call this method to
  /// report that a new packet was transmitted (but keep in mind the
  /// distinction between a new packet entering the system and a
  /// packet that is already known and is only being forwarded).
  /// \param probe the reporting probe
  /// \param flowId flow identification
  /// \param packetId Packet ID
  /// \param packetSize packet size
  //报告一个包发送了
  void ReportFirstTx (Ptr<FlowProbe> probe, FlowId flowId, FlowPacketId packetId, uint32_t packetSize, uint32_t interface,int16_t RtoRank = -1);
  /// FlowProbe implementations are supposed to call this method to
  /// report that a known packet is being forwarded.
  /// \param probe the reporting probe
  /// \param flowId flow identification
  /// \param packetId Packet ID
  /// \param packetSize packet size
  //一个知道的包转发了
  void ReportForwarding (Ptr<FlowProbe> probe, FlowId flowId, FlowPacketId packetId, uint32_t packetSize, uint32_t interface);
  /// FlowProbe implementations are supposed to call this method to
  /// report that a known packet is being received.
  /// \param probe the reporting probe
  /// \param flowId flow identification
  /// \param packetId Packet ID
  /// \param packe000000000tSize packet size
  // a known 包收到了
  void ReportLastRx (Ptr<FlowProbe> probe, FlowId flowId, FlowPacketId packetId, uint32_t packetSize);
  /// FlowProbe implementations are supposed to call this method to
  /// report that a known packet is being dropped due to some reason.
  /// \param probe the reporting probe
  /// \param flowId flow identification
  /// \param packetId Packet ID
  /// \param packetSize packet size
  /// \param reasonCode drop reason code
  //一个由于某些原因被Drop了
  void ReportDrop (Ptr<FlowProbe> probe, FlowId flowId, FlowPacketId packetId,
                   uint32_t packetSize, uint32_t reasonCode);

  /// Check right now for packets that appear to be lost
  //立刻检查那些丢了的包
  void CheckForLostPackets ();

  /// Check right now for packets that appear to be lost, considering
  /// packets as lost if not seen in the network for a time larger
  /// than maxDelay
  /// \param maxDelay the max delay for a packet
  //当超过maxDelay后立刻检查
  void CheckForLostPackets (Time maxDelay);

  /****************************************************************************/
  void ReportAppPacketSink(Ptr<FlowProbe> probe, uint32_t flowId, uint32_t packetId, uint32_t packetSize, const Address &address);
  /****************************************************************************/

  // --- methods to get the results ---

  /// Container: FlowId, FlowStats
  //一个存储FlowId与FlowStats的Map
  typedef std::map<FlowId, FlowStats> FlowStatsContainer;
  /// Container Iterator: FlowId, FlowStats
  //迭代指针
  typedef std::map<FlowId, FlowStats>::iterator FlowStatsContainerI;
  /// Container Const Iterator: FlowId, FlowStats
  typedef std::map<FlowId, FlowStats>::const_iterator FlowStatsContainerCI;
  /// Container: FlowProbe
  //FlowProbe的向量
  typedef std::vector< Ptr<FlowProbe> > FlowProbeContainer;
  /// Container Iterator: FlowProbe
  //迭代指针
  typedef std::vector< Ptr<FlowProbe> >::iterator FlowProbeContainerI;
  /// Container Const Iterator: FlowProbe
  //迭代指针
  typedef std::vector< Ptr<FlowProbe> >::const_iterator FlowProbeContainerCI;

  /// Retrieve all collected the flow statistics.  Note, if the
  /// FlowMonitor has not stopped monitoring yet, you should call
  /// CheckForLostPackets() to make sure all possibly lost packets are
  /// accounted for.
  /// \returns the flows statistics
  //返回所有收集到的流状况
  const FlowStatsContainer& GetFlowStats () const;

  /// Get a list of all FlowProbe's associated with this FlowMonitor
  /// \returns a list of all the probes
  //得到这个Monitor检测的所有FlowProbe
  const FlowProbeContainer& GetAllProbes () const;

  /// Serializes the results to an std::ostream in XML format
  /// \param os the output stream
  /// \param indent number of spaces to use as base indentation level
  /// \param enableHistograms if true, include also the histograms in the output
  /// \param enableProbes if true, include also the per-probe/flow pair statistics in the output
  //以XML形式输出
  void SerializeToXmlStream (std::ostream &os, int indent, bool enableHistograms, bool enableProbes);

  /// Same as SerializeToXmlStream, but returns the output as a std::string
  /// \param indent number of spaces to use as base indentation level
  /// \param enableHistograms if true, include also the histograms in the output
  /// \param enableProbes if true, include also the per-probe/flow pair statistics in the output
  /// \return the XML output as string
  //以String的形式输出
  std::string SerializeToXmlString (int indent, bool enableHistograms, bool enableProbes);

  /// Same as SerializeToXmlStream, but writes to a file instead
  /// \param fileName name or path of the output file that will be created
  /// \param enableHistograms if true, include also the histograms in the output
  /// \param enableProbes if true, include also the per-probe/flow pair statistics in the output
  //以XML文件形式输出
  void SerializeToXmlFile (std::string fileName, bool enableHistograms, bool enableProbes);


protected:

  virtual void NotifyConstructionCompleted ();
  virtual void DoDispose (void);

private:

  /// Structure to represent a single tracked packet data
  //用于表示一个包的追踪数据
  struct TrackedPacket
  {
    Time firstSeenTime; //!< absolute time when the packet was first seen by a probe
    Time lastSeenTime; //!< absolute time when the packet was last seen by a probe
    uint32_t timesForwarded; //!< number of times the packet was reportedly forwarded
  };

  /// FlowId --> FlowStats
  //用于根据FlowId查询FlowStats
  FlowStatsContainer m_flowStats;

  /// (FlowId,PacketId) --> TrackedPacket
  typedef std::map< std::pair<FlowId, FlowPacketId>, TrackedPacket> TrackedPacketMap;
  TrackedPacketMap m_trackedPackets; //!< Tracked packets
  Time m_maxPerHopDelay; //!< Minimum per-hop delay
  FlowProbeContainer m_flowProbes; //!< all the FlowProbes

  // note: this is needed only for serialization
  std::list<Ptr<FlowClassifier> > m_classifiers; //!< the FlowClassifiers

  //开始与结束事件
  EventId m_startEvent;     //!< Start event
  EventId m_stopEvent;      //!< Stop event
  //是否开启FlowMonitor
  bool m_enabled;           //!< FlowMon is enabled
  //用于绘制直方图
  double m_delayBinWidth;   //!< Delay bin width (for histograms)
  double m_jitterBinWidth;  //!< Jitter bin width (for histograms)
  double m_packetSizeBinWidth;  //!< packet size bin width (for histograms)
  double m_flowInterruptionsBinWidth; //!< Flow interruptions bin width (for histograms)
  Time m_flowInterruptionsMinTime; //!< Flow interruptions minimum time

  /// Get the stats for a given flow
  /// \param flowId the Flow identification
  /// \returns the stats of the flow
  //得到指定流的flowStat
  FlowStats& GetStatsForFlow (FlowId flowId);

  /// Periodic function to check for lost packets and prune statistics
  //周期性的检查丢失的包并且修改统计情况
  void PeriodicCheckForLostPackets ();
};


} // namespace ns3

#endif /* FLOW_MONITOR_H */

