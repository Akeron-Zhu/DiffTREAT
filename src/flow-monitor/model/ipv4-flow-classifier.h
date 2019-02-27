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

#ifndef IPV4_FLOW_CLASSIFIER_H
#define IPV4_FLOW_CLASSIFIER_H

#include <stdint.h>
#include <map>

#include "ns3/ipv4-header.h"
#include "ns3/flow-classifier.h"

namespace ns3 {

class Packet;

/// Classifies packets by looking at their IP and TCP/UDP headers.
/// From these packet headers, a tuple (source-ip, destination-ip,
/// protocol, source-port, destination-port) is created, and a unique
/// flow identifier is assigned for each different tuple combination
class Ipv4FlowClassifier : public FlowClassifier
{
public:

  /// Structure to classify a packet
  //五元组，用于唯一标识一个包
  struct FiveTuple
  {
    Ipv4Address sourceAddress;      //!< Source address
    Ipv4Address destinationAddress; //!< Destination address
    uint8_t protocol;               //!< Protocol
    uint16_t sourcePort;            //!< Source port
    uint16_t destinationPort;       //!< Destination port
  };

  Ipv4FlowClassifier ();

  /// \brief try to classify the packet into flow-id and packet-id
  ///
  /// \warning: it must be called only once per packet, from SendOutgoingLogger.
  ///
  /// \return true if the packet was classified, false if not (i.e. it
  /// does not appear to be part of a flow).
  /// \param ipHeader packet's IP header
  /// \param ipPayload packet's IP payload
  /// \param out_flowId packet's FlowId
  /// \param out_packetId packet's identifier
  //试图将包分类到流id与包id,其必须对每个包进行一次调用，如果分类完成返回ture，否则返回false.
  bool Classify (const Ipv4Header &ipHeader, Ptr<const Packet> ipPayload,
                 uint32_t *out_flowId, uint32_t *out_packetId);

  /// Searches for the FiveTuple corresponding to the given flowId
  /// \param flowId the FlowId to search for
  /// \returns the FiveTuple corresponding to flowId
  //根据给定的FlowId返回五元组
  FiveTuple FindFlow (FlowId flowId) const;
  FlowId FindFlowId(FiveTuple fiveTuple);
  //以XML形式输出信息
  virtual void SerializeToXmlStream (std::ostream &os, int indent) const;

private:

  /// Map to Flows Identifiers to FlowIds
  //根据五元组映射到FlowId
  std::map<FiveTuple, FlowId> m_flowMap;
  /// Map to FlowIds to FlowPacketId
  //根据FlowId映射到FlowPakcetId.
  std::map<FlowId, FlowPacketId> m_flowPktIdMap;

};

/**
 * \brief Less than operator.
 *
 * \param t1 the first operand
 * \param t2 the first operand
 * \returns true if the operands are equal
 */
//重载小于运算符，用于比较五元组
bool operator < (const Ipv4FlowClassifier::FiveTuple &t1, const Ipv4FlowClassifier::FiveTuple &t2);

/**
 * \brief Equal to operator.
 *
 * \param t1 the first operand
 * \param t2 the first operand
 * \returns true if the operands are equal
 */
//重载等于运算符用于比较五元组
bool operator == (const Ipv4FlowClassifier::FiveTuple &t1, const Ipv4FlowClassifier::FiveTuple &t2);


} // namespace ns3

#endif /* IPV4_FLOW_CLASSIFIER_H */
