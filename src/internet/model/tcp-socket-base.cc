/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2007 Georgia Tech Research Corporation
 * Copyright (c) 2010 Adrian Sai-wah Tam
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
 * Author: Adrian Sai-wah Tam <adrian.sw.tam@gmail.com>
 */

#define NS_LOG_APPEND_CONTEXT                          \
  if (m_node)                                          \
  {                                                    \
    std::clog << " [node " << m_node->GetId() << "] "; \
  }

#include "ns3/abort.h"
#include "ns3/node.h"
#include "ns3/inet-socket-address.h"
#include "ns3/inet6-socket-address.h"
#include "ns3/log.h"
#include "ns3/ipv4.h"
#include "ns3/ipv6.h"
#include "ns3/ipv4-interface-address.h"
#include "ns3/ipv4-route.h"
#include "ns3/ipv6-route.h"
#include "ns3/ipv4-routing-protocol.h"
#include "ns3/ipv6-routing-protocol.h"
#include "ns3/simulation-singleton.h"
#include "ns3/simulator.h"
#include "ns3/packet.h"
#include "ns3/uinteger.h"
#include "ns3/double.h"
#include "ns3/pointer.h"
#include "ns3/trace-source-accessor.h"
#include "tcp-socket-base.h"
#include "tcp-l4-protocol.h"
#include "ipv4-end-point.h"
#include "ipv6-end-point.h"
#include "ipv6-l3-protocol.h"
#include "tcp-header.h"
#include "tcp-option-winscale.h"
#include "tcp-option-ts.h"
#include "rtt-estimator.h"
#include "urge-tag.h"

#include "ipv4-ecn-tag.h"
#include "ns3/flow-id-tag.h"
#include "ns3/ipv4-xpath-tag.h"
#include "ns3/tcp-tlb-tag.h"
#include "ns3/ipv4-clove.h"
#include "ns3/tcp-clove-tag.h"
#include "ns3/hash.h"

#include <math.h>
#include <algorithm>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("TcpSocketBase");

NS_OBJECT_ENSURE_REGISTERED(TcpSocketBase);

//返回TypeId
TypeId
TcpSocketBase::GetTypeId(void)
{
  static TypeId tid = TypeId("ns3::TcpSocketBase")
                          .SetParent<TcpSocket>()
                          .SetGroupName("Internet")
                          .AddConstructor<TcpSocketBase>()
                          //    .AddAttribute ("TcpState", "State in TCP state machine",
                          //                   TypeId::ATTR_GET,
                          //                   EnumValue (CLOSED),
                          //                   MakeEnumAccessor (&TcpSocketBase::m_state),
                          //                   MakeEnumChecker (CLOSED, "Closed"))
                          .AddAttribute("MaxSegLifetime",
                                        "Maximum segment lifetime in seconds, use for TIME_WAIT state transition to CLOSED state",
                                        DoubleValue(120), /* RFC793 says MSL=2 minutes*/
                                        MakeDoubleAccessor(&TcpSocketBase::m_msl),
                                        MakeDoubleChecker<double>(0))
                          .AddAttribute("MaxWindowSize", "Max size of advertised window",
                                        UintegerValue(65535),
                                        MakeUintegerAccessor(&TcpSocketBase::m_maxWinSize),
                                        MakeUintegerChecker<uint16_t>())
                          .AddAttribute("IcmpCallback", "Callback invoked whenever an icmp error is received on this socket.",
                                        CallbackValue(),
                                        MakeCallbackAccessor(&TcpSocketBase::m_icmpCallback),
                                        MakeCallbackChecker())
                          .AddAttribute("IcmpCallback6", "Callback invoked whenever an icmpv6 error is received on this socket.",
                                        CallbackValue(),
                                        MakeCallbackAccessor(&TcpSocketBase::m_icmpCallback6),
                                        MakeCallbackChecker())
                          .AddAttribute("WindowScaling", "Enable or disable Window Scaling option",
                                        BooleanValue(true),
                                        MakeBooleanAccessor(&TcpSocketBase::m_winScalingEnabled),
                                        MakeBooleanChecker())
                          .AddAttribute("Timestamp", "Enable or disable Timestamp option",
                                        BooleanValue(true),
                                        MakeBooleanAccessor(&TcpSocketBase::m_timestampEnabled),
                                        MakeBooleanChecker())
                          .AddAttribute("MinRto",
                                        "Minimum retransmit timeout value",
                                        TimeValue(Seconds(1.0)), // RFC 6298 says min RTO=1 sec, but Linux uses 200ms.
                                        // See http://www.postel.org/pipermail/end2end-interest/2004-November/004402.html
                                        MakeTimeAccessor(&TcpSocketBase::SetMinRto,
                                                         &TcpSocketBase::GetMinRto),
                                        MakeTimeChecker())
                          .AddAttribute("ClockGranularity",
                                        "Clock Granularity used in RTO calculations",
                                        TimeValue(MilliSeconds(1)), // RFC6298 suggest to use fine clock granularity
                                        MakeTimeAccessor(&TcpSocketBase::SetClockGranularity,
                                                         &TcpSocketBase::GetClockGranularity),
                                        MakeTimeChecker())
                          .AddAttribute("TxBuffer",
                                        "TCP Tx buffer",
                                        PointerValue(),
                                        MakePointerAccessor(&TcpSocketBase::GetTxBuffer),
                                        MakePointerChecker<TcpTxBuffer>())
                          .AddAttribute("RxBuffer",
                                        "TCP Rx buffer",
                                        PointerValue(),
                                        MakePointerAccessor(&TcpSocketBase::GetRxBuffer),
                                        MakePointerChecker<TcpRxBuffer>())
                          .AddAttribute("ReTxThreshold", "Threshold for fast retransmit",
                                        UintegerValue(3),
                                        MakeUintegerAccessor(&TcpSocketBase::m_retxThresh),
                                        MakeUintegerChecker<uint32_t>())
                          .AddAttribute("LimitedTransmit", "Enable limited transmit",
                                        BooleanValue(true),
                                        MakeBooleanAccessor(&TcpSocketBase::m_limitedTx),
                                        MakeBooleanChecker())
                          .AddAttribute("ECN", "Enable ECN capable connection",
                                        BooleanValue(true),
                                        MakeBooleanAccessor(&TcpSocketBase::m_ecn),
                                        MakeBooleanChecker())
                          .AddAttribute("ResequenceBuffer", "Enable Resequence Buffer",
                                        BooleanValue(false),
                                        MakeBooleanAccessor(&TcpSocketBase::m_resequenceBufferEnabled),
                                        MakeBooleanChecker())
                          .AddAttribute("FlowBender", "Enable Flow Bender",
                                        BooleanValue(false),
                                        MakeBooleanAccessor(&TcpSocketBase::m_flowBenderEnabled),
                                        MakeBooleanChecker())
                          .AddAttribute("TLB", "Enable the TLB",
                                        BooleanValue(false),
                                        MakeBooleanAccessor(&TcpSocketBase::m_TLBEnabled),
                                        MakeBooleanChecker())
                          .AddAttribute("TLBReverseACK", "Enable the TLB Reverse ACK path selection",
                                        BooleanValue(false),
                                        MakeBooleanAccessor(&TcpSocketBase::m_TLBReverseAckEnabled),
                                        MakeBooleanChecker())
                          .AddAttribute("Clove", "Enable the Clove",
                                        BooleanValue(false),
                                        MakeBooleanAccessor(&TcpSocketBase::m_CloveEnabled),
                                        MakeBooleanChecker())
                          .AddAttribute("Pause", "Whether TCP should pause in FlowBender & TLB",
                                        BooleanValue(false),
                                        MakeBooleanAccessor(&TcpSocketBase::m_isPauseEnabled),
                                        MakeBooleanChecker())
                          .AddAttribute("ResequenceBufferPointer", "Resequence Buffer Pointer",
                                        PointerValue(),
                                        MakePointerAccessor(&TcpSocketBase::GetResequenceBuffer),
                                        MakePointerChecker<TcpResequenceBuffer>())
                          .AddAttribute("UrgeSend", "Whether TCP should pause in FlowBender & TLB",
                                        BooleanValue(false),
                                        MakeBooleanAccessor(&TcpSocketBase::m_enableUrgeSend),
                                        MakeBooleanChecker())
                          .AddTraceSource("RTO",
                                          "Retransmission timeout",
                                          MakeTraceSourceAccessor(&TcpSocketBase::m_rto),
                                          "ns3::Time::TracedValueCallback")
                          .AddTraceSource("RTT",
                                          "Last RTT sample",
                                          MakeTraceSourceAccessor(&TcpSocketBase::m_lastRtt),
                                          "ns3::Time::TracedValueCallback")
                          .AddTraceSource("NextTxSequence",
                                          "Next sequence number to send (SND.NXT)",
                                          MakeTraceSourceAccessor(&TcpSocketBase::m_nextTxSequence),
                                          "ns3::SequenceNumber32TracedValueCallback")
                          .AddTraceSource("HighestSequence",
                                          "Highest sequence number ever sent in socket's life time",
                                          MakeTraceSourceAccessor(&TcpSocketBase::m_highTxMark),
                                          "ns3::SequenceNumber32TracedValueCallback")
                          .AddTraceSource("State",
                                          "TCP state",
                                          MakeTraceSourceAccessor(&TcpSocketBase::m_state),
                                          "ns3::TcpStatesTracedValueCallback")
                          .AddTraceSource("CongState",
                                          "TCP Congestion machine state",
                                          MakeTraceSourceAccessor(&TcpSocketBase::m_congStateTrace),
                                          "ns3::TcpSocketState::TcpCongStatesTracedValueCallback")
                          .AddTraceSource("RWND",
                                          "Remote side's flow control window",
                                          MakeTraceSourceAccessor(&TcpSocketBase::m_rWnd),
                                          "ns3::TracedValueCallback::Uint32")
                          .AddTraceSource("BytesInFlight",
                                          "Socket estimation of bytes in flight",
                                          MakeTraceSourceAccessor(&TcpSocketBase::m_bytesInFlight),
                                          "ns3::TracedValueCallback::Uint32")
                          .AddTraceSource("HighestRxSequence",
                                          "Highest sequence number received from peer",
                                          MakeTraceSourceAccessor(&TcpSocketBase::m_highRxMark),
                                          "ns3::SequenceNumber32TracedValueCallback")
                          .AddTraceSource("HighestRxAck",
                                          "Highest ack received from peer",
                                          MakeTraceSourceAccessor(&TcpSocketBase::m_highRxAckMark),
                                          "ns3::SequenceNumber32TracedValueCallback")
                          .AddTraceSource("CongestionWindow",
                                          "The TCP connection's congestion window",
                                          MakeTraceSourceAccessor(&TcpSocketBase::m_cWndTrace),
                                          "ns3::TracedValueCallback::Uint32")
                          .AddTraceSource("SlowStartThreshold",
                                          "TCP slow start threshold (bytes)",
                                          MakeTraceSourceAccessor(&TcpSocketBase::m_ssThTrace),
                                          "ns3::TracedValueCallback::Uint32")
                          .AddTraceSource("Tx",
                                          "Send tcp packet to IP protocol",
                                          MakeTraceSourceAccessor(&TcpSocketBase::m_txTrace),
                                          "ns3::TcpSocketBase::TcpTxRxTracedCallback")
                          .AddTraceSource("Rx",
                                          "Receive tcp packet from IP protocol",
                                          MakeTraceSourceAccessor(&TcpSocketBase::m_rxTrace),
                                          "ns3::TcpSocketBase::TcpTxRxTracedCallback");
  return tid;
}

TypeId
TcpSocketBase::GetInstanceTypeId() const
{
  return TcpSocketBase::GetTypeId();
}

//返回TypeId,这里是TcpSocketState中的函数
TypeId
TcpSocketState::GetTypeId(void)
{
  static TypeId tid = TypeId("ns3::TcpSocketState")
                          .SetParent<Object>()
                          .SetGroupName("Internet")
                          .AddConstructor<TcpSocketState>()
                          .AddTraceSource("CongestionWindow",
                                          "The TCP connection's congestion window",
                                          MakeTraceSourceAccessor(&TcpSocketState::m_cWnd),
                                          "ns3::TracedValue::Uint32Callback")
                          .AddTraceSource("SlowStartThreshold",
                                          "TCP slow start threshold (bytes)",
                                          MakeTraceSourceAccessor(&TcpSocketState::m_ssThresh),
                                          "ns3::TracedValue::Uint32Callback")
                          .AddTraceSource("CongState",
                                          "TCP Congestion machine state",
                                          MakeTraceSourceAccessor(&TcpSocketState::m_congState),
                                          "ns3::TracedValue::TcpCongStatesTracedValueCallback");
  return tid;
}

//初始化变量
TcpSocketState::TcpSocketState(void)
    : Object(),
      m_cWnd(0),
      m_ssThresh(0),
      m_initialCWnd(0),
      m_initialSsThresh(0),
      m_segmentSize(0),
      m_ecnConn(true),
      m_ecnSeen(false),
      m_demandCWR(false),
      m_queueCWR(false),
      m_CWRSentSeq(0),
      m_congState(CA_OPEN)
{
}

TcpSocketState::TcpSocketState(const TcpSocketState &other)
    : Object(other),
      m_cWnd(other.m_cWnd),
      m_ssThresh(other.m_ssThresh),
      m_initialCWnd(other.m_initialCWnd),
      m_initialSsThresh(other.m_initialSsThresh),
      m_segmentSize(other.m_segmentSize),
      m_ecnConn(other.m_ecnConn),
      m_ecnSeen(other.m_ecnSeen),
      m_demandCWR(other.m_demandCWR),
      m_queueCWR(other.m_queueCWR),
      m_CWRSentSeq(0),
      m_congState(other.m_congState)
{
}

//定义几个TCPSocket的状态，其分别对应了其名字
const char *const
    TcpSocketState::TcpCongStateName[TcpSocketState::CA_LAST_STATE] =
        {
            "CA_OPEN", "CA_DISORDER", "CA_CWR", "CA_RECOVERY", "CA_LOSS"};

//初始化TCPSocketBase
TcpSocketBase::TcpSocketBase(void)
    : TcpSocket(),
      m_retxEvent(),
      m_urgePktEvent(), //Add by myself
      m_lastAckEvent(),
      m_delAckEvent(),
      m_persistEvent(),
      m_timewaitEvent(),
      m_dupAckCount(0),
      m_delAckCount(0),
      m_delAckMaxCount(0),
      m_noDelay(false),
      m_synCount(0),
      m_synRetries(0),
      m_dataRetrCount(0),
      m_dataRetries(0),
      m_rto(Seconds(0.0)),
      m_minRto(Time::Max()),
      m_clockGranularity(Seconds(0.001)),
      m_lastRtt(Seconds(0.0)),
      m_delAckTimeout(Seconds(0.0)),
      m_persistTimeout(Seconds(0.0)),
      m_cnTimeout(Seconds(0.0)),
      m_endPoint(0),
      m_endPoint6(0),
      m_node(0),
      m_tcp(0),
      m_rtt(0),
      m_nextTxSequence(0), // Change this for non-zero initial sequence number
      m_highTxMark(0),
      m_state(CLOSED),
      m_errno(ERROR_NOTERROR),
      m_closeNotified(false),
      m_closeOnEmpty(false),
      m_shutdownSend(false),
      m_shutdownRecv(false),
      m_connected(false),
      m_msl(0),
      m_maxWinSize(0),
      m_rWnd(0),
      m_highRxMark(0),
      m_highTxAck(0),
      m_highRxAckMark(0),
      m_bytesAckedNotProcessed(0),
      m_bytesInFlight(0),
      m_winScalingEnabled(false),
      m_rcvWindShift(0),
      m_sndWindShift(0),
      m_timestampEnabled(true),
      m_timestampToEcho(0),
      m_sendPendingDataEvent(),
      m_recover(0), // Set to the initial sequence number
      m_retxThresh(3),
      m_limitedTx(false),
      m_retransOut(0),
      m_ecn(true),
      m_resequenceBufferEnabled(false),
      m_flowBenderEnabled(false),
      // TLB
      m_TLBEnabled(false),
      m_TLBSendSide(false),
      m_piggybackTLBInfo(false),
      m_TLBReverseAckEnabled(false),
      // Clove
      m_CloveEnabled(false),
      m_CloveSendSide(false),
      m_piggybackCloveInfo(false),
      // Pause
      m_isPauseEnabled(false),
      m_isPause(false),
      m_oldPath(0),
      m_congestionControl(0),
      m_isFirstPartialAck(true),
      m_retransmit_time(0), //add by myself
      m_retranDupack(0),
      m_retranRec(0),
      m_retranTimeout(0),
      m_retranTime(0),
      m_urgeSendNum(0),
      m_urgeNum(10),
      m_flowId(0),
      enablePrt(false),
      m_cacheable(false),
      m_enableUrgeSend(false)
{
  NS_LOG_FUNCTION(this);
  m_rxBuffer = CreateObject<TcpRxBuffer>();
  m_txBuffer = CreateObject<TcpTxBuffer>();
  m_tcb = CreateObject<TcpSocketState>();

  if (m_ecn)
  {
    m_tcb->m_ecnConn = true;
  }
  else
  {
    m_tcb->m_ecnConn = false;
  }

  // Resequence Buffer support
  m_resequenceBuffer = CreateObject<TcpResequenceBuffer>();
  m_resequenceBuffer->SetTcp(this);

  // Flow Bender support
  m_flowBender = CreateObject<TcpFlowBender>();

  // Pause support
  m_pauseBuffer = CreateObject<TcpPauseBuffer>();

  bool ok;

  ok = m_tcb->TraceConnectWithoutContext("CongestionWindow",
                                         MakeCallback(&TcpSocketBase::UpdateCwnd, this));
  NS_ASSERT(ok == true);

  ok = m_tcb->TraceConnectWithoutContext("SlowStartThreshold",
                                         MakeCallback(&TcpSocketBase::UpdateSsThresh, this));
  NS_ASSERT(ok == true);

  ok = m_tcb->TraceConnectWithoutContext("CongState",
                                         MakeCallback(&TcpSocketBase::UpdateCongState, this));
  NS_ASSERT(ok == true);
}

//根据其它变量初始化
TcpSocketBase::TcpSocketBase(const TcpSocketBase &sock)
    : TcpSocket(sock),
      //copy object::m_tid and socket::callbacks
      m_dupAckCount(sock.m_dupAckCount),
      m_delAckCount(0),
      m_delAckMaxCount(sock.m_delAckMaxCount),
      m_noDelay(sock.m_noDelay),
      m_synCount(sock.m_synCount),
      m_synRetries(sock.m_synRetries),
      m_dataRetrCount(sock.m_dataRetrCount),
      m_dataRetries(sock.m_dataRetries),
      m_rto(sock.m_rto),
      m_minRto(sock.m_minRto),
      m_clockGranularity(sock.m_clockGranularity),
      m_lastRtt(sock.m_lastRtt),
      m_delAckTimeout(sock.m_delAckTimeout),
      m_persistTimeout(sock.m_persistTimeout),
      m_cnTimeout(sock.m_cnTimeout),
      m_endPoint(0),
      m_endPoint6(0),
      m_node(sock.m_node),
      m_tcp(sock.m_tcp),
      m_nextTxSequence(sock.m_nextTxSequence),
      m_highTxMark(sock.m_highTxMark),
      m_state(sock.m_state),
      m_errno(sock.m_errno),
      m_closeNotified(sock.m_closeNotified),
      m_closeOnEmpty(sock.m_closeOnEmpty),
      m_shutdownSend(sock.m_shutdownSend),
      m_shutdownRecv(sock.m_shutdownRecv),
      m_connected(sock.m_connected),
      m_msl(sock.m_msl),
      m_maxWinSize(sock.m_maxWinSize),
      m_rWnd(sock.m_rWnd),
      m_highRxMark(sock.m_highRxMark),
      m_highRxAckMark(sock.m_highRxAckMark),
      m_bytesAckedNotProcessed(sock.m_bytesAckedNotProcessed),
      m_bytesInFlight(sock.m_bytesInFlight),
      m_winScalingEnabled(sock.m_winScalingEnabled),
      m_rcvWindShift(sock.m_rcvWindShift),
      m_sndWindShift(sock.m_sndWindShift),
      m_timestampEnabled(sock.m_timestampEnabled),
      m_timestampToEcho(sock.m_timestampToEcho),
      m_recover(sock.m_recover),
      m_retxThresh(sock.m_retxThresh),
      m_limitedTx(sock.m_limitedTx),
      m_retransOut(sock.m_retransOut),
      m_ecn(sock.m_ecn),
      m_resequenceBufferEnabled(sock.m_resequenceBufferEnabled),
      m_flowBenderEnabled(sock.m_flowBenderEnabled),
      // TLB
      m_TLBEnabled(sock.m_TLBEnabled),
      m_TLBSendSide(false),
      m_piggybackTLBInfo(false),
      m_TLBReverseAckEnabled(sock.m_TLBReverseAckEnabled),
      // Clove
      m_CloveEnabled(sock.m_CloveEnabled),
      m_CloveSendSide(false),
      m_piggybackCloveInfo(false),
      // Pause
      m_isPauseEnabled(sock.m_isPauseEnabled),
      m_isPause(false),
      m_oldPath(0),
      m_isFirstPartialAck(sock.m_isFirstPartialAck),
      m_txTrace(sock.m_txTrace),
      m_rxTrace(sock.m_rxTrace),
      m_retransmit_time(0), //add by myself
      m_retranDupack(0),
      m_retranRec(0),
      m_retranTimeout(0),
      m_retranTime(0),
      m_urgeSendNum(0),
      m_urgeNum(10),
      m_flowId(0),
      m_cacheable(sock.m_cacheable),
      m_enableUrgeSend(sock.m_enableUrgeSend)
{
  NS_LOG_FUNCTION(this);
  NS_LOG_LOGIC("Invoked the copy constructor");
  // Copy the rtt estimator if it is set
  if (sock.m_rtt)
  {
    m_rtt = sock.m_rtt->Copy();
  }
  // Reset all callbacks to null
  Callback<void, Ptr<Socket>> vPS = MakeNullCallback<void, Ptr<Socket>>();
  Callback<void, Ptr<Socket>, const Address &> vPSA = MakeNullCallback<void, Ptr<Socket>, const Address &>();
  Callback<void, Ptr<Socket>, uint32_t> vPSUI = MakeNullCallback<void, Ptr<Socket>, uint32_t>();
  SetConnectCallback(vPS, vPS);
  SetDataSentCallback(vPSUI);
  SetSendCallback(vPSUI);
  SetRecvCallback(vPS);
  m_txBuffer = CopyObject(sock.m_txBuffer);
  m_rxBuffer = CopyObject(sock.m_rxBuffer);
  m_tcb = CopyObject(sock.m_tcb);

  if (m_ecn)
  {
    m_tcb->m_ecnConn = true;
  }
  else
  {
    m_tcb->m_ecnConn = false;
  }

  // Resequence Buffer support
  m_resequenceBuffer = CreateObject<TcpResequenceBuffer>();
  m_resequenceBuffer->m_tcpRBFlush = sock.m_resequenceBuffer->m_tcpRBFlush;
  m_resequenceBuffer->m_tcpRBBuffer = sock.m_resequenceBuffer->m_tcpRBBuffer;
  m_resequenceBuffer->SetTcp(this);

  // Flow Bender support
  m_flowBender = CreateObject<TcpFlowBender>();

  // Pause support
  m_pauseBuffer = CreateObject<TcpPauseBuffer>();

  if (sock.m_congestionControl)
  {
    m_congestionControl = sock.m_congestionControl->Fork();
  }

  bool ok;

  ok = m_tcb->TraceConnectWithoutContext("CongestionWindow",
                                         MakeCallback(&TcpSocketBase::UpdateCwnd, this));
  NS_ASSERT(ok == true);

  ok = m_tcb->TraceConnectWithoutContext("SlowStartThreshold",
                                         MakeCallback(&TcpSocketBase::UpdateSsThresh, this));
  NS_ASSERT(ok == true);

  ok = m_tcb->TraceConnectWithoutContext("CongState",
                                         MakeCallback(&TcpSocketBase::UpdateCongState, this));
  NS_ASSERT(ok == true);
}

