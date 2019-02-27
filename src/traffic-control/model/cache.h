/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2007 University of Washington
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
 */

#ifndef CACHE_H
#define CACHE_H

#include <queue>
#include <map>
#include <vector>
#include "ns3/data-rate.h"
#include "ns3/net-device.h"

//#include "ns3/prio-queue-disc.h" //add by myself

namespace ns3
{

class PrioQueueDisc;
/**
 * \ingroup queue
 *
 * \brief A FIFO packet queue that drops tail-end packets on overflow
 */
class Cache : public Object
{

public:
  /**
   * \brief Get the type ID.
   * \return the object TypeId
   */
  static TypeId GetTypeId(void);
  /**
   * \brief DropTailQueue Constructor
   *
   * Creates a droptail queue with a maximum size of 100 packets by default
   */
  Cache();
  ~Cache();

  typedef std::map<uint32_t, std::queue<Ptr<QueueItem>>> FlowCache;
  typedef std::map<uint32_t, std::queue<Ptr<QueueItem>>>::iterator FlowCacheI;
  typedef std::map<uint32_t, FlowCache> DiscCache;
  typedef std::map<uint32_t, FlowCache>::iterator DiscCacheI;
  typedef std::vector<uint32_t>::iterator VecI;


  enum Operation {WRITE=0,READ,URGE};

  std::string m_name;
  void SetDataRate(DataRate bps);
  DataRate GetDataRate();
  void SetWriteSignal(bool busy);
  bool GetWriteSignal();
  void SetReadSignal(bool busy);
  bool GetReadSignal();
  uint32_t AddQueueDisc(Ptr<PrioQueueDisc> disc);
  bool IsIdleNow(Operation operation, uint32_t id); ////opeation:false=>read ture=>write
  void DealWait(Operation operation);
  uint32_t GetCacheNumber();
  uint32_t GetDiscCacheNumber(uint32_t discId);
  uint32_t GetFlowCacheNumber(uint32_t discId, uint32_t flowid);
  bool DoEnqueue(uint32_t flowId, Ptr<QueueItem> item);
  Ptr<QueueItem> DoDequeue(uint32_t flowId);
  Ptr<QueueItem> DoDequeue(uint32_t discId,uint32_t flowid);
  Ptr<const QueueItem> DoPeek(uint32_t flowId);
  Ptr<const QueueItem> DoPeek(uint32_t discId,uint32_t flowid);
  uint32_t GetLocation();
  void PrintCache();
  void RecordLog();

  struct SataOper
  {
    uint32_t m_id;
    Operation m_oper;
    SataOper() {}
    SataOper(uint32_t id, Operation oper) : m_id(id),
                                       m_oper(oper)
    {
    }
  };

  

private:
  bool m_WRConcurrent;
  bool m_busyWrite;
  bool m_busyRead;
  bool m_contest;
  bool m_fifo;
  bool m_enableCacheLog;
  uint32_t m_cacheNumber;
  DataRate m_cacheSpeed;
  std::map<uint32_t,uint32_t> m_dequeueIte;
  std::vector<uint32_t> m_waitWrite;
  std::vector<uint32_t> m_waitRead;
  std::vector<uint32_t> m_urgeRead;

  std::vector<SataOper> m_wait;
  std::vector<Ptr<PrioQueueDisc>> m_discs; //这里也可以直接用PrioQueueDisc
  FlowCache m_fifoFlows;
  DiscCache m_flows;                       //!< the items in the queue
};

} // namespace ns3

#endif /* DROPTAIL_H */
