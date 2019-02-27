/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#ifndef TCP_FLOW_BENDER
#define TCP_FLOW_BENDER

#include "ns3/object.h"
#include "ns3/event-id.h"
#include "ns3/nstime.h"
#include "ns3/sequence-number.h"

namespace ns3 {

class TcpFlowBender : public Object
{
public:
    static TypeId GetTypeId (void);

    TcpFlowBender ();
    TcpFlowBender (const TcpFlowBender &other);
    ~TcpFlowBender ();

    virtual void DoDispose (void);

    void ReceivedPacket (SequenceNumber32 higTxhMark, SequenceNumber32 ackNumber, uint32_t ackedBytes, bool withECE);

    uint32_t GetV ();

    Time GetPauseTime ();

private:

    void CheckCongestion ();

    // Variables
    uint32_t m_totalBytes;//总共的字节数
    uint32_t m_markedBytes; //标记ECN的字节数

    //拥塞的RTT计数
    uint32_t m_numCongestionRtt;
    uint32_t m_V; //T改变V值用于重路由

    SequenceNumber32 m_highTxMark; //目前为止见过的最大的ACK号码

    // Parameters
    Time m_rtt;
    double m_T; //被认为拥塞的ECN阀值
    uint32_t m_N;//连续几次标记ECN超过阀值则应该换道
};

}

#endif
