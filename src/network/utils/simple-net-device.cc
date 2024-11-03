/*
 * Copyright (c) 2008 INRIA
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Author: Mathieu Lacage <mathieu.lacage@sophia.inria.fr>
 */
#include "simple-net-device.h"

#include "error-model.h"
#include "queue.h"
#include "simple-channel.h"

#include "ns3/boolean.h"
#include "ns3/log.h"
#include "ns3/node.h"
#include "ns3/packet.h"
#include "ns3/pointer.h"
#include "ns3/simulator.h"
#include "ns3/string.h"
#include "ns3/tag.h"
#include "ns3/trace-source-accessor.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("SimpleNetDevice");

/**
 * \brief SimpleNetDevice tag to store source, destination and protocol of each packet.
 */
class SimpleTag : public Tag
{
  public:
    /**
     * \brief Get the type ID.
     * \return the object TypeId
     */
    static TypeId GetTypeId();
    TypeId GetInstanceTypeId() const override;

    uint32_t GetSerializedSize() const override;
    void Serialize(TagBuffer i) const override;
    void Deserialize(TagBuffer i) override;

    /**
     * Set the source address
     * \param src source address
     */
    void SetSrc(Mac48Address src);
    /**
     * Get the source address
     * \return the source address
     */
    Mac48Address GetSrc() const;

    /**
     * Set the destination address
     * \param dst destination address
     */
    void SetDst(Mac48Address dst);
    /**
     * Get the destination address
     * \return the destination address
     */
    Mac48Address GetDst() const;

    /**
     * Set the protocol number
     * \param proto protocol number
     */
    void SetProto(uint16_t proto);
    /**
     * Get the protocol number
     * \return the protocol number
     */
    uint16_t GetProto() const;

    void Print(std::ostream& os) const override;

  private:
    Mac48Address m_src;        //!< source address
    Mac48Address m_dst;        //!< destination address
    uint16_t m_protocolNumber; //!< protocol number
};

NS_OBJECT_ENSURE_REGISTERED(SimpleTag);

TypeId
SimpleTag::GetTypeId()
{
    static TypeId tid = TypeId("ns3::SimpleTag")
                            .SetParent<Tag>()
                            .SetGroupName("Network")
                            .AddConstructor<SimpleTag>();
    return tid;
}

TypeId
SimpleTag::GetInstanceTypeId() const
{
    return GetTypeId();
}

uint32_t
SimpleTag::GetSerializedSize() const
{
    return 8 + 8 + 2;
}

void
SimpleTag::Serialize(TagBuffer i) const
{
    uint8_t mac[6];
    m_src.CopyTo(mac);
    i.Write(mac, 6);
    m_dst.CopyTo(mac);
    i.Write(mac, 6);
    i.WriteU16(m_protocolNumber);
}

void
SimpleTag::Deserialize(TagBuffer i)
{
    uint8_t mac[6];
    i.Read(mac, 6);
    m_src.CopyFrom(mac);
    i.Read(mac, 6);
    m_dst.CopyFrom(mac);
    m_protocolNumber = i.ReadU16();
}

void
SimpleTag::SetSrc(Mac48Address src)
{
    m_src = src;
}

Mac48Address
SimpleTag::GetSrc() const
{
    return m_src;
}

void
SimpleTag::SetDst(Mac48Address dst)
{
    m_dst = dst;
}

Mac48Address
SimpleTag::GetDst() const
{
    return m_dst;
}

void
SimpleTag::SetProto(uint16_t proto)
{
    m_protocolNumber = proto;
}

uint16_t
SimpleTag::GetProto() const
{
    return m_protocolNumber;
}

void
SimpleTag::Print(std::ostream& os) const
{
    os << "src=" << m_src << " dst=" << m_dst << " proto=" << m_protocolNumber;
}

NS_OBJECT_ENSURE_REGISTERED(SimpleNetDevice);

TypeId
SimpleNetDevice::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::SimpleNetDevice")
            .SetParent<NetDevice>()
            .SetGroupName("Network")
            .AddConstructor<SimpleNetDevice>()
            .AddAttribute("ReceiveErrorModel",
                          "The receiver error model used to simulate packet loss",
                          PointerValue(),
                          MakePointerAccessor(&SimpleNetDevice::m_receiveErrorModel),
                          MakePointerChecker<ErrorModel>())
            .AddAttribute("PointToPointMode",
                          "The device is configured in Point to Point mode",
                          BooleanValue(false),
                          MakeBooleanAccessor(&SimpleNetDevice::m_pointToPointMode),
                          MakeBooleanChecker())
            .AddAttribute("TxQueue",
                          "A queue to use as the transmit queue in the device.",
                          StringValue("ns3::DropTailQueue<Packet>"),
                          MakePointerAccessor(&SimpleNetDevice::m_queue),
                          MakePointerChecker<Queue<Packet>>())
            .AddAttribute("DataRate",
                          "The default data rate for point to point links. Zero means infinite",
                          DataRateValue(DataRate("0b/s")),
                          MakeDataRateAccessor(&SimpleNetDevice::m_bps),
                          MakeDataRateChecker())
            .AddTraceSource("PhyRxDrop",
                            "Trace source indicating a packet has been dropped "
                            "by the device during reception",
                            MakeTraceSourceAccessor(&SimpleNetDevice::m_phyRxDropTrace),
                            "ns3::Packet::TracedCallback");
    return tid;
}

