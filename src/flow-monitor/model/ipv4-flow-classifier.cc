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

#include "ns3/packet.h"

#include "ipv4-flow-classifier.h"
#include "ns3/udp-header.h"
#include "ns3/tcp-header.h"

namespace ns3 {

/* see http://www.iana.org/assignments/protocol-numbers */
//TCP的协议号UDP的协议号
const uint8_t TCP_PROT_NUMBER = 6;  //!< TCP Protocol number
const uint8_t UDP_PROT_NUMBER = 17; //!< UDP Protocol number


//比较两个五元组的信息
bool operator < (const Ipv4FlowClassifier::FiveTuple &t1,
                 const Ipv4FlowClassifier::FiveTuple &t2)
{
  if (t1.sourceAddress < t2.sourceAddress)
    {
      return true;
    }
  if (t1.sourceAddress != t2.sourceAddress)
    {
      return false;
    }

  if (t1.destinationAddress < t2.destinationAddress)
    {
      return true;
    }
  if (t1.destinationAddress != t2.destinationAddress)
    {
      return false;
    }

  if (t1.protocol < t2.protocol)
    {
      return true;
    }
  if (t1.protocol != t2.protocol)
    {
      return false;
    }

  if (t1.sourcePort < t2.sourcePort)
    {
      return true;
    }
  if (t1.sourcePort != t2.sourcePort)
    {
      return false;
    }

  if (t1.destinationPort < t2.destinationPort)
    {
      return true;
    }
  if (t1.destinationPort != t2.destinationPort)
    {
      return false;
    }

  return false;
}

//重载等于运算符
bool operator == (const Ipv4FlowClassifier::FiveTuple &t1,
                  const Ipv4FlowClassifier::FiveTuple &t2)
{
  return (t1.sourceAddress      == t2.sourceAddress &&
          t1.destinationAddress == t2.destinationAddress &&
          t1.protocol           == t2.protocol &&
          t1.sourcePort         == t2.sourcePort &&
          t1.destinationPort    == t2.destinationPort);
}



Ipv4FlowClassifier::Ipv4FlowClassifier ()
{
}

//分类
bool
Ipv4FlowClassifier::Classify (const Ipv4Header &ipHeader, Ptr<const Packet> ipPayload,
                              uint32_t *out_flowId, uint32_t *out_packetId)
{
  //如果是分片后的包，则它不是一个完整的包，得到偏移量
  if (ipHeader.GetFragmentOffset () > 0 )
    {
      // Ignore fragments: they don't carry a valid L4 header
      return false;
    }
  //得到五元组
  FiveTuple tuple;
  tuple.sourceAddress = ipHeader.GetSource ();
  tuple.destinationAddress = ipHeader.GetDestination ();
  tuple.protocol = ipHeader.GetProtocol ();
  //如果不是TCP或UDP协议则返回false
  if ((tuple.protocol != UDP_PROT_NUMBER) && (tuple.protocol != TCP_PROT_NUMBER))
    {
      return false;
    }
  //得到负载的字节数量，如果小于4则返回falsed
  if (ipPayload->GetSize () < 4)
    {
      // the packet doesn't carry enough bytes
      return false;
    }

  // we rely on the fact that for both TCP and UDP the ports are
  // carried in the first 4 octects.
  // This allows to read the ports even on fragmented packets
  // not carrying a full TCP or UDP header.
  //得到源端口与目的端口
  uint8_t data[4];
  ipPayload->CopyData (data, 4);
  //采用或其实是在赋值，初始为0或后即相当于赋值
  uint16_t srcPort = 0;
  srcPort |= data[0];
  srcPort <<= 8;
  srcPort |= data[1];

  uint16_t dstPort = 0;
  dstPort |= data[2];
  dstPort <<= 8;
  dstPort |= data[3];

  tuple.sourcePort = srcPort;
  tuple.destinationPort = dstPort;

  /******************************Add by kk**************************************/
  //使FlowMonitor不统计ACK流
  FiveTuple tmp;
  tmp.sourceAddress = tuple.destinationAddress;
  tmp.destinationAddress = tuple.sourceAddress;
  tmp.sourcePort = tuple.destinationPort;
  tmp.destinationPort = tuple.sourcePort;
  tmp.protocol = tuple.protocol;
  if(m_flowMap.find(tmp) != m_flowMap.end())
  {
    return false;
  }
  
  /******************************************************************************/

  
  // try to insert the tuple, but check if it already exists
  //将五元组对FlowId的映射插入到m_flowMap中，会返回一个迭代指针，第二个变量表示是否成功插入
  std::pair<std::map<FiveTuple, FlowId>::iterator, bool> insert
    = m_flowMap.insert (std::pair<FiveTuple, FlowId> (tuple, 0));
  //std::cout<<m_flowMap.size()<<std::endl;
  // if the insertion succeeded, we need to assign this tuple a new flow identifier
  //如果成功插入，则需要赋予其一个新FlowId,插入时的flowId是0
  if (insert.second)
    {
      FlowId newFlowId = GetNewFlowId ();
      std::cout<<newFlowId<<' '<<std::flush;
      insert.first->second = newFlowId;
      m_flowPktIdMap[newFlowId] = 0;
    }
  else//如果插入未成功，则表示已经存在，直接对FlowPacketId加一就好
    {
      m_flowPktIdMap[insert.first->second] ++;
    }

  *out_flowId = insert.first->second; //返回流id
  *out_packetId = m_flowPktIdMap[*out_flowId];//返回包id

  return true;
}

//根据FlowId得到其五元组
Ipv4FlowClassifier::FiveTuple
Ipv4FlowClassifier::FindFlow (FlowId flowId) const
{
  //遍历m_flowMap来寻找
  for (std::map<FiveTuple, FlowId>::const_iterator
       iter = m_flowMap.begin (); iter != m_flowMap.end (); iter++)
    {
      if (iter->second == flowId)
        {
          return iter->first;
        }
    }
  //找不到时返回空
  NS_FATAL_ERROR ("Could not find the flow with ID " << flowId);
  FiveTuple retval = { Ipv4Address::GetZero (), Ipv4Address::GetZero (), 0, 0, 0 };
  return retval;
}

FlowId
Ipv4FlowClassifier::FindFlowId (FiveTuple fiveTuple)
{
  //遍历m_flowMap来寻找
  FlowId flowId = -1;
 // printf("m_flowMap size is:%d\n",m_flowMap.size());
  std::map<Ipv4FlowClassifier::FiveTuple, FlowId>::const_iterator iter = m_flowMap.find(fiveTuple);
  if( iter != m_flowMap.end())
  {
    flowId = iter->second;
  }

  return flowId;
}

//以XML流形式输出信息
void
Ipv4FlowClassifier::SerializeToXmlStream (std::ostream &os, int indent) const
{
#define INDENT(level) for (int __xpto = 0; __xpto < level; __xpto++) os << ' ';

  INDENT (indent); os << "<Ipv4FlowClassifier>\n";

  indent += 2;
  for (std::map<FiveTuple, FlowId>::const_iterator
       iter = m_flowMap.begin (); iter != m_flowMap.end (); iter++)
    {
      INDENT (indent);
      os << "<Flow flowId=\"" << iter->second << "\""
         << " sourceAddress=\"" << iter->first.sourceAddress << "\""
         << " destinationAddress=\"" << iter->first.destinationAddress << "\""
         << " protocol=\"" << int(iter->first.protocol) << "\""
         << " sourcePort=\"" << iter->first.sourcePort << "\""
         << " destinationPort=\"" << iter->first.destinationPort << "\""
         << " />\n";
    }

  indent -= 2;
  INDENT (indent); os << "</Ipv4FlowClassifier>\n";

#undef INDENT
}


} // namespace ns3

