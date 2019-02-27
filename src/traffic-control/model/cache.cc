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

#include "ns3/log.h"
#include "cache.h"
#include "ns3/flow-id-tag.h"
#include <algorithm>
#include "ns3/prio-queue-disc.h" //add by myself
#include <fstream>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("Cache");

NS_OBJECT_ENSURE_REGISTERED(Cache);

TypeId Cache::GetTypeId(void)
{
  static TypeId tid = TypeId("ns3::Cache")
                          .SetParent<Object>()
                          .SetGroupName("Network")
                          .AddConstructor<Cache>()
                          .AddAttribute("WRConcurrent", "WR",
                                        BooleanValue(false),
                                        MakeBooleanAccessor(&Cache::m_WRConcurrent),
                                        MakeBooleanChecker())
                          .AddAttribute("BusyWirte", "Busy Write",
                                        BooleanValue(false),
                                        MakeBooleanAccessor(&Cache::m_busyWrite),
                                        MakeBooleanChecker())
                          .AddAttribute("BusyRead", "BusyRead",
                                        BooleanValue(false),
                                        MakeBooleanAccessor(&Cache::m_busyRead),
                                        MakeBooleanChecker())
                          .AddAttribute("Contest", "Contest",
                                        BooleanValue(false),
                                        MakeBooleanAccessor(&Cache::m_contest),
                                        MakeBooleanChecker())
                          .AddAttribute("DataRate",
                                        "The default data rate for cache links",
                                        DataRateValue(DataRate("10Gbps")),
                                        MakeDataRateAccessor(&Cache::m_cacheSpeed),
                                        MakeDataRateChecker())
                          .AddAttribute("FIFO", "FIFO",
                                        BooleanValue(false),
                                        MakeBooleanAccessor(&Cache::m_fifo),
                                        MakeBooleanChecker())
                          .AddAttribute("CacheLog", "FIFO",
                                        BooleanValue(false),
                                        MakeBooleanAccessor(&Cache::m_enableCacheLog),
                                        MakeBooleanChecker());
  return tid;
}

Cache::Cache() : m_flows(),
                 m_discs(),
                 m_cacheNumber(0),
                 m_cacheSpeed(DataRate("8Gbps")),
                 m_WRConcurrent(false),
                 m_busyWrite(false),
                 m_busyRead(false),
                 m_contest(false),
                 m_fifo(false),
                 m_enableCacheLog(false),
                 m_name("")
{
  NS_LOG_FUNCTION(this);
  //NS_LOG_DEBUG("Cache num: " << this);
}

Cache::~Cache()
{
  NS_LOG_FUNCTION(this);
  std::cout<<"CacheNumber:"<<m_cacheNumber<<'\n';
}

void Cache::SetDataRate(DataRate bps)
{
  NS_LOG_FUNCTION(this);
  m_cacheSpeed = bps;
}

DataRate
Cache::GetDataRate()
{
  NS_LOG_FUNCTION(this);
  return m_cacheSpeed;
}

void Cache::DealWait(Operation operation)
{
  NS_LOG_FUNCTION(this);
  uint32_t inx = 0;
  if (m_WRConcurrent)
  {
    if (operation == WRITE)
    {
      if (m_waitWrite.size() > 0)
      {
        std::vector<uint32_t>::iterator ite;
        for (ite = m_waitWrite.begin(); ite != m_waitWrite.end();)
        {
          inx = *ite;
          ite = m_waitWrite.erase(ite);
          if (m_discs[inx]->CacheIdle(operation))
          {
            NS_LOG_DEBUG("Deal the WRITE request of disc " << inx);
            break;
          }
        }
      }
    }
    else if (operation == READ)
    {
      if (m_waitRead.size() > 0)
      {
        std::vector<uint32_t>::iterator ite;
        for (ite = m_waitRead.begin(); ite != m_waitRead.end();)
        {
          inx = *ite;
          NS_LOG_DEBUG("INDEX " << inx);
          ite = m_waitRead.erase(ite);
          if (m_discs[inx]->CacheIdle(operation))
          {
            NS_LOG_DEBUG("Deal the READ request of disc " << inx);
            break;
          }
        }
      }
      else if (m_urgeRead.size() > 0)
      {
        std::vector<uint32_t>::iterator ite;
        for (ite = m_urgeRead.begin(); ite != m_urgeRead.end();)
        {
          inx = *ite;
          NS_LOG_DEBUG("INDEX " << inx);
          ite = m_urgeRead.erase(ite);
          if (m_discs[inx]->CacheIdle(URGE))
          {
            NS_LOG_DEBUG("Deal the URGE request of disc " << inx);
            break;
          }
        }
      }
    }
  }
  else
  {
    if (m_wait.size() > 0)
    {
      std::vector<SataOper>::iterator it;
      for (it = m_wait.begin(); it != m_wait.end();)
      {
        inx = it->m_id;
        Operation oper = it->m_oper;
        it = m_wait.erase(it);
        if (m_discs[inx]->CacheIdle(oper))
        {
          NS_LOG_DEBUG("Deal the request of disc " << inx);
          break;
        }
      }
    }
  }
  /*Simulator::ScheduleWithContext ( "/NodeList/" << node->GetId () << "/$ns3::TrafficControlLayer/RootQueueDiscList/,
                                  txTime + m_delay, &PointToPointNetDevice::Receive,
                                  m_link[wire].m_dst, p);//*/
}