SimpleNetDevice::SimpleNetDevice()
    : m_channel(nullptr),
      m_node(nullptr),
      m_mtu(0xffff),
      m_ifIndex(0),
      m_linkUp(false)
{
    NS_LOG_FUNCTION(this);
}

void
SimpleNetDevice::Receive(Ptr<Packet> packet, uint16_t protocol, Mac48Address to, Mac48Address from)
{
    NS_LOG_FUNCTION(this << packet << protocol << to << from);
    NetDevice::PacketType packetType;

    if (m_receiveErrorModel && m_receiveErrorModel->IsCorrupt(packet))
    {
        m_phyRxDropTrace(packet);
        return;
    }

    if (to == m_address)
    {
        packetType = NetDevice::PACKET_HOST;
    }
    else if (to.IsBroadcast())
    {
        packetType = NetDevice::PACKET_BROADCAST;
    }
    else if (to.IsGroup())
    {
        packetType = NetDevice::PACKET_MULTICAST;
    }
    else
    {
        packetType = NetDevice::PACKET_OTHERHOST;
    }

    if (packetType != NetDevice::PACKET_OTHERHOST)
    {
        m_rxCallback(this, packet, protocol, from);
    }

    if (!m_promiscCallback.IsNull())
    {
        m_promiscCallback(this, packet, protocol, from, to, packetType);
    }
}

void
SimpleNetDevice::SetChannel(Ptr<SimpleChannel> channel)
{
    NS_LOG_FUNCTION(this << channel);
    m_channel = channel;
    m_channel->Add(this);
    m_linkUp = true;
    m_linkChangeCallbacks();
}

Ptr<Queue<Packet>>
SimpleNetDevice::GetQueue() const
{
    NS_LOG_FUNCTION(this);
    return m_queue;
}

void
SimpleNetDevice::SetQueue(Ptr<Queue<Packet>> q)
{
    NS_LOG_FUNCTION(this << q);
    m_queue = q;
}

void
SimpleNetDevice::SetReceiveErrorModel(Ptr<ErrorModel> em)
{
    NS_LOG_FUNCTION(this << em);
    m_receiveErrorModel = em;
}

void
SimpleNetDevice::SetIfIndex(const uint32_t index)
{
    NS_LOG_FUNCTION(this << index);
    m_ifIndex = index;
}

uint32_t
SimpleNetDevice::GetIfIndex() const
{
    NS_LOG_FUNCTION(this);
    return m_ifIndex;
}

Ptr<Channel>
SimpleNetDevice::GetChannel() const
{
    NS_LOG_FUNCTION(this);
    return m_channel;
}

void
SimpleNetDevice::SetAddress(Address address)
{
    NS_LOG_FUNCTION(this << address);
    m_address = Mac48Address::ConvertFrom(address);
}

Address
SimpleNetDevice::GetAddress() const
{
    //
    // Implicit conversion from Mac48Address to Address
    //
    NS_LOG_FUNCTION(this);
    return m_address;
}

bool
SimpleNetDevice::SetMtu(const uint16_t mtu)
{
    NS_LOG_FUNCTION(this << mtu);
    m_mtu = mtu;
    return true;
}

uint16_t
SimpleNetDevice::GetMtu() const
{
    NS_LOG_FUNCTION(this);
    return m_mtu;
}

bool
SimpleNetDevice::IsLinkUp() const
{
    NS_LOG_FUNCTION(this);
    return m_linkUp;
}

void
SimpleNetDevice::AddLinkChangeCallback(Callback<void> callback)
{
    NS_LOG_FUNCTION(this << &callback);
    m_linkChangeCallbacks.ConnectWithoutContext(callback);
}

bool
SimpleNetDevice::IsBroadcast() const
{
    NS_LOG_FUNCTION(this);
    return !m_pointToPointMode;
}

Address
SimpleNetDevice::GetBroadcast() const
{
    NS_LOG_FUNCTION(this);
    return Mac48Address::GetBroadcast();
}

bool
SimpleNetDevice::IsMulticast() const
{
    NS_LOG_FUNCTION(this);
    return !m_pointToPointMode;
}