//清理
TcpSocketBase::~TcpSocketBase(void)
{
  NS_LOG_FUNCTION(this);

  if (m_retransmit_time)
  {
    if (m_cacheable)
      printf("RTO 2: %u :", m_flowId);
    else
      printf("RTO 1: %u :", m_flowId);
    //NS_LOG_DEBUG(
    std::cout << " retransmit time is " << m_retransmit_time << " retranDupack: " << m_retranDupack
              << " m_retranRec: " << m_retranRec << " m_retranRTO: " << m_retranTimeout << " m_retranTime: " << m_retranTime << '\n'; //);
  }
  m_node = 0;
  if (m_endPoint != 0)
  {
    NS_ASSERT(m_tcp != 0);
    /*
       * Upon Bind, an Ipv4Endpoint is allocated and set to m_endPoint, and
       * DestroyCallback is set to TcpSocketBase::Destroy. If we called
       * m_tcp->DeAllocate, it wil destroy its Ipv4EndpointDemux::DeAllocate,
       * which in turn destroys my m_endPoint, and in turn invokes
       * TcpSocketBase::Destroy to nullify m_node, m_endPoint, and m_tcp.
       */
    NS_ASSERT(m_endPoint != 0);
    m_tcp->DeAllocate(m_endPoint);
    NS_ASSERT(m_endPoint == 0);
  }
  if (m_endPoint6 != 0)
  {
    NS_ASSERT(m_tcp != 0);
    NS_ASSERT(m_endPoint6 != 0);
    m_tcp->DeAllocate(m_endPoint6);
    NS_ASSERT(m_endPoint6 == 0);
  }
  m_tcp = 0;
  CancelAllTimers();
}

/* Associate a node with this TCP socket */
// 给这个TCP Socket分配一个节点
void TcpSocketBase::SetNode(Ptr<Node> node)
{
  m_node = node;
}

/* Associate the L4 protocol (e.g. mux/demux) with this socket */
//给这个TcpSockBase分配一个Tcp
void TcpSocketBase::SetTcp(Ptr<TcpL4Protocol> tcp)
{
  m_tcp = tcp;
}

/* Set an RTT estimator with this socket */
// 分配RTT估计器
void TcpSocketBase::SetRtt(Ptr<RttEstimator> rtt)
{
  m_rtt = rtt;
}

/* Inherit from Socket class: Returns error code */
// 返回错误码
enum Socket::SocketErrno
TcpSocketBase::GetErrno(void) const
{
  return m_errno;
}

/* Inherit from Socket class: Returns socket type, NS3_SOCK_STREAM */
// 返回Socket类型
enum Socket::SocketType
TcpSocketBase::GetSocketType(void) const
{
  return NS3_SOCK_STREAM;
}

/* Inherit from Socket class: Returns associated node */
// 返回节点
Ptr<Node>
TcpSocketBase::GetNode(void) const
{
  NS_LOG_FUNCTION_NOARGS();
  return m_node;
}

/* Inherit from Socket class: Bind socket to an end-point in TcpL4Protocol */
int TcpSocketBase::Bind(void)
{
  NS_LOG_FUNCTION(this);
  m_endPoint = m_tcp->Allocate(); //Allocate an IPv4 Endpoint.
  if (0 == m_endPoint)
  {
    m_errno = ERROR_ADDRNOTAVAIL;
    return -1;
  }

  m_tcp->AddSocket(this);

  return SetupCallback();
}

int TcpSocketBase::Bind6(void)
{
  NS_LOG_FUNCTION(this);
  m_endPoint6 = m_tcp->Allocate6();
  if (0 == m_endPoint6)
  {
    m_errno = ERROR_ADDRNOTAVAIL;
    return -1;
  }

  m_tcp->AddSocket(this);

  return SetupCallback();
}

/* Inherit from Socket class: Bind socket (with specific address) to an end-point in TcpL4Protocol */
// 绑定Socket到指定地址
int TcpSocketBase::Bind(const Address &address)
{
  NS_LOG_FUNCTION(this << address);
  if (InetSocketAddress::IsMatchingType(address))
  {
    InetSocketAddress transport = InetSocketAddress::ConvertFrom(address);
    Ipv4Address ipv4 = transport.GetIpv4();
    uint16_t port = transport.GetPort();
    // getAny返回0.0.0.0地址，也即当地址为0.0.0.0或端口为0时取任何值
    if (ipv4 == Ipv4Address::GetAny() && port == 0)
    {
      m_endPoint = m_tcp->Allocate();
    }
    else if (ipv4 == Ipv4Address::GetAny() && port != 0)
    {
      m_endPoint = m_tcp->Allocate(port);
    }
    else if (ipv4 != Ipv4Address::GetAny() && port == 0)
    {
      m_endPoint = m_tcp->Allocate(ipv4);
    }
    else if (ipv4 != Ipv4Address::GetAny() && port != 0)
    {
      m_endPoint = m_tcp->Allocate(ipv4, port);
    }
    if (0 == m_endPoint)
    {
      m_errno = port ? ERROR_ADDRINUSE : ERROR_ADDRNOTAVAIL;
      return -1;
    }
  }
  else if (Inet6SocketAddress::IsMatchingType(address)) //同上
  {
    Inet6SocketAddress transport = Inet6SocketAddress::ConvertFrom(address);
    Ipv6Address ipv6 = transport.GetIpv6();
    uint16_t port = transport.GetPort();
    if (ipv6 == Ipv6Address::GetAny() && port == 0)
    {
      m_endPoint6 = m_tcp->Allocate6();
    }
    else if (ipv6 == Ipv6Address::GetAny() && port != 0)
    {
      m_endPoint6 = m_tcp->Allocate6(port);
    }
    else if (ipv6 != Ipv6Address::GetAny() && port == 0)
    {
      m_endPoint6 = m_tcp->Allocate6(ipv6);
    }
    else if (ipv6 != Ipv6Address::GetAny() && port != 0)
    {
      m_endPoint6 = m_tcp->Allocate6(ipv6, port);
    }
    if (0 == m_endPoint6)
    {
      m_errno = port ? ERROR_ADDRINUSE : ERROR_ADDRNOTAVAIL;
      return -1;
    }
  }
  else
  {
    m_errno = ERROR_INVAL;
    return -1;
  }

  m_tcp->AddSocket(this);

  NS_LOG_LOGIC("TcpSocketBase " << this << " got an endpoint: " << m_endPoint);

  return SetupCallback();
}

void TcpSocketBase::SetInitialSSThresh(uint32_t threshold)
{
  NS_ABORT_MSG_UNLESS((m_state == CLOSED) || threshold == m_tcb->m_initialSsThresh,
                      "TcpSocketBase::SetSSThresh() cannot change initial ssThresh after connection started.");

  m_tcb->m_initialSsThresh = threshold;
}

uint32_t
TcpSocketBase::GetInitialSSThresh(void) const
{
  return m_tcb->m_initialSsThresh;
}

//设置慢启动阀值
void TcpSocketBase::SetInitialCwnd(uint32_t cwnd)
{
  NS_ABORT_MSG_UNLESS((m_state == CLOSED) || cwnd == m_tcb->m_initialCWnd,
                      "TcpSocketBase::SetInitialCwnd() cannot change initial cwnd after connection started.");

  m_tcb->m_initialCWnd = cwnd;
}

//得到初始化的cwnd值
uint32_t
TcpSocketBase::GetInitialCwnd(void) const
{
  return m_tcb->m_initialCWnd;
}

/* Inherit from Socket class: Initiate connection to a remote address:port */
// 对地址进行连接
int TcpSocketBase::Connect(const Address &address)
{
  NS_LOG_FUNCTION(this << address);

  // If haven't do so, Bind() this socket first
  if (InetSocketAddress::IsMatchingType(address) && m_endPoint6 == 0)
  {
    if (m_endPoint == 0)
    {
      if (Bind() == -1)
      {
        NS_ASSERT(m_endPoint == 0);
        return -1; // Bind() failed
      }
      NS_ASSERT(m_endPoint != 0);
    }
    //设定对方地址等
    InetSocketAddress transport = InetSocketAddress::ConvertFrom(address);
    m_endPoint->SetPeer(transport.GetIpv4(), transport.GetPort());
    m_endPoint6 = 0;

    // Get the appropriate local address and port number from the routing protocol and set up endpoint
    if (SetupEndpoint() != 0) //Configure the endpoint to a local address.
    {
      NS_LOG_ERROR("Route to destination does not exist ?!");
      return -1;
    }
  }
  else if (Inet6SocketAddress::IsMatchingType(address) && m_endPoint == 0)
  {
    // If we are operating on a v4-mapped address, translate the address to
    // a v4 address and re-call this function
    Inet6SocketAddress transport = Inet6SocketAddress::ConvertFrom(address);
    Ipv6Address v6Addr = transport.GetIpv6();
    if (v6Addr.IsIpv4MappedAddress() == true)
    {
      Ipv4Address v4Addr = v6Addr.GetIpv4MappedAddress();
      return Connect(InetSocketAddress(v4Addr, transport.GetPort()));
    }

    if (m_endPoint6 == 0)
    {
      if (Bind6() == -1)
      {
        NS_ASSERT(m_endPoint6 == 0);
        return -1; // Bind() failed
      }
      NS_ASSERT(m_endPoint6 != 0);
    }
    m_endPoint6->SetPeer(v6Addr, transport.GetPort());
    m_endPoint = 0;

    // Get the appropriate local address and port number from the routing protocol and set up endpoint
    if (SetupEndpoint6() != 0)
    { // Route to destination does not exist
      return -1;
    }
  }
  else
  {
    m_errno = ERROR_INVAL;
    return -1;
  }

  // Re-initialize parameters in case this socket is being reused after CLOSE
  m_rtt->Reset();
  m_synCount = m_synRetries;
  m_dataRetrCount = m_dataRetries;

  // DoConnect() will do state-checking and send a SYN packet
  // 执行真正的连接，即发送SYN包等
  return DoConnect();
}

/* Inherit from Socket class: Listen on the endpoint for an incoming connection */
// 进入到LISTEN状态
int TcpSocketBase::Listen(void)
{
  NS_LOG_FUNCTION(this);

  // Linux quits EINVAL if we're not in CLOSED state, so match what they do
  if (m_state != CLOSED)
  {
    m_errno = ERROR_INVAL;
    return -1;
  }
  // In other cases, set the state to LISTEN and done
  NS_LOG_DEBUG("CLOSED -> LISTEN");
  m_state = LISTEN;
  return 0;
}

/* Inherit from Socket class: Kill this socket and signal the peer (if any) */
// 关闭Socket并通知对方
int TcpSocketBase::Close(void)
{
  NS_LOG_FUNCTION(this);
  /// \internal
  /// First we check to see if there is any unread rx data.
  /// \bugid{426} claims we should send reset in this case.
  // 检查rx_buff有没有未读的数据
  if (m_rxBuffer->Size() != 0)
  {
    NS_LOG_WARN("Socket " << this << " << unread rx data during close.  Sending reset."
                          << "This is probably due to a bad sink application; check its code");
    SendRST();
    return 0;
  }

  // 如果tx_buff还有数据则延迟关闭
  if (m_txBuffer->SizeFromSequence(m_nextTxSequence) > 0)
  { // App close with pending data must wait until all data transmitted
    if (m_closeOnEmpty == false)
    {
      m_closeOnEmpty = true;
      NS_LOG_INFO("Socket " << this << " deferring close, state " << TcpStateName[m_state]);
    }
    return 0;
  }
  return DoClose();
}

/* Inherit from Socket class: Signal a termination of send */
// 关闭发包
int TcpSocketBase::ShutdownSend(void)
{
  NS_LOG_FUNCTION(this);

  //this prevents data from being added to the buffer
  m_shutdownSend = true;
  m_closeOnEmpty = true;
  //if buffer is already empty, send a fin now
  //otherwise fin will go when buffer empties.
  // 如果buff已空，发送一个FIN的Flag包，否则等待buff空才发
  if (m_txBuffer->Size() == 0)
  {
    if (m_state == ESTABLISHED || m_state == CLOSE_WAIT)
    {
      NS_LOG_INFO("Emtpy tx buffer, send fin");
      SendEmptyPacket(TcpHeader::FIN);

      if (m_state == ESTABLISHED)
      { // On active close: I am the first one to send FIN
        NS_LOG_DEBUG("ESTABLISHED -> FIN_WAIT_1");
        m_state = FIN_WAIT_1;
      }
      else
      { // On passive close: Peer sent me FIN already
        NS_LOG_DEBUG("CLOSE_WAIT -> LAST_ACK");
        m_state = LAST_ACK;
      }
    }
  }

  return 0;
}

/* Inherit from Socket class: Signal a termination of receive */
// 关闭接收
int TcpSocketBase::ShutdownRecv(void)
{
  NS_LOG_FUNCTION(this);
  m_shutdownRecv = true;
  return 0;
}

/* Inherit from Socket class: Send a packet. Parameter flags is not used.
    Packet has no TCP header. Invoked by upper-layer application */
//发送包并返回发的字节，否则返回-1
int TcpSocketBase::Send(Ptr<Packet> p, uint32_t flags)
{
  NS_LOG_FUNCTION(this << p);
  NS_ABORT_MSG_IF(flags, "use of flags is not supported in TcpSocketBase::Send()");
  // 只有在这些状态下才允许发包
  if (m_state == ESTABLISHED || m_state == SYN_SENT || m_state == CLOSE_WAIT)
  {
    // 如果关闭了发送，则返回错误
    if (m_shutdownSend)
    {
      m_errno = ERROR_SHUTDOWN;
      return -1;
    }
    // Store the packet into Tx buffer
    // 将包添加到tx buff中
    if (!m_txBuffer->Add(p))
    { // TxBuffer overflow, send failed
      // 如果未加入，则返回error
      m_errno = ERROR_MSGSIZE;
      return -1;
    }
    // Submit the data to lower layers
    NS_LOG_LOGIC("txBufSize=" << m_txBuffer->Size() << " state " << TcpStateName[m_state]);
    if (m_state == ESTABLISHED || m_state == CLOSE_WAIT)
    { // Try to send the data out
      // 尝试发包出去
      if (!m_sendPendingDataEvent.IsRunning())
      {
        m_sendPendingDataEvent = Simulator::Schedule(TimeStep(1),
                                                     &TcpSocketBase::SendPendingData,
                                                     this, m_connected);
      }
    }
    return p->GetSize();
  }
  else
  { // Connection not established yet
    m_errno = ERROR_NOTCONN;
    return -1; // Send failure
  }
}

/* Inherit from Socket class: In TcpSocketBase, it is same as Send() call */
int TcpSocketBase::SendTo(Ptr<Packet> p, uint32_t flags, const Address &address)
{
  return Send(p, flags); // SendTo() and Send() are the same
}

/* Inherit from Socket class: Return data to upper-layer application. Parameter flags
   is not used. Data is returned as a packet of size no larger than maxSize */
//接收包
Ptr<Packet>
TcpSocketBase::Recv(uint32_t maxSize, uint32_t flags)
{
  NS_LOG_FUNCTION(this);
  NS_ABORT_MSG_IF(flags, "use of flags is not supported in TcpSocketBase::Recv()");
  // 如果rx buff已空并且状态为关闭，返回空包
  if (m_rxBuffer->Size() == 0 && m_state == CLOSE_WAIT)
  {
    return Create<Packet>(); // Send EOF on connection close
  }
  //从buff中提取packet，最大为MaxSize
  Ptr<Packet> outPacket = m_rxBuffer->Extract(maxSize);
  //添加SocketTag，包括peer的地址与端口
  if (outPacket != 0 && outPacket->GetSize() != 0)
  {
    SocketAddressTag tag;
    if (m_endPoint != 0)
    {
      tag.SetAddress(InetSocketAddress(m_endPoint->GetPeerAddress(), m_endPoint->GetPeerPort()));
    }
    else if (m_endPoint6 != 0)
    {
      tag.SetAddress(Inet6SocketAddress(m_endPoint6->GetPeerAddress(), m_endPoint6->GetPeerPort()));
    }
    outPacket->AddPacketTag(tag);
  }
  return outPacket;
}

/* Inherit from Socket class: Recv and return the remote's address */
// 收包并返回对方地址
Ptr<Packet>
TcpSocketBase::RecvFrom(uint32_t maxSize, uint32_t flags, Address &fromAddress)
{
  NS_LOG_FUNCTION(this << maxSize << flags);
  Ptr<Packet> packet = Recv(maxSize, flags);
  // Null packet means no data to read, and an empty packet indicates EOF
  if (packet != 0 && packet->GetSize() != 0)
  {
    if (m_endPoint != 0)
    {
      fromAddress = InetSocketAddress(m_endPoint->GetPeerAddress(), m_endPoint->GetPeerPort());
    }
    else if (m_endPoint6 != 0)
    {
      fromAddress = Inet6SocketAddress(m_endPoint6->GetPeerAddress(), m_endPoint6->GetPeerPort());
    }
    else
    {
      fromAddress = InetSocketAddress(Ipv4Address::GetZero(), 0);
    }
  }
  return packet;
}

/* Inherit from Socket class: Get the max number of bytes an app can send */
// 得到tx_buff中有多少字节可读，得到应用每次可发送的最大值
uint32_t
TcpSocketBase::GetTxAvailable(void) const
{
  NS_LOG_FUNCTION(this);
  return m_txBuffer->Available();
}

/* Inherit from Socket class: Get the max number of bytes an app can read */
// 得到rx_buff中有多少字节，即应用每次可以读取的最大值
uint32_t
TcpSocketBase::GetRxAvailable(void) const
{
  NS_LOG_FUNCTION(this);
  return m_rxBuffer->Available();
}

/* Inherit from Socket class: Return local address:port */
// 返回本地的地址与端口
int TcpSocketBase::GetSockName(Address &address) const
{
  NS_LOG_FUNCTION(this);
  if (m_endPoint != 0)
  {
    address = InetSocketAddress(m_endPoint->GetLocalAddress(), m_endPoint->GetLocalPort());
  }
  else if (m_endPoint6 != 0)
  {
    address = Inet6SocketAddress(m_endPoint6->GetLocalAddress(), m_endPoint6->GetLocalPort());
  }
  else
  { // It is possible to call this method on a socket without a name
    // in which case, behavior is unspecified
    // Should this return an InetSocketAddress or an Inet6SocketAddress?
    address = InetSocketAddress(Ipv4Address::GetZero(), 0);
  }
  return 0;
}

//得到对等结点的地址与端口
int TcpSocketBase::GetPeerName(Address &address) const
{
  NS_LOG_FUNCTION(this << address);

  if (!m_endPoint && !m_endPoint6)
  {
    m_errno = ERROR_NOTCONN;
    return -1;
  }

  if (m_endPoint)
  {
    address = InetSocketAddress(m_endPoint->GetPeerAddress(),
                                m_endPoint->GetPeerPort());
  }
  else if (m_endPoint6)
  {
    address = Inet6SocketAddress(m_endPoint6->GetPeerAddress(),
                                 m_endPoint6->GetPeerPort());
  }
  else
  {
    NS_ASSERT(false);
  }

  return 0;
}

/* Inherit from Socket class: Bind this socket to the specified NetDevice */
// 将Socket绑定到指定的设备
void TcpSocketBase::BindToNetDevice(Ptr<NetDevice> netdevice)
{
  NS_LOG_FUNCTION(netdevice);
  Socket::BindToNetDevice(netdevice); // Includes sanity check
  if (m_endPoint == 0)
  {
    if (Bind() == -1)
    {
      NS_ASSERT(m_endPoint == 0);
      return;
    }
    NS_ASSERT(m_endPoint != 0);
  }
  m_endPoint->BindToNetDevice(netdevice);

  if (m_endPoint6 == 0)
  {
    if (Bind6() == -1)
    {
      NS_ASSERT(m_endPoint6 == 0);
      return;
    }
    NS_ASSERT(m_endPoint6 != 0);
  }
  m_endPoint6->BindToNetDevice(netdevice);

  return;
}

