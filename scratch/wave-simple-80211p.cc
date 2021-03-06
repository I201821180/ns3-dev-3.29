/* -*-  Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2005,2006,2007 INRIA
 * Copyright (c) 2013 Dalian University of Technology
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
 * Author: Mathieu Lacage <mathieu.lacage@sophia.inria.fr>
 * Author: Junling Bu <linlinjavaer@gmail.com>
 *
 */
/**
 * This example shows basic construction of an 802.11p node.  Two nodes
 * are constructed with 802.11p devices, and by default, one node sends a single
 * packet to another node (the number of packets and interval between
 * them can be configured by command-line arguments).  The example shows
 * typical usage of the helper classes for this mode of WiFi (where "OCB" refers
 * to "Outside the Context of a BSS")."
 */

#include <iostream>

#include "ns3/core-module.h"
#include "ns3/mobility-module.h"
#include "ns3/internet-module.h"
#include "ns3/csma-module.h"
#include "ns3/applications-module.h"
#include "ns3/yans-wifi-helper.h"
#include "ns3/wave-mac-helper.h"
#include "ns3/wifi-80211p-helper.h"
#include "ns3/wifi-net-device.h"
#include "ns3/netanim-module.h"
#include "ns3/lte-module.h"
#include "ns3/point-to-point-helper.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("WifiSimpleOcb");

/*
 * In WAVE module, there is no net device class named like "Wifi80211pNetDevice",
 * instead, we need to use Wifi80211pHelper to create an object of
 * WifiNetDevice class.
 *
 * usage:
 *  NodeContainer nodes;
 *  NetDeviceContainer devices;
 *  nodes.Create (2);
 *  YansWifiPhyHelper wifiPhy = YansWifiPhyHelper::Default ();
 *  YansWifiChannelHelper wifiChannel = YansWifiChannelHelper::Default ();
 *  wifiPhy.SetChannel (wifiChannel.Create ());
 *  NqosWaveMacHelper wifi80211pMac = NqosWave80211pMacHelper::Default();
 *  Wifi80211pHelper wifi80211p = Wifi80211pHelper::Default ();
 *  devices = wifi80211p.Install (wifiPhy, wifi80211pMac, nodes);
 *
 * The reason of not providing a 802.11p class is that most of modeling
 * 802.11p standard has been done in wifi module, so we only need a high
 * MAC class that enables OCB mode.
 */

static inline std::string
PrintReceivedPacket (Ptr<Socket> socket, Ptr<Packet> packet, Address srcAddress)
{
  std::ostringstream oss;

  oss << Simulator::Now ().GetSeconds () << " node " << socket->GetNode ()->GetId ();

  if (InetSocketAddress::IsMatchingType (srcAddress))
    {
      InetSocketAddress addr = InetSocketAddress::ConvertFrom (srcAddress);
      oss << " received one packet from " << addr.GetIpv4 ();
      uint8_t buffer[packet->GetSize()];
      packet->CopyData(buffer, packet->GetSize());
      oss << ". data: " << buffer;
    }
  else
    {
      oss << " received one packet!";
    }
  return oss.str ();
}

void
ReceivePacket (Ptr<Socket> socket)
{
  Ptr<Packet> packet;
  Address srcAddress;
  while ((packet = socket->RecvFrom (srcAddress)))
    {
//      uint32_t RxRoutingBytes = packet->GetSize ();

      NS_LOG_UNCOND (PrintReceivedPacket (socket, packet, srcAddress));
    }
}

void
TwoAddressTrace (std::string context, Ptr<const Packet> packet, const Address & srcAddr, const Address & destAddr)
{
  std::ostringstream oss;

  oss << Simulator::Now ().GetSeconds ();
  if (InetSocketAddress::IsMatchingType (destAddr))
      {
        InetSocketAddress addr = InetSocketAddress::ConvertFrom (destAddr);
        oss << " " << addr.GetIpv4 ();
      }

  if (InetSocketAddress::IsMatchingType (srcAddr))
    {
      InetSocketAddress addr = InetSocketAddress::ConvertFrom (srcAddr);
      oss << " received one packet from " << addr.GetIpv4 ();
      uint8_t buffer[packet->GetSize() + 1];
      packet->CopyData(buffer, packet->GetSize());
      buffer[packet->GetSize()] = '\0';
      oss << ". data: " << buffer;
    }
  else
    {
      oss << " received one packet!";
    }
  NS_LOG_UNCOND(oss.str ());
}

