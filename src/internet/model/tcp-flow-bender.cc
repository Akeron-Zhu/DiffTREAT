#include "tcp-flow-bender.h"

#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/uinteger.h"
#include "ns3/double.h"

NS_LOG_COMPONENT_DEFINE ("TcpFlowBender");

namespace ns3 {

NS_OBJECT_ENSURE_REGISTERED (TcpFlowBender);

//得到TypeId
TypeId
TcpFlowBender::GetTypeId (void)
{
    static TypeId tid = TypeId ("ns3::TcpFlowBender")
        .SetParent<Object> ()
        .SetGroupName ("Internent")
        .AddConstructor<TcpFlowBender> ()
         .AddAttribute ("T", "The congestion degree within one RTT",
            DoubleValue (0.05),
            MakeDoubleAccessor (&TcpFlowBender::m_T),
            MakeDoubleChecker<double> (0))
        .AddAttribute ("N", "The number of allowed congestion RTT",
            UintegerValue (1),
            MakeUintegerAccessor (&TcpFlowBender::m_N),
            MakeUintegerChecker<uint32_t> ());

    return tid;
}
//初始化
TcpFlowBender::TcpFlowBender ()
    :Object (),
     m_totalBytes (0),
     m_markedBytes (0),
     m_numCongestionRtt (0),
     m_V (1),
     m_highTxMark (0),
     m_T (0.05),
     m_N (1)
{
    NS_LOG_FUNCTION (this);
}
//用一个同类型的变量初始化
TcpFlowBender::TcpFlowBender (const TcpFlowBender &other)
     :Object (),
     m_totalBytes (0),
     m_markedBytes (0),
     m_numCongestionRtt (0),
     m_V (1),
     m_highTxMark (0),
     m_T (other.m_T),
     m_N (other.m_N)
{
    NS_LOG_FUNCTION (this);
}

TcpFlowBender::~TcpFlowBender ()
{
    NS_LOG_FUNCTION (this);
}

void
TcpFlowBender::DoDispose (void)
{
    NS_LOG_FUNCTION (this);
}

//收到一个包后进行的处理
void
TcpFlowBender::ReceivedPacket (SequenceNumber32 highTxhMark, SequenceNumber32 ackNumber,
        uint32_t ackedBytes, bool withECE)
{
    NS_LOG_INFO (this << " High TX Mark: " << m_highTxMark << ", ACK Number: " << ackNumber);
    m_totalBytes += ackedBytes; //总字节数
    if (withECE) //如果被ECN标记了
    {
        m_markedBytes += ackedBytes; //增加标记的总字节数
    }
    if (ackNumber >= m_highTxMark) //如果ackNumber比m_highTxMark才进行检测是否拥塞，也即重传的包不进行检测
    {
        m_highTxMark = highTxhMark;
        TcpFlowBender::CheckCongestion ();
    }
}


uint32_t
TcpFlowBender::GetV ()
{
    NS_LOG_INFO ( this << " retuning V as " << m_V );
    return m_V;
}

Time
TcpFlowBender::GetPauseTime ()
{
    return MicroSeconds (80);
}
//检测是否拥塞
void
TcpFlowBender::CheckCongestion ()
{
    //被标记ECN字节数的比例
    double f = static_cast<double> (m_markedBytes) / m_totalBytes;
    NS_LOG_LOGIC (this << "\tMarked packet: " << m_markedBytes
                       << "\tTotal packet: " << m_totalBytes
                       << "\tf: " << f);
    if (f > m_T) //如果超过了阀值
    {
        m_numCongestionRtt ++; //累积次数加1
        if (m_numCongestionRtt >= m_N) //如果已经连续几次超过ECN阀值且次数大于N，应该换道
        {
            m_numCongestionRtt = 0; //clear
            // XXX Do we need to clear the congestion state
            // m_V = m_V + 1 + (rand() % 10);
            m_V ++;
        }
    }
    else
    {
        m_numCongestionRtt = 0; //如果中间有一次未超过阀值则归零
    }
    //清零
    m_markedBytes = 0;
    m_totalBytes = 0;
}

}