/* Clean up after Bind. Set up callback functions in the end-point. */
//设置终端的回调函数等
int TcpSocketBase::SetupCallback(void)
{
  NS_LOG_FUNCTION(this);

  if (m_endPoint == 0 && m_endPoint6 == 0)
  {
    return -1;
  }
  if (m_endPoint != 0)
  {
    //收到包时调用ForwardUp函数
    m_endPoint->SetRxCallback(MakeCallback(&TcpSocketBase::ForwardUp, Ptr<TcpSocketBase>(this)));
    m_endPoint->SetIcmpCallback(MakeCallback(&TcpSocketBase::ForwardIcmp, Ptr<TcpSocketBase>(this)));
    m_endPoint->SetDestroyCallback(MakeCallback(&TcpSocketBase::Destroy, Ptr<TcpSocketBase>(this)));
  }
  if (m_endPoint6 != 0)
  {
    m_endPoint6->SetRxCallback(MakeCallback(&TcpSocketBase::ForwardUp6, Ptr<TcpSocketBase>(this)));
    m_endPoint6->SetIcmpCallback(MakeCallback(&TcpSocketBase::ForwardIcmp6, Ptr<TcpSocketBase>(this)));
    m_endPoint6->SetDestroyCallback(MakeCallback(&TcpSocketBase::Destroy6, Ptr<TcpSocketBase>(this)));
  }

  return 0;
}

/* Perform the real connection tasks: Send SYN if allowed, RST if invalid */
//执行真正的连接功能
int TcpSocketBase::DoConnect(void)
{
  NS_LOG_FUNCTION(this);

  // A new connection is allowed only if this socket does not have a connection
  // 只有从这些状态才能进行连接 转换为SYN_SENT状态
  if (m_state == CLOSED || m_state == LISTEN || m_state == SYN_SENT || m_state == LAST_ACK || m_state == CLOSE_WAIT)
  { // send a SYN packet and change state into SYN_SENT
    uint8_t sendflags = TcpHeader::SYN;
    if (m_tcb->m_ecnConn)
    {
      sendflags |= (TcpHeader::ECE | TcpHeader::CWR);
      NS_LOG_LOGIC(this << " ECN capable connection, sending ECN setup SYN");
    }
    if (m_TLBEnabled)
    {
      m_TLBSendSide = true;
    }
    if (m_CloveEnabled)
    {
      m_CloveSendSide = true;
    }
    SendEmptyPacket(sendflags);

    // XXX Resequence Buffer Support, disable resequence buffer on sender side
    // 在发送端禁止这resequenceBuffer
    m_resequenceBufferEnabled = false;

    NS_LOG_DEBUG(TcpStateName[m_state] << " -> SYN_SENT");
    m_state = SYN_SENT;
  }
  else if (m_state != TIME_WAIT)
  { // In states SYN_RCVD, ESTABLISHED, FIN_WAIT_1, FIN_WAIT_2, and CLOSING, an connection
    // exists. We send RST, tear down everything, and close this socket.
    // 在其它状态下试图连接，关闭这个socket
    SendRST();
    CloseAndNotify();
  }
  return 0;
}

/* Do the action to close the socket. Usually send a packet with appropriate
    flags depended on the current m_state. */
//关闭连接,在不同状态下发送不同Flag的包
int TcpSocketBase::DoClose(void)
{
  NS_LOG_FUNCTION(this);
  switch (m_state)
  {
  case SYN_RCVD:
  case ESTABLISHED:
    // send FIN to close the peer
    SendEmptyPacket(TcpHeader::FIN);
    NS_LOG_DEBUG("ESTABLISHED -> FIN_WAIT_1");
    m_state = FIN_WAIT_1;
    break;
  case CLOSE_WAIT:
    // send FIN+ACK to close the peer
    SendEmptyPacket(TcpHeader::FIN | TcpHeader::ACK);
    NS_LOG_DEBUG("CLOSE_WAIT -> LAST_ACK");
    m_state = LAST_ACK;
    break;
  case SYN_SENT:
  case CLOSING:
    // Send RST if application closes in SYN_SENT and CLOSING
    SendRST();
    CloseAndNotify();
    break;
  case LISTEN:
  case LAST_ACK:
    // In these three states, move to CLOSED and tear down the end point
    CloseAndNotify();
    break;
  case CLOSED:
  case FIN_WAIT_1:
  case FIN_WAIT_2:
  case TIME_WAIT:
  default: /* mute compiler */
    // Do nothing in these four states
    break;
  }
  return 0;
}

/* Peacefully close the socket by notifying the upper layer and deallocate end point */
// 通知上层关闭并且deallocated end point
void TcpSocketBase::CloseAndNotify(void)
{
  NS_LOG_FUNCTION(this);
  //如果还没通知关闭，则通知
  if (!m_closeNotified)
  {
    NotifyNormalClose();
    m_closeNotified = true;
  }

  NS_LOG_DEBUG(TcpStateName[m_state] << " -> CLOSED");
  //并将状态变为关闭
  m_state = CLOSED;
  DeallocateEndPoint();
}

/* Tell if a sequence number range is out side the range that my rx buffer can
    accpet */
//查看序列号是否超出了rx buff的范围
bool TcpSocketBase::OutOfRange(SequenceNumber32 head, SequenceNumber32 tail) const
{
  //这些状态时buff还没有初始化
  if (m_state == LISTEN || m_state == SYN_SENT || m_state == SYN_RCVD)
  { // Rx buffer in these states are not initialized.
    return false;
  }
  if (m_state == LAST_ACK || m_state == CLOSING || m_state == CLOSE_WAIT)
  { // In LAST_ACK and CLOSING states, it only wait for an ACK and the
    // sequence number must equals to m_rxBuffer->NextRxSequence ()
    //这些状态时只等一个ACK包
    return (m_rxBuffer->NextRxSequence() != head);
  }

  // In all other cases, check if the sequence number is in range
  //其它状态检查一下即可
  return (tail < m_rxBuffer->NextRxSequence() || m_rxBuffer->MaxRxSequence() <= head);
}

/* Function called by the L3 protocol when it received a packet to pass on to
    the TCP. This function is registered as the "RxCallback" function in
    SetupCallback(), which invoked by Bind(), and CompleteFork() */
//当L3得到包时调用，这个函数做为RxCallback的回调函数，在Bind()和CompleteFork()中调用
void TcpSocketBase::ForwardUp(Ptr<Packet> packet, Ipv4Header header, uint16_t port,
                              Ptr<Ipv4Interface> incomingInterface)
{
  NS_LOG_LOGIC("Socket " << this << " forward up " << m_endPoint->GetPeerAddress() << ":" << m_endPoint->GetPeerPort() << " to " << m_endPoint->GetLocalAddress() << ":" << m_endPoint->GetLocalPort());

  Address fromAddress = InetSocketAddress(header.GetSource(), port);
  Address toAddress = InetSocketAddress(header.GetDestination(),
                                        m_endPoint->GetLocalPort());

  // ECN Support - Extract the ECN information in IP header
  // 从IP头部得到ECN标记并且添加ECN标签
  Ipv4EcnTag ipv4EcnTag;
  ipv4EcnTag.SetEcn(header.GetEcn());
  packet->AddPacketTag(ipv4EcnTag);

  // XXX Resequence Buffer Support
  // 如果开启了resequenceBuff，则存入buff
  if (m_resequenceBufferEnabled)
  {
    // If the resequence buffer is enabled, forwarding the packet is deferred to the resequence buffer
    m_resequenceBuffer->BufferPacket(packet, fromAddress, toAddress);
  }
  else
  {
    DoForwardUp(packet, fromAddress, toAddress);
  }
}

void TcpSocketBase::ForwardUp6(Ptr<Packet> packet, Ipv6Header header, uint16_t port,
                               Ptr<Ipv6Interface> incomingInterface)
{
  NS_LOG_LOGIC("Socket " << this << " forward up " << m_endPoint6->GetPeerAddress() << ":" << m_endPoint6->GetPeerPort() << " to " << m_endPoint6->GetLocalAddress() << ":" << m_endPoint6->GetLocalPort());

  Address fromAddress = Inet6SocketAddress(header.GetSourceAddress(), port);
  Address toAddress = Inet6SocketAddress(header.GetDestinationAddress(),
                                         m_endPoint6->GetLocalPort());

  DoForwardUp(packet, fromAddress, toAddress);
}

//对ICMP包的回调函数
void TcpSocketBase::ForwardIcmp(Ipv4Address icmpSource, uint8_t icmpTtl,
                                uint8_t icmpType, uint8_t icmpCode,
                                uint32_t icmpInfo)
{
  NS_LOG_FUNCTION(this << icmpSource << (uint32_t)icmpTtl << (uint32_t)icmpType << (uint32_t)icmpCode << icmpInfo);
  if (!m_icmpCallback.IsNull())
  {
    m_icmpCallback(icmpSource, icmpTtl, icmpType, icmpCode, icmpInfo);
  }
}

void TcpSocketBase::ForwardIcmp6(Ipv6Address icmpSource, uint8_t icmpTtl,
                                 uint8_t icmpType, uint8_t icmpCode,
                                 uint32_t icmpInfo)
{
  NS_LOG_FUNCTION(this << icmpSource << (uint32_t)icmpTtl << (uint32_t)icmpType << (uint32_t)icmpCode << icmpInfo);
  if (!m_icmpCallback6.IsNull())
  {
    m_icmpCallback6(icmpSource, icmpTtl, icmpType, icmpCode, icmpInfo);
  }
}

//从L3得到包
void TcpSocketBase::DoForwardUp(Ptr<Packet> packet, const Address &fromAddress,
                                const Address &toAddress)
{
  // Peel off TCP header and do validity checking
  // 分出TCP头部并且检查
  TcpHeader tcpHeader;
  uint32_t bytesRemoved = packet->RemoveHeader(tcpHeader);
  SequenceNumber32 seq = tcpHeader.GetSequenceNumber();
  //检查TCP头部大小
  if (bytesRemoved == 0 || bytesRemoved > 60)
  {
    NS_LOG_ERROR("Bytes removed: " << bytesRemoved << " invalid");
    return; // Discard invalid packet
  }
  else if (packet->GetSize() > 0 && OutOfRange(seq, seq + packet->GetSize())) //检查是否超出buff范围
  {
    // Discard fully out of range data packets
    NS_LOG_LOGIC("At state " << TcpStateName[m_state] << " received packet of seq [" << seq << ":" << seq + packet->GetSize() << ") out of range [" << m_rxBuffer->NextRxSequence() << ":" << m_rxBuffer->MaxRxSequence() << ")");
    // Acknowledgement should be sent for all unacceptable packets (RFC793, p.69)
    if (m_state == ESTABLISHED && !(tcpHeader.GetFlags() & TcpHeader::RST))
    {
      // TODO Check @ Hong
      // We just ignore those packets
      // SendEmptyPacket (TcpHeader::ACK);
    }
    return;
  }
  m_rxTrace(packet, tcpHeader, this);
  //如果是个SYN包
  if (tcpHeader.GetFlags() & TcpHeader::SYN)
  {
    /* The window field in a segment where the SYN bit is set (i.e., a <SYN>
       * or <SYN,ACK>) MUST NOT be scaled (from RFC 7323 page 9). But should be
       * saved anyway..
       */
    // 根据TCP头部的选项来调整窗口
    m_rWnd = tcpHeader.GetWindowSize();

    if (tcpHeader.HasOption(TcpOption::WINSCALE) && m_winScalingEnabled)
    {
      ProcessOptionWScale(tcpHeader.GetOption(TcpOption::WINSCALE));
    }
    else
    {
      m_winScalingEnabled = false;
    }

    // When receiving a <SYN> or <SYN-ACK> we should adapt TS to the other end
    // 根据TCP头部选项来调整时间戳
    if (tcpHeader.HasOption(TcpOption::TS) && m_timestampEnabled)
    {
      ProcessOptionTimestamp(tcpHeader.GetOption(TcpOption::TS),
                             tcpHeader.GetSequenceNumber());
    }
    else
    {
      m_timestampEnabled = false;
    }

    // Initialize cWnd and ssThresh
    //初始化拥塞窗口和慢启动阀值
    m_tcb->m_cWnd = GetInitialCwnd() * GetSegSize();
    m_tcb->m_ssThresh = GetInitialSSThresh();
    //估计RTT,并且更新当前得到的最大的ACK号码
    if (tcpHeader.GetFlags() & TcpHeader::ACK)
    {
      EstimateRtt(tcpHeader);
      m_highRxAckMark = tcpHeader.GetAckNumber();
    }
  }
  else if (tcpHeader.GetFlags() & TcpHeader::ACK) //如果是一个ACK包
  {
    NS_ASSERT(!(tcpHeader.GetFlags() & TcpHeader::SYN));
    if (m_timestampEnabled) //更新时间戳
    {
      //如果无时间选项则丢弃
      if (!tcpHeader.HasOption(TcpOption::TS))
      {
        // Ignoring segment without TS, RFC 7323
        NS_LOG_LOGIC("At state " << TcpStateName[m_state] << " received packet of seq [" << seq << ":" << seq + packet->GetSize() << ") without TS option. Silently discard it");
        return;
      }
      else //否则更新时间戳
      {
        ProcessOptionTimestamp(tcpHeader.GetOption(TcpOption::TS),
                               tcpHeader.GetSequenceNumber());
      }
    }

    //更新RTT和窗口大小
    EstimateRtt(tcpHeader);
    UpdateWindowSize(tcpHeader);
  }

  //进入persist状态，此时接收端拥塞窗口为0，隔一段时间去发送一字节去探测
  if (m_rWnd.Get() == 0 && m_persistEvent.IsExpired())
  { // Zero window: Enter persist state to send 1 byte to probe
    NS_LOG_LOGIC(this << " Enter zerowindow persist state");
    NS_LOG_LOGIC(this << " Cancelled ReTxTimeout event which was set to expire at " << (Simulator::Now() + Simulator::GetDelayLeft(m_retxEvent)).GetSeconds());
    m_retxEvent.Cancel();
    NS_LOG_LOGIC("Schedule persist timeout at time " << Simulator::Now().GetSeconds() << " to expire at time " << (Simulator::Now() + m_persistTimeout).GetSeconds());
    m_persistEvent = Simulator::Schedule(m_persistTimeout, &TcpSocketBase::PersistTimeout, this);
    NS_ASSERT(m_persistTimeout == Simulator::GetDelayLeft(m_persistEvent));
  }

  // TCP state machine code in different process functions
  // C.f.: tcp_rcv_state_process() in tcp_input.c in Linux kernel
  switch (m_state)
  {
  case ESTABLISHED:
    ProcessEstablished(packet, tcpHeader);
    break;
  case LISTEN:
    ProcessListen(packet, tcpHeader, fromAddress, toAddress);
    break;
  case TIME_WAIT:
    // Do nothing
    break;
  case CLOSED:
    // Send RST if the incoming packet is not a RST
    if ((tcpHeader.GetFlags() & ~(TcpHeader::PSH | TcpHeader::URG | TcpHeader::ECE | TcpHeader::CWR)) != TcpHeader::RST)
    { // Since m_endPoint is not configured yet, we cannot use SendRST here
      TcpHeader h;
      Ptr<Packet> p = Create<Packet>();
      h.SetFlags(TcpHeader::RST);
      h.SetSequenceNumber(m_nextTxSequence);
      h.SetAckNumber(m_rxBuffer->NextRxSequence());
      h.SetSourcePort(tcpHeader.GetDestinationPort());
      h.SetDestinationPort(tcpHeader.GetSourcePort());
      h.SetWindowSize(AdvertisedWindowSize());
      AddOptions(h);
      m_txTrace(p, h, this);
      if (m_endPoint)
      {
        TcpSocketBase::AttachFlowId(p, m_endPoint->GetLocalAddress(),
                                    m_endPoint->GetPeerAddress(), tcpHeader.GetSourcePort(), tcpHeader.GetDestinationPort());
      }
      m_tcp->SendPacket(p, h, toAddress, fromAddress, m_boundnetdevice);
    }
    break;
  case SYN_SENT:
    ProcessSynSent(packet, tcpHeader);
    break;
  case SYN_RCVD:
    ProcessSynRcvd(packet, tcpHeader, fromAddress, toAddress);
    break;
  case FIN_WAIT_1:
  case FIN_WAIT_2:
  case CLOSE_WAIT:
    ProcessWait(packet, tcpHeader);
    break;
  case CLOSING:
    ProcessClosing(packet, tcpHeader);
    break;
  case LAST_ACK:
    ProcessLastAck(packet, tcpHeader);
    break;
  default: // mute compiler
    break;
  }
  // 如果perist探测结束 另一端增大窗口
  if (m_rWnd.Get() != 0 && m_persistEvent.IsRunning())
  { // persist probes end, the other end has increased the window
    NS_ASSERT(m_connected);
    NS_LOG_LOGIC(this << " Leaving zerowindow persist state");
    m_persistEvent.Cancel();

    // Try to send more data, since window has been updated
    if (!m_sendPendingDataEvent.IsRunning())
    {
      m_sendPendingDataEvent = Simulator::Schedule(TimeStep(1),
                                                   &TcpSocketBase::SendPendingData,
                                                   this, m_connected);
    }
  }
}

/* Received a packet upon ESTABLISHED state. This function is mimicking the
    role of tcp_rcv_established() in tcp_input.c in Linux kernel. */
// 在Established状态下收到一个包的处理过程
void TcpSocketBase::ProcessEstablished(Ptr<Packet> packet, const TcpHeader &tcpHeader)
{
  NS_LOG_FUNCTION(this << tcpHeader);
  //得到flags
  // Extract the flags. PSH, URG, ECE and CRW are not honoured.
  uint8_t tcpflags = tcpHeader.GetFlags() & ~(TcpHeader::PSH | TcpHeader::URG | TcpHeader::ECE | TcpHeader::CWR);

  // Different flags are different events
  //如果是ACK
  if (tcpflags == TcpHeader::ACK)
  {
    //如果收到重复的ACK
    if (tcpHeader.GetAckNumber() < m_txBuffer->HeadSequence())
    {
      // Case 1:  If the ACK is a duplicate (SEG.ACK < SND.UNA), it can be ignored.
      // Pag. 72 RFC 793
      NS_LOG_LOGIC("Ignored ack of " << tcpHeader.GetAckNumber() << " SND.UNA = " << m_txBuffer->HeadSequence());

      // TODO: RFC 5961 5.2 [Blind Data Injection Attack].[Mitigation]
    } //如果这个ACK是确认没发送的包，则丢弃
    else if (tcpHeader.GetAckNumber() > m_highTxMark)
    {
      // If the ACK acks something not yet sent (SEG.ACK > HighTxMark) then
      // send an ACK, drop the segment, and return.
      // Pag. 72 RFC 793
      NS_LOG_LOGIC("Ignored ack of " << tcpHeader.GetAckNumber() << " HighTxMark = " << m_highTxMark);

      SendEmptyPacket(TcpHeader::ACK);
    }
    else
    {
      // SND.UNA < SEG.ACK =< HighTxMark
      // Pag. 72 RFC 793
      ReceivedAck(packet, tcpHeader);
    }
  } //如果是SYN包，则忽略
  else if (tcpflags == TcpHeader::SYN)
  { // Received SYN, old NS-3 behaviour is to set state to SYN_RCVD and
    // respond with a SYN+ACK. But it is not a legal state transition as of
    // RFC793. Thus this is ignored.
  }
  else if (tcpflags == (TcpHeader::SYN | TcpHeader::ACK))
  { // No action for received SYN+ACK, it is probably a duplicated packet
  } //如果收到的是结束包则将这个包处理
  else if (tcpflags == TcpHeader::FIN || tcpflags == (TcpHeader::FIN | TcpHeader::ACK))
  { // Received FIN or FIN+ACK, bring down this socket nicely
    PeerClose(packet, tcpHeader);
  } //如果没有flags,表示只收到了数据
  else if (tcpflags == 0)
  { // No flags means there is only data
    ReceivedData(packet, tcpHeader);
    if (m_rxBuffer->Finished())
    {
      PeerClose(packet, tcpHeader);
    }
  }
  else //如果收到了RST包，或着Flag无效，则终止socket
  {    // Received RST or the TCP flags is invalid, in either case, terminate this socket
    if (tcpflags != TcpHeader::RST)
    { // this must be an invalid flag, send reset
      NS_LOG_LOGIC("Illegal flag " << TcpHeader::FlagsToString(tcpflags) << " received. Reset packet is sent.");
      SendRST();
    }
    CloseAndNotify();
  }
}