static void
GenerateTraffic (Ptr<Socket> socket, uint32_t pktSize,
                             uint32_t pktCount, Time pktInterval )
{
  if (pktCount > 0)
    {
      socket->Send (Create<Packet> (pktSize));
      Simulator::Schedule (pktInterval, &GenerateTraffic,
                           socket, pktSize,pktCount - 1, pktInterval);
    }
  else
    {
      socket->Close ();
    }
}

uint32_t m_dataSize = 0; //!< packet payload size (must be equal to m_size)
uint8_t *m_data = 0; //!< packet payload data
void
SetFill (std::string fill)
{
  uint32_t dataSize = fill.size () + 1;

  if (dataSize != m_dataSize)
    {
      delete [] m_data;
      m_data = new uint8_t [dataSize];
      m_dataSize = dataSize;
    }

  memcpy (m_data, fill.c_str (), dataSize);
}

void
SendData (Ptr<Socket> socket, std::string data)
{
  SetFill(data);
  Ptr<Packet> packet = Create<Packet> (m_data, m_dataSize);
  socket->Send (packet);
//  socket->Close ();
}

const uint8_t *
XOR (const uint8_t * a, const uint8_t * b, size_t size, uint8_t * c)
{
  for (uint32_t i = 0; i < size; i++)
    {
      c[i] = a[i] ^ b[i];
    }

  return c;
}

const uint8_t *
XOR (const uint8_t * a, const std::string b, size_t size, uint8_t * c)
{
  uint8_t b1[size] = {0};
  memcpy(b1, (const uint8_t *)b.c_str(), b.length() + 1);
  for (uint32_t i = 0; i < size; i++)
    {
      c[i] = a[i] ^ b1[i];
    }

  return c;
}

const uint8_t *
XOR (const std::string a, const std::string b, uint8_t * c)
{
  uint32_t size = std::max(a.length(), b.length()) + 1;
  uint8_t a1[size] = {0};
  uint8_t b1[size] = {0};
  memcpy(a1, (const uint8_t *)a.c_str(), a.length() + 1);
  memcpy(b1, (const uint8_t *)b.c_str(), b.length() + 1);
  for (uint32_t i = 0; i < size; i++)
    {
      c[i] = a1[i] ^ b1[i];
    }

  return c;
}

