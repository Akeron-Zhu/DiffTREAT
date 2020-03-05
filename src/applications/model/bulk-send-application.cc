/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2010 Georgia Institute of Technology
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: George F. Riley <riley@ece.gatech.edu>
 */

#include "ns3/log.h"
#include "ns3/address.h"
#include "ns3/node.h"
#include "ns3/nstime.h"
#include "ns3/socket.h"
#include "ns3/simulator.h"
#include "ns3/socket-factory.h"
#include "ns3/packet.h"
#include "ns3/uinteger.h"
#include "ns3/trace-source-accessor.h"
#include "ns3/tcp-socket-factory.h"
#include "bulk-send-application.h"

#include "ns3/rto-pri-tag.h"
#include "ns3/tcp-socket-base.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("BulkSendApplication");

NS_OBJECT_ENSURE_REGISTERED(BulkSendApplication);

TypeId
BulkSendApplication::GetTypeId(void)
{
  static TypeId tid = TypeId("ns3::BulkSendApplication")
                          .SetParent<Application>()
                          .SetGroupName("Applications")
                          .AddConstructor<BulkSendApplication>()
                          .AddAttribute("SendSize", "The amount of data to send each time.",
                                        UintegerValue(512),
                                        MakeUintegerAccessor(&BulkSendApplication::m_sendSize),
                                        MakeUintegerChecker<uint32_t>(1))
                          .AddAttribute("Remote", "The address of the destination",
                                        AddressValue(),
                                        MakeAddressAccessor(&BulkSendApplication::m_peer),
                                        MakeAddressChecker())
                          .AddAttribute("MaxBytes",
                                        "The total number of bytes to send. "
                                        "Once these bytes are sent, "
                                        "no data  is sent again. The value zero means "
                                        "that there is no limit.",
                                        UintegerValue(0),
                                        MakeUintegerAccessor(&BulkSendApplication::m_maxBytes),
                                        MakeUintegerChecker<uint32_t>())
                          .AddAttribute("DelayThresh",
                                        "How many packets can pass before we have delay, 0 for disable",
                                        UintegerValue(0),
                                        MakeUintegerAccessor(&BulkSendApplication::m_delayThresh),
                                        MakeUintegerChecker<uint32_t>())
                          .AddAttribute("DelayTime",
                                        "The time for a delay",
                                        TimeValue(MicroSeconds(100)),
                                        MakeTimeAccessor(&BulkSendApplication::m_delayTime),
                                        MakeTimeChecker())
                          .AddAttribute("Protocol", "The type of protocol to use.",
                                        TypeIdValue(TcpSocketFactory::GetTypeId()),
                                        MakeTypeIdAccessor(&BulkSendApplication::m_tid),
                                        MakeTypeIdChecker())
                          .AddTraceSource("Tx", "A new packet is created and is sent",
                                          MakeTraceSourceAccessor(&BulkSendApplication::m_txTrace),
                                          "ns3::Packet::TracedCallback")
                          .AddAttribute("RtoRank", "The rank of RTO.", //ADD  MODIFIED
                                        UintegerValue(0),
                                        MakeUintegerAccessor(&BulkSendApplication::m_rtoRank),
                                        MakeUintegerChecker<uint8_t>())
                          .AddAttribute("EnableRTORank", "Enable RTORank",
                                        BooleanValue(false),
                                        MakeBooleanAccessor(&BulkSendApplication::m_enableRTORank),
                                        MakeBooleanChecker())
                          .AddAttribute("EnableSizeRank", "Enable SizeRank",
                                        BooleanValue(false),
                                        MakeBooleanAccessor(&BulkSendApplication::m_enableSizeRank),
                                        MakeBooleanChecker())
                          .AddAttribute("Scheduler", "Enable Cacheable",
                                        UintegerValue(0),
                                        MakeUintegerAccessor(&BulkSendApplication::m_scheduler),
                                        MakeUintegerChecker<uint32_t>())
                          .AddAttribute("CacheBand", "Cache Band",
                                        UintegerValue(2),
                                        MakeUintegerAccessor(&BulkSendApplication::m_cacheBand),
                                        MakeUintegerChecker<uint32_t>())
                          .AddAttribute("SizeRank", "The rank of flow size.",
                                        UintegerValue(0),
                                        MakeUintegerAccessor(&BulkSendApplication::m_sizeRank),
                                        MakeUintegerChecker<uint8_t>())
                          .AddAttribute("CDFType", "The rank of flow size.",
                                        UintegerValue(0),
                                        MakeUintegerAccessor(&BulkSendApplication::m_cdfType),
                                        MakeUintegerChecker<uint32_t>())
                          .AddAttribute("RTO", "Propagation delay through the channel",
                                        TimeValue(MilliSeconds(10)),
                                        MakeTimeAccessor(&BulkSendApplication::m_minRto),
                                        MakeTimeChecker())
                          .AddAttribute("ReTxThre", "The rank of flow size.",
                                        UintegerValue(1000),
                                        MakeUintegerAccessor(&BulkSendApplication::m_reTxThre),
                                        MakeUintegerChecker<uint32_t>())
                          .AddAttribute("Load", "LOAD",
                                        UintegerValue(1),
                                        MakeUintegerAccessor(&BulkSendApplication::m_load),
                                        MakeUintegerChecker<uint32_t>());
  return tid;
}

