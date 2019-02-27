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

#ifndef PRIO_QUEUE_DISC_FILTER_H
#define PRIO_QUEUE_DISC_FILTER_H

#include "ns3/object.h"
#include "ns3/packet-filter.h"
#include "ns3/queue-disc.h"

namespace ns3{

/**
 * \ingroup traffic-control-test
 * \ingroup tests
 *
 * \brief Prio Queue Disc Test Item
 */
class PrioQueueDiscFilter : public PacketFilter
{
public:
  /**
   * \brief Get the type ID.
   * \return the object TypeId
   */
  static TypeId GetTypeId (void);
  /**
   * Constructor
   *
   * \param cls whether this filter is able to classify a PrioQueueDiscTestItem
   */
  PrioQueueDiscFilter ();
  virtual ~PrioQueueDiscFilter ();

private:
  virtual bool CheckProtocol (Ptr<QueueDiscItem> item) const;
  virtual int32_t DoClassify (Ptr<QueueDiscItem> item) const;

};




/**
 * \ingroup traffic-control-test
 * \ingroup tests
 *
 * \brief Prio Queue Disc Test Packet Filter
 */
class PrioSubqueueDiscFilter : public PacketFilter
{
public:
  /**
   * \brief Get the type ID.
   * \return the object TypeId
   */
  static TypeId GetTypeId (void);
  /**
   * Constructor
   *
   * \param cls whether this filter is able to classify a PrioQueueDiscTestItem
   */
  PrioSubqueueDiscFilter ();
  virtual ~PrioSubqueueDiscFilter ();

private:
  virtual bool CheckProtocol (Ptr<QueueDiscItem> item) const;
  virtual int32_t DoClassify (Ptr<QueueDiscItem> item) const;
};

}
#endif 