int main (int argc, char *argv[])
{
  std::string phyMode ("OfdmRate6MbpsBW10MHz");
  uint32_t packetSize = 1000; // bytes
  uint32_t numPackets = 20;
  double interval = 1.0; // seconds
  bool verbose = false;
  double txp = 29.0;
  double distance = 29.0;
  double intervalTime = 0.1;

  std::map<std::uint32_t, std::vector<std::uint8_t *>> cache;

  CommandLine cmd;

  cmd.AddValue ("phyMode", "Wifi Phy mode", phyMode);
  cmd.AddValue ("packetSize", "size of application packet sent", packetSize);
  cmd.AddValue ("numPackets", "number of packets generated", numPackets);
  cmd.AddValue ("interval", "interval (seconds) between packets", interval);
  cmd.AddValue ("verbose", "turn on all WifiNetDevice log components", verbose);
  cmd.AddValue ("txp", "txp", txp);
  cmd.AddValue ("distance", "distance", distance);
  cmd.AddValue ("intervalTime", "broadcast interval time", intervalTime);
  cmd.Parse (argc, argv);
  // Convert to time object
  Time interPacketInterval = Seconds (interval);

  //Set Non-unicastMode rate to unicast mode
//  Config::SetDefault ("ns3::WifiRemoteStationManager::NonUnicastMode",StringValue (phyMode));

  NodeContainer nodes;
  nodes.Create (3);

  // The below set of helpers will help us to put together the wifi NICs we want
  YansWifiPhyHelper wifiPhy =  YansWifiPhyHelper::Default ();
  YansWifiChannelHelper wifiChannel = YansWifiChannelHelper::Default ();
  Ptr<YansWifiChannel> channel = wifiChannel.Create ();
  wifiPhy.SetChannel (channel);
  // ns-3 supports generate a pcap trace
  wifiPhy.SetPcapDataLinkType (WifiPhyHelper::DLT_IEEE802_11);
  NqosWaveMacHelper wifi80211pMac = NqosWaveMacHelper::Default ();
  Wifi80211pHelper wifi80211p = Wifi80211pHelper::Default ();
  if (verbose)
    {
      wifi80211p.EnableLogComponents ();      // Turn on all Wifi 802.11p logging
    }

  wifi80211p.SetRemoteStationManager ("ns3::ConstantRateWifiManager",
                                      "DataMode",StringValue (phyMode),
                                      "ControlMode",StringValue (phyMode));

  // Set Tx Power
  wifiPhy.Set ("TxPowerStart",DoubleValue (29));
  wifiPhy.Set ("TxPowerEnd", DoubleValue (29));

  NetDeviceContainer devices_80211p = wifi80211p.Install (wifiPhy, wifi80211pMac, nodes);

  // Adjust Tx Power 1
//  Ptr<WifiNetDevice> device = DynamicCast<WifiNetDevice>(devices_80211p.Get(2));
//  device->GetPhy()->SetTxPowerStart(txp);
//  device->GetPhy()->SetTxPowerEnd(txp);

  // Adjust Tx Power 2
  Config::Set("/NodeList/*/DeviceList/0/$ns3::WifiNetDevice/Phy/TxPowerStart", DoubleValue (txp));
  Config::Set("/NodeList/*/DeviceList/0/$ns3::WifiNetDevice/Phy/TxPowerEnd", DoubleValue (txp));

  CsmaHelper csmaHelper;
  csmaHelper.SetChannelAttribute ("DataRate", StringValue ("5Mbps"));
  csmaHelper.SetChannelAttribute ("Delay", StringValue ("2ms"));

  NetDeviceContainer devices_csma = csmaHelper.Install(nodes);

  // Tracing
//  wifiPhy.EnableAscii ("wave-simple-80211p", devices_80211p);
//  wifiPhy.EnableAscii ("wave-simple-csma", devices_csma);

  MobilityHelper mobility;
  Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator> ();
  positionAlloc->Add (Vector (0.0, 0.0, 0.0));
  positionAlloc->Add (Vector (distance, 0.0, 0.0));
  positionAlloc->Add (Vector (600.0, 0.0, 0.0));
//  positionAlloc->Add (Vector (2500.0, 0.0, 0.0));

  mobility.SetPositionAllocator (positionAlloc);
  mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
  mobility.Install (nodes);

  InternetStackHelper internet;
  internet.Install (nodes);

  Ipv4AddressHelper ipv4;
  NS_LOG_INFO ("Assign IP Addresses.");
  ipv4.SetBase ("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer interface_80211p = ipv4.Assign (devices_80211p);

//  ipv4.SetBase ("192.168.1.0", "255.255.255.0");
//  Ipv4InterfaceContainer interface_csma = ipv4.Assign (devices_csma);

  TypeId tid = TypeId::LookupByName ("ns3::UdpSocketFactory");
//  InetSocketAddress local = InetSocketAddress (Ipv4Address::GetAny (), 80);
//  for (uint32_t i =0; i < nodes.GetN(); i++)
//    {
//      Ptr<Socket> recvSink = Socket::CreateSocket (nodes.Get (i), tid);
//      recvSink->Bind (local);
//      recvSink->SetRecvCallback (MakeCallback (&ReceivePacket));
//    }

  Ptr<Socket> source = Socket::CreateSocket (nodes.Get (0), tid);
//  InetSocketAddress remote = InetSocketAddress (Ipv4Address ("255.255.255.255"), 80);
  source->SetAllowBroadcast (true);
//  InetSocketAddress remote = InetSocketAddress (interface_80211p.GetAddress(1), 80);
  InetSocketAddress remote = InetSocketAddress (Ipv4Address::GetBroadcast(), 80);
  source->Connect (remote);

  Ptr<Socket> source1 = Socket::CreateSocket (nodes.Get (2), tid);
//  InetSocketAddress remote2 = InetSocketAddress (Ipv4Address ("255.255.255.255"), 80);
  source1->SetAllowBroadcast (true);
  InetSocketAddress remote2 = InetSocketAddress (interface_80211p.GetAddress(1), 80);
  source1->Connect (remote2);

//  Simulator::ScheduleWithContext (source->GetNode ()->GetId (),
//                                  Seconds (1.0), &GenerateTraffic,
//                                  source, packetSize, numPackets, interPacketInterval);

  ApplicationContainer serverApps;
  ApplicationContainer clientApps;
  for (uint32_t u = 0; u < nodes.GetN (); ++u)
    {
//      PacketSinkHelper obuPacketSinkHelper ("ns3::UdpSocketFactory", InetSocketAddress (interface_80211p.GetAddress(u), 80));
      PacketSinkHelper obuPacketSinkHelper ("ns3::UdpSocketFactory", InetSocketAddress (Ipv4Address::GetAny(), 80));
      serverApps.Add (obuPacketSinkHelper.Install (nodes.Get(u)));
    }
  Config::Connect ("/NodeList/*/ApplicationList/*/$ns3::PacketSink/RxWithAddresses", MakeCallback (&TwoAddressTrace));
  serverApps.Start (Seconds (0.01));

  Simulator::Schedule(Seconds(2), &SendData, source, "haha");
//  Simulator::Schedule(Seconds(1 + intervalTime), &SendData, source1, "test");

  // -----------------------------------------XOR-----------------------------------------------------
//  std::string a = "hahale()11*#&*&*";
//  std::string b = "wowo";
//  size_t size = std::max(a.length(), b.length()) + 1;
//  uint8_t c[size] = {0};
//  uint8_t d[size] = {0};
//
//  NS_LOG_UNCOND("d " << XOR(XOR(a, b, c), b, size, d));
//  NS_LOG_UNCOND("test " << ((12345 ^ 98765) ^ 12345));
//
//  NS_LOG_UNCOND("test float " << ((double)997 / 998));


  // -----------------------------------------LTE-----------------------------------------------------
//  Ptr<LteHelper> lteHelper = CreateObject<LteHelper> ();
//  Ptr<PointToPointEpcHelper>  epcHelper = CreateObject<PointToPointEpcHelper> ();
//  lteHelper->SetEpcHelper (epcHelper);
//
//  Ptr<Node> pgw = epcHelper->GetPgwNode ();
//
//   // Create a single RemoteHost
//  NodeContainer remoteHostContainer;
//  remoteHostContainer.Create (1);
//  Ptr<Node> remoteHost = remoteHostContainer.Get (0);
//  internet.Install (remoteHostContainer);
//
//  // Create the Internet
//  PointToPointHelper p2ph;
//  p2ph.SetDeviceAttribute ("DataRate", DataRateValue (DataRate ("100Gb/s")));
//  p2ph.SetDeviceAttribute ("Mtu", UintegerValue (1500));
//  p2ph.SetChannelAttribute ("Delay", TimeValue (Seconds (0.010)));
//  NetDeviceContainer internetDevices = p2ph.Install (pgw, remoteHost);
//  ipv4.SetBase ("1.0.0.0", "255.0.0.0");
//  Ipv4InterfaceContainer internetIpIfaces = ipv4.Assign (internetDevices);
//
//  Ipv4StaticRoutingHelper ipv4RoutingHelper;
//  Ptr<Ipv4StaticRouting> remoteHostStaticRouting = ipv4RoutingHelper.GetStaticRouting (remoteHost->GetObject<Ipv4> ());
//  remoteHostStaticRouting->AddNetworkRouteTo (Ipv4Address ("7.0.0.0"), Ipv4Mask ("255.0.0.0"), 1);
//
//  NodeContainer ueNodes;
//  NodeContainer enbNodes;
//  enbNodes.Create(1);
//  ueNodes.Add(nodes);
//
//  positionAlloc = CreateObject<ListPositionAllocator> ();
//  positionAlloc->Add (Vector (0.0, 5000.0, 0.0));
//  mobility.SetPositionAllocator (positionAlloc);
//  mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
//  mobility.Install (enbNodes);
//
//  // Install LTE Devices to the nodes
//  NetDeviceContainer enbLteDevs = lteHelper->InstallEnbDevice (enbNodes);
//  NetDeviceContainer ueLteDevs = lteHelper->InstallUeDevice (ueNodes);
//
//  Ipv4InterfaceContainer ueIpIface;
//  ueIpIface = epcHelper->AssignUeIpv4Address (NetDeviceContainer (ueLteDevs));
//
//  for (uint32_t u = 0; u < ueNodes.GetN (); ++u)
//    {
//      Ptr<Node> ueNode = ueNodes.Get (u);
////      NS_LOG_UNCOND("ueNode->GetNDevices() " << ueNode->GetNDevices());
////      for (uint32_t i = 0; i < ueNode->GetNDevices(); i++)
////      	{
////      	  Ptr<NetDevice> device = ueNode->GetDevice(i);
////      	  Ptr<Ipv4> ipv4 = ueNode->GetObject<Ipv4> ();
////      	  int32_t interface = ipv4->GetInterfaceForDevice (device);
////      	  NS_LOG_UNCOND("node " << ueNode->GetId() << " ip " << ipv4->GetAddress(interface, 0).GetLocal());
////      	}
//      // Set the default gateway for the UE
//      Ptr<Ipv4StaticRouting> ueStaticRouting = ipv4RoutingHelper.GetStaticRouting (ueNode->GetObject<Ipv4> ());
//      ueStaticRouting->SetDefaultRoute (epcHelper->GetUeDefaultGatewayAddress (), 1);
//    }
//
//  // Attach one UE to eNodeB
//  lteHelper->Attach (ueLteDevs, enbLteDevs.Get(0));  // side effect: the default EPS bearer will be activated
//
//  // Install and start applications on UEs and remote host
//  uint16_t dlPort = 1234;
//  TypeId tid1 = TypeId::LookupByName ("ns3::UdpSocketFactory");
//  InetSocketAddress local1 = InetSocketAddress (Ipv4Address::GetAny (), dlPort);
//  for (int i =0; i < 4; i++)
//    {
//      Ptr<Socket> recvSink = Socket::CreateSocket (ueNodes.Get (i), tid1);
//      recvSink->Bind (local1);
//      recvSink->SetRecvCallback (MakeCallback (&ReceivePacket));
//    }
//
//  Ptr<Socket> source1 = Socket::CreateSocket (remoteHost, tid);
////  InetSocketAddress remote1 = InetSocketAddress (Ipv4Address ("10.1.1.255"), dlPort);
////  source1->SetAllowBroadcast (true);
//  InetSocketAddress remote1 = InetSocketAddress (ueIpIface.GetAddress(0), dlPort);
//  source1->Connect (remote1);
//
//  Simulator::Schedule(Seconds(1), &SendData, source1, "haha-1");
//  Simulator::Schedule(Seconds(2), &SendData, source1, "test-1");
//  Simulator::Schedule(Seconds(10), &SendData, source1, "hehe-1");
//  Simulator::Schedule(Seconds(100), &SendData, source1, "heihei-1");

//  OnOffHelper onoff1 ("ns3::UdpSocketFactory",Address ());
//  onoff1.SetAttribute ("OnTime", StringValue ("ns3::ConstantRandomVariable[Constant=1.0]"));
//  onoff1.SetAttribute ("OffTime", StringValue ("ns3::ConstantRandomVariable[Constant=0.0]"));
//  Ptr<UniformRandomVariable> var = CreateObject<UniformRandomVariable> ();
//  for (uint32_t i = 0; i < ueIpIface.GetN(); i++)
//    {
//      Ptr<Socket> source = Socket::CreateSocket (remoteHost, tid1);
//      InetSocketAddress remote = InetSocketAddress (ueIpIface.GetAddress(i), dlPort);
//      source->Connect (remote);
//      Ptr<UniformRandomVariable> var = CreateObject<UniformRandomVariable> ();
//      var->SetStream (i);
//      Simulator::ScheduleWithContext (source->GetNode ()->GetId (),
//				      Seconds (var->GetValue(0.0, 1.0)), &GenerateTraffic,
//				      source, packetSize, numPackets, interPacketInterval);
////  source->SetAllowBroadcast (true);
//
////      var->SetStream (i);
////      InetSocketAddress dest = InetSocketAddress (ueIpIface.GetAddress(i), dlPort);
////      onoff1.SetAttribute ("Remote", AddressValue(dest));
////      ApplicationContainer temp = onoff1.Install (remoteHost);
////      temp.Start (Seconds (var->GetValue (1.0,2.0)));
////      temp.Stop (Seconds (10));
//    }


  Simulator::Stop(Seconds(110));
  Simulator::Run ();
  Simulator::Destroy ();

  return 0;
}
