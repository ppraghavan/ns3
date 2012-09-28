/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright 2007 University of Washington
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
#include "ns3/ipv4-address.h"
#include "ns3/address-utils.h"
#include "ns3/nstime.h"
#include "ns3/inet-socket-address.h"
#include "ns3/socket.h"
#include "ns3/udp-socket.h"
#include "ns3/simulator.h"
#include "ns3/socket-factory.h"
#include "ns3/packet.h"
#include "ns3/trace-source-accessor.h"
#include "ns3/uinteger.h"
#include "ns3/ipv4.h"
#include "ns3/vector.h"
#include "ns3/mobility-module.h"

#include "mdc-event-sensor.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("MdcEventSensorApplication");
NS_OBJECT_ENSURE_REGISTERED (MdcEventSensor);

TypeId
MdcEventSensor::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::MdcEventSensor")
    .SetParent<Application> ()
    .AddConstructor<MdcEventSensor> ()
    .AddAttribute ("Port", "Port on which we send packets to sink nodes or MDCs.",
                   UintegerValue (9999),
                   MakeUintegerAccessor (&MdcEventSensor::m_port),
                   MakeUintegerChecker<uint16_t> ())
    .AddAttribute ("RemoteAddress", 
                   "The destination Ipv4Address of the sink",
                   Ipv4AddressValue (),
                   MakeIpv4AddressAccessor (&MdcEventSensor::m_servAddress),
                   MakeIpv4AddressChecker ())
    .AddAttribute ("Timeout", "Time to wait for a reply before trying to resend.",
                   TimeValue (Seconds (3)),
                   MakeTimeAccessor (&MdcEventSensor::m_timeout),
                   MakeTimeChecker ())
    .AddAttribute ("MaxRetries", 
                   "The maximum number of times the application will attempt to resend a failed packet",
                   UintegerValue (3),
                   MakeUintegerAccessor (&MdcEventSensor::m_retries),
                   MakeUintegerChecker<uint8_t> ())
    .AddAttribute ("PacketSize", "Size of sensed data in outbound packets",
                   UintegerValue (10000),
                   MakeUintegerAccessor (&MdcEventSensor::SetDataSize,
                                         &MdcEventSensor::GetDataSize),
                   MakeUintegerChecker<uint32_t> ())
    .AddTraceSource ("Send", "A new packet is created and is sent to the sink or an MDC",
                     MakeTraceSourceAccessor (&MdcEventSensor::m_sendTrace))
    .AddTraceSource ("Rcv", "A packet is received",
                     MakeTraceSourceAccessor (&MdcEventSensor::m_rcvTrace))
  ;
  return tid;
}

MdcEventSensor::MdcEventSensor ()
{
  NS_LOG_FUNCTION_NOARGS ();

  m_sent = 0;
  m_socket = 0;
  m_data = 0;
  m_dataSize = 0;

  m_address = Ipv4Address((uint32_t)0);
}

MdcEventSensor::~MdcEventSensor()
{
  NS_LOG_FUNCTION_NOARGS ();
  m_socket = 0;

  delete [] m_data;

  m_data = 0;
  m_dataSize = 0;
}

void 
MdcEventSensor::SetSink (Ipv4Address ip, uint16_t port /* = 0*/)
{
  m_servAddress = ip;

  if (port)
    m_port = port; //may already be set
}

void
MdcEventSensor::DoDispose (void)
{
  NS_LOG_FUNCTION_NOARGS ();
  Application::DoDispose ();
}

