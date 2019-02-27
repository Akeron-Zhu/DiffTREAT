/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2007, 2014 University of Washington
 *               2015 Universita' degli Studi di Napoli Federico II
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
 *           Tom Henderson <tomhend@u.washington.edu>
 */

#include "ns3/log.h"
#include "ns3/pointer.h"
#include "ns3/object-factory.h"
#include "ns3/drop-tail-queue.h"
#include "prio-subqueue-disc.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("PrioSubqueueDisc");

NS_OBJECT_ENSURE_REGISTERED(PrioSubqueueDisc);

TypeId PrioSubqueueDisc::GetTypeId(void)
{
  static TypeId tid = TypeId("ns3::PrioSubqueueDisc")
                          .SetParent<QueueDisc>()
                          .SetGroupName("TrafficControl")
                          .AddConstructor<PrioSubqueueDisc>()
                          .AddAttribute("Limit",
                                        "The maximum number of packets accepted by this queue disc.",
                                        UintegerValue(1000),
                                        MakeUintegerAccessor(&PrioSubqueueDisc::m_limit),
                                        MakeUintegerChecker<uint32_t>());
  return tid;
}

PrioSubqueueDisc::PrioSubqueueDisc()
{
  NS_LOG_FUNCTION(this);
}

PrioSubqueueDisc::~PrioSubqueueDisc()
{
  NS_LOG_FUNCTION(this);
}

bool PrioSubqueueDisc::DoEnqueue(Ptr<QueueDiscItem> item)
{
  NS_LOG_FUNCTION(this << item);
  // NS_LOG_DEBUG(this<<" "<<GetNPackets());

  if (GetNPackets() > m_limit)
  {
    NS_LOG_LOGIC("Queue disc limit exceeded -- dropping packet");
    Drop(item);
    return false;
  }

  uint32_t band;
  int32_t ret = Classify(item);

  if (ret == PacketFilter::PF_NO_MATCH)
  {
    band = 0;
    // NS_LOG_DEBUG ("The filter was unable to classify; using default band of " << band);
  }
  else if (ret < 0 || ret > 8)
  {
    band = 0;
    //  NS_LOG_DEBUG ("The filter returned an invalid value; using default band of " << band);
  }
  else
  {
    band = ret;
    NS_LOG_DEBUG("Classfied! The filter returned size rank is " << band);
  }

  if (!GetInternalQueue(band)->Enqueue(item))
  {
    NS_LOG_LOGIC("Enqueue failed -- dropping pkt");
    Drop(item);
    return false;
  }
  NS_LOG_LOGIC("Number packets band " << band << ": " << GetInternalQueue(band)->GetNPackets());

  return true;
}

Ptr<QueueDiscItem>
PrioSubqueueDisc::DoDequeue(void)
{
  NS_LOG_FUNCTION(this);

  Ptr<QueueDiscItem> item;

  for (uint32_t i = 0; i < GetNInternalQueues(); i++)
  {
    if ((item = StaticCast<QueueDiscItem>(GetInternalQueue(i)->Dequeue())) != 0)
    {
      NS_LOG_LOGIC("Popped from band " << i << ": " << item);
      NS_LOG_LOGIC("Number packets band " << i << ": " << GetInternalQueue(i)->GetNPackets());
      return item;
    }
  }

  NS_LOG_LOGIC("Queue empty");
  return item;
}

Ptr<QueueDiscItem>
PrioSubqueueDisc::DoDequeueReverse(void)
{
  NS_LOG_FUNCTION(this);

  Ptr<QueueDiscItem> item;

  for (int32_t i = GetNInternalQueues() - 1; i >= 0; i--)
  {
    if ((item = StaticCast<QueueDiscItem>(GetInternalQueue(i)->Dequeue())) != 0)
    {
      DequeueEncache(item);
      NS_LOG_LOGIC("Popped from band " << i << ": " << item);
      NS_LOG_LOGIC("Number packets band " << i << ": " << GetInternalQueue(i)->GetNPackets());
      return item;
    }
  }

  NS_LOG_LOGIC("Queue empty");
  return item;
}

Ptr<const QueueDiscItem>
PrioSubqueueDisc::DoPeek(void) const
{
  NS_LOG_FUNCTION(this);

  Ptr<const QueueDiscItem> item;

  for (uint32_t i = 0; i < GetNInternalQueues(); i++)
  {
    if ((item = StaticCast<const QueueDiscItem>(GetInternalQueue(i)->Peek())) != 0)
    {

      NS_LOG_LOGIC("Peeked from band " << i << ": " << item);
      NS_LOG_LOGIC("Number packets band " << i << ": " << GetInternalQueue(i)->GetNPackets());
      return item;
    }
  }

  NS_LOG_LOGIC("Queue empty");
  return item;
}

bool PrioSubqueueDisc::CheckConfig(void)
{
  NS_LOG_FUNCTION(this);
  if (GetNQueueDiscClasses() > 0)
  {
    NS_LOG_ERROR("PrioSubqueueDisc cannot have classes");
    return false;
  }

  if (GetNPacketFilters() == 0)
  {
    NS_LOG_ERROR("PrioSubqueueDisc needs at least a packet filter");
    return false;
  }

  if (GetNInternalQueues() == 0)
  {
    // create 3 DropTail queues with m_limit packets each
    ObjectFactory factory;
    factory.SetTypeId("ns3::DropTailQueue");
    factory.Set("Mode", EnumValue(Queue::QUEUE_MODE_PACKETS));
    factory.Set("MaxPackets", UintegerValue(m_limit));
    for (int i = 0; i < 8; i++)
      AddInternalQueue(factory.Create<Queue>());
  }

  if (GetNInternalQueues() != 8)
  {
    NS_LOG_ERROR("PrioSubqueueDisc needs 8 internal queues");
    return false;
  }

  for (uint8_t i = 0; i < 8; i++)
  {
    if (GetInternalQueue(i)->GetMode() != Queue::QUEUE_MODE_PACKETS)
    {
      NS_LOG_ERROR("PrioSubqueueDisc needs 8 internal queues operating in packet mode");
      return false;
    }
    if (GetInternalQueue(i)->GetMaxPackets() < m_limit)
    {
      NS_LOG_ERROR("The capacity of some internal queue(s) is less than the queue disc capacity");
      return false;
    }
  }

  return true;
}

void PrioSubqueueDisc::InitializeParams(void)
{
  NS_LOG_FUNCTION(this);
}

} // namespace ns3