Address
SimpleNetDevice::GetMulticast(Ipv4Address multicastGroup) const
{
    NS_LOG_FUNCTION(this << multicastGroup);
    return Mac48Address::GetMulticast(multicastGroup);
}

Address
SimpleNetDevice::GetMulticast(Ipv6Address addr) const
{
    NS_LOG_FUNCTION(this << addr);
    return Mac48Address::GetMulticast(addr);
}

bool
SimpleNetDevice::IsPointToPoint() const
{
    NS_LOG_FUNCTION(this);
    return m_pointToPointMode;
}

bool NetDevice::IsQbb(void) const {
    return false;
}

bool
SimpleNetDevice::IsBridge() const
{
    NS_LOG_FUNCTION(this);
    return false;
}

bool
SimpleNetDevice::Send(Ptr<Packet> packet, const Address& dest, uint16_t protocolNumber)
{
    NS_LOG_FUNCTION(this << packet << dest << protocolNumber);

    return SendFrom(packet, m_address, dest, protocolNumber);
}

bool
SimpleNetDevice::SendFrom(Ptr<Packet> p,
                          const Address& source,
                          const Address& dest,
                          uint16_t protocolNumber)
{
    NS_LOG_FUNCTION(this << p << source << dest << protocolNumber);
    if (p->GetSize() > GetMtu())
    {
        return false;
    }

    Mac48Address to = Mac48Address::ConvertFrom(dest);
    Mac48Address from = Mac48Address::ConvertFrom(source);

    SimpleTag tag;
    tag.SetSrc(from);
    tag.SetDst(to);
    tag.SetProto(protocolNumber);

    p->AddPacketTag(tag);

    if (m_queue->Enqueue(p))
    {
        if (m_queue->GetNPackets() == 1 && !FinishTransmissionEvent.IsPending())
        {
            StartTransmission();
        }
        return true;
    }

    return false;
}

void
SimpleNetDevice::StartTransmission()
{
    if (m_queue->GetNPackets() == 0)
    {
        return;
    }
    NS_ASSERT_MSG(!FinishTransmissionEvent.IsPending(),
                  "Tried to transmit a packet while another transmission was in progress");
    Ptr<Packet> packet = m_queue->Dequeue();

    /**
     * SimpleChannel will deliver the packet to the far end(s) of the link as soon as Send is called
     * (or after its fixed delay, if one is configured). So we have to handle the rate of the link
     * here, which we do by scheduling FinishTransmission (packetSize / linkRate) time in the
     * future. While that event is running, the transmit path of this NetDevice is busy, so we can't
     * send other packets.
     *
     * SimpleChannel doesn't have a locking mechanism, and doesn't check for collisions, so there's
     * nothing we need to do with the channel until the transmission has "completed" from the
     * perspective of this NetDevice.
     */
    Time txTime = Time(0);
    if (m_bps > DataRate(0))
    {
        txTime = m_bps.CalculateBytesTxTime(packet->GetSize());
    }
    FinishTransmissionEvent =
        Simulator::Schedule(txTime, &SimpleNetDevice::FinishTransmission, this, packet);
}

void
SimpleNetDevice::FinishTransmission(Ptr<Packet> packet)
{
    NS_LOG_FUNCTION(this);

    SimpleTag tag;
    packet->RemovePacketTag(tag);

    Mac48Address src = tag.GetSrc();
    Mac48Address dst = tag.GetDst();
    uint16_t proto = tag.GetProto();

    m_channel->Send(packet, proto, dst, src, this);

    StartTransmission();
}

Ptr<Node>
SimpleNetDevice::GetNode() const
{
    NS_LOG_FUNCTION(this);
    return m_node;
}

void
SimpleNetDevice::SetNode(Ptr<Node> node)
{
    NS_LOG_FUNCTION(this << node);
    m_node = node;
}

bool
SimpleNetDevice::NeedsArp() const
{
    NS_LOG_FUNCTION(this);
    return !m_pointToPointMode;
}

void
SimpleNetDevice::SetReceiveCallback(NetDevice::ReceiveCallback cb)
{
    NS_LOG_FUNCTION(this << &cb);
    m_rxCallback = cb;
}

void
SimpleNetDevice::DoDispose()
{
    NS_LOG_FUNCTION(this);
    m_channel = nullptr;
    m_node = nullptr;
    m_receiveErrorModel = nullptr;
    m_queue->Dispose();
    if (FinishTransmissionEvent.IsPending())
    {
        FinishTransmissionEvent.Cancel();
    }
    NetDevice::DoDispose();
}

void
SimpleNetDevice::SetPromiscReceiveCallback(PromiscReceiveCallback cb)
{
    NS_LOG_FUNCTION(this << &cb);
    m_promiscCallback = cb;
}

bool
SimpleNetDevice::SupportsSendFrom() const
{
    NS_LOG_FUNCTION(this);
    return true;
}

} // namespace ns3