void 
MdcEventSensor::StartApplication (void)
{
  NS_LOG_FUNCTION_NOARGS ();

  if (m_startTime >= m_stopTime)
    {
      NS_LOG_LOGIC ("Cancelling application (start > stop)");
      //DoDispose ();
      return;
    }

  TypeId tid = TypeId::LookupByName ("ns3::UdpSocketFactory");

  if (m_socket == 0)
    {
      TypeId tid = TypeId::LookupByName ("ns3::UdpSocketFactory");
      m_socket = Socket::CreateSocket (GetNode (), tid);
      InetSocketAddress local = InetSocketAddress (Ipv4Address::GetAny (), m_port);
      m_socket->Bind (local);

      NS_LOG_LOGIC ("Socket bound");
    }

  // Use the first address of the first non-loopback device on the node for our address
  if (m_address.Get () == 0)
    {
      Ptr<Ipv4> ipv4 = GetNode ()->GetObject<Ipv4> ();
      Ipv4Address loopback = Ipv4Address::GetLoopback ();
      
      for (uint32_t i = 0; i < ipv4->GetNInterfaces (); i++)
        {
          Ipv4Address addr = ipv4->GetAddress (i,0).GetLocal ();
          if (addr != loopback)
            m_address = addr;
        }
    }

  m_socket->SetRecvCallback (MakeCallback (&MdcEventSensor::HandleRead, this));
}

void 
MdcEventSensor::StopApplication ()
{
  NS_LOG_FUNCTION_NOARGS ();

  if (m_socket != 0) 
    {
      m_socket->Close ();
      m_socket->SetRecvCallback (MakeNullCallback<void, Ptr<Socket> > ());
      m_socket = 0;
    }

  CancelEvents ();
}

void
MdcEventSensor::CancelEvents ()
{
  NS_LOG_FUNCTION_NOARGS ();

  for (std::list<EventId>::iterator itr = m_events.begin ();
       itr != m_events.end (); itr++)
    Simulator::Cancel (*itr);
}

void 
MdcEventSensor::SetFill (std::string fill)
{
  NS_LOG_FUNCTION (fill);

  uint32_t dataSize = fill.size () + 1;

  if (dataSize != m_dataSize)
    {
      delete [] m_data;
      m_data = new uint8_t [dataSize];
      m_dataSize = dataSize;
    }

  memcpy (m_data, fill.c_str (), dataSize);

  //

  // Overwrite packet size attribute.
  //
  m_size = dataSize;
}

void 
MdcEventSensor::SetFill (uint8_t fill, uint32_t dataSize)
{
  if (dataSize != m_dataSize)
    {
      delete [] m_data;
      m_data = new uint8_t [dataSize];
      m_dataSize = dataSize;
    }

  memset (m_data, fill, dataSize);

  //
  // Overwrite packet size attribute.
  //
  m_size = dataSize;
}

void 
MdcEventSensor::SetFill (uint8_t *fill, uint32_t fillSize, uint32_t dataSize)
{
  if (dataSize != m_dataSize)
    {
      delete [] m_data;
      m_data = new uint8_t [dataSize];
      m_dataSize = dataSize;
    }

  if (fillSize >= dataSize)
    {
      memcpy (m_data, fill, dataSize);
      return;
    }

  //
  // Do all but the final fill.
  //
  uint32_t filled = 0;
  while (filled + fillSize < dataSize)
    {
      memcpy (&m_data[filled], fill, fillSize);
      filled += fillSize;
    }

  //
  // Last fill may be partial
  //
  memcpy (&m_data[filled], fill, dataSize - filled);

  //
  // Overwrite packet size attribute.
  //
  m_size = dataSize;
}

void 
MdcEventSensor::SetDataSize (uint32_t dataSize)
{
  NS_LOG_FUNCTION (dataSize);

  //
  // If the client is setting the echo packet data size this way, we infer
  // that she doesn't care about the contents of the packet at all, so 
  // neither will we.
  //
  delete [] m_data;
  m_data = 0;
  m_dataSize = 0;
  m_size = dataSize;
}

uint32_t 
MdcEventSensor::GetDataSize (void) const
{
  NS_LOG_FUNCTION_NOARGS ();
  return m_size;
}

void
MdcEventSensor::ScheduleEventDetection (Time t, bool noData /* = false*/)
{
  ScheduleTransmit (t, noData);
}

void 
MdcEventSensor::ScheduleTransmit (Time dt, bool noData /*= false*/)
{
  NS_LOG_FUNCTION_NOARGS ();
  m_events.push_front (Simulator::Schedule (dt, &MdcEventSensor::Send, this, noData));
}