//处理新收到的带有ACK的包，并转换状态
/* Process the newly received ACK */
void TcpSocketBase::ReceivedAck(Ptr<Packet> packet, const TcpHeader &tcpHeader)
{
  NS_LOG_FUNCTION(this << tcpHeader);
  /*****************************************************************/
  if (m_cacheable)
    m_retxThresh = m_tmpRetxThre;
  /****************************************************************/
  NS_ASSERT(0 != (tcpHeader.GetFlags() & TcpHeader::ACK));
  NS_ASSERT(m_tcb->m_segmentSize > 0);
  //得到ACK号 并得到ACK了多少字节和几个Segment
  SequenceNumber32 ackNumber = tcpHeader.GetAckNumber();
  uint32_t bytesAcked = ackNumber - m_txBuffer->HeadSequence();
  uint32_t segsAcked = bytesAcked / m_tcb->m_segmentSize;
  //最后得到有多少字节无法构成一个segment加上上次没ACK的
  m_bytesAckedNotProcessed += bytesAcked % m_tcb->m_segmentSize;

  // XXX Pass to the congestion control alogrithm that this ACK is with ECE
  // 如果现在没Ack的字节已经构成一个segment则加一
  if (m_bytesAckedNotProcessed >= m_tcb->m_segmentSize)
  {
    segsAcked += 1;
    m_bytesAckedNotProcessed -= m_tcb->m_segmentSize;
  }

  NS_LOG_LOGIC(" Bytes acked: " << bytesAcked << " Segments acked: " << segsAcked << " bytes left: " << m_bytesAckedNotProcessed);

  NS_LOG_DEBUG("ACK of " << ackNumber << " SND.UNA=" << m_txBuffer->HeadSequence() << " SND.NXT=" << m_nextTxSequence);
  bool withECE = false;
  // XXX ECN Support, state goes into CA_CWR when receives ECE in TCP header
  // 如果包被标记了ECN
  if (m_tcb->m_ecnConn && tcpHeader.GetFlags() & TcpHeader::ECE && m_tcb->m_queueCWR == false) //如果还没调整窗口
  {
    if (m_tcb->m_congState == TcpSocketState::CA_OPEN ||
        m_tcb->m_congState == TcpSocketState::CA_DISORDER)
    { // Only OPEN and DISORDER state can go into the CWR state
      NS_LOG_DEBUG(TcpSocketState::TcpCongStateName[m_tcb->m_congState] << " -> CA_CWR");
      // 调整调整慢启动阀值和拥塞窗口
      // The ssThresh and cWnd should be reduced because of the congestion notification
      m_tcb->m_ssThresh = m_congestionControl->GetSsThresh(m_tcb, BytesInFlight());
      m_tcb->m_cWnd = m_congestionControl->GetCwnd(m_tcb);
      m_tcb->m_congState = TcpSocketState::CA_CWR;
      m_tcb->m_queueCWR = true; //在回发送数据时会标记CWR标记
    }
  }
  //得到ECE
  if (tcpHeader.GetFlags() & TcpHeader::ECE)
  {
    withECE = true;
  }

  // XXX TLB Support
  //如果开启了TLB并且是TLB的发送端
  if (m_TLBEnabled && m_TLBSendSide)
  {
    //得到TLB的标签
    TcpTLBTag tcpTLBTag;
    bool found = packet->RemovePacketTag(tcpTLBTag);
    if (found)
    {
      //计算FlowId
      uint32_t flowId = TcpSocketBase::CalFlowId(m_endPoint->GetLocalAddress(),
                                                 m_endPoint->GetPeerAddress(), m_endPoint->GetLocalPort(), m_endPoint->GetPeerPort());
      //得到这个结点上的Ipv4TLB对象
      Ptr<Ipv4TLB> ipv4TLB = m_node->GetObject<Ipv4TLB>();
      m_pathAcked = tcpTLBTag.GetPath();
      // std::cout << this << " Path acked: " << m_pathAcked << std::endl;
      // 调用TLB中的收包方法
      ipv4TLB->FlowRecv(flowId, m_pathAcked, m_endPoint->GetPeerAddress(), bytesAcked, withECE, tcpTLBTag.GetTime());
    }
  }

  // XXX Clove Support
  // 如果开启了Clove支持并且是Clove的发送端
  if (m_CloveEnabled && m_CloveSendSide)
  {
    //得到Clove的标签
    TcpCloveTag tcpCloveTag;
    bool found = packet->RemovePacketTag(tcpCloveTag);
    if (found)
    {
      Ptr<Ipv4Clove> ipv4Clove = m_node->GetObject<Ipv4Clove>();
      m_pathAcked = tcpCloveTag.GetPath();
      //调用Clove收包方法
      ipv4Clove->FlowRecv(m_pathAcked, m_endPoint->GetPeerAddress(), withECE);
    }
  }

  //如果收到一个ACK等于txBuffer中的头部，并且小于期望的下一个包且包的大小为0则这是一个重复的ACK
  if (ackNumber == m_txBuffer->HeadSequence() && ackNumber < m_nextTxSequence && packet->GetSize() == 0)
  {
    // There is a DupAck
    ++m_dupAckCount; //重复ACK计数
    //如果收到ACK包则从CA_OPEN状态转换到CA_DISORDER
    if (m_tcb->m_congState == TcpSocketState::CA_OPEN)
    {
      // From Open we go Disorder
      NS_ASSERT_MSG(m_dupAckCount == 1, "From OPEN->DISORDER but with " << m_dupAckCount << " dup ACKs");
      m_tcb->m_congState = TcpSocketState::CA_DISORDER;
      //SizeFromSequence返回到buff最后有多少字节
      if (m_limitedTx && m_txBuffer->SizeFromSequence(m_nextTxSequence) > 0)
      {
        // RFC3042 Limited transmit: Send a new packet for each duplicated ACK before fast retransmit
        NS_LOG_INFO("Limited transmit");
        //发送数据包
        uint32_t sz = SendDataPacket(m_nextTxSequence, m_tcb->m_segmentSize, true);
        m_nextTxSequence += sz;
      }

      NS_LOG_DEBUG("OPEN -> DISORDER");
      if (enablePrt)
        printf("OPEN -> DISORDER\n");
    }
    else if (m_tcb->m_congState == TcpSocketState::CA_DISORDER ||
             m_tcb->m_congState == TcpSocketState::CA_CWR) //TODO PLEASE CHECK
    {
      //如果重复的ACK值达到了阀值，则进行快重传

      if ((m_dupAckCount == m_retxThresh) && (m_highRxAckMark >= m_recover))
      {
        /*****************************************************************/
        if (enablePrt)
        {
          if (m_cacheable)
            printf("RTO 2: %u :", m_flowId);
          else
            printf("RTO 1: %u :", m_flowId);
          std::cout << m_retxThresh << "\n\n\n";
        }
        /*****************************************************************/
        // triple duplicate ack triggers fast retransmit (RFC2582 sec.3 bullet #1)
        NS_LOG_DEBUG(TcpSocketState::TcpCongStateName[m_tcb->m_congState] << " -> RECOVERY");
        if (enablePrt)
          std::cout << TcpSocketState::TcpCongStateName[m_tcb->m_congState] << " -> RECOVERY\n";
        m_recover = m_highTxMark;
        m_tcb->m_congState = TcpSocketState::CA_RECOVERY;

        m_tcb->m_ssThresh = m_congestionControl->GetSsThresh(m_tcb,
                                                             BytesInFlight());
        m_tcb->m_cWnd = m_tcb->m_ssThresh + m_dupAckCount * m_tcb->m_segmentSize;
        //NS_LOG_INFO
        NS_LOG_DEBUG("Retransmit: " << m_dupAckCount << " dupack. Enter fast recovery mode."
                                    << "Reset cwnd to " << m_tcb->m_cWnd << ", ssthresh to " << m_tcb->m_ssThresh << " at fast recovery seqnum " << m_recover << "\nm_retranDupack: " << m_retranDupack);
        if (enablePrt)
          printf("Do Retransmit!\n");
        DoRetransmit();
      } //否则根据设定每次ACK都会发一个新包
      else if (m_limitedTx && m_txBuffer->SizeFromSequence(m_nextTxSequence) > 0)
      {
        // RFC3042 Limited transmit: Send a new packet for each duplicated ACK before fast retransmit
        NS_LOG_INFO("Limited transmit");
        if (enablePrt)
          printf("Limited transmit!\n");
        uint32_t sz = SendDataPacket(m_nextTxSequence, m_tcb->m_segmentSize, true);
        m_nextTxSequence += sz;
      }
    } //在快重传模式下收到ACK包则增大拥塞窗口
    else if (m_tcb->m_congState == TcpSocketState::CA_RECOVERY)
    { // Increase cwnd for every additional dupack (RFC2582, sec.3 bullet #3)
      m_tcb->m_cWnd += m_tcb->m_segmentSize;
      if (enablePrt)
        printf("Receive dup ACK!\n");
      NS_LOG_INFO(m_dupAckCount << " Dupack received in fast recovery mode."
                                   "Increase cwnd to "
                                << m_tcb->m_cWnd);
      SendPendingData(m_connected); //并发送数据
    }

    // Artificially call PktsAcked. After all, one segment has been ACKed.
    m_congestionControl->PktsAcked(m_tcb, 1, m_lastRtt, withECE, m_highTxMark, ackNumber);

    // XXX FlowBender
    // 如果开启了FlowBender支持
    if (m_flowBenderEnabled)
    {
      //调用FlowBender中的收包方法
      m_flowBender->ReceivedPacket(m_highTxMark, ackNumber, m_tcb->m_segmentSize, withECE);
    }
  }
  else if (ackNumber == m_txBuffer->HeadSequence() && ackNumber == m_nextTxSequence)
  {
    // Dupack, but the ACK is precisely equal to the nextTxSequence
  } //收到新包
  else if (ackNumber > m_txBuffer->HeadSequence())
  { // Case 3: New ACK, reset m_dupAckCount and update m_txBuffer。
    bool callCongestionControl = true;
    bool resetRTO = true;

    /* The following switch is made because m_dupAckCount can be
       * "inflated" through out-of-order segments (e.g. from retransmission,
       * while segments have not been lost but are network-reordered). At
       * least one segment has been acked; in the luckiest case, an amount
       * equals to segsAcked-m_dupAckCount has not been processed.
       *
       * To be clear: segsAcked will be passed to PktsAcked, and it should take
       * in considerations the times that it has been already called, while newSegsAcked
       * will be passed to IncreaseCwnd, and it represents the amount of
       * segments that are allowed to increase the cWnd value.
       */
    uint32_t newSegsAcked = segsAcked;
    if (segsAcked > m_dupAckCount)
    {
      segsAcked -= m_dupAckCount;
    }
    else
    {
      segsAcked = 1;
    }

    if (m_tcb->m_congState == TcpSocketState::CA_OPEN)
    {
      m_congestionControl->PktsAcked(m_tcb, segsAcked, m_lastRtt, withECE, m_highTxMark, ackNumber);
      // XXX FlowBender
      // 调用FlowBender的包处理
      if (m_flowBenderEnabled)
      {
        m_flowBender->ReceivedPacket(m_highTxMark, ackNumber, m_tcb->m_segmentSize * segsAcked, withECE);
      }
    }
    // XXX After the CWR has been acked, the CA_CWR exits
    else if (m_tcb->m_congState == TcpSocketState::CA_CWR)
    {
      //更改状态并接收包
      if (m_tcb->m_sentCWR && ackNumber > m_tcb->m_CWRSentSeq)
      {
        NS_LOG_DEBUG("CA_CWR -> OPEN");
        if (enablePrt)
          printf("CA_CWR -> OPEN\n");
        m_tcb->m_congState = TcpSocketState::CA_OPEN;
        m_tcb->m_sentCWR = false;
      }
      m_dupAckCount = 0;
      m_retransOut = 0;

      m_congestionControl->PktsAcked(m_tcb, segsAcked, m_lastRtt, withECE, m_highTxMark, ackNumber);
      // XXX FlowBender
      if (m_flowBenderEnabled)
      {
        m_flowBender->ReceivedPacket(m_highTxMark, ackNumber, m_tcb->m_segmentSize * segsAcked, withECE);
      }
    }
    else if (m_tcb->m_congState == TcpSocketState::CA_DISORDER)
    {
      // The network reorder packets. Linux changes the counting lost
      // packet algorithm from FACK to NewReno. We simply go back in Open.
      // 即更改状态和收包
      m_tcb->m_congState = TcpSocketState::CA_OPEN;
      m_congestionControl->PktsAcked(m_tcb, segsAcked, m_lastRtt, withECE, m_highTxMark, ackNumber);
      // XXX FlowBender
      if (m_flowBenderEnabled)
      {
        m_flowBender->ReceivedPacket(m_highTxMark, ackNumber, m_tcb->m_segmentSize * segsAcked, withECE);
      }

      m_dupAckCount = 0;
      m_retransOut = 0;

      NS_LOG_DEBUG("DISORDER -> OPEN");
      if (enablePrt)
        printf("DISORDER -> OPEN\n");
    }
    else if (m_tcb->m_congState == TcpSocketState::CA_RECOVERY)
    {
      if (ackNumber < m_recover)
      {
        //部分ACK，重传后ACK部分包
        /* Partial ACK.
               * In case of partial ACK, retransmit the first unacknowledged
               * segment. Deflate the congestion window by the amount of new
               * data acknowledged by the Cumulative Acknowledgment field.
               * If the partial ACK acknowledges at least one SMSS of new data,
               * then add back SMSS bytes to the congestion window.
               * This artificially inflates the congestion window in order to
               * reflect the additional segment that has left the network.
               * Send a new segment if permitted by the new value of cwnd.
               * This "partial window deflation" attempts to ensure that, when
               * fast recovery eventually ends, approximately ssthresh amount
               * of data will be outstanding in the network.  Do not exit the
               * fast recovery procedure (i.e., if any duplicate ACKs subsequently
               * arrive, execute step 4 of Section 3.2 of [RFC5681]).
                */
        m_tcb->m_cWnd = SafeSubtraction(m_tcb->m_cWnd, bytesAcked);

        if (segsAcked >= 1)
        {
          m_tcb->m_cWnd += m_tcb->m_segmentSize;
        }

        callCongestionControl = false;                             // No congestion control on cWnd show be invoked
        m_dupAckCount = SafeSubtraction(m_dupAckCount, segsAcked); // Update the dupAckCount
        m_retransOut = SafeSubtraction(m_retransOut, 1);           // at least one retransmission
                                                                   // has reached the other side
        m_txBuffer->DiscardUpTo(ackNumber);                        //Bug 1850:  retransmit before newack
        NS_LOG_DEBUG("Retransmit: In the Recover Mode and Doing Retransmit! " << ++m_retranRec);
        DoRetransmit(); // Assume the next seq is lost. Retransmit lost packet
        if (m_isFirstPartialAck)
        {
          m_isFirstPartialAck = false;
        }
        else
        {
          resetRTO = false;
        }

        /* This partial ACK acknowledge the fact that one segment has been
               * previously lost and now successfully received. All others have
               * been processed when they come under the form of dupACKs
               */
        m_congestionControl->PktsAcked(m_tcb, 1, m_lastRtt, withECE, m_highTxMark, ackNumber);
        // XXX FlowBender
        if (m_flowBenderEnabled)
        {
          m_flowBender->ReceivedPacket(m_highTxMark, ackNumber, m_tcb->m_segmentSize, withECE);
        }

        NS_LOG_INFO("Partial ACK for seq " << ackNumber << " in fast recovery: cwnd set to " << m_tcb->m_cWnd << " recover seq: " << m_recover << " dupAck count: " << m_dupAckCount);
        if (enablePrt)
          printf("Partial ACK\n");
      }
      else if (ackNumber >= m_recover)
      { // Full ACK (RFC2582 sec.3 bullet #5 paragraph 2, option 1)
        // 全部ACK
        m_tcb->m_cWnd = std::min(m_tcb->m_ssThresh.Get(),
                                 BytesInFlight() + m_tcb->m_segmentSize);
        m_isFirstPartialAck = true;
        m_dupAckCount = 0;
        m_retransOut = 0;

        /* This FULL ACK acknowledge the fact that one segment has been
               * previously lost and now successfully received. All others have
               * been processed when they come under the form of dupACKs,
               * except the (maybe) new ACKs which come from a new window
               */
        m_congestionControl->PktsAcked(m_tcb, segsAcked, m_lastRtt, withECE, m_highTxMark, ackNumber);
        // XXX FlowBender
        if (m_flowBenderEnabled)
        {
          m_flowBender->ReceivedPacket(m_highTxMark, ackNumber, m_tcb->m_segmentSize * segsAcked, withECE);
        }

        newSegsAcked = (ackNumber - m_recover) / m_tcb->m_segmentSize;
        m_tcb->m_congState = TcpSocketState::CA_OPEN;

        NS_LOG_INFO("Received full ACK for seq " << ackNumber << ". Leaving fast recovery with cwnd set to " << m_tcb->m_cWnd);
        NS_LOG_DEBUG("RECOVERY -> OPEN");
        if (enablePrt)
          printf("RECOVERY -> OPEN");
      }
    }
    else if (m_tcb->m_congState == TcpSocketState::CA_LOSS)
    {
      // Go back in OPEN state
      m_isFirstPartialAck = true;
      m_congestionControl->PktsAcked(m_tcb, segsAcked, m_lastRtt, withECE, m_highTxMark, ackNumber);
      // XXX FlowBender
      if (m_flowBenderEnabled)
      {
        m_flowBender->ReceivedPacket(m_highTxMark, ackNumber, m_tcb->m_segmentSize * segsAcked, withECE);
      }

      m_dupAckCount = 0;
      m_retransOut = 0;
      m_tcb->m_congState = TcpSocketState::CA_OPEN;
      NS_LOG_DEBUG("LOSS -> OPEN");
      if (enablePrt)
        printf("LOSS-->OPEN\n");
    }
    //增加拥塞窗口事件
    if (callCongestionControl)
    {
      m_congestionControl->IncreaseWindow(m_tcb, newSegsAcked);

      NS_LOG_LOGIC("Congestion control called: "
                   << " cWnd: " << m_tcb->m_cWnd << " ssTh: " << m_tcb->m_ssThresh);
    }

    // Reset the data retransmission count. We got a new ACK!
    // 得到一个新的ACK所以更新m_dataRetrCount
    m_dataRetrCount = m_dataRetries;

    if (m_isFirstPartialAck == false)
    {
      NS_ASSERT(m_tcb->m_congState == TcpSocketState::CA_RECOVERY);
    }
    //重设超时事件，更新RTO及发送窗口
    NewAck(ackNumber, resetRTO);

    // Try to send more data
    // 试图发送更多数据
    if (!m_sendPendingDataEvent.IsRunning())
    {
      m_sendPendingDataEvent = Simulator::Schedule(TimeStep(1),
                                                   &TcpSocketBase::SendPendingData,
                                                   this, m_connected);
    }
  }

  // If there is any data piggybacked, store it into m_rxBuffer
  // 如果有数据将其放入tx buffer中
  if (packet->GetSize() > 0)
  {
    ReceivedData(packet, tcpHeader);
  }
}

/* Received a packet upon LISTEN state. */
// 在LISTEN状态下只收SYN包
void TcpSocketBase::ProcessListen(Ptr<Packet> packet, const TcpHeader &tcpHeader,
                                  const Address &fromAddress, const Address &toAddress)
{
  NS_LOG_FUNCTION(this << tcpHeader);

  // Extract the flags. PSH, URG, ECE and CRW are not honoured.
  //得到Flag
  uint8_t tcpflags = tcpHeader.GetFlags() & ~(TcpHeader::PSH | TcpHeader::URG | TcpHeader::ECE | TcpHeader::CWR);

  // Fork a socket if received a SYN. Do nothing otherwise.
  // C.f.: the LISTEN part in tcp_v4_do_rcv() in tcp_ipv4.c in Linux kernel
  // 如果不是SYN的一概不理
  if (tcpflags != TcpHeader::SYN)
  {
    return;
  }

  // Call socket's notify function to let the server app know we got a SYN
  // If the server app refuses the connection, do nothing
  // 通知应用得到一个SYN包，如果应用拒绝连接则不做任何事情
  if (!NotifyConnectionRequest(fromAddress))
  {
    return;
  }
  // Clone the socket, simulate fork
  // 否则复制sock
  Ptr<TcpSocketBase> newSock = Fork();
  m_resequenceBuffer->Stop();
  NS_LOG_LOGIC("Cloned a TcpSocketBase " << newSock);
  Simulator::ScheduleNow(&TcpSocketBase::CompleteFork, newSock,
                         packet, tcpHeader, fromAddress, toAddress);
}

/* Received a packet upon SYN_SENT */
// 在SYN_SENT状态下收到包后的处理
void TcpSocketBase::ProcessSynSent(Ptr<Packet> packet, const TcpHeader &tcpHeader)
{
  NS_LOG_FUNCTION(this << tcpHeader);

  // Extract the flags. PSH, URG, ECE and CRW are not honoured.
  // 得到Flags
  uint8_t tcpflags = tcpHeader.GetFlags() & ~(TcpHeader::PSH | TcpHeader::URG | TcpHeader::ECE | TcpHeader::CWR);
  uint8_t ecnflags = tcpHeader.GetFlags() & (TcpHeader::ECE | TcpHeader::CWR);
  uint8_t sendflags = (TcpHeader::SYN | TcpHeader::ACK);

  if (tcpflags == 0)
  { // Bare data, accept it and move to ESTABLISHED state. This is not a normal behaviour. Remove this?
    NS_LOG_DEBUG("SYN_SENT -> ESTABLISHED");
    m_state = ESTABLISHED; //更改状态
    m_connected = true;    //将连接状态设为true
    m_retxEvent.Cancel();  //取消事件
    m_delAckCount = m_delAckMaxCount;
    ReceivedData(packet, tcpHeader); //收到包
    Simulator::ScheduleNow(&TcpSocketBase::ConnectionSucceeded, this);
  }
  else if (tcpflags == TcpHeader::ACK)
  { // Ignore ACK in SYN_SENT
  }
  else if (tcpflags == TcpHeader::SYN)
  { // Received SYN, move to SYN_RCVD state and respond with SYN+ACK
    //在SYN_SENT情况下收到SYN包，则移动到SYN_RCVD状态
    NS_LOG_DEBUG("SYN_SENT -> SYN_RCVD");
    m_state = SYN_RCVD;
    m_synCount = m_synRetries;
    m_rxBuffer->SetNextRxSequence(tcpHeader.GetSequenceNumber() + SequenceNumber32(1));

    //如果这个SYN包中有ECN标
    // Check if it is ECN SYN
    if (ecnflags == (TcpHeader::ECE | TcpHeader::CWR))
    {
      NS_LOG_LOGIC(this << " Receving ECN setup SYN");
      if (m_tcb->m_ecnConn)
      {
        sendflags |= TcpHeader::ECE; //如果本身运行收到ECE+CWR后返回ECE
      }
      else
      {
        NS_LOG_LOGIC(this << " I am not ECN capable");
      }
    }
    else // 如SYN+包中没有ECN+CWR标记证明对方不支持ECN
    {
      NS_LOG_LOGIC(this << " Peer is not ECN capable, disable ECN support");
      m_tcb->m_ecnConn = false;
    }

    //返回包
    SendEmptyPacket(sendflags);
  }
  else if (tcpflags == (TcpHeader::SYN | TcpHeader::ACK) && m_nextTxSequence + SequenceNumber32(1) == tcpHeader.GetAckNumber())
  { // Handshake completed
    NS_LOG_DEBUG("SYN_SENT -> ESTABLISHED");
    m_state = ESTABLISHED;
    m_connected = true;
    m_retxEvent.Cancel();
    m_rxBuffer->SetNextRxSequence(tcpHeader.GetSequenceNumber() + SequenceNumber32(1));
    m_highTxMark = ++m_nextTxSequence;
    m_txBuffer->SetHeadSequence(m_nextTxSequence);
    m_delAckCount = m_delAckMaxCount;

    // Check if ti is ECN SYN-ACK
    // 如果在SYN+ACK中收到ECE证明支持ECN
    if (ecnflags == (TcpHeader::ECE) && m_tcb->m_ecnConn)
    {
      NS_LOG_LOGIC(this << "Receiving ECN setup SYN-ACK, ECN connection setup nicely");
    }
    else
    {
      NS_LOG_LOGIC(this << "Non ECN connection setup nicely");
    }

    SendEmptyPacket(TcpHeader::ACK);
    SendPendingData(m_connected);
    Simulator::ScheduleNow(&TcpSocketBase::ConnectionSucceeded, this);
    // Always respond to first data packet to speed up the connection.
    // Remove to get the behaviour of old NS-3 code.
  }
  else //其它直接RST
  {    // Other in-sequence input
    if (tcpflags != TcpHeader::RST)
    { // When (1) rx of FIN+ACK; (2) rx of FIN; (3) rx of bad flags
      NS_LOG_LOGIC("Illegal flag " << TcpHeader::FlagsToString(tcpflags) << " received. Reset packet is sent.");
      SendRST();
    }
    CloseAndNotify();
  }
}