void Cache::SetWriteSignal(bool isBusy)
{
  NS_LOG_FUNCTION(this << isBusy);
  m_busyWrite = isBusy;

  //NS_LOG_DEBUG("device " << m_name << ' ' << m_busyWrite);
  if (m_contest && !isBusy)
    DealWait(WRITE);
}

bool Cache::GetWriteSignal()
{
  NS_LOG_FUNCTION(this);
  return m_busyWrite;
}

void Cache::SetReadSignal(bool isBusy)
{
  NS_LOG_FUNCTION(this << isBusy);
  m_busyRead = isBusy;
  //NS_LOG_DEBUG("device " << m_name << ' ' << m_busyRead);
  if (m_contest && !isBusy)
    DealWait(READ);
}

bool Cache::GetReadSignal()
{
  NS_LOG_FUNCTION(this);
  return m_busyRead;
}

uint32_t
Cache::AddQueueDisc(Ptr<PrioQueueDisc> disc)
{
  NS_LOG_FUNCTION(this << disc);
  uint32_t index = m_discs.size();
  m_discs.push_back(disc);
  //NS_LOG_DEBUG(this << ' ' << index);
  disc->SetCache(this);
  disc->SetDiscId(index);
  return index;
} //*/

//opeation:false=>read ture=>write
bool Cache::IsIdleNow(Operation operation, uint32_t discId)
{
  NS_LOG_FUNCTION(this << operation << discId);
  if (!m_contest)
    return true; //Every port have a cache
  if (m_WRConcurrent)
  {
    //NS_LOG_DEBUG("Checking Idleing! " <<m_name <<' '<< discId);
    if (operation == WRITE)
    {
      if (m_busyWrite)
      {
        NS_LOG_DEBUG("Wait Writte insert " << discId);
        if (find(m_waitWrite.begin(), m_waitWrite.end(), discId) == m_waitWrite.end())
          m_waitWrite.push_back(discId);
      }
      //else
      // NS_LOG_DEBUG("Not busy wirte " << m_name);
      return !m_busyWrite;
    }
    else if (operation == READ)
    {
      if (m_busyRead)
      {
        NS_LOG_DEBUG("Wait read insert " << discId);
        if (find(m_waitRead.begin(), m_waitRead.end(), discId) == m_waitRead.end())
          m_waitRead.push_back(discId);
      }
      // else
      //NS_LOG_DEBUG("Not busy read " << m_name);
      return !m_busyRead;
    }
    else if (operation == URGE)
    {
      if (m_busyRead)
      {
        NS_LOG_DEBUG("Wait read insert " << discId);
        m_waitRead.push_back(discId);
      }
      // else
      //NS_LOG_DEBUG("Not busy read " << m_name);
      return !m_busyRead;
    }
  }
  else
  {
    if (m_busyRead || m_busyWrite)
    {
      NS_LOG_DEBUG("Wait insert " << discId);
      bool isHave = false;
      for (auto it = m_wait.begin(); it != m_wait.end(); it++)
      {
        if ((*it).m_id == discId && (*it).m_oper == operation)
        {
          isHave = true;
          break;
        }
      }
      if (!isHave)
      {
        SataOper s(discId, operation);
        m_wait.push_back(s);
      }
    }
    //else
    //NS_LOG_DEBUG("Not busy " << m_name);
    return !(m_busyRead || m_busyWrite);
  }
}

uint32_t
Cache::GetCacheNumber()
{
  NS_LOG_FUNCTION(this);
  return m_cacheNumber;
}

uint32_t
Cache::GetFlowCacheNumber(uint32_t discId, uint32_t flowid)
{
  NS_LOG_FUNCTION(this << discId << flowid);
  uint32_t num = 0;
  DiscCacheI itr = m_flows.find(discId);
  if (itr != m_flows.end())
  {
    FlowCache fc_tmp = itr->second;
    FlowCacheI fc_itr = fc_tmp.find(flowid);
    if (fc_itr != fc_tmp.end())
    {
      num = m_flows[discId][flowid].size();
    }
  }
  return num;
}

