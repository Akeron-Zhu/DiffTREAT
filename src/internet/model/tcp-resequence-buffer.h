#ifndef TCP_RESEQUENCE_BUFFER_H
#define TCP_RESEQUENCE_BUFFER_H

#include "ns3/object.h"
#include "ns3/packet.h"
#include "ns3/sequence-number.h"
#include "ns3/nstime.h"
#include "ns3/event-id.h"
#include "ns3/callback.h"
#include "ns3/traced-value.h"

#include <vector>
#include <queue>
#include <set>

namespace ns3
{

//序列buffer提交的原因
enum TcpRBPopReason
{
  IN_ORDER_FULL = 0,
  IN_ORDER_TIMEOUT,
  OUT_ORDER_TIMEOUT,
  RE_TRANS
};

class TcpSocketBase;

class TcpResequenceBufferElement
{

public:
  //序列号
  SequenceNumber32 m_seq;
  //是否标记同步
  bool m_isSyn;
  //是否标记结束
  bool m_isFin;
  //数据大小字节
  uint32_t m_dataSize; // In bytes
  //包本身 
  Ptr<Packet> m_packet;
  //源地址
  Address m_fromAddress;
  //目的地址
  Address m_toAddress;
  //重载比较运算符，用于priority_queue排序
  friend inline bool operator > (const TcpResequenceBufferElement &l, const TcpResequenceBufferElement &r)
  {
    return l.m_seq > r.m_seq;
  }
};

class TcpResequenceBuffer : public Object
{

public:

  static TypeId GetTypeId (void);

  TcpResequenceBuffer ();
  //TcpResequenceBuffer (const TcpResequenceBuffer &);
  ~TcpResequenceBuffer ();

  virtual void DoDispose (void);

  void BufferPacket (Ptr<Packet> packet, const Address& fromAddress, const Address& toAddress);

  void SetTcp (TcpSocketBase *tcp);

  void Stop (void);

  TracedCallback <uint32_t, Time, SequenceNumber32, SequenceNumber32> m_tcpRBBuffer;
  TracedCallback <uint32_t, Time, SequenceNumber32, uint32_t, uint32_t, TcpRBPopReason> m_tcpRBFlush;

private:

  bool PutInTheInOrderQueue (const TcpResequenceBufferElement &element);
  SequenceNumber32 CalculateNextSeq (const TcpResequenceBufferElement &element);

  void PeriodicalCheck ();

  void FlushOneElement (const TcpResequenceBufferElement &element, TcpRBPopReason reason);
  void FlushInOrderQueue (TcpRBPopReason reason);
  void FlushOutOrderQueue (TcpRBPopReason reason);

  // Parameters
  // buffer的大小
  uint32_t m_sizeLimit;
  //超时的时间阀
  Time m_inOrderQueueTimerLimit;
  Time m_outOrderQueueTimerLimit;
  //周期性检测的时间 
  Time m_periodicalCheckTime;
  //缓存的流的ID
  uint32_t m_traceFlowId;

  // Variables
  // InOrderQueue现在的大小
  uint32_t m_size;
  //超时事件计时器
  Time m_inOrderQueueTimer;
  Time m_outOrderQueueTimer;
  //检测事件
  EventId m_checkEvent;
  //是否还在运行的变量
  bool m_hasStopped;
  //InOrederQueue中第一个包的
  //序列号和下一个期望的序列号
  SequenceNumber32 m_firstSeq;
  SequenceNumber32 m_nextSeq;
  //有序队列
  std::vector<TcpResequenceBufferElement> m_inOrderQueue;
  //无序队列,注意这个实现中无序队列没有限制大小
  std::priority_queue<TcpResequenceBufferElement,
      std::vector<TcpResequenceBufferElement>,
      std::greater<TcpResequenceBufferElement> > m_outOrderQueue;
  //无序队列中包的顺序号
  std::set<SequenceNumber32> m_outOrderSeqSet;
  //tcp
  TcpSocketBase *m_tcp;

  typedef void (* TcpRBBuffer) (uint32_t flowId, Time time, SequenceNumber32 recSeq,
          SequenceNumber32 nextSeq);
  typedef void (* TcpRBFlush) (uint32_t flowId, Time time, SequenceNumber32 popSeq,
          uint32_t inOrderLength, uint32_t outOrderLength, TcpRBPopReason reason);
};

}

#endif