/* Received a packet upon SYN_RCVD */
// 在SYN_RCVD状态下收到包
void TcpSocketBase::ProcessSynRcvd(Ptr<Packet> packet, const TcpHeader &tcpHeader,
                                   const Address &fromAddress, const Address &toAddress)
{
  NS_LOG_FUNCTION(this << tcpHeader);

  // Extract the flags. PSH, URG, ECE and CRW are not honoured.
  uint8_t tcpflags = tcpHeader.GetFlags() & ~(TcpHeader::PSH | TcpHeader::URG | TcpHeader::ECE | TcpHeader::CWR);

  //如果是纯数据情况下或收到ACK则变为ESTABLISHED状态
  if (tcpflags == 0 || (tcpflags == TcpHeader::ACK && m_nextTxSequence + SequenceNumber32(1) == tcpHeader.GetAckNumber()))
  { // If it is bare data, accept it and move to ESTABLISHED state. This is
    // possibly due to ACK lost in 3WHS. If in-sequence ACK is received, the
    // handshake is completed nicely.
    // 更新变量
    NS_LOG_DEBUG("SYN_RCVD -> ESTABLISHED");
    m_state = ESTABLISHED;                         //更新状态
    m_connected = true;                            //更新连接标志
    m_retxEvent.Cancel();                          //取消重传事件
    m_highTxMark = ++m_nextTxSequence;             //更新
    m_txBuffer->SetHeadSequence(m_nextTxSequence); //更新tx buff中的头部
    if (m_endPoint)
    {
      m_endPoint->SetPeer(InetSocketAddress::ConvertFrom(fromAddress).GetIpv4(),
                          InetSocketAddress::ConvertFrom(fromAddress).GetPort());
    }
    else if (m_endPoint6)
    {
      m_endPoint6->SetPeer(Inet6SocketAddress::ConvertFrom(fromAddress).GetIpv6(),
                           Inet6SocketAddress::ConvertFrom(fromAddress).GetPort());
    }
    // Always respond to first data packet to speed up the connection.
    // Remove to get the behaviour of old NS-3 code.
    m_delAckCount = m_delAckMaxCount;              //设置Delay ACK的数量
    ReceivedAck(packet, tcpHeader);                //按收到ACK处理
    NotifyNewConnectionCreated(this, fromAddress); //通知已经连接成功
    // As this connection is established, the socket is available to send data now
    if (GetTxAvailable() > 0) //如果还有可发送的数据，则发送
    {
      NotifySend(GetTxAvailable());
    }
  }
  else if (tcpflags == TcpHeader::SYN)
  { // Probably the peer lost my SYN+ACK
    //有可能是peer没收到发过去的SYN+ACK包
    m_rxBuffer->SetNextRxSequence(tcpHeader.GetSequenceNumber() + SequenceNumber32(1));

    //在SYN阶段如果标记了ECE与CWR则说明支持ECN标记
    uint8_t ecnflags = tcpHeader.GetFlags() & (TcpHeader::ECE | TcpHeader::CWR);
    uint8_t sendflags = (TcpHeader::SYN | TcpHeader::ACK);
    if (ecnflags == (TcpHeader::ECE | TcpHeader::CWR))
    {
      NS_LOG_LOGIC(this << " Receiving ECN setup SYN in SyncRecv state");
      if (m_tcb->m_ecnConn)
      {
        sendflags |= (TcpHeader::ECE | TcpHeader::CWR);
        NS_LOG_LOGIC(this << " I am not ECN capable");
      }
    }
    else
    {
      NS_LOG_LOGIC(this << " Peer is not ECN capable, disable ECN support");
      m_tcb->m_ecnConn = false;
    }

    SendEmptyPacket(sendflags);
  } //如果收到FIN+ACK
  else if (tcpflags == (TcpHeader::FIN | TcpHeader::ACK))
  {
    //关闭连接 不过之前要先设置好连接
    if (tcpHeader.GetSequenceNumber() == m_rxBuffer->NextRxSequence())
    { // In-sequence FIN before connection complete. Set up connection and close.
      m_connected = true;
      m_retxEvent.Cancel();
      m_highTxMark = ++m_nextTxSequence;
      m_txBuffer->SetHeadSequence(m_nextTxSequence);
      if (m_endPoint)
      {
        m_endPoint->SetPeer(InetSocketAddress::ConvertFrom(fromAddress).GetIpv4(),
                            InetSocketAddress::ConvertFrom(fromAddress).GetPort());
      }
      else if (m_endPoint6)
      {
        m_endPoint6->SetPeer(Inet6SocketAddress::ConvertFrom(fromAddress).GetIpv6(),
                             Inet6SocketAddress::ConvertFrom(fromAddress).GetPort());
      }
      PeerClose(packet, tcpHeader);
    }
  }
  else //其它情况统一设置好连接，然后RST
  {    // Other in-sequence input
    if (tcpflags != TcpHeader::RST)
    { // When (1) rx of SYN+ACK; (2) rx of FIN; (3) rx of bad flags
      NS_LOG_LOGIC("Illegal flag " << TcpHeader::FlagsToString(tcpflags) << " received. Reset packet is sent.");
      if (m_endPoint)
      {
        m_endPoint->SetPeer(InetSocketAddress::ConvertFrom(fromAddress).GetIpv4(),
                            InetSocketAddress::ConvertFrom(fromAddress).GetPort());
      }
      else if (m_endPoint6)
      {
        m_endPoint6->SetPeer(Inet6SocketAddress::ConvertFrom(fromAddress).GetIpv6(),
                             Inet6SocketAddress::ConvertFrom(fromAddress).GetPort());
      }
      SendRST();
    }
    CloseAndNotify();
  }
}

/* Received a packet upon CLOSE_WAIT, FIN_WAIT_1, or FIN_WAIT_2 states */
// 在上述三种状态下收到包的处理
void TcpSocketBase::ProcessWait(Ptr<Packet> packet, const TcpHeader &tcpHeader)
{
  NS_LOG_FUNCTION(this << tcpHeader);

  // Extract the flags. PSH, URG, ECE and CRW are not honoured.
  // 得到Flag
  uint8_t tcpflags = tcpHeader.GetFlags() & ~(TcpHeader::PSH | TcpHeader::URG | TcpHeader::ECE | TcpHeader::CWR);
  // 如果没ACK，则代表只有数据，处理数据
  if (packet->GetSize() > 0 && tcpflags != TcpHeader::ACK)
  { // Bare data, accept it
    ReceivedData(packet, tcpHeader);
  } //如果有ACK则开始处理，并进行状态转换
  else if (tcpflags == TcpHeader::ACK)
  {                                 // Process the ACK, and if in FIN_WAIT_1, conditionally move to FIN_WAIT_2
    ReceivedAck(packet, tcpHeader); // 其中有状态转换处理
    if (m_state == FIN_WAIT_1 && m_txBuffer->Size() == 0 && tcpHeader.GetAckNumber() == m_highTxMark + SequenceNumber32(1))
    { // This ACK corresponds to the FIN sent
      NS_LOG_DEBUG("FIN_WAIT_1 -> FIN_WAIT_2");
      m_state = FIN_WAIT_2;
    }
  } //得到FIN标记和ACK，则处理已得到的ACK和设置FIN标记
  else if (tcpflags == TcpHeader::FIN || tcpflags == (TcpHeader::FIN | TcpHeader::ACK))
  { // Got FIN, respond with ACK and move to next state
    if (tcpflags & TcpHeader::ACK)
    { // Process the ACK first
      ReceivedAck(packet, tcpHeader);
    }
    m_rxBuffer->SetFinSequence(tcpHeader.GetSequenceNumber());
  }
  else if (tcpflags == TcpHeader::SYN || tcpflags == (TcpHeader::SYN | TcpHeader::ACK))
  { // Duplicated SYN or SYN+ACK, possibly due to spurious retransmission
    return;
  }
  else //否则RST
  {    // This is a RST or bad flags
    if (tcpflags != TcpHeader::RST)
    {
      NS_LOG_LOGIC("Illegal flag " << TcpHeader::FlagsToString(tcpflags) << " received. Reset packet is sent.");
      SendRST();
    }
    CloseAndNotify();
    return;
  }

  // Check if the close responder sent an in-sequence FIN, if so, respond ACK
  if ((m_state == FIN_WAIT_1 || m_state == FIN_WAIT_2) && m_rxBuffer->Finished())
  {
    if (m_state == FIN_WAIT_1)
    {
      NS_LOG_DEBUG("FIN_WAIT_1 -> CLOSING");
      m_state = CLOSING;
      if (m_txBuffer->Size() == 0 && tcpHeader.GetAckNumber() == m_highTxMark + SequenceNumber32(1))
      { // This ACK corresponds to the FIN sent
        TimeWait();
      }
    }
    else if (m_state == FIN_WAIT_2)
    {
      TimeWait();
    }
    SendEmptyPacket(TcpHeader::ACK);
    if (!m_shutdownRecv)
    {
      NotifyDataRecv();
    }
  }
}

/* Received a packet upon CLOSING */
// 在Closing状态下收到包
void TcpSocketBase::ProcessClosing(Ptr<Packet> packet, const TcpHeader &tcpHeader)
{
  NS_LOG_FUNCTION(this << tcpHeader);

  // Extract the flags. PSH, URG, ECE and CRW are not honoured.
  // 得到Flag
  uint8_t tcpflags = tcpHeader.GetFlags() & ~(TcpHeader::PSH | TcpHeader::URG | TcpHeader::ECE | TcpHeader::CWR);
  //如果在CLOSING下收到ACK转到TIME_WAIT状态
  if (tcpflags == TcpHeader::ACK)
  {
    if (tcpHeader.GetSequenceNumber() == m_rxBuffer->NextRxSequence())
    { // This ACK corresponds to the FIN sent
      TimeWait();
    }
  }
  else
  { // CLOSING state means simultaneous close, i.e. no one is sending data to
    // anyone. If anything other than ACK is received, respond with a reset.
    // 可能是ACK包丢了
    if (tcpflags == TcpHeader::FIN || tcpflags == (TcpHeader::FIN | TcpHeader::ACK))
    { // FIN from the peer as well. We can close immediately.
      SendEmptyPacket(TcpHeader::ACK);
    }
    else if (tcpflags != TcpHeader::RST) //其它状态下直接RST
    {                                    // Receive of SYN or SYN+ACK or bad flags or pure data
      NS_LOG_LOGIC("Illegal flag " << TcpHeader::FlagsToString(tcpflags) << " received. Reset packet is sent.");
      SendRST();
    }
    CloseAndNotify(); //通知APP关闭
  }
}

/* Received a packet upon LAST_ACK */
// 在LAST_ACK状态下收到包
void TcpSocketBase::ProcessLastAck(Ptr<Packet> packet, const TcpHeader &tcpHeader)
{
  NS_LOG_FUNCTION(this << tcpHeader);

  // Extract the flags. PSH, URG, ECE and CRW are not honoured.
  // 得到Flag
  uint8_t tcpflags = tcpHeader.GetFlags() & ~(TcpHeader::PSH | TcpHeader::URG | TcpHeader::ECE | TcpHeader::CWR);

  // 如果Flag中无数据，则处理包数据
  if (tcpflags == 0)
  {
    ReceivedData(packet, tcpHeader);
  }
  else if (tcpflags == TcpHeader::ACK) //如果是ACK，对应FIN的请求，则关闭
  {
    if (tcpHeader.GetSequenceNumber() == m_rxBuffer->NextRxSequence())
    { // This ACK corresponds to the FIN sent. This socket closed peacefully.
      CloseAndNotify();
    }
  }
  else if (tcpflags == TcpHeader::FIN) // 如果是FIN包，则上次回复的FIN+ACK可能丢失了
  {                                    // Received FIN again, the peer probably lost the FIN+ACK
    SendEmptyPacket(TcpHeader::FIN | TcpHeader::ACK);
  } //如果是FIN+ACK，或RST则关闭
  else if (tcpflags == (TcpHeader::FIN | TcpHeader::ACK) || tcpflags == TcpHeader::RST)
  {
    CloseAndNotify();
  }
  else
  { // Received a SYN or SYN+ACK or bad flags 其它的都关闭
    NS_LOG_LOGIC("Illegal flag " << TcpHeader::FlagsToString(tcpflags) << " received. Reset packet is sent.");
    SendRST();
    CloseAndNotify();
  }
}

/* Peer sent me a FIN. Remember its sequence in rx buffer. */
void TcpSocketBase::PeerClose(Ptr<Packet> p, const TcpHeader &tcpHeader)
{
  NS_LOG_FUNCTION(this << tcpHeader);

  // Ignore all out of range packets
  // 如果超出了范围则忽略
  if (tcpHeader.GetSequenceNumber() < m_rxBuffer->NextRxSequence() || tcpHeader.GetSequenceNumber() > m_rxBuffer->MaxRxSequence())
  {
    return;
  }
  // For any case, remember the FIN position in rx buffer first
  // 记录FIN在rx buff中的位置
  m_rxBuffer->SetFinSequence(tcpHeader.GetSequenceNumber() + SequenceNumber32(p->GetSize()));
  NS_LOG_LOGIC("Accepted FIN at seq " << tcpHeader.GetSequenceNumber() + SequenceNumber32(p->GetSize()));
  // If there is any piggybacked data, process it
  // 如果有携带数据
  if (p->GetSize())
  {
    ReceivedData(p, tcpHeader);
  }
  // Return if FIN is out of sequence, otherwise move to CLOSE_WAIT state by DoPeerClose
  if (!m_rxBuffer->Finished())
  {
    return;
  }

  // Simultaneous close: Application invoked Close() when we are processing this FIN packet
  // 由FIN_WAIT_1到CLSING状态
  if (m_state == FIN_WAIT_1)
  {
    NS_LOG_DEBUG("FIN_WAIT_1 -> CLOSING");
    m_state = CLOSING;
    return;
  }

  //改变状态并回复ACK
  DoPeerClose(); // Change state, respond with ACK
}

/* Received a in-sequence FIN. Close down this socket. */
// peer端关闭了Socket
void TcpSocketBase::DoPeerClose(void)
{
  //由这两种状态变换为CLOSE_WAIT
  NS_ASSERT(m_state == ESTABLISHED || m_state == SYN_RCVD);

  // Move the state to CLOSE_WAIT
  NS_LOG_DEBUG(TcpStateName[m_state] << " -> CLOSE_WAIT");
  m_state = CLOSE_WAIT;

  //如果还没通知APP关闭，则通知
  if (!m_closeNotified)
  {
    // The normal behaviour for an application is that, when the peer sent a in-sequence
    // FIN, the app should prepare to close. The app has two choices at this point: either
    // respond with ShutdownSend() call to declare that it has nothing more to send and
    // the socket can be closed immediately; or remember the peer's close request, wait
    // until all its existing data are pushed into the TCP socket, then call Close()
    // explicitly.
    NS_LOG_LOGIC("TCP " << this << " calling NotifyNormalClose");
    NotifyNormalClose();
    m_closeNotified = true;
  }
  //如果已经关闭了发送
  if (m_shutdownSend)
  {          // The application declares that it would not sent any more, close this socket
    Close(); //关闭Socket
  }
  else
  {                                  // Need to ack, the application will close later
    SendEmptyPacket(TcpHeader::ACK); //否则发送空包Ack
  }
  //TODO
  if (m_state == LAST_ACK)
  {
    NS_LOG_LOGIC("TcpSocketBase " << this << " scheduling LATO1");
    Time lastRto = m_rtt->GetEstimate() + Max(m_clockGranularity, m_rtt->GetVariation() * 4);
    m_lastAckEvent = Simulator::Schedule(lastRto, &TcpSocketBase::LastAckTimeout, this);
  }
}

/* Kill this socket. This is a callback function configured to m_endpoint in
   SetupCallback(), invoked when the endpoint is destroyed. */
// 当endpoint destoryed时调用，结束socket
void TcpSocketBase::Destroy(void)
{
  NS_LOG_FUNCTION(this);
  m_endPoint = 0;
  if (m_tcp != 0)
  {
    m_tcp->RemoveSocket(this);
  }
  NS_LOG_LOGIC(this << " Cancelled ReTxTimeout event which was set to expire at " << (Simulator::Now() + Simulator::GetDelayLeft(m_retxEvent)).GetSeconds());
  CancelAllTimers();
}

/* Kill this socket. This is a callback function configured to m_endpoint in
   SetupCallback(), invoked when the endpoint is destroyed. */
void TcpSocketBase::Destroy6(void)
{
  NS_LOG_FUNCTION(this);
  m_endPoint6 = 0;
  if (m_tcp != 0)
  {
    m_tcp->RemoveSocket(this);
  }
  NS_LOG_LOGIC(this << " Cancelled ReTxTimeout event which was set to expire at " << (Simulator::Now() + Simulator::GetDelayLeft(m_retxEvent)).GetSeconds());
  CancelAllTimers();
}

/*************************************************************************************/

/* Send an empty packet with specified TCP flags */
// 发送一个带有指定标记的空包
void TcpSocketBase::SendUrgePacket(uint32_t urgeNum)
{
  NS_LOG_FUNCTION(this);
  //创建包
  Ptr<Packet> p = Create<Packet>();
  TcpHeader header;
  header.SetFlags(0);
  header.SetSequenceNumber(m_nextTxSequence);
  header.SetAckNumber(m_rxBuffer->NextRxSequence());
  if (m_endPoint != 0)
  {
    header.SetSourcePort(m_endPoint->GetLocalPort());
    header.SetDestinationPort(m_endPoint->GetPeerPort());
  }
  else
  {
    header.SetSourcePort(m_endPoint6->GetLocalPort());
    header.SetDestinationPort(m_endPoint6->GetPeerPort());
  }
  //添加选项
  AddOptions(header);
  header.SetWindowSize(AdvertisedWindowSize());

  // XXX Clove Support
  // 如果开启了Clove
  UrgeTag urgeTag;
  p->AddPacketTag(urgeTag);

  m_txTrace(p, header, this);

  if (m_endPoint != 0)
  {
    TcpSocketBase::AttachFlowId(p, m_endPoint->GetLocalAddress(),
                                m_endPoint->GetPeerAddress(), header.GetSourcePort(), header.GetDestinationPort());

    if (m_isPause) //如果有Pause则缓存
    {
      std::cout << "Pause enabled, buffering packet..." << std::endl;
      m_pauseBuffer->BufferItem(p, header);
    }
    else
    { //否则直接TCP发送
      m_tcp->SendPacket(p, header, m_endPoint->GetLocalAddress(),
                        m_endPoint->GetPeerAddress(), m_boundnetdevice);
    }
  }
  else
  {
    m_tcp->SendPacket(p, header, m_endPoint6->GetLocalAddress(),
                      m_endPoint6->GetPeerAddress(), m_boundnetdevice);
  }
}

/*************************************************************************************/