uint32_t
Cache::GetDiscCacheNumber(uint32_t discId)
{
  NS_LOG_FUNCTION(this << discId);
  if (m_fifo)
  {
    return m_fifoFlows[discId].size();
  }
  else
  {
    uint32_t num = 0;
    DiscCacheI itr = m_flows.find(discId);
    if (itr != m_flows.end())
    {
      FlowCache fc = itr->second;
      FlowCacheI iter = fc.begin();
      for (; iter != fc.end(); iter++)
      {
        num += (iter->second).size();
      }
    }
    //if (num)  NS_LOG_DEBUG("disc number = " << num);
    return num;
  }
}

bool Cache::DoEnqueue(uint32_t discId, Ptr<QueueItem> item)
{
  NS_LOG_FUNCTION(this << item);
  if (m_fifo)
  {
    FlowCacheI itr = m_fifoFlows.find(discId);
    if (itr == m_fifoFlows.end())
    {
      std::queue<Ptr<QueueItem>> que;
      m_fifoFlows[discId] = que;
    }
    m_fifoFlows[discId].push(item);
    m_cacheNumber++;
    NS_LOG_LOGIC("FIFO: Cached " << this << " Packet number is " << m_cacheNumber
                                 << "\t Disc " << discId << " have number packets is " << m_fifoFlows[discId].size());
    return true;
  }
  else
  {
    uint32_t flowid = 0;
    Ptr<Packet> p = item->GetPacket();
    FlowIdTag flowIdTag;
    bool found = p->PeekPacketTag(flowIdTag);
    if (found)
      flowid = flowIdTag.GetFlowId();
    m_flows[discId][flowid].push(item);
    m_cacheNumber++;
    NS_LOG_LOGIC("Flow: Cached " << this << " Packet number is " << m_cacheNumber
                                 << "\t Disc " << discId << " have number packets is " << m_flows[discId].size());
    if(m_enableCacheLog) RecordLog();
    return true;
  }
}

Ptr<QueueItem>
Cache::DoDequeue(uint32_t discId)
{
  //PrintCache();
  NS_LOG_FUNCTION(this << discId);
  Ptr<QueueItem> item;
  if (m_fifo)
  {
    if (m_fifoFlows[discId].size() > 0)
    {
      item = m_fifoFlows[discId].front();
      m_fifoFlows[discId].pop();
      if (item != 0)
        m_cacheNumber--;
    }
    NS_LOG_LOGIC("FIFO: Poped Pakcet from Cache " << this << ", leave number is " << m_cacheNumber
                                                  << "\t Disc " << discId << " have number packets is " << m_fifoFlows[discId].size());
  }
  else
  {
    DiscCacheI itr = m_flows.find(discId);
    if (itr != m_flows.end())
    {
      uint32_t flowid = 0;
      FlowCache &fc_tmp = itr->second;
      FlowCacheI fc_itr = fc_tmp.begin();
      if (m_dequeueIte.find(discId) != m_dequeueIte.end())
      {
        flowid = m_dequeueIte[discId];
        fc_itr = fc_tmp.find(flowid);
        if (++fc_itr == fc_tmp.end())
          fc_itr = fc_tmp.begin();
      }
      for (; fc_itr != fc_tmp.end();)
      {

        NS_LOG_DEBUG("For");
        flowid = fc_itr->first;
        while (m_flows[discId][flowid].size() > 0)
        {
          NS_LOG_DEBUG("MTRAP");
          item = m_flows[discId][flowid].front();
          m_flows[discId][flowid].pop();
          if (item != 0)
          {
            m_cacheNumber--;
            break;
          }
        }
        if (m_flows[discId][flowid].size() == 0)
        {
          fc_itr = fc_tmp.erase(fc_itr);
        }
        else
          fc_itr++;
        if (item != 0)
        {
          m_dequeueIte[discId] = flowid;
          break;
        }
      }
      if (m_flows[discId].size() == 0)
      {
        m_flows.erase(itr);
        if (m_dequeueIte.find(discId) != m_dequeueIte.end())
          m_dequeueIte.erase(discId);
      }
    }

    NS_LOG_LOGIC("Flow: Poped Pakcet from Cache " << this << ", leave number is " << m_cacheNumber
                                                  << "\t Disc " << discId << " have number packets is " << m_flows[discId].size());
  }
  if(m_enableCacheLog) RecordLog();
  return item;
}