void 
MdcEventSensor::Send (bool noData)
{
  NS_LOG_FUNCTION_NOARGS ();

  NS_LOG_LOGIC ("Sending " << (noData ? "data notify" : "full data") << " packet.");

  MdcHeader head;
  //head.SetSeq (m_sent);
  head.SetOrigin (m_address);
  head.SetDest (m_servAddress); //TODO: handle addressing MDC!

  Vector pos = GetNode ()->GetObject<MobilityModel> ()->GetPosition ();
  head.SetPosition (pos.x, pos.y);

  Ptr<Packet> p;
  if (noData)
    {
      p = Create<Packet> (0);
      head.SetData (0);
    }
  else
    {
      // If only notifying of an event and not sending the full data,
      // don't add fill and set the data size to be 0.
      if (m_dataSize)
        {
          //
          // If m_dataSize is non-zero, we have a data buffer of the same size that we
          // are expected to copy and send.  This state of affairs is created if one of
          // the Fill functions is called.  In this case, m_size must have been set
          // to agree with m_dataSize
          //
          NS_ASSERT_MSG (m_dataSize == m_size, "MdcEventSensor::Send(): m_size and m_dataSize inconsistent");
          NS_ASSERT_MSG (m_data, "MdcEventSensor::Send(): m_dataSize but no m_data");
          p = Create<Packet> (m_data, m_dataSize);
        }
      else
        {
          //
          // If m_dataSize is zero, the client has indicated that she doesn't care 
          // about the data itself either by specifying the data size by setting
          // the corresponding atribute or by not calling a SetFill function.  In 
          // this case, we don't worry about it either.  But we do allow m_size
          // to have a value different from the (zero) m_dataSize.
          //
          p = Create<Packet> (m_size);
        }
      head.SetData (p->GetSize ());
    }

  p->AddHeader (head);

  // call to the trace sinks before the packet is actually sent,
  // so that tags added to the packet can be sent as well
  m_sendTrace (p);
  m_socket->SendTo (p, 0, InetSocketAddress(head.GetDest (), m_port));
  //ScheduleTimeout (m_sent++);
}

void 
MdcEventSensor::HandleRead (Ptr<Socket> socket)
{
  Ptr<Packet> packet;
  Address from;

  while (packet = socket->RecvFrom (from))
    {
      NS_LOG_LOGIC ("Reading packet from socket.");

      if (InetSocketAddress::IsMatchingType (from))
        {
          Ipv4Address source = InetSocketAddress::ConvertFrom (from).GetIpv4 ();

          MdcHeader head;
          packet->PeekHeader (head);

          // If the packet is for us, process the ACK
          if (head.GetDest () == m_address)
            {
              ProcessAck (packet, source);
            }

          // Else it was a broadcast from an MDC
          else
            {
              return; //TODO: implement!
            }
        }
    }
}

void
MdcEventSensor::ProcessAck (Ptr<Packet> packet, Ipv4Address source)
{
  NS_LOG_LOGIC ("ACK received");

  MdcHeader head;
  packet->PeekHeader (head);
  //uint32_t seq = head.GetSeq ();
  
  //m_ackTrace (packet, GetNode ()-> GetId ());
  //m_outstandingSeqs.erase (seq);
  //TODO: handle an ack from an old seq number
}

void
MdcEventSensor::ScheduleTimeout (uint32_t seq)
{
  m_events.push_front (Simulator::Schedule (m_timeout, &MdcEventSensor::CheckTimeout, this, seq));
  m_outstandingSeqs.insert (seq);
}

Ipv4Address
MdcEventSensor::GetAddress () const
{
  return m_address;
}

void
MdcEventSensor::CheckTimeout (uint32_t seq)
{
  std::set<uint32_t>::iterator itr = m_outstandingSeqs.find (seq);

  // If it's timed out, we should try a different path to the server
  if (itr != m_outstandingSeqs.end ())
    {
      NS_LOG_LOGIC ("Packet with seq# " << seq << " timed out.");
      m_outstandingSeqs.erase (itr);
      
      if (m_sent < m_retries) //TODO: this doesn't do as intended! must keep tries of attempts for this packet.
        ScheduleTransmit (Seconds (0.0), true);
    }
}

} // Namespace ns3