/* Send an empty packet with specified TCP flags */
// 发送一个带有指定标记的空包
void TcpSocketBase::SendEmptyPacket(uint8_t flags)
{
  NS_LOG_FUNCTION(this << (uint32_t)flags);
  //创建包
  Ptr<Packet> p = Create<Packet>();
  TcpHeader header;
  SequenceNumber32 s = m_nextTxSequence;

  /*
   * Add tags for each socket option.
   * Note that currently the socket adds both IPv4 tag and IPv6 tag
   * if both options are set. Once the packet got to layer three, only
   * the corresponding tags will be read.
   */
  //添加标签
  if (IsManualIpTos())
  {
    SocketIpTosTag ipTosTag;
    ipTosTag.SetTos(GetIpTos());
    p->AddPacketTag(ipTosTag);
  }

  if (IsManualIpv6Tclass())
  {
    SocketIpv6TclassTag ipTclassTag;
    ipTclassTag.SetTclass(GetIpv6Tclass());
    p->AddPacketTag(ipTclassTag);
  }

  if (IsManualIpTtl())
  {
    SocketIpTtlTag ipTtlTag;
    ipTtlTag.SetTtl(GetIpTtl());
    p->AddPacketTag(ipTtlTag);
  }

  if (IsManualIpv6HopLimit())
  {
    SocketIpv6HopLimitTag ipHopLimitTag;
    ipHopLimitTag.SetHopLimit(GetIpv6HopLimit());
    p->AddPacketTag(ipHopLimitTag);
  }

  if (m_endPoint == 0 && m_endPoint6 == 0)
  {
    NS_LOG_WARN("Failed to send empty packet due to null endpoint");
    return;
  }
  // 如果flag里有FIN则再添加ACK
  if (flags & TcpHeader::FIN)
  {
    flags |= TcpHeader::ACK;
  }
  else if (m_state == FIN_WAIT_1 || m_state == LAST_ACK || m_state == CLOSING)
  {
    ++s; //如是以上这些顺序号加一
  }
  //设置TCP头部
  header.SetFlags(flags);
  header.SetSequenceNumber(s);
  header.SetAckNumber(m_rxBuffer->NextRxSequence());
  if (m_endPoint != 0)
  {
    header.SetSourcePort(m_endPoint->GetLocalPort());
    header.SetDestinationPort(m_endPoint->GetPeerPort());
  }
  else
  {
    header.SetSourcePort(m_endPoint6->GetLocalPort());
    header.SetDestinationPort(m_endPoint6->GetPeerPort());
  }
  //添加选项
  AddOptions(header);
  header.SetWindowSize(AdvertisedWindowSize());

  // RFC 6298, clause 2.4
  // 计算RTO
  m_rto = Max(m_rtt->GetEstimate() + Max(m_clockGranularity, m_rtt->GetVariation() * 4), m_minRto);
  //查看flag中是否有SYN、FIN
  bool hasSyn = flags & TcpHeader::SYN;
  bool hasFin = flags & TcpHeader::FIN;
  //～是按位取反运算符，即除了ECE与CWR位，是否有ACK位
  bool isAck = (flags & ~(TcpHeader::ECE | TcpHeader::CWR)) == TcpHeader::ACK;
  NS_LOG_DEBUG("Is ACK: " << isAck);
  //如果有SYN位
  if (hasSyn)
  {
    //已经没有剩余次数去同步
    if (m_synCount == 0)
    { // No more connection retries, give up
      NS_LOG_LOGIC("Connection failed.");
      m_rtt->Reset(); //According to recommendation -> RFC 6298
      CloseAndNotify();
      return;
    }
    else //否则按照指数后退的方法再次尝试
    {    // Exponential backoff of connection time out
      int backoffCount = 0x1 << (m_synRetries - m_synCount);
      m_rto = m_cnTimeout * backoffCount;
      m_synCount--;
    }
    //如果才发一次机，更新RTT
    if (m_synRetries - 1 == m_synCount)
    {
      //第一次并不是重传
      UpdateRttHistory(s, 0, false);
    }
    else
    { // This is SYN retransmission
      // 之后是重传
      UpdateRttHistory(s, 0, true);
    }
  }

  // XXX TLB Support
  // 如果支持TLB并且是发送端
  if (m_TLBEnabled)
  {
    if (m_TLBSendSide)
    {
      uint32_t flowId = TcpSocketBase::CalFlowId(m_endPoint->GetLocalAddress(),
                                                 m_endPoint->GetPeerAddress(), header.GetSourcePort(), header.GetDestinationPort());

      Ptr<Ipv4TLB> ipv4TLB = m_node->GetObject<Ipv4TLB>();
      uint32_t path = ipv4TLB->GetPath(flowId, m_endPoint->GetLocalAddress(), m_endPoint->GetPeerAddress());
      // std::cout << this << " Get Path From TLB: " << path << std::endl;

      // XPath Support
      Ipv4XPathTag ipv4XPathTag;
      ipv4XPathTag.SetPathId(path);
      p->AddPacketTag(ipv4XPathTag);

      // TLB Support
      TcpTLBTag tcpTLBTag;
      tcpTLBTag.SetPath(path);
      tcpTLBTag.SetTime(Simulator::Now());
      p->AddPacketTag(tcpTLBTag);
      //判断是否为第一次发送，如果是则是重传
      bool synRetrans = hasSyn && (m_synCount != m_synRetries - 1);
      // 发送数据
      ipv4TLB->FlowSend(flowId, m_endPoint->GetPeerAddress(), path, p->GetSize(), synRetrans);
      if (synRetrans)
      { //同时更新超时信息
        ipv4TLB->FlowTimeout(flowId, m_endPoint->GetPeerAddress(), path);
      }

      // Pause Support
      // 如果支持Pause
      if (m_isPauseEnabled && m_oldPath == 0)
      {
        m_oldPath = path;
      }

      if (m_isPauseEnabled && !m_isPause && m_oldPath != path)
      {
        std::cout << "Turning on pause" << std::endl;
        m_isPause = true;
        m_oldPath = path;
        Time pauseTime = ipv4TLB->GetPauseTime(flowId);
        Simulator::Schedule(pauseTime, &TcpSocketBase::RecoverFromPause, this);
      }
    }
    // 如果设定了m_piggybackTLBInfo，则添加TLB标签
    if (m_piggybackTLBInfo)
    {
      TcpTLBTag tcpTLBTag;
      tcpTLBTag.SetPath(m_TLBPath);
      tcpTLBTag.SetTime(m_onewayRtt);
      p->AddPacketTag(tcpTLBTag);
    }

    // 如果是接收方，并且得到包中有ACK或SYN，返回
    if (m_TLBReverseAckEnabled && (hasSyn || isAck) && !m_TLBSendSide)
    {
      uint32_t flowId = TcpSocketBase::CalFlowId(m_endPoint->GetLocalAddress(),
                                                 m_endPoint->GetPeerAddress(), header.GetSourcePort(), header.GetDestinationPort());

      Ptr<Ipv4TLB> ipv4TLB = m_node->GetObject<Ipv4TLB>();
      uint32_t path = ipv4TLB->GetAckPath(flowId, m_endPoint->GetLocalAddress(), m_endPoint->GetPeerAddress());

      // XPath Support
      Ipv4XPathTag ipv4XPathTag;
      ipv4XPathTag.SetPathId(path);
      p->AddPacketTag(ipv4XPathTag);
    }
  }

  // XXX Clove Support
  // 如果开启了Clove
  if (m_CloveEnabled)
  {
    //并且是发送端
    if (m_CloveSendSide)
    {
      uint32_t flowId = TcpSocketBase::CalFlowId(m_endPoint->GetLocalAddress(),
                                                 m_endPoint->GetPeerAddress(), header.GetSourcePort(), header.GetDestinationPort());

      Ptr<Ipv4Clove> ipv4Clove = m_node->GetObject<Ipv4Clove>();
      uint32_t path = ipv4Clove->GetPath(flowId, m_endPoint->GetLocalAddress(), m_endPoint->GetPeerAddress());

      // XPath Support
      Ipv4XPathTag ipv4XPathTag;
      ipv4XPathTag.SetPathId(path);
      p->AddPacketTag(ipv4XPathTag);

      // Clove Support
      TcpCloveTag tcpCloveTag;
      tcpCloveTag.SetPath(path);
      p->AddPacketTag(tcpCloveTag);
    }

    //如果开启了piggyback //TODO，如果是发送端带两个Tag?
    if (m_piggybackCloveInfo)
    {
      TcpCloveTag tcpCloveTag;
      tcpCloveTag.SetPath(m_ClovePath);
      p->AddPacketTag(tcpCloveTag);
    }
  }

  m_txTrace(p, header, this);

  if (m_endPoint != 0)
  {
    TcpSocketBase::AttachFlowId(p, m_endPoint->GetLocalAddress(),
                                m_endPoint->GetPeerAddress(), header.GetSourcePort(), header.GetDestinationPort());

    if (m_isPause) //如果有Pause则缓存
    {
      std::cout << "Pause enabled, buffering packet..." << std::endl;
      m_pauseBuffer->BufferItem(p, header);
    }
    else
    { //否则直接TCP发送
      m_tcp->SendPacket(p, header, m_endPoint->GetLocalAddress(),
                        m_endPoint->GetPeerAddress(), m_boundnetdevice);
    }
  }
  else
  {
    m_tcp->SendPacket(p, header, m_endPoint6->GetLocalAddress(),
                      m_endPoint6->GetPeerAddress(), m_boundnetdevice);
  }

  if (flags & TcpHeader::ACK)
  { // If sending an ACK, cancel the delay ACK as well
    // 如果TCP头部有ACK，则取消Ack事件，和清零并更新
    m_congestionControl->CwndEvent(m_tcb, TcpCongestionOps::CA_EVENT_DELAY_ACK_NO_RESERVED, this);
    m_delAckEvent.Cancel();
    m_delAckCount = 0;
    if (m_highTxAck < header.GetAckNumber())
    {
      m_highTxAck = header.GetAckNumber();
    }
  }
  //开启重传事件以防止丢包，在RTO后重传
  if (m_retxEvent.IsExpired() && (hasSyn || hasFin) && !isAck)
  { // Retransmit SYN / SYN+ACK / FIN / FIN+ACK to guard against lost
    NS_LOG_LOGIC("Schedule retransmission timeout at time "
                 << Simulator::Now().GetSeconds() << " to expire at time "
                 << (Simulator::Now() + m_rto.Get()).GetSeconds());
    m_retxEvent = Simulator::Schedule(m_rto, &TcpSocketBase::SendEmptyPacket, this, flags);
  }
}

/* This function closes the endpoint completely. Called upon RST_TX action. */
// 发送RST标记
void TcpSocketBase::SendRST(void)
{
  NS_LOG_FUNCTION(this);
  SendEmptyPacket(TcpHeader::RST);
  NotifyErrorClose();
  DeallocateEndPoint();
}

/* Deallocate the end point and cancel all the timers */
// 清除终端分配并且取消所有Timer
void TcpSocketBase::DeallocateEndPoint(void)
{
  // XXX Stop the resequence buffer
  m_resequenceBuffer->Stop();
  if (m_endPoint != 0)
  {
    CancelAllTimers();
    m_endPoint->SetDestroyCallback(MakeNullCallback<void>());
    m_tcp->DeAllocate(m_endPoint);
    m_endPoint = 0;
    m_resequenceBuffer->Stop();
    m_tcp->RemoveSocket(this);
  }
  else if (m_endPoint6 != 0)
  {
    CancelAllTimers();
    m_endPoint6->SetDestroyCallback(MakeNullCallback<void>());
    m_tcp->DeAllocate(m_endPoint6);
    m_endPoint6 = 0;
    m_tcp->RemoveSocket(this);
  }
}

/* Configure the endpoint to a local address. Called by Connect() if Bind() didn't specify one. */
// 配置endpoint到本地地址
int TcpSocketBase::SetupEndpoint()
{
  NS_LOG_FUNCTION(this);
  Ptr<Ipv4> ipv4 = m_node->GetObject<Ipv4>();
  NS_ASSERT(ipv4 != 0);
  if (ipv4->GetRoutingProtocol() == 0)
  {
    NS_FATAL_ERROR("No Ipv4RoutingProtocol in the node");
  }
  // Create a dummy packet, then ask the routing function for the best output
  // interface's address
  Ipv4Header header;
  header.SetDestination(m_endPoint->GetPeerAddress());
  Socket::SocketErrno errno_;
  Ptr<Ipv4Route> route;
  Ptr<NetDevice> oif = m_boundnetdevice; //the device this socket is bound to
  route = ipv4->GetRoutingProtocol()->RouteOutput(Ptr<Packet>(), header, oif, errno_);
  if (route == 0)
  {
    NS_LOG_LOGIC("Route to " << m_endPoint->GetPeerAddress() << " does not exist");
    NS_LOG_ERROR(errno_);
    m_errno = errno_;
    return -1;
  }
  NS_LOG_LOGIC("Route exists");
  m_endPoint->SetLocalAddress(route->GetSource());
  return 0;
}

int TcpSocketBase::SetupEndpoint6()
{
  NS_LOG_FUNCTION(this);
  Ptr<Ipv6L3Protocol> ipv6 = m_node->GetObject<Ipv6L3Protocol>();
  NS_ASSERT(ipv6 != 0);
  if (ipv6->GetRoutingProtocol() == 0)
  {
    NS_FATAL_ERROR("No Ipv6RoutingProtocol in the node");
  }
  // Create a dummy packet, then ask the routing function for the best output
  // interface's address
  Ipv6Header header;
  header.SetDestinationAddress(m_endPoint6->GetPeerAddress());
  Socket::SocketErrno errno_;
  Ptr<Ipv6Route> route;
  Ptr<NetDevice> oif = m_boundnetdevice;
  route = ipv6->GetRoutingProtocol()->RouteOutput(Ptr<Packet>(), header, oif, errno_);
  if (route == 0)
  {
    NS_LOG_LOGIC("Route to " << m_endPoint6->GetPeerAddress() << " does not exist");
    NS_LOG_ERROR(errno_);
    m_errno = errno_;
    return -1;
  }
  NS_LOG_LOGIC("Route exists");
  m_endPoint6->SetLocalAddress(route->GetSource());
  return 0;
}

/* This function is called only if a SYN received in LISTEN state. After
   TcpSocketBase cloned, allocate a new end point to handle the incoming
   connection and send a SYN+ACK to complete the handshake. */
// 这个函数仅在LISTEN状态收到SYN时才调用，如果之后会克隆TcpSocketBase
void TcpSocketBase::CompleteFork(Ptr<Packet> p, const TcpHeader &h,
                                 const Address &fromAddress, const Address &toAddress)
{
  //得到Flag
  // Extract the ECN flag
  uint32_t ecnflags = h.GetFlags() & (TcpHeader::ECE | TcpHeader::CWR);
  // Sending TCP flags
  uint32_t sendflags = (TcpHeader::SYN | TcpHeader::ACK);
  // Get port and address from peer (connecting host)
  // 克隆Socket
  if (InetSocketAddress::IsMatchingType(toAddress))
  {
    m_endPoint = m_tcp->Allocate(InetSocketAddress::ConvertFrom(toAddress).GetIpv4(),
                                 InetSocketAddress::ConvertFrom(toAddress).GetPort(),
                                 InetSocketAddress::ConvertFrom(fromAddress).GetIpv4(),
                                 InetSocketAddress::ConvertFrom(fromAddress).GetPort());
    m_endPoint6 = 0;
  }
  else if (Inet6SocketAddress::IsMatchingType(toAddress))
  {
    m_endPoint6 = m_tcp->Allocate6(Inet6SocketAddress::ConvertFrom(toAddress).GetIpv6(),
                                   Inet6SocketAddress::ConvertFrom(toAddress).GetPort(),
                                   Inet6SocketAddress::ConvertFrom(fromAddress).GetIpv6(),
                                   Inet6SocketAddress::ConvertFrom(fromAddress).GetPort());
    m_endPoint = 0;
  }
  m_tcp->AddSocket(this);

  // Change the cloned socket from LISTEN state to SYN_RCVD
  // 更改克隆后的socket状态
  NS_LOG_DEBUG("LISTEN -> SYN_RCVD");
  m_state = SYN_RCVD;
  // 初始化可重试连接的次数和重传数据包的次数
  m_synCount = m_synRetries;
  m_dataRetrCount = m_dataRetries;
  SetupCallback();
  // 返回SYN+ACK包
  // Set the sequence number and send SYN+ACK
  m_rxBuffer->SetNextRxSequence(h.GetSequenceNumber() + SequenceNumber32(1));

  if (ecnflags == (TcpHeader::ECE | TcpHeader::CWR))
  {
    NS_LOG_LOGIC(this << " Receiving ECN setup SYN");
    if (m_tcb->m_ecnConn)
    {
      sendflags |= TcpHeader::ECE;
    }
    else
    {
      NS_LOG_LOGIC(this << " I am not ECN capable");
    }
  }
  else
  {
    NS_LOG_LOGIC(this << " Peer is not ECN capable, disable ECN support");
    m_tcb->m_ecnConn = false;
  }

  SendEmptyPacket(sendflags);
}

//通知连接成功
void TcpSocketBase::ConnectionSucceeded()
{ // Wrapper to protected function NotifyConnectionSucceeded() so that it can
  // be called as a scheduled event
  NotifyConnectionSucceeded();
  // The if-block below was moved from ProcessSynSent() to here because we need
  // to invoke the NotifySend() only after NotifyConnectionSucceeded() to
  // reflect the behaviour in the real world.
  if (GetTxAvailable() > 0)
  {
    NotifySend(GetTxAvailable());
  }
}

/* Extract at most maxSize bytes from the TxBuffer at sequence seq, add the
    TCP header, and send to TcpL4Protocol */
// 释释放最多MaxSize字节从seq字节号，填加TCP头部并发送到TCPL4Prtocol
uint32_t
TcpSocketBase::SendDataPacket(SequenceNumber32 seq, uint32_t maxSize, bool withAck)
{
  NS_LOG_FUNCTION(this << seq << maxSize << withAck);
  //得到是否为重传
  bool isRetransmission = false;
  if (seq != m_highTxMark)
  {
    isRetransmission = true;
  }
  //得到包及大小还有flga还有buff中剩余的数据量
  Ptr<Packet> p = m_txBuffer->CopyFromSequence(maxSize, seq);
  uint32_t sz = p->GetSize(); // Size of packet
  uint8_t flags = withAck ? TcpHeader::ACK : 0;
  uint32_t remainingData = m_txBuffer->SizeFromSequence(seq + SequenceNumber32(sz));

  //如果有ACK，则调用窗口事件
  if (withAck)
  {
    m_congestionControl->CwndEvent(m_tcb, TcpCongestionOps::CA_EVENT_DELAY_ACK_NO_RESERVED, this);
    m_delAckEvent.Cancel(); //取消Delay ACK 事件并清零
    m_delAckCount = 0;
  }

  /*
   * Add tags for each socket option.
   * Note that currently the socket adds both IPv4 tag and IPv6 tag
   * if both options are set. Once the packet got to layer three, only
   * the corresponding tags will be read.
   */
  //设置一系列Tag
  if (IsManualIpTos())
  {
    SocketIpTosTag ipTosTag;
    ipTosTag.SetTos(GetIpTos());
    p->AddPacketTag(ipTosTag);
  }

  if (IsManualIpv6Tclass())
  {
    SocketIpv6TclassTag ipTclassTag;
    ipTclassTag.SetTclass(GetIpv6Tclass());
    p->AddPacketTag(ipTclassTag);
  }

  if (IsManualIpTtl())
  {
    SocketIpTtlTag ipTtlTag;
    ipTtlTag.SetTtl(GetIpTtl());
    p->AddPacketTag(ipTtlTag);
  }

  if (IsManualIpv6HopLimit())
  {
    SocketIpv6HopLimitTag ipHopLimitTag;
    ipHopLimitTag.SetHopLimit(GetIpv6HopLimit());
    p->AddPacketTag(ipHopLimitTag);
  }

  // XXX If this data packet is not retransmission, set ECT
  if (m_tcb->m_ecnConn && !isRetransmission)
  {
    //如果开启了ECN并且不是重传
    if (m_tcb->m_queueCWR)
    { //如果发送端需要返回CWR，则标记CWR将queueCWR设为false
      // The congestion control has responeded, mark CWR in TCP header
      m_tcb->m_queueCWR = false;
      flags |= TcpHeader::CWR;
      // Mark the sequence number for CA_CWR to exit
      m_tcb->m_CWRSentSeq = seq;
      m_tcb->m_sentCWR = true;
    }
    //添加ECN标签
    Ipv4EcnTag ipv4EcnTag;
    ipv4EcnTag.SetEcn(Ipv4Header::ECN_ECT1);
    p->AddPacketTag(ipv4EcnTag);
  }

  //如果由于txBuff空了
  if (m_closeOnEmpty && (remainingData == 0))
  {
    flags |= TcpHeader::FIN;    //则带上FIN标记
    if (m_state == ESTABLISHED) //并由ESTABLISHED进入FIN_WAIT_1
    {                           // On active close: I am the first one to send FIN
      NS_LOG_DEBUG("ESTABLISHED -> FIN_WAIT_1");
      m_state = FIN_WAIT_1;
    }
    else if (m_state == CLOSE_WAIT) //如果peer已关闭，则进入LAST_ACK状态
    {                               // On passive close: Peer sent me FIN already
      NS_LOG_DEBUG("CLOSE_WAIT -> LAST_ACK");
      m_state = LAST_ACK;
    }
  }

  //设置TCP头部
  TcpHeader header;
  header.SetFlags(flags);
  header.SetSequenceNumber(seq);
  header.SetAckNumber(m_rxBuffer->NextRxSequence());
  if (m_endPoint)
  {
    header.SetSourcePort(m_endPoint->GetLocalPort());
    header.SetDestinationPort(m_endPoint->GetPeerPort());
  }
  else
  {
    header.SetSourcePort(m_endPoint6->GetLocalPort());
    header.SetDestinationPort(m_endPoint6->GetPeerPort());
  }
  header.SetWindowSize(AdvertisedWindowSize()); //设置初始窗口大小
  AddOptions(header);                           //添加选项

  //如果重传事件过期并且这次是重传则double timer
  if (m_retxEvent.IsExpired())
  {
    // Schedules retransmit timeout. If this is a retransmission, double the timer

    if (isRetransmission)
    { // This is a retransmit
      // RFC 6298, clause 2.5
      Time doubledRto = m_rto + m_rto;
      m_rto = Min(doubledRto, Time::FromDouble(60, Time::S));
    }

    NS_LOG_LOGIC(this << " SendDataPacket Schedule ReTxTimeout at time " << Simulator::Now().GetSeconds() << " to expire at time " << (Simulator::Now() + m_rto.Get()).GetSeconds());
    //重新开启Timer
    m_retxEvent = Simulator::Schedule(m_rto, &TcpSocketBase::ReTxTimeout, this);
  }

  /***************************************************************************/
  if (m_enableUrgeSend)
  {
    m_urgeSendNum++;
    if (m_urgeSendNum % 10 == 0 && m_cacheable && m_urgePktEvent.IsExpired())
    {
      m_urgePktEvent = Simulator::Schedule(m_rto * 4 / 5, &TcpSocketBase::SendUrgePacket, this, 10);
    }
  }
  /***************************************************************************/

  m_txTrace(p, header, this);

  if (m_endPoint)
  {
    //对这个包加上flowId
    TcpSocketBase::AttachFlowId(p, m_endPoint->GetLocalAddress(),
                                m_endPoint->GetPeerAddress(), header.GetSourcePort(), header.GetDestinationPort());
    /***************************************************************************/
    // XXX TLB Support
    // 如果开启了TLB并且是发送端
    if (m_TLBEnabled && m_TLBSendSide)
    {
      //得到FlowId并且添加xpath标签和TLB标签
      uint32_t flowId = TcpSocketBase::CalFlowId(m_endPoint->GetLocalAddress(),
                                                 m_endPoint->GetPeerAddress(), header.GetSourcePort(), header.GetDestinationPort());
      Ptr<Ipv4TLB> ipv4TLB = m_node->GetObject<Ipv4TLB>();
      uint32_t path = ipv4TLB->GetPath(flowId, m_endPoint->GetLocalAddress(), m_endPoint->GetPeerAddress());
      // std::cout << this << " Get Path From TLB: " << path << std::endl;

      // XPath Support
      Ipv4XPathTag ipv4XPathTag;
      ipv4XPathTag.SetPathId(path);
      p->AddPacketTag(ipv4XPathTag);

      // TLB Support
      TcpTLBTag tcpTLBTag;
      tcpTLBTag.SetPath(path);
      tcpTLBTag.SetTime(Simulator::Now());
      p->AddPacketTag(tcpTLBTag);
      ipv4TLB->FlowSend(flowId, m_endPoint->GetPeerAddress(), path, p->GetSize(), isRetransmission);

      // Pause Support
      if (m_isPauseEnabled && m_oldPath == 0)
      {
        m_oldPath = path;
      }
      if (m_isPauseEnabled && !m_isPause && m_oldPath != path)
      {
        std::cout << "Turning on pause ..." << std::endl;
        m_isPause = true;
        m_oldPath = path;
        Time pauseTime = ipv4TLB->GetPauseTime(flowId);
        Simulator::Schedule(pauseTime, &TcpSocketBase::RecoverFromPause, this);
      }
    }

    // XXX Clove Support
    // 如果开启了Clove支持并且是发送端添加标签
    if (m_CloveEnabled)
    {
      if (m_CloveSendSide)
      {
        uint32_t flowId = TcpSocketBase::CalFlowId(m_endPoint->GetLocalAddress(),
                                                   m_endPoint->GetPeerAddress(), header.GetSourcePort(), header.GetDestinationPort());

        Ptr<Ipv4Clove> ipv4Clove = m_node->GetObject<Ipv4Clove>();
        uint32_t path = ipv4Clove->GetPath(flowId, m_endPoint->GetLocalAddress(), m_endPoint->GetPeerAddress());

        // XPath Support
        Ipv4XPathTag ipv4XPathTag;
        ipv4XPathTag.SetPathId(path);
        p->AddPacketTag(ipv4XPathTag);

        // Clove Support
        TcpCloveTag tcpCloveTag;
        tcpCloveTag.SetPath(path);
        p->AddPacketTag(tcpCloveTag);
      }
    }

    //如果开启了Pause则暂时缓存
    if (m_isPause)
    {
      std::cout << "Pause enabled, buffering packet ..." << std::endl;
      m_pauseBuffer->BufferItem(p, header);
    }
    else //否则直接发送
    {
      m_tcp->SendPacket(p, header, m_endPoint->GetLocalAddress(),
                        m_endPoint->GetPeerAddress(), m_boundnetdevice);
    }
    NS_LOG_DEBUG("Send segment of size " << sz << " with remaining data " << remainingData << " via TcpL4Protocol to " << m_endPoint->GetPeerAddress() << ". Header " << header);
  }
  else //否则IPV6支持
  {
    m_tcp->SendPacket(p, header, m_endPoint6->GetLocalAddress(),
                      m_endPoint6->GetPeerAddress(), m_boundnetdevice);
    NS_LOG_DEBUG("Send segment of size " << sz << " with remaining data " << remainingData << " via TcpL4Protocol to " << m_endPoint6->GetPeerAddress() << ". Header " << header);
  }
  //更新rtt历史
  UpdateRttHistory(seq, sz, isRetransmission);

  // Notify the application of the data being sent unless this is a retransmit
  // 通知应用已重传
  if (seq + sz > m_highTxMark)
  {
    Simulator::ScheduleNow(&TcpSocketBase::NotifyDataSent, this, (seq + sz - m_highTxMark.Get()));
  }
  // Update highTxMark
  // 更新
  m_highTxMark = std::max(seq + sz, m_highTxMark.Get());
  return sz;
}