BulkSendApplication::BulkSendApplication()
    : m_socket(0),
      m_connected(false),
      m_totBytes(0),
      m_isDelay(false),
      m_accumPackets(0),
      m_enableRTORank(false),
      m_enableSizeRank(false),
      m_rtoRank(0),
      m_sizeRank(0),
      m_cacheBand(2),
      m_reTxThre(1000),
      m_load(1)
{
  NS_LOG_FUNCTION(this);
}

BulkSendApplication::~BulkSendApplication()
{
  NS_LOG_FUNCTION(this);
}

void BulkSendApplication::SetMaxBytes(uint32_t maxBytes)
{
  NS_LOG_FUNCTION(this << maxBytes);
  m_maxBytes = maxBytes;
}

Ptr<Socket>
BulkSendApplication::GetSocket(void) const
{
  NS_LOG_FUNCTION(this);
  return m_socket;
}

void BulkSendApplication::DoDispose(void)
{
  NS_LOG_FUNCTION(this);

  m_socket = 0;
  // chain up
  Application::DoDispose();
}

// Application Methods
void BulkSendApplication::StartApplication(void) // Called at time specified by Start
{
  NS_LOG_FUNCTION(this);

  // Create the socket if not already
  //如果不存在则创建Socket
  if (!m_socket)
  {
    m_socket = Socket::CreateSocket(GetNode(), m_tid);
    /*********************************************************************************/
    if (m_scheduler == 0 && m_enableRTORank)
    {
      Ptr<TcpSocketBase> tsb = DynamicCast<TcpSocketBase>(m_socket);
      if (tsb)
      {
        if (m_rtoRank >= m_cacheBand)
          tsb->SetNewArgu(true, m_reTxThre);
        else
          tsb->SetMinRto(m_minRto);
      }
    }
    /*********************************************************************************/
    // Fatal error if socket type is not NS3_SOCK_STREAM or NS3_SOCK_SEQPACKET
    //如果使用的不是TCP
    if (m_socket->GetSocketType() != Socket::NS3_SOCK_STREAM &&
        m_socket->GetSocketType() != Socket::NS3_SOCK_SEQPACKET)
    {
      NS_FATAL_ERROR("Using BulkSend with an incompatible socket type. "
                     "BulkSend requires SOCK_STREAM or SOCK_SEQPACKET. "
                     "In other words, use TCP instead of UDP.");
    }
    //进行绑定
    if (Inet6SocketAddress::IsMatchingType(m_peer))
    {
      m_socket->Bind6();
    }
    else if (InetSocketAddress::IsMatchingType(m_peer))
    {
      m_socket->Bind();
    }

    m_socket->Connect(m_peer);
    //关闭接收数据
    m_socket->ShutdownRecv();
    //分别设置成功连接与连接失败时的调用
    m_socket->SetConnectCallback(
        MakeCallback(&BulkSendApplication::ConnectionSucceeded, this),
        MakeCallback(&BulkSendApplication::ConnectionFailed, this));
    //Notify application when space in transmit buffer is added.
    //当tx buffer有空间时调用
    m_socket->SetSendCallback(
        MakeCallback(&BulkSendApplication::DataSend, this));
  }
  //如果连接成功，则发送数据
  if (m_connected)
  {
    NS_LOG_DEBUG("Connected Success!");
    SendData();
  }
}