Ptr<const QueueItem>
Cache::DoPeek(uint32_t discId)
{
  NS_LOG_FUNCTION(this << discId);
  Ptr<const QueueItem> item;
  if (m_fifo)
  {
    if (m_fifoFlows[discId].size() > 0)
    {
      item = m_fifoFlows[discId].front();
    }
  }
  else
  {
    DiscCacheI itr = m_flows.find(discId);
    if (itr != m_flows.end())
    {
      FlowCache fc_tmp = itr->second;
      FlowCacheI fc_itr = fc_tmp.begin();
      for (; fc_itr != fc_tmp.end();)
      {
        uint32_t flowid = fc_itr->first;
        while (m_flows[discId][flowid].size() > 0)
        {
          item = m_flows[discId][flowid].front();
          if (item != 0)
          {
            break;
          }
        }
        if (item != 0)
          break;
      }
    }
  }

  NS_LOG_LOGIC("Poped Pakcet from Cache, leave number is " << m_cacheNumber);
  return item;
}

Ptr<QueueItem>
Cache::DoDequeue(uint32_t discId, uint32_t flowid)
{
  NS_LOG_FUNCTION(this << discId << flowid);

  //PrintCache();
  // NS_LOG_DEBUG("In DoDequeue!");
  Ptr<QueueItem> item;
  DiscCacheI itr = m_flows.find(discId);
  if (itr != m_flows.end())
  {
    FlowCache &fc_tmp = itr->second;
    FlowCacheI fc_itr = fc_tmp.find(flowid);
    if (fc_itr != fc_tmp.end())
    {
      while (m_flows[discId][flowid].size() > 0)
      {
        NS_LOG_DEBUG("MTRAP2");
        item = m_flows[discId][flowid].front();
        m_flows[discId][flowid].pop();
        if (item != 0)
        {
          m_cacheNumber--;
          break;
        }
      }
      if (m_flows[discId][flowid].size() == 0)
      {
        fc_itr = fc_tmp.erase(fc_itr);
      }
    }
    if (m_flows[discId].size() == 0)
    {
      m_flows.erase(itr);
      if (m_dequeueIte.find(discId) != m_dequeueIte.end())
        m_dequeueIte.erase(discId);
    }
  }
  NS_LOG_LOGIC("Poped Pakcet from Cache " << this << ", leave number is " << m_cacheNumber
                                          << "\n\t Disc " << discId << " have number packets is " << m_flows[discId].size());
  if(m_enableCacheLog) RecordLog();
  return item;
}

Ptr<const QueueItem>
Cache::DoPeek(uint32_t discId, uint32_t flowid)
{
  NS_LOG_FUNCTION(this);
  Ptr<const QueueItem> item;
  DiscCacheI itr = m_flows.find(discId);
  if (itr != m_flows.end())
  {
    FlowCache &fc_tmp = itr->second;
    FlowCacheI fc_itr = fc_tmp.find(flowid);
    if (fc_itr != fc_tmp.end())
    {
      while (m_flows[discId][flowid].size() > 0)
      {
        item = m_flows[discId][flowid].front();
        if (item != 0)
        {
          break;
        }
      }
    }
  }
  NS_LOG_LOGIC("Poped Pakcet from Cache " << this << ", leave number is " << m_cacheNumber
                                          << "\n\t Disc " << discId << " have number packets is " << m_flows[discId].size());
  return item;
}

uint32_t Cache::GetLocation()
{
  return 1;
  
  if (m_name[0] == 's')
  {
    if(m_name[1] == 'e') return 0;
    else return 1;
  }
  else return 2;
  return 1;//*/
}

void Cache::PrintCache()
{
  if (m_name[0] == 's' && m_name[1] == 'e')
    return;
  DiscCacheI dci = m_flows.begin();
  uint32_t sum = 0;
  for (auto dci = m_flows.begin(); dci != m_flows.end(); dci++)
  {
    std::cout << m_name << '-' << "DiscId: " << dci->first << '\n';
    FlowCache fc = dci->second;
    for (auto fci = fc.begin(); fci != fc.end(); fci++)
    {
      sum += (fci->second).size();
      printf("\tFlowId: %-12u Number: %-10d\n", fci->first, (fci->second).size());
    }
  }
  printf("\tTotal Number: %-10d\n\n", sum);
}

void Cache::RecordLog()
{
  std::string outputName = m_name + "_cache.txt";
  std::ofstream out(outputName.c_str(), std::ios::app);
  out<< Simulator::Now ().GetSeconds () << "\t" << m_cacheNumber<<'\n';
  out.close();
}

} // namespace ns3