//更新RTT历史
void TcpSocketBase::UpdateRttHistory(const SequenceNumber32 &seq, uint32_t sz,
                                     bool isRetransmission)
{
  NS_LOG_FUNCTION(this);

  // update the history of sequence numbers used to calculate the RTT
  if (isRetransmission == false)
  { // This is the next expected one, just log at end
    m_history.push_back(RttHistory(seq, sz, Simulator::Now()));
  }
  else
  { // This is a retransmit, find in list and mark as re-tx
    for (RttHistory_t::iterator i = m_history.begin(); i != m_history.end(); ++i)
    {
      if ((seq >= i->seq) && (seq < (i->seq + SequenceNumber32(i->count))))
      { // Found it
        i->retx = true;
        i->count = ((seq + SequenceNumber32(sz)) - i->seq); // And update count in hist
        break;
      }
    }
  }
}

/* Send as much pending data as possible according to the Tx window. Note that
 *  this function did not implement the PSH flag
 */
//发包并且返回发的包数
bool TcpSocketBase::SendPendingData(bool withAck)
{
  NS_LOG_FUNCTION(this << withAck);
  if (m_txBuffer->Size() == 0)
  {
    return false; // Nothing to send
  }
  if (m_endPoint == 0 && m_endPoint6 == 0)
  {
    NS_LOG_INFO("TcpSocketBase::SendPendingData: No endpoint; m_shutdownSend=" << m_shutdownSend);
    return false; // Is this the right way to handle this condition?
  }
  uint32_t nPacketsSent = 0;
  // 得到从m_nextTxSequence到buff最后有多少字节
  while (m_txBuffer->SizeFromSequence(m_nextTxSequence))
  {
    //得到窗口还有多少
    uint32_t w = AvailableWindow(); // Get available window size
    // Stop sending if we need to wait for a larger Tx window (prevent silly window syndrome)
    // 窗口大小不够，所以等等
    if (w < m_tcb->m_segmentSize && m_txBuffer->SizeFromSequence(m_nextTxSequence) > w)
    {
      NS_LOG_LOGIC("Preventing Silly Window Syndrome. Wait to send.");
      break; // No more
    }
    // Nagle's algorithm (RFC896): Hold off sending if there is unacked data
    // in the buffer and the amount of data to send is less than one segment
    if (!m_noDelay && UnAckDataCount() > 0 && m_txBuffer->SizeFromSequence(m_nextTxSequence) < m_tcb->m_segmentSize)
    {
      NS_LOG_LOGIC("Invoking Nagle's algorithm. Wait to send.");
      break;
    }
    NS_LOG_LOGIC("TcpSocketBase " << this << " SendPendingData"
                                  << " w " << w << " rxwin " << m_rWnd << " segsize " << m_tcb->m_segmentSize << " nextTxSeq " << m_nextTxSequence << " highestRxAck " << m_txBuffer->HeadSequence() << " pd->Size " << m_txBuffer->Size() << " pd->SFS " << m_txBuffer->SizeFromSequence(m_nextTxSequence));

    NS_LOG_DEBUG("Window: " << w << " cWnd: " << m_tcb->m_cWnd << " unAck: " << UnAckDataCount());
    //发包
    uint32_t s = std::min(w, m_tcb->m_segmentSize); // Send no more than window
    uint32_t sz = SendDataPacket(m_nextTxSequence, s, withAck);
    nPacketsSent++;         // Count sent this loop
    m_nextTxSequence += sz; // Advance next tx sequence
    if (nPacketsSent == 2)  //最多发送两个包就break
    {
      break;
    }
  }
  if (nPacketsSent > 0)
  {
    NS_LOG_DEBUG("SendPendingData sent " << nPacketsSent << " segments");
  }
  return (nPacketsSent > 0);
}

//得到没有ACK的字节数
uint32_t
TcpSocketBase::UnAckDataCount() const
{
  NS_LOG_FUNCTION(this);
  return m_nextTxSequence.Get() - m_txBuffer->HeadSequence();
}

//返回正在传输的字节量
uint32_t
TcpSocketBase::BytesInFlight()
{
  NS_LOG_FUNCTION(this);
  // Previous (see bug 1783):
  // uint32_t bytesInFlight = m_highTxMark.Get () - m_txBuffer->HeadSequence ();
  // RFC 4898 page 23
  // PipeSize=SND.NXT-SND.UNA+(retransmits-dupacks)*CurMSS

  // flightSize == UnAckDataCount (), but we avoid the call to save log lines
  uint32_t flightSize = m_nextTxSequence.Get() - m_txBuffer->HeadSequence();
  uint32_t duplicatedSize;
  uint32_t bytesInFlight;

  if (m_retransOut > m_dupAckCount)
  { //每一个ACK增大一次窗口？即使是DUPACK？
    duplicatedSize = (m_retransOut - m_dupAckCount) * m_tcb->m_segmentSize;
    bytesInFlight = flightSize + duplicatedSize;
  }
  else
  {
    duplicatedSize = (m_dupAckCount - m_retransOut) * m_tcb->m_segmentSize;
    bytesInFlight = duplicatedSize > flightSize ? 0 : flightSize - duplicatedSize;
  }

  // m_bytesInFlight is traced; avoid useless assignments which would fire
  // fruitlessly the callback
  if (m_bytesInFlight != bytesInFlight)
  {
    m_bytesInFlight = bytesInFlight;
  }

  return bytesInFlight;
}

//返回拥塞窗口大小
uint32_t
TcpSocketBase::Window(void) const
{
  NS_LOG_FUNCTION(this);
  return std::min(m_rWnd.Get(), m_tcb->m_cWnd.Get());
}

//返回窗口中还有的空间，得到窗口的大小和未ACK的数据量求还有的空间
uint32_t
TcpSocketBase::AvailableWindow() const
{
  NS_LOG_FUNCTION_NOARGS();
  uint32_t unack = UnAckDataCount(); // Number of outstanding bytes
  uint32_t win = Window();           // Number of bytes allowed to be outstanding

  NS_LOG_DEBUG("UnAckCount=" << unack << ", Win=" << win);
  return (win < unack) ? 0 : (win - unack);
}

//告诉对方Rx Window的大小
uint16_t
TcpSocketBase::AdvertisedWindowSize() const
{
  uint32_t w = m_rxBuffer->MaxBufferSize();

  w >>= m_rcvWindShift;

  if (w > m_maxWinSize)
  {
    NS_LOG_WARN("There is a loss in the adv win size, wrt buffer size");
    w = m_maxWinSize;
  }

  return (uint16_t)w;
}

// Receipt of new packet, put into Rx buffer
// 收到新包将其放至RxBuf
void TcpSocketBase::ReceivedData(Ptr<Packet> p, const TcpHeader &tcpHeader)
{
  NS_LOG_FUNCTION(this << tcpHeader);
  NS_LOG_DEBUG("Data segment, seq=" << tcpHeader.GetSequenceNumber() << " pkt size=" << p->GetSize());

  uint8_t sendflags = TcpHeader::ACK;

  // XXX ECN Support We should set the ECE flag in TCP if there is CE in IP header
  if (m_tcb->m_ecnConn) // First, the connection should be ECN capable
  {
    // 得到ECN标签
    Ipv4EcnTag ipv4EcnTag;
    bool found = p->RemovePacketTag(ipv4EcnTag);
    //重传的包
    if (found && ipv4EcnTag.GetEcn() == Ipv4Header::ECN_NotECT && m_tcb->m_ecnSeen) // We have seen ECN before
    {
      NS_LOG_LOGIC(this << " Received Not ECT packet but we have seen ecn, maybe retransmission");
    }
    //无拥塞情况
    if (found && ipv4EcnTag.GetEcn() == Ipv4Header::ECN_ECT1)
    {
      NS_LOG_LOGIC(this << " Received ECT1, notify the congestion control algorithm of the non congestion");
      m_tcb->m_ecnSeen = true;
      m_congestionControl->CwndEvent(m_tcb, TcpCongestionOps::CA_EVENT_ECN_NO_CE, this);
    }
    //有拥塞情况
    if (found && ipv4EcnTag.GetEcn() == Ipv4Header::ECN_CE)
    {
      NS_LOG_LOGIC(this << " Received CE, notify the congestion control algorithm of the congestion");
      m_tcb->m_demandCWR = true; //设置m_demandCWR为true，表示需要看到回应
      m_tcb->m_ecnSeen = true;
      m_congestionControl->CwndEvent(m_tcb, TcpCongestionOps::CA_EVENT_ECN_IS_CE, this);
    }
  }

  // XXX ECN Support We should set demandCWR to false when we have received CWR codepoint
  // 如果收到了CWR标记的包表示对方已经收到了ECN标记的ack并降低了发送速率
  // 将m_demandCWR设为false
  if (m_tcb->m_ecnConn && tcpHeader.GetFlags() & TcpHeader::CWR)
  {
    NS_LOG_LOGIC(this << "Received CWR in TCP header");
    m_tcb->m_demandCWR = false;
  }
  // 如果m_demandCWR为true表示看到了ECN，需要在ACK时也返回ECN
  if (m_tcb->m_demandCWR)
  {
    sendflags |= TcpHeader::ECE;
  }

  // XXX TLB Support
  // 如果支持TLB并且是的接收方
  if (m_TLBEnabled && !m_TLBSendSide)
  {
    //得到TLB标签并且取得相应信息
    TcpTLBTag tcpTLBTag;
    bool found = p->RemovePacketTag(tcpTLBTag);
    if (found)
    {
      m_piggybackTLBInfo = true;
      m_onewayRtt = Simulator::Now() - tcpTLBTag.GetTime();
      m_TLBPath = tcpTLBTag.GetPath();
    }
  }

  // XXX Clove Support
  // 如果支持Clove并且是接收方
  if (m_CloveEnabled && !m_CloveSendSide)
  {
    TcpCloveTag tcpCloveTag;
    bool found = p->RemovePacketTag(tcpCloveTag);
    if (found)
    {
      m_piggybackCloveInfo = true;
      m_ClovePath = tcpCloveTag.GetPath();
    }
  }

  // Put into Rx buffer
  SequenceNumber32 expectedSeq = m_rxBuffer->NextRxSequence();
  if (!m_rxBuffer->Add(p, tcpHeader)) //这里会将包插入buff，如果失败，则退出
  {                                   // Insert failed: No data or RX buffer full
    SendEmptyPacket(sendflags);
    return;
  }
  // Now send a new ACK packet acknowledging all received and delivered data
  // 如果buff实际上占用空间大于可读空间证明中间有gap，则返回ACK
  if (m_rxBuffer->Size() > m_rxBuffer->Available() || m_rxBuffer->NextRxSequence() > expectedSeq + p->GetSize())
  { // A gap exists in the buffer, or we filled a gap: Always ACK
    SendEmptyPacket(sendflags);
  }
  else
  { // In-sequence packet: ACK if delayed ack count allows
    // 如果足够的ACK数量，则返回一个ACK，同时取消事件
    if (++m_delAckCount >= m_delAckMaxCount)
    {
      m_congestionControl->CwndEvent(m_tcb, TcpCongestionOps::CA_EVENT_DELAY_ACK_NO_RESERVED, this);
      m_delAckEvent.Cancel();
      m_delAckCount = 0;
      SendEmptyPacket(sendflags);
    }
    else if (m_delAckEvent.IsExpired()) //如果delay Ack事件已经过期则重新调度
    {
      m_congestionControl->CwndEvent(m_tcb, TcpCongestionOps::CA_EVENT_DELAY_ACK_RESERVED, this);
      m_delAckEvent = Simulator::Schedule(m_delAckTimeout,
                                          &TcpSocketBase::DelAckTimeout, this);
      NS_LOG_LOGIC(this << " scheduled delayed ACK at " << (Simulator::Now() + Simulator::GetDelayLeft(m_delAckEvent)).GetSeconds());
    }
  }
  // Notify app to receive if necessary
  if (expectedSeq < m_rxBuffer->NextRxSequence())
  {                      // NextRxSeq advanced, we have something to send to the app
    if (!m_shutdownRecv) //如果已经关闭了接收，则通知上层来取数据
    {
      NotifyDataRecv();
    }
    // Handle exceptions
    if (m_closeNotified) //如果已经关闭又收到数据则报错了
    {
      NS_LOG_WARN("Why TCP " << this << " got data after close notification?");
    }
    // If we received FIN before and now completed all "holes" in rx buffer,
    // invoke peer close procedure
    // 如果之前收到过结束命令，现在补全所有漏洞后就结束
    // Finished函数检查是否收到了所有数据或连接已经关闭
    if (m_rxBuffer->Finished() && (tcpHeader.GetFlags() & TcpHeader::FIN) == 0)
    {
      DoPeerClose();
    }
  }
}

/**
 * \brief Estimate the RTT
 *
 * Called by ForwardUp() to estimate RTT.
 *
 * \param tcpHeader TCP header for the incoming packet
 */
void TcpSocketBase::EstimateRtt(const TcpHeader &tcpHeader)
{
  SequenceNumber32 ackSeq = tcpHeader.GetAckNumber();
  Time m = Time(0.0);

  // An ack has been received, calculate rtt and log this measurement
  // Note we use a linear search (O(n)) for this since for the common
  // case the ack'ed packet will be at the head of the list
  if (!m_history.empty())
  {
    RttHistory &h = m_history.front();
    if (!h.retx && ackSeq >= (h.seq + SequenceNumber32(h.count)))
    { // Ok to use this sample
      if (m_timestampEnabled && tcpHeader.HasOption(TcpOption::TS))
      {
        Ptr<TcpOptionTS> ts;
        ts = DynamicCast<TcpOptionTS>(tcpHeader.GetOption(TcpOption::TS));
        m = TcpOptionTS::ElapsedTimeFromTsValue(ts->GetEcho());
      }
      else
      {
        m = Simulator::Now() - h.time; // Elapsed time
      }
    }
  }

  // Now delete all ack history with seq <= ack
  // TODO
  while (!m_history.empty())
  {
    RttHistory &h = m_history.front();
    if ((h.seq + SequenceNumber32(h.count)) > ackSeq)
    {
      break; // Done removing
    }
    m_history.pop_front(); // Remove
  }

  if (!m.IsZero())
  {
    m_rtt->Measurement(m); // Log the measurement
    // RFC 6298, clause 2.4
    m_rto = Max(m_rtt->GetEstimate() + Max(m_clockGranularity, m_rtt->GetVariation() * 4), m_minRto);
    m_lastRtt = m_rtt->GetEstimate();
    NS_LOG_FUNCTION(this << m_lastRtt);
  }
}

// Called by the ReceivedAck() when new ACK received and by ProcessSynRcvd()
// when the three-way handshake completed. This cancels retransmission timer
// and advances Tx window
// 更新txBuff
void TcpSocketBase::NewAck(SequenceNumber32 const &ack, bool resetRTO)
{
  NS_LOG_FUNCTION(this << ack);

  if (m_state != SYN_RCVD && resetRTO)
  { // Set RTO unless the ACK is received in SYN_RCVD state
    NS_LOG_LOGIC(this << " Cancelled ReTxTimeout event which was set to expire at " << (Simulator::Now() + Simulator::GetDelayLeft(m_retxEvent)).GetSeconds());
    m_retxEvent.Cancel(); //取消重传事件
    m_urgePktEvent.Cancel();
    // On receiving a "New" ack we restart retransmission timer .. RFC 6298
    // RFC 6298, clause 2.4
    // 更新RTO

    //if (m_cacheable) m_minRto = m_tmpMinRto;
    m_rto = Max(m_rtt->GetEstimate() + Max(m_clockGranularity, m_rtt->GetVariation() * 4), m_minRto);

    NS_LOG_LOGIC(this << " Schedule ReTxTimeout at time " << Simulator::Now().GetSeconds() << " to expire at time " << (Simulator::Now() + m_rto.Get()).GetSeconds());
    // 设置重传事件

    if (m_cacheable && m_enableUrgeSend)
    {
      m_urgePktEvent = Simulator::Schedule(m_rto * 4 / 5, &TcpSocketBase::SendUrgePacket, this, m_urgeNum);
    }

    m_retxEvent = Simulator::Schedule(m_rto, &TcpSocketBase::ReTxTimeout, this);
  }

  // Note the highest ACK and tell app to send more
  NS_LOG_LOGIC("TCP " << this << " NewAck " << ack << " numberAck " << (ack - m_txBuffer->HeadSequence())); // Number bytes ack'ed
  m_txBuffer->DiscardUpTo(ack);                                                                             //丢弃这个ack号码之前的包，不包括这个ack
  // 如果还有可发送的数据，则发送
  if (GetTxAvailable() > 0)
  {
    NotifySend(GetTxAvailable());
  }
  //更新接下来要发送的顺序
  if (ack > m_nextTxSequence)
  {
    m_nextTxSequence = ack; // If advanced
  }
  //如果没数据可发送，则取消超时事件
  if (m_txBuffer->Size() == 0 && m_state != FIN_WAIT_1 && m_state != CLOSING)
  { // No retransmit timer if no data to retransmit
    NS_LOG_LOGIC(this << " Cancelled ReTxTimeout event which was set to expire at " << (Simulator::Now() + Simulator::GetDelayLeft(m_retxEvent)).GetSeconds());
    m_retxEvent.Cancel();
  }
}

// Retransmit timeout
// 重传超时
void TcpSocketBase::ReTxTimeout()
{
  NS_LOG_FUNCTION(this);
  NS_LOG_LOGIC(this << " ReTxTimeout Expired at time " << Simulator::Now().GetSeconds());
  // If erroneous timeout in closed/timed-wait state, just return
  if (m_state == CLOSED || m_state == TIME_WAIT)
  {
    return;
  }
  // If all data are received (non-closing socket and nothing to send), just return
  if (m_state <= ESTABLISHED && m_txBuffer->HeadSequence() >= m_highTxMark)
  {
    return;
  }

  m_recover = m_highTxMark;
  Retransmit();
  NS_LOG_DEBUG("Retransmit: retransmit time out " << ++m_retranTime);
}

//delay ACK超时，发送一个ACK
void TcpSocketBase::DelAckTimeout(void)
{
  m_congestionControl->CwndEvent(m_tcb, TcpCongestionOps::CA_EVENT_DELAY_ACK_NO_RESERVED, this);
  m_delAckCount = 0;
  uint32_t sendflags = TcpHeader::ACK;
  if (m_tcb->m_ecnConn && m_tcb->m_demandCWR)
  {
    sendflags |= TcpHeader::ECE;
  }
  SendEmptyPacket(sendflags);
}

//LastAck状态超时，lastAck状态为在对方shutdown后才shutdown但仍有数据要发送
void TcpSocketBase::LastAckTimeout(void)
{
  NS_LOG_FUNCTION(this);

  m_lastAckEvent.Cancel();
  if (m_state == LAST_ACK)
  {
    CloseAndNotify();
  }
  if (!m_closeNotified) //tell APP 关闭Socket
  {
    m_closeNotified = true;
  }
}