//关闭应应用
void BulkSendApplication::StopApplication(void) // Called at time specified by Stop
{
  NS_LOG_FUNCTION(this);

  if (m_socket != 0)
  {
    m_socket->Close();
    m_connected = false;
  }
  else
  {
    NS_LOG_WARN("BulkSendApplication found null socket to close in StopApplication");
  }
}

// Private helpers
//发送数据
void BulkSendApplication::SendData(void)
{
  NS_LOG_FUNCTION(this);
  //1314000, 1898000, 2336000 , 2628000, 2920000, 3212000, 3358000,
  //2e4, 5e4, 8e4, 2e5, 1e6, 2e6, 5e6,
  //1175300, 1606000, 2117000, 17762360, 18980000, 30952000, 37960000

  // uniform seed: 1575299868
  //                           0.5       0.6       0.7        0.8        0.9
  double pias_thresh[3][9][7] = {
                        {1059*1460, 1412*1460, 1643*1460, 1869*1460, 2008*1460, 2115*1460, 2184*1460, 
                          956*1460, 1381*1460, 1718*1460, 2028*1460, 2297*1460, 2551*1460, 2660*1460, 
                          999*1460, 1305*1460, 1564*1460, 1763*1460, 1956*1460, 2149*1460, 2309*1460, 
                          909*1460, 1329*1460, 1648*1460, 1960*1460, 2143*1460, 2337*1460, 2484*1460, 
                          1059*1460, 1412*1460, 1643*1460, 1869*1460, 2008*1460, 2115*1460, 2184*1460, 
                          956*1460, 1381*1460, 1718*1460, 2028*1460, 2297*1460, 2551*1460, 2660*1460, 
                          999*1460, 1305*1460, 1564*1460, 1763*1460, 1956*1460, 2149*1460, 2309*1460, 
                          909*1460, 1329*1460, 1648*1460, 1960*1460, 2143*1460, 2337*1460, 2484*1460, 
                          759*1460, 1132*1460, 1456*1460, 1737*1460, 2010*1460, 2199*1460, 2325*1460}, 
                        {805*1460, 1106*1460, 1401*1460, 10693*1460, 11970*1460, 21162*1460, 22272*1460, 
                          840*1460, 1232*1460, 1617*1460, 11950*1460, 12238*1460, 21494*1460, 25720*1460, 
                          907*1460, 1301*1460, 1619*1460, 12166*1460, 12915*1460, 21313*1460, 26374*1460, 
                          745*1460, 1083*1460, 1391*1460, 13689*1460, 14936*1460, 21149*1460, 27245*1460, 
                          805*1460, 1106*1460, 1401*1460, 10693*1460, 11970*1460, 21162*1460, 22272*1460, 
                          840*1460, 1232*1460, 1617*1460, 11950*1460, 12238*1460, 21494*1460, 25720*1460, 
                          907*1460, 1301*1460, 1619*1460, 12166*1460, 12915*1460, 21313*1460, 26374*1460, 
                          745*1460, 1083*1460, 1391*1460, 13689*1460, 14936*1460, 21149*1460, 27245*1460, 
                          750*1460, 1083*1460, 1416*1460, 13705*1460, 14952*1460, 21125*1460, 28253*1460}, 
                        {347*1460, 2860*1460, 3662*1460, 5450*1460, 6820*1460, 6820*1460, 6850*1460, 
                          1420*1460, 3117*1460, 6850*1460, 6850*1460, 6850*1460, 6850*1460, 6850*1460, 
                          1246*1460, 3632*1460, 5869*1460, 6850*1460, 6850*1460, 6850*1460, 6850*1460, 
                          2234*1460, 3417*1460, 4886*1460, 5857*1460, 5857*1460, 6850*1460, 6850*1460, 
                          347*1460, 2860*1460, 3662*1460, 5450*1460, 6820*1460, 6820*1460, 6850*1460, 
                          1420*1460, 3117*1460, 6850*1460, 6850*1460, 6850*1460, 6850*1460, 6850*1460, 
                          1246*1460, 3632*1460, 5869*1460, 6850*1460, 6850*1460, 6850*1460, 6850*1460, 
                          2234*1460, 3417*1460, 4886*1460, 5857*1460, 5857*1460, 6850*1460, 6850*1460, 
                          3243*1460, 4527*1460, 6239*1460, 6239*1460, 6239*1460, 6850*1460, 6850*1460} //uniform seed: 1575299868
                            
  };

  //如果允许一直发送或还没达到最大的发送量，则继续发送。
  while (m_maxBytes == 0 || m_totBytes < m_maxBytes)
  {
    //如果在delay期间则跳出
    if (m_isDelay)
    {
      break;
    }

    //确定发送量
    // Time to send more
    uint32_t toSend = m_sendSize;
    // Make sure we don't send too many
    if (m_maxBytes > 0)
    {
      toSend = std::min(m_sendSize, m_maxBytes - m_totBytes);
    }
    //edit
    NS_LOG_DEBUG("sending packet size is " << toSend);
    //创建一个toSend大小的包
    Ptr<Packet> packet = Create<Packet>(toSend);
    //回调
    m_txTrace(packet);
    /*********************************************************************************/
    int inx = m_cdfType;
    if (m_enableSizeRank)
    {
      if(m_scheduler == 1)
      {
        for(uint8_t i = 6;i>=0;i--)
        {
          if(m_totBytes >= pias_thresh[inx][m_load-1][i])
          {
            m_sizeRank = i+1;
            break;
          }
          if(i==0) m_sizeRank = 0;
        }
      }
    }
    if (m_enableRTORank || m_enableSizeRank)
    {
      //if (m_rtoRank == 2) m_sizeRank = 0;
      RtoPriTag rtoPriTag(m_rtoRank, m_sizeRank);
      packet->AddPacketTag(rtoPriTag);
    }
    /*********************************************************************************/
    //返回发送包的字节数
    int actual = m_socket->Send(packet);
    //如果发送成功，则增加发送的字节和包数
    if (actual > 0)
    {
      m_totBytes += actual;
      m_accumPackets++;
    }

    // We exit this loop when actual < toSend as the send side
    // buffer is full. The "DataSent" callback will pop when
    // some buffer space has freed ip.
    //如果发送的量没达到设定的量则说明buffer满了。
    if ((unsigned)actual != toSend)
    {
      break;
    }
    //如果有设置delayThresh则当达到后就行m_delayTime后再进行发送，此时将m_isDleay设置为true
    if (m_delayThresh != 0 && m_accumPackets > m_delayThresh)
    {
      m_isDelay = true;
      Simulator::Schedule(m_delayTime, &BulkSendApplication::ResumeSend, this);
    }
  }
  // Check if time to close (all sent)
  //如果已经发送够字节则停止
  if (m_totBytes == m_maxBytes && m_connected)
  {
    m_socket->Close();
    m_connected = false;
  }
}
//连接成功则将m_connected设置为true
void BulkSendApplication::ConnectionSucceeded(Ptr<Socket> socket)
{
  NS_LOG_FUNCTION(this << socket);
  //EDIT
  NS_LOG_DEBUG("BulkSendApplication Connection succeeded");
  m_connected = true;
  SendData();
}
//若没有连接成功则提示false
void BulkSendApplication::ConnectionFailed(Ptr<Socket> socket)
{
  NS_LOG_FUNCTION(this << socket);
  NS_LOG_DEBUG("BulkSendApplication, Connection Failed");
}

void BulkSendApplication::DataSend(Ptr<Socket>, uint32_t)
{
  NS_LOG_FUNCTION(this);

  if (m_connected)
  { // Only send new data if the connection has completed
    SendData();
  }
}
//delay时间到后继续发送，并m_isDelay设置为false,并将m_accumPackets设置为0
void BulkSendApplication::ResumeSend(void)
{
  NS_LOG_FUNCTION(this);

  m_isDelay = false;
  m_accumPackets = 0;

  if (m_connected)
  {
    SendData();
  }
}

/***************************************************************/
void BulkSendApplication::SetRtoRank(uint8_t rtoRank)
{
  NS_LOG_FUNCTION(this);
  m_rtoRank = rtoRank;
}

void BulkSendApplication::SetSizeRank(uint8_t sizeRank)
{
  NS_LOG_FUNCTION(this);
  m_sizeRank = sizeRank;
}
/***************************************************************/

} // Namespace ns3
