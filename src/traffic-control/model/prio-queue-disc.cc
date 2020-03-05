/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2017 Universita' degli Studi di Napoli Federico II
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
 * Authors:  Stefano Avallone <stavallo@unina.it>
 */

#include "ns3/log.h"
#include "ns3/pointer.h"
#include "ns3/object-factory.h"
#include "ns3/socket.h"
#include "prio-queue-disc.h"
#include "ns3/prio-queue-disc-filter.h"
#include <algorithm>
#include <iterator>
#include "ns3/ipv4-header.h"
#include "ns3/ipv4-queue-disc-item.h"
#include "prio-subqueue-disc.h"

//#include "ns3/cache.h"
#include "ns3/urge-tag.h"
#include "ns3/flow-id-tag.h"
#include "ns3/rto-pri-tag.h"
namespace ns3
{

NS_LOG_COMPONENT_DEFINE("PrioQueueDisc");

NS_OBJECT_ENSURE_REGISTERED(PrioQueueDisc);

ATTRIBUTE_HELPER_CPP(Priomap);

std::ostream &
operator<<(std::ostream &os, const Priomap &priomap)
{
  std::copy(priomap.begin(), priomap.end() - 1, std::ostream_iterator<uint16_t>(os, " "));
  os << priomap.back();
  return os;
}

std::istream &operator>>(std::istream &is, Priomap &priomap)
{
  for (int i = 0; i < 16; i++)
  {
    if (!(is >> priomap[i]))
    {
      NS_FATAL_ERROR("Incomplete priomap specification (" << i << " values provided, 16 required)");
    }
  }
  return is;
}

TypeId PrioQueueDisc::GetTypeId(void)
{
  static TypeId tid = TypeId("ns3::PrioQueueDisc")
                          .SetParent<QueueDisc>()
                          .SetGroupName("TrafficControl")
                          .AddConstructor<PrioQueueDisc>()
                          .AddAttribute("Priomap", "The priority to band mapping.",
                                        PriomapValue(Priomap{{1, 2, 2, 2, 1, 2, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1}}),
                                        MakePriomapAccessor(&PrioQueueDisc::m_prio2band),
                                        MakePriomapChecker())
                          .AddAttribute("Mode",
                                        "Determines unit for QueueLimit",
                                        EnumValue(Queue::QUEUE_MODE_BYTES),
                                        MakeEnumAccessor(&PrioQueueDisc::SetMode),
                                        MakeEnumChecker(Queue::QUEUE_MODE_BYTES, "QUEUE_MODE_BYTES",
                                                        Queue::QUEUE_MODE_PACKETS, "QUEUE_MODE_PACKETS"))
                          .AddAttribute("EnableCache", "Enable Cache",
                                        BooleanValue(false),
                                        MakeBooleanAccessor(&PrioQueueDisc::m_EnableCache),
                                        MakeBooleanChecker())
                          .AddAttribute("EnableMarking", "Enable Makring",
                                        BooleanValue(false),
                                        MakeBooleanAccessor(&PrioQueueDisc::m_EnableMarking),
                                        MakeBooleanChecker())
                          .AddAttribute("EnableUrge", "Enable Urge",
                                        BooleanValue(false),
                                        MakeBooleanAccessor(&PrioQueueDisc::m_EnableUrge),
                                        MakeBooleanChecker())
                          .AddAttribute("EnCacheFirst", "Enable Cache First",
                                        BooleanValue(false),
                                        MakeBooleanAccessor(&PrioQueueDisc::m_EnCacheFirst),
                                        MakeBooleanChecker())
                          .AddAttribute("CacheThre",
                                        "Maximum segment lifetime in seconds, use for TIME_WAIT state transition to CLOSED state",
                                        DoubleValue(0.7),
                                        MakeDoubleAccessor(&PrioQueueDisc::m_cacheThre),
                                        MakeDoubleChecker<double>(0))
                          .AddAttribute("AlertThre",
                                        "Maximum segment lifetime in seconds, use for TIME_WAIT state transition to CLOSED state",
                                        DoubleValue(0.5),
                                        MakeDoubleAccessor(&PrioQueueDisc::m_alertThre),
                                        MakeDoubleChecker<double>(0))
                          .AddAttribute("UnCacheThre",
                                        "Maximum segment lifetime in seconds, use for TIME_WAIT state transition to CLOSED state",
                                        DoubleValue(0.3),
                                        MakeDoubleAccessor(&PrioQueueDisc::m_uncacheThre),
                                        MakeDoubleChecker<double>(0))
                          .AddAttribute("MarkThre",
                                        "Maximum segment lifetime in seconds, use for TIME_WAIT state transition to CLOSED state",
                                        DoubleValue(0.34),
                                        MakeDoubleAccessor(&PrioQueueDisc::m_markingThre),
                                        MakeDoubleChecker<double>(0))
                          .AddAttribute("CacheBand", "Cache Band",
                                        UintegerValue(2),
                                        MakeUintegerAccessor(&PrioQueueDisc::m_cacheBand),
                                        MakeUintegerChecker<uint32_t>())
                          .AddAttribute("Scheduler", "NTcp or PIAS",
                                        UintegerValue(0),
                                        MakeUintegerAccessor(&PrioQueueDisc::m_scheduler),
                                        MakeUintegerChecker<uint32_t>())
                          .AddAttribute("MarkCacheThre", "Cache Band",
                                        UintegerValue(240),
                                        MakeUintegerAccessor(&PrioQueueDisc::m_markCacheThre),
                                        MakeUintegerChecker<uint32_t>());
  return tid;
}

PrioQueueDisc::PrioQueueDisc()
    : m_PktsLimit(600),
      m_BytesLimit(600 * 1450),
      m_EnableCache(false),
      m_EnableMarking(false),
      m_EnableUrge(false),
      m_EnCacheFirst(false),
      m_cacheBand(2),
      m_cacheThre(0.7),
      m_alertThre(0.5),
      m_uncacheThre(0.3),
      m_scheduler(0)
{
  NS_LOG_FUNCTION(this);
}

PrioQueueDisc::~PrioQueueDisc()
{
  NS_LOG_FUNCTION(this);
}

void PrioQueueDisc::SetBandForPriority(uint8_t prio, uint16_t band)
{
  NS_LOG_FUNCTION(this << prio << band);

  NS_ASSERT_MSG(prio < 16, "Priority must be a value between 0 and 15");

  m_prio2band[prio] = band;
}

uint16_t
PrioQueueDisc::GetBandForPriority(uint8_t prio) const
{
  NS_LOG_FUNCTION(this << prio);

  NS_ASSERT_MSG(prio < 16, "Priority must be a value between 0 and 15");

  return m_prio2band[prio];
}

/*********************************************add by myself********************************/
//设置模式
void PrioQueueDisc::SetMode(Queue::QueueMode mode)
{
  NS_LOG_FUNCTION(this << mode);
  m_mode = mode; //字节模式或包模式
}

//返回模式
Queue::QueueMode
PrioQueueDisc::GetMode(void) const
{
  NS_LOG_FUNCTION(this);
  return m_mode;
}

Ptr<const QueueDiscItem>
PrioQueueDisc::DoPeek(uint32_t band)
{
  NS_LOG_FUNCTION(this << band);

  Ptr<const QueueDiscItem> item;

  for (uint32_t i = band; i < GetNQueueDiscClasses(); i++)
  {
    if ((item = GetQueueDiscClass(i)->GetQueueDisc()->Peek()) != 0)
    {
      NS_LOG_LOGIC("Peeked from band " << i << ": " << item);
      NS_LOG_LOGIC("Number packets band " << i << ": " << GetDiscClassSize(i));
      return item;
    }
  }

  NS_LOG_LOGIC("Queue empty");
  return item;
}

bool PrioQueueDisc::DoEnqueue(uint32_t band, Ptr<QueueDiscItem> item)
{
  NS_LOG_FUNCTION(this << item);
  EnqueueDecache(item); //must place here, reference the enqueue.
  if (OverThre(1.0))
  {
    if (GetDiscClassSize(m_cacheBand) > 0)
    {
      Ptr<QueueDiscItem> ite = DoDequeue(m_cacheBand);
      uint32_t num = GetNPackets();
      if (num <= 0)
        printf("Drop 1, Pkt num %d\n", num);
      Drop(ite);
    }
    else
    {
      NS_LOG_LOGIC("Queue disc limit exceeded -- dropping packet");
      uint32_t num = GetNPackets();
      if (num <= 0)
        printf("Drop 2, Pkt num %d\n", num);
      Drop(item);
      return false;
    }
  }
  NS_ASSERT_MSG(band < GetNQueueDiscClasses(), "Selected band out of range");
  bool retval = GetQueueDiscClass(band)->GetQueueDisc()->Enqueue(item);
  return retval;
}

Ptr<QueueDiscItem>
PrioQueueDisc::DoDequeue(uint32_t band)
{
  NS_LOG_FUNCTION(this);

  Ptr<QueueDiscItem> item;

  for (uint32_t i = band; i < GetNQueueDiscClasses(); i++)
  {
    Ptr<PrioSubqueueDisc> disc = DynamicCast<PrioSubqueueDisc>(GetQueueDiscClass(i)->GetQueueDisc());
    if (disc && ((item = disc->DoDequeueReverse()) != 0))
    {
      NS_LOG_LOGIC("Popped from band " << i << ": " << item);
      NS_LOG_LOGIC("Number packets band " << i << ": " << GetDiscClassSize(i));
      //DequeuePktCache(item);
      return item;
    }
  }

  NS_LOG_LOGIC("Queue empty");
  return item;
}

void PrioQueueDisc::CachePacket()
{
  NS_LOG_FUNCTION(this);
  if (GetDiscClassSize(m_cacheBand) > 0)
  {
    Ptr<QueueDiscItem> item = DoDequeue(m_cacheBand);
    if (item != 0)
    {
      DequeueEncache(item);
      m_cache->DoEnqueue(m_discId, item);
      if (OverThre(m_alertThre)) //0331 version use the m_cacheThre
      {
        Time txTime = m_cache->GetDataRate().CalculateBytesTxTime(item->GetPacketSize());
        m_CacheEvent = Simulator::Schedule(txTime, &PrioQueueDisc::CachePacket, this);
      }
      else
        m_cache->SetWriteSignal(false);
    }
    else
    {
      printf("ERROR:1\n\n\n");
      m_cache->SetWriteSignal(false);
    }
  }
  else
  {
    NS_LOG_LOGIC("There is no packet in band 1, Band[0] size is "
                 << GetDiscClassSize(0));
    m_cache->SetWriteSignal(false);
  }
}

void PrioQueueDisc::UnCachePacket()
{
  NS_LOG_FUNCTION(this);
  if (m_cache->GetDiscCacheNumber(m_discId) > 0)
  {
    Ptr<QueueDiscItem> item = StaticCast<QueueDiscItem>(m_cache->DoDequeue(m_discId));
    if (item != 0)
    {
      DoEnqueue(m_cacheBand - 1, item);
      if (!OverThre(m_alertThre)) //0331 version use m_uncacheThre
      {
        Time txTime = m_cache->GetDataRate().CalculateBytesTxTime(item->GetPacketSize());
        m_UnCacheEvent = Simulator::Schedule(txTime, &PrioQueueDisc::UnCachePacket, this);
      }
      else
        m_cache->SetReadSignal(false);
    }
    else
    {
      printf("ERROR:2\n\n");
      m_cache->SetReadSignal(false);
    }
  }
  else
    m_cache->SetReadSignal(false);
}

void PrioQueueDisc::UrgeCachePacket(uint32_t flowid, uint32_t urgeNum)
{
  NS_LOG_FUNCTION(this << flowid);
  NS_LOG_DEBUG("In the UrgeCachePkt");
  if (urgeNum > 0 && m_cache->GetFlowCacheNumber(m_discId, flowid) > 0)
  {
    Ptr<QueueDiscItem> item = StaticCast<QueueDiscItem>(m_cache->DoDequeue(m_discId, flowid));
    if (item != 0)
    {
      RtoPriTag rtoPriTag;
      bool found = item->GetPacket()->PeekPacketTag(rtoPriTag);
      rtoPriTag.SetRtoRank(m_cacheBand - 2);
      DoEnqueue(m_cacheBand - 2, item);
      Time txTime = m_cache->GetDataRate().CalculateBytesTxTime(item->GetPacketSize());
      m_UrgeEvent = Simulator::Schedule(txTime, &PrioQueueDisc::UrgeCachePacket, this, flowid, urgeNum - 1);
    }
    else
    {
      printf("Item from Cache 1 is NULL\n\n\n");
      m_cache->SetReadSignal(false);
    }
  }
  else
  {
    if (urgeTable.size() > 0)
    {
      auto ite = urgeTable.begin();
      while (ite != urgeTable.end())
      {
        flowid = *ite;
        ite = urgeTable.erase(ite);
        if (m_cache->GetFlowCacheNumber(m_discId, flowid) > 0)
          break;
      }
      if (ite == urgeTable.end())
      {
        return;
      }
      Ptr<const QueueItem> item_tmp = m_cache->DoPeek(m_discId, flowid);
      if (item_tmp)
      {
        Time txTime = m_cache->GetDataRate().CalculateBytesTxTime(item_tmp->GetPacketSize());
        m_UrgeEvent = Simulator::Schedule(txTime, &PrioQueueDisc::UrgeCachePacket, this, flowid, 10);
      }
    }
    else
      m_cache->SetReadSignal(false);
  }

} //*/.

bool PrioQueueDisc::CheckEncache()
{
  NS_LOG_FUNCTION(this);

  if (m_CacheEvent.IsExpired() && OverThre(m_cacheThre) && GetDiscClassSize(m_cacheBand) > 0 && m_cache->IsIdleNow(Cache::WRITE, m_discId))
  {
    // NS_LOG_DEBUG("Over Limit " << GetDiscClassSize(m_cacheBand));
    Ptr<const QueueDiscItem> item_tmp = DoPeek(m_cacheBand);
    if (item_tmp)
    {
      Time txTime = m_cache->GetDataRate().CalculateBytesTxTime(item_tmp->GetPacketSize());
      m_CacheEvent = Simulator::Schedule(txTime, &PrioQueueDisc::CachePacket, this);
      m_cache->SetWriteSignal(true);
      return true;
    }
    else
    {
      printf("ERROR:4\n\n");
      return false;
    }
  }
  return false;
}

bool PrioQueueDisc::CheckDecache()
{
  NS_LOG_FUNCTION(this);
  if (m_UnCacheEvent.IsExpired() && !OverThre(m_uncacheThre) && m_cache->GetDiscCacheNumber(m_discId) > 0 && m_cache->IsIdleNow(Cache::READ, m_discId))
  {
    //NS_LOG_DEBUG("Below Thre " << GetNPackets());
    Ptr<const QueueItem> item_tmp = m_cache->DoPeek(m_discId);
    if (item_tmp)
    {
      Time txTime = m_cache->GetDataRate().CalculateBytesTxTime(item_tmp->GetPacketSize());
      m_UnCacheEvent = Simulator::Schedule(txTime, &PrioQueueDisc::UnCachePacket, this);
      m_cache->SetReadSignal(true);
      return true;
    }
    else
    {
      printf("ERROR:5\n\n");
      return false;
    }
  }
  else
    return false;
}

bool PrioQueueDisc::CheckUrge()
{
  NS_LOG_FUNCTION(this);
  for (auto ite = urgeTable.begin(); ite != urgeTable.end();)
  {
    if (m_cache->GetFlowCacheNumber(m_discId, *ite) > 0)
    {
      if (m_UrgeEvent.IsExpired() && m_cache->IsIdleNow(Cache::URGE, m_discId))
      {
        Ptr<const QueueItem> item_tmp = m_cache->DoPeek(m_discId, *ite);
        if (item_tmp)
        {
          Time txTime = m_cache->GetDataRate().CalculateBytesTxTime(item_tmp->GetPacketSize());
          m_UrgeEvent = Simulator::Schedule(txTime, &PrioQueueDisc::UrgeCachePacket, this, *ite, 10);
          m_cache->SetReadSignal(true);
          return true;
        }
        else
        {
          printf("ERROR:6\n\n");
        }
      }
    }
    else
      ite = urgeTable.erase(ite);
  }
  return false;
}

bool PrioQueueDisc::CacheIdle(Cache::Operation operation)
{
  NS_LOG_FUNCTION(this);
  std::cout << "CacheIdle is trigger" << std::endl;
  if (operation == Cache::WRITE)
    return CheckEncache();
  else if (operation == Cache::READ)
    return CheckDecache();
  else if (operation == Cache::URGE)
    return CheckUrge();
}

bool PrioQueueDisc::CheckEncache2()
{
  NS_LOG_FUNCTION(this);

  if (m_CacheEvent.IsExpired() && GetDiscClassSize(m_cacheBand + 1) > 0 && m_cache->IsIdleNow(Cache::WRITE, m_discId))
  {
    // NS_LOG_DEBUG("Over Limit " << GetDiscClassSize(m_cacheBand));
    Ptr<const QueueDiscItem> item_tmp = DoPeek(m_cacheBand + 1);
    if (item_tmp)
    {
      Time txTime = m_cache->GetDataRate().CalculateBytesTxTime(item_tmp->GetPacketSize());
      m_CacheEvent = Simulator::Schedule(txTime, &PrioQueueDisc::CacheNewPacket, this);
      m_cache->SetWriteSignal(true);
      return true;
    }
    else
    {
      printf("ERROR:4\n\n");
      return false;
    }
  }
  return false;
}

void PrioQueueDisc::CacheNewPacket()
{
  NS_LOG_FUNCTION(this);
  if (GetDiscClassSize(m_cacheBand + 1) > 0)
  {
    Ptr<QueueDiscItem> item = DoDequeue(m_cacheBand + 1);
    if (item != 0)
    {
      DequeueEncache(item);
      m_cache->DoEnqueue(m_discId, item);
      if (GetDiscClassSize(m_cacheBand + 1) > 0)
      {
        Time txTime = m_cache->GetDataRate().CalculateBytesTxTime(item->GetPacketSize());
        m_CacheEvent = Simulator::Schedule(txTime, &PrioQueueDisc::CacheNewPacket, this);
      }
      else
        m_cache->SetWriteSignal(false);
    }
    else
    {
      printf("ERROR:1\n\n\n");
      m_cache->SetWriteSignal(false);
    }
  }
  else
  {
    NS_LOG_LOGIC("There is no packet in band 1, Band[0] size is "
                 << GetDiscClassSize(0));
    m_cache->SetWriteSignal(false);
  }
}
/*********************************************add by myself********************************/

bool PrioQueueDisc::DoEnqueue(Ptr<QueueDiscItem> item)
{
  NS_LOG_FUNCTION(this << item);

  uint32_t band = 0;
  int32_t ret = -1;
  if(m_scheduler==0) ret=Classify(item);
  else ret=0;

  if (ret == PacketFilter::PF_NO_MATCH)
  {
    band = 0;
    NS_LOG_LOGIC("The filter was unable to classify; using default band of " << band);
  }
  else
  {
    // NS_LOG_DEBUG ("PacketId is " << item->GetPacket()->GetUid());
    NS_LOG_LOGIC("Classfied! The filter returned RTO rank is " << ret);
    // NS_LOG_DEBUG ("Interface is " << m_device);
    if (ret >= 0 && static_cast<uint32_t>(ret) < GetNQueueDiscClasses())
    {
      band = ret;
    }
    else
    {
      //NS_LOG_DEBUG(
      printf("Illegal band %d, use the default band 0\n",ret);
    }
  }

  /**************************************add by myself***************************/

  if (m_EnableCache && m_cache->GetLocation())
  {
    if (m_EnCacheFirst)
      CheckEncache();
    else
    {
      if (band == m_cacheBand)
      {
        if (OverThre(m_cacheThre)) //|| m_cache->GetFlowCacheNumber(m_discId, flowid) > 0)
        {
          band = m_cacheBand + 1;
        }
      }
    }

    if (m_EnableUrge)
    {
      UrgeTag urgeTag;
      Ptr<Packet> p = item->GetPacket();
      bool found = p->PeekPacketTag(urgeTag);
      if (found)
      {
        FlowIdTag flowIdTag;
        p->PeekPacketTag(flowIdTag);
        //NS_LOG_DEBUG(
        //std::cout<<"Urge Packet\n";
        uint32_t flowid = flowIdTag.GetFlowId();
        std::cout << "Found Urge Packet! " << flowIdTag.GetFlowId() << ' ' << m_cache->GetFlowCacheNumber(m_discId, flowid) << '\n';
        if (m_cache->GetFlowCacheNumber(m_discId, flowid) > 0)
        {
          if (m_UrgeEvent.IsExpired() && m_cache->IsIdleNow(Cache::URGE, m_discId)) //必须这个顺序
          {
            Ptr<const QueueItem> item_tmp = m_cache->DoPeek(m_discId, flowid);
            if (item_tmp)
            {
              Time txTime = m_cache->GetDataRate().CalculateBytesTxTime(item_tmp->GetPacketSize());
              m_UrgeEvent = Simulator::Schedule(txTime, &PrioQueueDisc::UrgeCachePacket, this, flowid, 500);
              m_cache->SetReadSignal(true);
            }
            else
            {
              printf("ERROR:6\n\n");
            }
          }
          else
          {
            urgeTable.push_back(flowid);
          }
        }
      }
    }
  } //*/

  if (m_EnableMarking)
  {
    //std::cout<<"Mark Thre is:" <<m_markingThre<<'\n';
    if (m_cache->GetLocation() == 0)
    {
      if (OverThre(m_markingThre))
      {
        Ptr<Ipv4QueueDiscItem> ipv4Item = DynamicCast<Ipv4QueueDiscItem>(item);
        Ipv4Header header;
        if (ipv4Item)
        {
          header = ipv4Item->GetHeader();
          //NS_LOG_DEBUG("\t Header: " << header);
        }
        //NS_LOG_DEBUG("\t Dropping\\Marking due to Hard Mark ");
        if (ipv4Item && header.GetEcn() == Ipv4Header::ECN_ECT1)
        {
          //NS_LOG_DEBUG("\t Marking CE Due to DTYPE_FORCED");
          header.SetEcn(Ipv4Header::ECN_CE);
          ipv4Item->SetHeader(header);
          //if ((rand() % 100) < 10)
          //band = m_cacheBand - 2;
        }
      }
    }
    else
    {
      //if ((band == m_cacheBand - 1 && DiscClassOverThre(m_cacheBand - 1, m_markingThre)) || (band == m_cacheBand && m_cache->GetDiscCacheNumber(m_discId) > m_markCacheThre))
      if ((band < m_cacheBand  && GetDiscClassSizeSum(0,m_cacheBand) > 65) || (band == m_cacheBand && GetDiscClassSize(m_cacheBand)>150))
      {
        Ptr<Ipv4QueueDiscItem> ipv4Item = DynamicCast<Ipv4QueueDiscItem>(item);
        Ipv4Header header;
        if (ipv4Item)
        {
          header = ipv4Item->GetHeader();
          //NS_LOG_DEBUG("\t Header: " << header);
        }
        //NS_LOG_DEBUG("\t Dropping\\Marking due to Hard Mark ");
        if (ipv4Item && header.GetEcn() == Ipv4Header::ECN_ECT1)
        {
          //NS_LOG_DEBUG("\t Marking CE Due to DTYPE_FORCED");
          header.SetEcn(Ipv4Header::ECN_CE);
          ipv4Item->SetHeader(header);
          //if ((rand() % 100) < 10)
          //band = m_cacheBand - 2;
        }
      }
    }
  }

  if (OverThre(1.0))
  {
    if (band < m_cacheBand && GetDiscClassSize(m_cacheBand) > 0)
    {
      Ptr<QueueDiscItem> ite = DoDequeue(m_cacheBand);
      uint32_t num = GetNPackets();
      if (num <= 0)
        printf("Drop 3, Pkt num %d\n", num);
      Drop(ite); //Not use DequeuePktCache, Drop will decrease
    }
    else
    {
      NS_LOG_LOGIC("Queue disc limit exceeded -- dropping packet");
      uint32_t num = GetNPackets();
      if (num <= 0)
        printf("Drop 4, Pkt num %d\n", num);
      Drop(item);
      return false;
    }
  }

  /**************************************add by myself***************************/

  NS_ASSERT_MSG(band < GetNQueueDiscClasses(), "Selected band out of range");
  bool retval = GetQueueDiscClass(band)->GetQueueDisc()->Enqueue(item);
  if (m_EnableCache && m_cache->GetLocation())
  {
    if (!m_EnCacheFirst)
      CheckEncache2();
  }

  NS_LOG_LOGIC("RTORank[" << band << "] has " << GetDiscClassSize(band) << " packets!");

  return retval;
}

Ptr<QueueDiscItem>
PrioQueueDisc::DoDequeue(void)
{
  NS_LOG_FUNCTION(this);
  /**************************************add by myself***************************/
  //if(m_UnCacheEvent.IsRunning() )
  //NS_LOG_DEBUG("On device "<<m_discId<<" m_UnCacheEvent.IsRunning()  "<<m_UnCacheEvent.IsRunning() );
  // if (((m_discId < 24 && m_discId % 2 == 0) || m_discId > 23) && m_cache->GetCacheNumber() > 0)
  // NS_LOG_DEBUG("DEqueue " << m_discId << ' ' << GetNPackets() << ' ' << m_cache->GetCacheNumber());

  if (m_EnableCache && m_cache->GetLocation())
    CheckDecache();
  /**************************************add by myself***************************/
  Ptr<QueueDiscItem> item;
  for (uint32_t i = 0; i < GetNQueueDiscClasses(); i++)
  {
    if ((item = GetQueueDiscClass(i)->GetQueueDisc()->Dequeue()) != 0)
    {
      NS_LOG_LOGIC("Popped from band " << i << ": " << item);
      NS_LOG_LOGIC("Number packets band " << i << ": " << GetDiscClassSize(i));
      return item;
    }
  }

  NS_LOG_LOGIC("Queue empty");
  return item;
}

Ptr<const QueueDiscItem>
PrioQueueDisc::DoPeek(void) const
{
  NS_LOG_FUNCTION(this);

  Ptr<const QueueDiscItem> item;

  for (uint32_t i = 0; i < GetNQueueDiscClasses(); i++)
  {
    if ((item = GetQueueDiscClass(i)->GetQueueDisc()->Peek()) != 0)
    {
      NS_LOG_LOGIC("Peeked from band " << i << ": " << item);
      NS_LOG_LOGIC("Number packets band " << i << ": " << GetDiscClassSize(i));
      return item;
    }
  }

  NS_LOG_LOGIC("Queue empty");
  return item;
}

bool PrioQueueDisc::CheckConfig(void)
{
  NS_LOG_FUNCTION(this);
  if (GetNInternalQueues() > 0)
  {
    NS_LOG_ERROR("PrioQueueDisc cannot have internal queues");
    return false;
  }

  if (GetNQueueDiscClasses() == 0)
  {
    // create 3 fifo queue discs
    ObjectFactory factory;
    factory.SetTypeId("ns3::PrioSubqueueDisc");
    factory.Set("Limit", UintegerValue(m_PktsLimit));
    for (uint8_t i = 0; i < m_cacheBand + 2; i++)
    {
      Ptr<QueueDisc> qd = factory.Create<QueueDisc>();
      qd->AddPacketFilter(CreateObject<PrioSubqueueDiscFilter>());
      qd->Initialize();
      Ptr<QueueDiscClass> c = CreateObject<QueueDiscClass>();
      c->SetQueueDisc(qd);
      AddQueueDiscClass(c);
    }
    NS_LOG_LOGIC("8 PrioSubqueueDisc Created!");
  }

  if (GetNQueueDiscClasses() < 2)
  {
    NS_LOG_ERROR("PrioQueueDisc needs at least 2 classes");
    return false;
  }

  return true;
}

void PrioQueueDisc::InitializeParams(void)
{
  NS_LOG_FUNCTION(this);
}

/******************************************************************/
void PrioQueueDisc::SetCache(Ptr<Cache> cache)
{
  NS_LOG_FUNCTION(this);
  m_cache = cache;
}

void PrioQueueDisc::SetDiscId(uint32_t id)
{
  NS_LOG_FUNCTION(this << id);
  m_discId = id;
}

uint32_t
PrioQueueDisc::GetDiscId()
{
  NS_LOG_FUNCTION(this);
  return m_discId;
}

uint32_t
PrioQueueDisc::GetDiscClassSize(uint32_t inx) const
{
  NS_LOG_FUNCTION(this << inx);
  if (GetMode() == Queue::QUEUE_MODE_PACKETS)
  {
    return GetQueueDiscClass(inx)->GetQueueDisc()->GetNPackets();
  }
  else if (GetMode() == Queue::QUEUE_MODE_BYTES)
  {
    return GetQueueDiscClass(inx)->GetQueueDisc()->GetNBytes();
  }//*/
}

uint32_t
PrioQueueDisc::GetDiscClassSizeSum(uint32_t start,uint32_t end) const
{
  NS_LOG_FUNCTION(this << start <<end);
  uint32_t res=0;
  if (GetMode() == Queue::QUEUE_MODE_PACKETS)
  {
    for(uint32_t i = start;i < end; i++)
      res+= GetQueueDiscClass(i)->GetQueueDisc()->GetNPackets();
  }
  else if (GetMode() == Queue::QUEUE_MODE_BYTES)
  {
    for(uint32_t i = start;i < end; i++)
      res+=GetQueueDiscClass(i)->GetQueueDisc()->GetNBytes();
  }
  return res;
}

bool PrioQueueDisc::DiscClassOverThre(uint32_t inx, double thre)
{
  NS_LOG_FUNCTION(this);
  if (GetMode() == Queue::QUEUE_MODE_PACKETS)
  {
    return GetQueueDiscClass(inx)->GetQueueDisc()->GetNPackets() >= m_PktsLimit * thre;
  }
  else if (GetMode() == Queue::QUEUE_MODE_BYTES)
  {
    return GetQueueDiscClass(inx)->GetQueueDisc()->GetNBytes() >= m_BytesLimit * thre;
  }
}

bool PrioQueueDisc::OverThre(double thre)
{
  NS_LOG_FUNCTION(this);
  if (GetMode() == Queue::QUEUE_MODE_PACKETS)
  {
    return GetNPackets() >= m_PktsLimit * thre;
  }
  else if (GetMode() == Queue::QUEUE_MODE_BYTES)
  {
    return GetNBytes() >= m_BytesLimit * thre;
  }
}

} // namespace ns3