// Send 1-byte data to probe for the window size at the receiver when
// the local knowledge tells that the receiver has zero window size
// C.f.: RFC793 p.42, RFC1112 sec.4.2.2.17
// 当本地信息显示接收端窗口为0时，发送一个字节的数据来探测接收端的拥塞窗口
void TcpSocketBase::PersistTimeout()
{
  NS_LOG_LOGIC("PersistTimeout expired at " << Simulator::Now().GetSeconds());
  m_persistTimeout = std::min(Seconds(60), Time(2 * m_persistTimeout)); // max persist timeout = 60s
  Ptr<Packet> p = m_txBuffer->CopyFromSequence(1, m_nextTxSequence);
  TcpHeader tcpHeader;
  tcpHeader.SetSequenceNumber(m_nextTxSequence);
  tcpHeader.SetAckNumber(m_rxBuffer->NextRxSequence());
  tcpHeader.SetWindowSize(AdvertisedWindowSize());
  if (m_endPoint != 0)
  {
    tcpHeader.SetSourcePort(m_endPoint->GetLocalPort());
    tcpHeader.SetDestinationPort(m_endPoint->GetPeerPort());
  }
  else
  {
    tcpHeader.SetSourcePort(m_endPoint6->GetLocalPort());
    tcpHeader.SetDestinationPort(m_endPoint6->GetPeerPort());
  }
  AddOptions(tcpHeader); //添加时间戳和scale选项

  m_txTrace(p, tcpHeader, this);

  if (m_endPoint != 0)
  {
    TcpSocketBase::AttachFlowId(p, m_endPoint->GetLocalAddress(),
                                m_endPoint->GetPeerAddress(), tcpHeader.GetSourcePort(), tcpHeader.GetDestinationPort());

    m_tcp->SendPacket(p, tcpHeader, m_endPoint->GetLocalAddress(),
                      m_endPoint->GetPeerAddress(), m_boundnetdevice);
  }
  else
  {
    m_tcp->SendPacket(p, tcpHeader, m_endPoint6->GetLocalAddress(),
                      m_endPoint6->GetPeerAddress(), m_boundnetdevice);
  }

  NS_LOG_LOGIC("Schedule persist timeout at time "
               << Simulator::Now().GetSeconds() << " to expire at time "
               << (Simulator::Now() + m_persistTimeout).GetSeconds());
  m_persistEvent = Simulator::Schedule(m_persistTimeout, &TcpSocketBase::PersistTimeout, this);
}

//重传，调用DoRetransmit()
void TcpSocketBase::Retransmit()
{
  // If erroneous timeout in closed/timed-wait state, just return
  if (m_state == CLOSED || m_state == TIME_WAIT)
  {
    return;
  }
  // If all data are received (non-closing socket and nothing to send), just return
  // 如果连接正常，但是拥塞窗口的初值大于当前发送过的最大序列号值表明没有丢包，不用重传
  if (m_state <= ESTABLISHED && m_txBuffer->HeadSequence() >= m_highTxMark)
  {
    return;
  }

  /*
   * When a TCP sender detects segment loss using the retransmission timer
   * and the given segment has not yet been resent by way of the
   * retransmission timer, the value of ssthresh MUST be set to no more
   * than the value given in equation (4):
   *
   *   ssthresh = max (FlightSize / 2, 2*SMSS)            (4)
   *
   * where, as discussed above, FlightSize is the amount of outstanding
   * data in the network.
   *
   * On the other hand, when a TCP sender detects segment loss using the
   * retransmission timer and the given segment has already been
   * retransmitted by way of the retransmission timer at least once, the
   * value of ssthresh is held constant.
   *
   * Conditions to decrement slow - start threshold are as follows:
   *
   * *) The TCP state should be less than disorder, which is nothing but open.
   * If we are entering into the loss state from the open state, we have not yet
   * reduced the slow - start threshold for the window of data. (Nat: Recovery?)
   * *) If we have entered the loss state with all the data pointed to by high_seq
   * acknowledged. Once again it means that in whatever state we are (other than
   * open state), all the data from the window that got us into the state, prior to
   * retransmission timer expiry, has been acknowledged. (Nat: How this can happen?)
   * *) If the above two conditions fail, we still have one more condition that can
   * demand reducing the slow - start threshold: If we are already in the loss state
   * and have not yet retransmitted anything. The condition may arise in case we
   * are not able to retransmit anything because of local congestion.
   */
  //如果有丢包发生
  if (m_tcb->m_congState != TcpSocketState::CA_LOSS)
  {
    // XXX TLB Support
    //如果运行TLB，则调用TLB的超时事件
    if (m_TLBEnabled)
    {
      uint32_t flowId = TcpSocketBase::CalFlowId(m_endPoint->GetLocalAddress(),
                                                 m_endPoint->GetPeerAddress(), m_endPoint->GetLocalPort(), m_endPoint->GetPeerPort());
      Ptr<Ipv4TLB> ipv4TLB = m_node->GetObject<Ipv4TLB>();
      ipv4TLB->FlowTimeout(flowId, m_endPoint->GetPeerAddress(), m_pathAcked);
    }
    m_tcb->m_congState = TcpSocketState::CA_LOSS;
    m_tcb->m_ssThresh = m_congestionControl->GetSsThresh(m_tcb, BytesInFlight());
    m_tcb->m_cWnd = m_tcb->m_segmentSize;
  }
  //得到当前要发送的序列号即发送buff中的第一个
  m_nextTxSequence = m_txBuffer->HeadSequence(); // Restart from highest Ack
  m_dupAckCount = 0;                             //将重复的ACK计数器清零

  NS_LOG_DEBUG("Retransmit: RTO Timeout." << ++m_retranTimeout << " Reset cwnd to " << m_tcb->m_cWnd << ", ssthresh to " << m_tcb->m_ssThresh << ", restart from seqnum " << m_nextTxSequence);
  DoRetransmit(); // Retransmit the packet
}

//重传
void TcpSocketBase::DoRetransmit()
{
  NS_LOG_FUNCTION(this);
  // Retransmit SYN packet
  // 重传SYN包
  if (m_state == SYN_SENT) //如果重试连接的次数还大于0,则重试
  {
    if (m_synCount > 0)
    {
      SendEmptyPacket(TcpHeader::SYN);
    }
    else
    {
      NotifyConnectionFailed(); //否则直接通知无法连接
    }
    return;
  }

  if (m_dataRetrCount == 0) //如果重传数据包的次数还为0
  {
    NS_LOG_INFO("No more data retries available. Dropping connection");
    NotifyErrorClose();   //通知无法重传
    DeallocateEndPoint(); //断开连接
    return;
  }
  else
  {
    --m_dataRetrCount; //否则数量减
  }

  // Retransmit non-data packet: Only if in FIN_WAIT_1 or CLOSING state
  // 如果发送端buff中无数据并且状态为如下两个状态，则是丢失了FIN，重传
  if (m_txBuffer->Size() == 0)
  {
    if (m_state == FIN_WAIT_1 || m_state == CLOSING)
    { // Must have lost FIN, re-send
      SendEmptyPacket(TcpHeader::FIN);
    }
    return;
  }

  // Retransmit a data packet: Call SendDataPacket
  // 重传数据包
  uint32_t sz = SendDataPacket(m_txBuffer->HeadSequence(), m_tcb->m_segmentSize, true);
  ++m_retransOut;      //这个窗口重传的次数
  ++m_retransmit_time; //add by myself

  // In case of RTO, advance m_nextTxSequence
  // 得到接下来要传的序列号的值，因为和重传的有可能是序列号较小的包
  m_nextTxSequence = std::max(m_nextTxSequence.Get(), m_txBuffer->HeadSequence() + sz);

  NS_LOG_DEBUG("retxing seq " << m_txBuffer->HeadSequence());
}

//取消所有的Timer
void TcpSocketBase::CancelAllTimers()
{
  m_retxEvent.Cancel();
  m_urgePktEvent.Cancel();
  m_persistEvent.Cancel();
  m_delAckEvent.Cancel();
  m_lastAckEvent.Cancel();
  m_timewaitEvent.Cancel();
  m_sendPendingDataEvent.Cancel();
}

//在关闭TCP连接转到TIME_WAIT状态进行过渡然后到关闭状态
/* Move TCP to Time_Wait state and schedule a transition to Closed state */
void TcpSocketBase::TimeWait()
{
  NS_LOG_DEBUG(TcpStateName[m_state] << " -> TIME_WAIT");
  m_state = TIME_WAIT;
  CancelAllTimers();
  // Move from TIME_WAIT to CLOSED after 2*MSL. Max segment lifetime is 2 min
  // according to RFC793, p.28
  m_timewaitEvent = Simulator::Schedule(Seconds(2 * m_msl),
                                        &TcpSocketBase::CloseAndNotify, this);
}

/* Below are the attribute get/set functions */

void TcpSocketBase::SetSndBufSize(uint32_t size)
{
  NS_LOG_FUNCTION(this << size);
  m_txBuffer->SetMaxBufferSize(size);
}

uint32_t
TcpSocketBase::GetSndBufSize(void) const
{
  return m_txBuffer->MaxBufferSize();
}

//增加接收端的窗口大小
void TcpSocketBase::SetRcvBufSize(uint32_t size)
{
  NS_LOG_FUNCTION(this << size);
  uint32_t oldSize = GetRcvBufSize();

  m_rxBuffer->SetMaxBufferSize(size);

  /* The size has (manually) increased. Actively inform the other end to prevent
   * stale zero-window states.
   */
  //如果窗口增大了，则发送ACK使接收端增大发送量
  if (oldSize < size && m_connected)
  {
    SendEmptyPacket(TcpHeader::ACK);
  }
}

uint32_t
TcpSocketBase::GetRcvBufSize(void) const
{
  return m_rxBuffer->MaxBufferSize();
}

void TcpSocketBase::SetSegSize(uint32_t size)
{
  NS_LOG_FUNCTION(this << size);
  m_tcb->m_segmentSize = size;

  NS_ABORT_MSG_UNLESS(m_state == CLOSED, "Cannot change segment size dynamically.");
}

uint32_t
TcpSocketBase::GetSegSize(void) const
{
  return m_tcb->m_segmentSize;
}

void TcpSocketBase::SetConnTimeout(Time timeout)
{
  NS_LOG_FUNCTION(this << timeout);
  m_cnTimeout = timeout;
}

Time TcpSocketBase::GetConnTimeout(void) const
{
  return m_cnTimeout;
}

void TcpSocketBase::SetSynRetries(uint32_t count)
{
  NS_LOG_FUNCTION(this << count);
  m_synRetries = count;
}

uint32_t
TcpSocketBase::GetSynRetries(void) const
{
  return m_synRetries;
}

void TcpSocketBase::SetDataRetries(uint32_t retries)
{
  NS_LOG_FUNCTION(this << retries);
  m_dataRetries = retries;
}

uint32_t
TcpSocketBase::GetDataRetries(void) const
{
  NS_LOG_FUNCTION(this);
  return m_dataRetries;
}

void TcpSocketBase::SetDelAckTimeout(Time timeout)
{
  NS_LOG_FUNCTION(this << timeout);
  m_delAckTimeout = timeout;
}

Time TcpSocketBase::GetDelAckTimeout(void) const
{
  return m_delAckTimeout;
}

void TcpSocketBase::SetDelAckMaxCount(uint32_t count)
{
  NS_LOG_FUNCTION(this << count);
  m_delAckMaxCount = count;
}

uint32_t
TcpSocketBase::GetDelAckMaxCount(void) const
{
  return m_delAckMaxCount;
}

void TcpSocketBase::SetTcpNoDelay(bool noDelay)
{
  NS_LOG_FUNCTION(this << noDelay);
  m_noDelay = noDelay;
}

bool TcpSocketBase::GetTcpNoDelay(void) const
{
  return m_noDelay;
}

void TcpSocketBase::SetPersistTimeout(Time timeout)
{
  NS_LOG_FUNCTION(this << timeout);
  m_persistTimeout = timeout;
}

Time TcpSocketBase::GetPersistTimeout(void) const
{
  return m_persistTimeout;
}

bool TcpSocketBase::SetAllowBroadcast(bool allowBroadcast)
{
  // Broadcast is not implemented. Return true only if allowBroadcast==false
  return (!allowBroadcast);
}

bool TcpSocketBase::GetAllowBroadcast(void) const
{
  return false;
}

//添加选项
void TcpSocketBase::AddOptions(TcpHeader &header)
{
  NS_LOG_FUNCTION(this << header);

  // The window scaling option is set only on SYN packets
  if (m_winScalingEnabled && (header.GetFlags() & TcpHeader::SYN))
  {
    AddOptionWScale(header);
  }

  if (m_timestampEnabled)
  {
    AddOptionTimestamp(header);
  }
}

//得到选项中的scale值
void TcpSocketBase::ProcessOptionWScale(const Ptr<const TcpOption> option)
{
  NS_LOG_FUNCTION(this << option);

  Ptr<const TcpOptionWinScale> ws = DynamicCast<const TcpOptionWinScale>(option);

  // In naming, we do the contrary of RFC 1323. The received scaling factor
  // is Rcv.Wind.Scale (and not Snd.Wind.Scale)
  m_sndWindShift = ws->GetScale();

  if (m_sndWindShift > 14)
  {
    NS_LOG_WARN("Possible error; m_sndWindShift exceeds 14: " << m_sndWindShift);
    m_sndWindShift = 14;
  }

  NS_LOG_INFO(m_node->GetId() << " Received a scale factor of " << static_cast<int>(m_sndWindShift));
}

//得到现在的rxBuff是最大拥塞窗口的几倍
uint8_t
TcpSocketBase::CalculateWScale() const
{
  NS_LOG_FUNCTION(this);
  uint32_t maxSpace = m_rxBuffer->MaxBufferSize();
  uint8_t scale = 0;

  while (maxSpace > m_maxWinSize)
  {
    maxSpace = maxSpace >> 1;
    ++scale;
  }

  if (scale > 14)
  {
    NS_LOG_WARN("Possible error; scale exceeds 14: " << scale);
    scale = 14;
  }

  NS_LOG_INFO("Node " << m_node->GetId() << " calculated wscale factor of " << static_cast<int>(scale) << " for buffer size " << m_rxBuffer->MaxBufferSize());
  return scale;
}

//将CalculatedScale的值加入选项
void TcpSocketBase::AddOptionWScale(TcpHeader &header)
{
  NS_LOG_FUNCTION(this << header);
  NS_ASSERT(header.GetFlags() & TcpHeader::SYN);

  Ptr<TcpOptionWinScale> option = CreateObject<TcpOptionWinScale>();

  // In naming, we do the contrary of RFC 1323. The sended scaling factor
  // is Snd.Wind.Scale (and not Rcv.Wind.Scale)

  m_rcvWindShift = CalculateWScale();
  option->SetScale(m_rcvWindShift);

  header.AppendOption(option);

  NS_LOG_INFO(m_node->GetId() << " Send a scaling factor of " << static_cast<int>(m_rcvWindShift));
}

//得到选项中的时间戳
void TcpSocketBase::ProcessOptionTimestamp(const Ptr<const TcpOption> option,
                                           const SequenceNumber32 &seq)
{
  NS_LOG_FUNCTION(this << option);

  Ptr<const TcpOptionTS> ts = DynamicCast<const TcpOptionTS>(option);

  if (seq == m_rxBuffer->NextRxSequence() && seq <= m_highTxAck)
  {
    m_timestampToEcho = ts->GetTimestamp();
  }

  NS_LOG_INFO(m_node->GetId() << " Got timestamp=" << m_timestampToEcho << " and Echo=" << ts->GetEcho());
}

//在头部添加时间戳
void TcpSocketBase::AddOptionTimestamp(TcpHeader &header)
{
  NS_LOG_FUNCTION(this << header);

  Ptr<TcpOptionTS> option = CreateObject<TcpOptionTS>();

  option->SetTimestamp(TcpOptionTS::NowToTsValue());
  option->SetEcho(m_timestampToEcho);

  header.AppendOption(option);
  NS_LOG_INFO(m_node->GetId() << " Add option TS, ts=" << option->GetTimestamp() << " echo=" << m_timestampToEcho);
}

//更新接收端窗口大小
void TcpSocketBase::UpdateWindowSize(const TcpHeader &header)
{
  NS_LOG_FUNCTION(this << header);
  //  If the connection is not established, the window size is always
  //  updated
  uint32_t receivedWindow = header.GetWindowSize();
  receivedWindow <<= m_sndWindShift;
  NS_LOG_INFO("Received (scaled) window is " << receivedWindow << " bytes");
  if (m_state < ESTABLISHED)
  {
    m_rWnd = receivedWindow;
    NS_LOG_LOGIC("State less than ESTABLISHED; updating rWnd to " << m_rWnd);
    return;
  }

  // Test for conditions that allow updating of the window
  // 1) segment contains new data (advancing the right edge of the receive
  // buffer),
  // 2) segment does not contain new data but the segment acks new data
  // (highest sequence number acked advances), or
  // 3) the advertised window is larger than the current send window
  bool update = false;
  if (header.GetAckNumber() == m_highRxAckMark && receivedWindow > m_rWnd)
  {
    // right edge of the send window is increased (window update)
    update = true;
  }
  if (header.GetAckNumber() > m_highRxAckMark)
  {
    m_highRxAckMark = header.GetAckNumber();
    update = true;
  }
  if (header.GetSequenceNumber() > m_highRxMark)
  {
    m_highRxMark = header.GetSequenceNumber();
    update = true;
  }
  if (update == true)
  {
    m_rWnd = receivedWindow;
    NS_LOG_LOGIC("updating rWnd to " << m_rWnd);
  }
}

void TcpSocketBase::SetMinRto(Time minRto)
{
  NS_LOG_FUNCTION(this << minRto);
  m_minRto = minRto;
}

Time TcpSocketBase::GetMinRto(void) const
{
  return m_minRto;
}

void TcpSocketBase::SetClockGranularity(Time clockGranularity)
{
  NS_LOG_FUNCTION(this << clockGranularity);
  m_clockGranularity = clockGranularity;
}

Time TcpSocketBase::GetClockGranularity(void) const
{
  return m_clockGranularity;
}

Ptr<TcpTxBuffer>
TcpSocketBase::GetTxBuffer(void) const
{
  return m_txBuffer;
}

Ptr<TcpRxBuffer>
TcpSocketBase::GetRxBuffer(void) const
{
  return m_rxBuffer;
}

Ptr<TcpResequenceBuffer>
TcpSocketBase::GetResequenceBuffer(void) const
{
  return m_resequenceBuffer;
}

//更新拥塞窗口
void TcpSocketBase::UpdateCwnd(uint32_t oldValue, uint32_t newValue)
{
  m_cWndTrace(oldValue, newValue);
}

//更新慢启动阀值
void TcpSocketBase::UpdateSsThresh(uint32_t oldValue, uint32_t newValue)
{
  m_ssThTrace(oldValue, newValue);
}

//更新拥塞状态
void TcpSocketBase::UpdateCongState(TcpSocketState::TcpCongState_t oldValue,
                                    TcpSocketState::TcpCongState_t newValue)
{
  m_congStateTrace(oldValue, newValue);
}

//设置拥塞控制算法
void TcpSocketBase::SetCongestionControlAlgorithm(Ptr<TcpCongestionOps> algo)
{
  NS_LOG_FUNCTION(this << algo);
  m_congestionControl = algo;
}

//返回一个副本
Ptr<TcpSocketBase>
TcpSocketBase::Fork(void)
{
  return CopyObject<TcpSocketBase>(this);
}

//安全减法不返回负数
uint32_t
TcpSocketBase::SafeSubtraction(uint32_t a, uint32_t b)
{
  if (a > b)
  {
    return a - b;
  }

  return 0;
}

//更改包的FlowId
void TcpSocketBase::AttachFlowId(Ptr<Packet> packet,
                                 const Ipv4Address &saddr, const Ipv4Address &daddr, uint16_t sport, uint16_t dport)
{
  // const static uint8_t PROT_NUMBER = 6;
  // XXX Per flow ECMP support
  // Calculate the flow id and store it in the packet flow id packet tag
  // NOTE Here we do not use the byte tag since we want the flow id tag to be applied to each packet
  // after TCP fragmentation

  // uint32_t flowId = 0;

  // flowId ^= saddr.Get();
  // flowId ^= daddr.Get();
  // flowId ^= sport;
  // flowId ^= (dport << 16);
  // flowId += PROT_NUMBER;

  uint32_t flowId = TcpSocketBase::CalFlowId(saddr, daddr, sport, dport);
  m_flowId = flowId;
  // XXX Flow Bender support
  if (m_flowBenderEnabled)
  {
    uint32_t path = m_flowBender->GetV();
    flowId += path;

    //如果开启了Pause支持
    // Pause Support
    if (m_isPauseEnabled && m_oldPath == 0)
    {
      m_oldPath = path;
    }
    if (m_isPauseEnabled && !m_isPause && m_oldPath != 0 && m_oldPath != path)
    {
      std::cout << "Turning on pause ..." << std::endl;
      m_isPause = true;
      m_oldPath = path;
      Time pauseTime = m_flowBender->GetPauseTime();
      //在pauseTime后从Pause中恢复过来
      Simulator::Schedule(pauseTime, &TcpSocketBase::RecoverFromPause, this);
    }
  }

  //更改Tag
  packet->AddPacketTag(FlowIdTag(flowId));
}

/************************************************Modify*********************************/
//计算flowId,根据目的地址与端口进行哈希
uint32_t
TcpSocketBase::CalFlowId(const Ipv4Address &saddr, const Ipv4Address &daddr,
                         uint16_t sport, uint16_t dport)
{
  // Time now = Simulator::Now();
  std::stringstream hash_string;
  //hash_string << now.GetMicroSeconds();
  hash_string << daddr.Get() << saddr.Get();
  hash_string << dport << sport;

  return Hash32(hash_string.str());
}
/************************************************Modify*********************************/

//将m_pauseBuff中的包都发送出去，并且关闭Pause
void TcpSocketBase::RecoverFromPause(void)
{
  std::cout << "Recovering from pause, flushing packets out..." << std::endl;
  while (m_pauseBuffer->HasBufferedItem())
  {
    struct TcpPauseItem item = m_pauseBuffer->GetBufferedItem();

    m_tcp->SendPacket(item.packet, item.header, m_endPoint->GetLocalAddress(),
                      m_endPoint->GetPeerAddress(), m_boundnetdevice);
  }
  m_isPause = false;
}

/***************************************************************/
void TcpSocketBase::SetNewArgu(bool cacheable, uint32_t retxThre)
{
  NS_LOG_FUNCTION(this);
  m_cacheable = cacheable;
  m_tmpRetxThre = retxThre;
}
/***************************************************************/

//初始化
//RttHistory methods
RttHistory::RttHistory(SequenceNumber32 s, uint32_t c, Time t)
    : seq(s),
      count(c),
      time(t),
      retx(false)
{
}

RttHistory::RttHistory(const RttHistory &h)
    : seq(h.seq),
      count(h.count),
      time(h.time),
      retx(h.retx)
{
}

} // namespace ns3
