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
 * Authors: Stefano Avallone <stavallo@unina.it>
 *
 */

#include "ns3/log.h"
#include "ns3/enum.h"
#include "prio-queue-disc-filter.h"

#include "ns3/packet.h"
#include "ns3/rto-pri-tag.h"

namespace ns3{

NS_LOG_COMPONENT_DEFINE ("PrioQueueDiscFilter");

NS_OBJECT_ENSURE_REGISTERED (PrioQueueDiscFilter);

TypeId 
PrioQueueDiscFilter::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::PrioQueueDiscFilter")
    .SetParent<PacketFilter> ()
    .SetGroupName ("TrafficControl")
    .AddConstructor<PrioQueueDiscFilter> () //属于TrafficControl的模块要填加constructor
  ;
  return tid;
}

PrioQueueDiscFilter::PrioQueueDiscFilter ()
{
  NS_LOG_FUNCTION (this);
}

PrioQueueDiscFilter::~PrioQueueDiscFilter ()
{
  NS_LOG_FUNCTION (this);
}

bool
PrioQueueDiscFilter::CheckProtocol (Ptr<QueueDiscItem> item) const
{
  NS_LOG_FUNCTION (this);
  RtoPriTag rtoPriTag;
  Ptr<Packet> p = item->GetPacket();
  bool found = p->PeekPacketTag(rtoPriTag);
  NS_LOG_DEBUG("found is "<< found);
  return found;
}

int32_t
PrioQueueDiscFilter::DoClassify (Ptr<QueueDiscItem> item) const
{
  NS_LOG_FUNCTION (this);
  RtoPriTag rtoPriTag;
  Ptr<Packet> p = item->GetPacket();
  bool found = p->PeekPacketTag(rtoPriTag);
  return rtoPriTag.GetRtoRank();
    
}




NS_OBJECT_ENSURE_REGISTERED (PrioSubqueueDiscFilter);


TypeId 
PrioSubqueueDiscFilter::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::PrioSubqueueDiscFilter")
    .SetParent<PacketFilter> ()
    .SetGroupName ("TrafficControl")
    .AddConstructor<PrioSubqueueDiscFilter> () //属于TrafficControl的模块要填加constructor
  ;
  return tid;
}

PrioSubqueueDiscFilter::PrioSubqueueDiscFilter ()
{
  NS_LOG_FUNCTION (this);
}

PrioSubqueueDiscFilter::~PrioSubqueueDiscFilter ()
{
  NS_LOG_FUNCTION (this);
}


bool
PrioSubqueueDiscFilter::CheckProtocol (Ptr<QueueDiscItem> item) const
{
  NS_LOG_FUNCTION (this);
  RtoPriTag rtoPriTag;
  Ptr<Packet> p = item->GetPacket();
  bool found = p->PeekPacketTag(rtoPriTag);
  return found;
}

int32_t
PrioSubqueueDiscFilter::DoClassify (Ptr<QueueDiscItem> item) const
{
  NS_LOG_FUNCTION (this);
  RtoPriTag rtoPriTag;
  Ptr<Packet> p = item->GetPacket();
  bool found = p->PeekPacketTag(rtoPriTag);
  return rtoPriTag.GetSizeRank();
}

}