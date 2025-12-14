/*
 * LoRa-only SDcloud-style simulation with energy and packet metrics.
 *
 * Outputs (under results/<experimentName>/run_<runSeed>_<timestamp>/):
 *  - metadata.json : simulation parameters
 *  - metrics.json  : packets sent/received, loss, and energy usage
 *  - energy.csv    : per-node remaining energy over time
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/mobility-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/energy-module.h"

#include "ns3/basic-energy-source.h"
#include "ns3/basic-energy-source-helper.h"
#include "ns3/lora-radio-energy-model.h"
#include "ns3/lora-radio-energy-model-helper.h"

#include "ns3/constant-position-mobility-model.h"
#include "ns3/correlated-shadowing-propagation-loss-model.h"
#include "ns3/forest-penetration-loss.h"

#include "ns3/end-device-lora-phy.h"
#include "ns3/end-device-lorawan-mac.h"
#include "ns3/forwarder-helper.h"
#include "ns3/gateway-lora-phy.h"
#include "ns3/gateway-lorawan-mac.h"
#include "ns3/lora-device-address.h"
#include "ns3/lora-frame-header.h"
#include "ns3/lora-helper.h"
#include "ns3/lora-net-device.h"
#include "ns3/lora-phy.h"
#include "ns3/lorawan-mac-header.h"
#include "ns3/network-server-helper.h"
#include "ns3/periodic-sender-helper.h"
#include "ns3/position-allocator.h"
#include "ns3/random-variable-stream.h"

#include "ns3/log.h"
#include "ns3/simulator.h"

#include "ns3/sender-id-tag.h"
#include "ns3/forest-penetration-loss.h"

#include <ctime>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

using namespace ns3;
using namespace lorawan;
using namespace energy;

NS_LOG_COMPONENT_DEFINE("SdcloudLoraSim");

// ---------------- Global metrics ----------------

static uint64_t g_packetsSent = 0;
static uint64_t g_packetsReceived = 0;
static std::ofstream g_energyCsv;
static std::unordered_map<uint32_t, uint64_t> nodePacketsSent;
static std::unordered_map<uint32_t, uint64_t> nodePacketsReceived;
static std::unordered_map<uint32_t, std::vector<double>> nodeLatencies;

// ---------------- Traces ----------------

static void OnTransmissionCallback(Ptr<const Packet> packet, uint32_t senderNodeId)
{
    NS_LOG_FUNCTION(packet << senderNodeId);
    ++g_packetsSent;
    ++nodePacketsSent[senderNodeId];
}

static void OnPacketReceptionCallback(Ptr<const Packet> packet, uint32_t receiverNodeId)
{
    NS_LOG_FUNCTION(packet << receiverNodeId);
    ++g_packetsReceived;
    SenderIdTag tag;
    if (packet->PeekPacketTag(tag))
    {
        // std::cout << Simulator::Now().GetSeconds() << " " << tag.GetSendTime() << std::endl;
        uint32_t senderNode = tag.GetSenderId();
        ++nodePacketsReceived[senderNode];
        nodeLatencies[senderNode].push_back(Simulator::Now().GetSeconds() - tag.GetSendTime());
    }
}

static void RemainingEnergyTrace(uint32_t nodeId, double oldValue, double newValue)
{
    // g_energyCsv << Simulator::Now().GetSeconds() << ","
    //             << nodeId << ","
    //             << newValue << std::endl;
}

// ---------------- Energy model setup ----------------

DeviceEnergyModelContainer SetupLoraEnergyModel(NodeContainer& nodes, NetDeviceContainer& devices, const std::string& filename)
{
    g_energyCsv.open(filename.c_str());
    g_energyCsv << "time,node,remaining_energy_joules\n";

    BasicEnergySourceHelper sourceHelper;
    sourceHelper.Set("BasicEnergySourceInitialEnergyJ", DoubleValue(300.0));
    EnergySourceContainer sources = sourceHelper.Install(nodes);

    LoraRadioEnergyModelHelper loraEnergy;
    loraEnergy.Set("StandbyCurrentA", DoubleValue(0.0704));
    loraEnergy.Set("TxCurrentA", DoubleValue(0.0868+0.125));
    // loraEnergy.Set("SleepCurrentA", DoubleValue(0.0704));
    loraEnergy.Set("SleepCurrentA", DoubleValue(0.0000010));
    loraEnergy.Set("RxCurrentA", DoubleValue(0.0868+0.0076));

    DeviceEnergyModelContainer models = loraEnergy.Install(devices, sources);

    // Trace remaining energy over time
    for (uint32_t i = 0; i < sources.GetN(); ++i)
    {
        Ptr<EnergySource> src = sources.Get(i);
        src->TraceConnectWithoutContext(
            "RemainingEnergy",
            MakeBoundCallback(&RemainingEnergyTrace, i));
    }

    return models;
}


int main(int argc, char* argv[])
{
    // Parameters (CLI overridable)
    uint32_t nDevices        = 64;      // number of end devices
    uint32_t nGateways       = 1;       // number of gateways
    double   distance        = 1000.0;  // deployment radius
    double   simTimeSec      = 3000.0;  // simulation time (s)
    double   intervalSec     = 500.0;   // app send interval (s)
    uint32_t payloadBytes    = 32;      // LoRa payload size (bytes)
    std::string     environment = "field";  // correlated shadowing + buildings
    std::string experimentName = "lora_default";
    uint32_t runSeed         = 1;

    CommandLine cmd(__FILE__);
    cmd.AddValue("nDevices", "Number of end devices", nDevices);
    cmd.AddValue("distance", "Size of grid", distance);
    cmd.AddValue("simTimeSec", "Simulation time (s)", simTimeSec);
    cmd.AddValue("intervalSec", "LoRa application interval (s)", intervalSec);
    cmd.AddValue("payloadBytes", "LoRa payload size (bytes)", payloadBytes);
    cmd.AddValue("environment",
                 "Environment: field | forest ",
                 environment);
    cmd.AddValue("experimentName", "Experiment folder name", experimentName);
    cmd.AddValue("runSeed", "Run number / RNG seed", runSeed);
    cmd.Parse(argc, argv);

    RngSeedManager::SetSeed(1);
    RngSeedManager::SetRun(runSeed);

    // Output directory
    std::string timestamp = std::to_string(time(nullptr));
    std::string outDir =
        "results/" + experimentName +
        "/run_" + std::to_string(runSeed) + "_" + timestamp + "/";

    system(("mkdir -p " + outDir).c_str());

    std::string energyFile  = outDir + "energy.csv";
    std::string metaFile    = outDir + "metadata.json";
    std::string metricsFile = outDir + "metrics.json";

    for (uint32_t i = 0; i < nDevices; ++i)
    {
        nodePacketsSent[i] = 0;
        nodePacketsReceived[i] = 0;
    }

    /***********
     *  Setup  *
     ***********/

    MobilityHelper mobility;
    uint32_t k = static_cast<uint32_t>(std::sqrt(nDevices));
    double delta = (k > 1) ? distance / (k - 1) : 0.0;

    mobility.SetPositionAllocator(
        "ns3::GridPositionAllocator",
        "MinX", DoubleValue(0.0),
        "MinY", DoubleValue(0.0),
        "DeltaX", DoubleValue(delta),
        "DeltaY", DoubleValue(delta),
        "GridWidth", UintegerValue(k),
        "LayoutType", StringValue("RowFirst")
    );
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");

    /************************
     *  Create end devices  *
     ************************/

    NodeContainer endDevices;
    endDevices.Create(nDevices);
    mobility.Install(endDevices);

    // Lift devices to some height > 0
    for (auto j = endDevices.Begin(); j != endDevices.End(); ++j)
    {
        Ptr<MobilityModel> mob = (*j)->GetObject<MobilityModel>();
        Vector pos = mob->GetPosition();
        pos.z = 1.5;
        mob->SetPosition(pos);
        // auto raisedPos = mob->GetPosition();
        // std::cout << "Sensor at " << raisedPos.x << "," << raisedPos.y << "," << raisedPos.z << "\n";
    }

    /*********************
     *  Create gateways  *
     *********************/

    NodeContainer gateways;
    gateways.Create(nGateways);

    Ptr<ListPositionAllocator> allocator = CreateObject<ListPositionAllocator>();
    // Place gateways at center at some height
    for (uint32_t i = 0; i < nGateways; ++i)
    {
        allocator->Add(Vector(distance / 2, distance / 2, 2.0));
    }

    mobility.SetPositionAllocator(allocator);
    mobility.Install(gateways);

    /************************
     *  Create the channel  *
     ************************/

    Ptr<LogDistancePropagationLossModel> loss = CreateObject<LogDistancePropagationLossModel>();
    loss->SetPathLossExponent(3.76);
    loss->SetReference(1, 7.7);

    if (environment == "forest")
    {
        Ptr<CorrelatedShadowingPropagationLossModel> shadowing =
            CreateObject<CorrelatedShadowingPropagationLossModel>();
        loss->SetNext(shadowing);

        Ptr<ForestPenetrationLoss> forestLoss = CreateObject<ForestPenetrationLoss>();
        shadowing->SetNext(forestLoss);
    }

    Ptr<PropagationDelayModel> delay = CreateObject<ConstantSpeedPropagationDelayModel>();
    Ptr<LoraChannel> channel = CreateObject<LoraChannel>(loss, delay);

    /************************
     *  Create the helpers  *
     ************************/

    LoraPhyHelper phyHelper = LoraPhyHelper();
    phyHelper.SetChannel(channel);

    LorawanMacHelper macHelper = LorawanMacHelper();
    macHelper.SetRegion(LorawanMacHelper::ALOHA);

    LoraHelper helper = LoraHelper();
    helper.EnablePacketTracking(); // enable built-in packet tracking (optional)

    NetworkServerHelper nsHelper = NetworkServerHelper();
    ForwarderHelper forHelper    = ForwarderHelper();

    /******************************
     *  Install LoRa on devices   *
     ******************************/

    // Address generator
    uint8_t  nwkId   = 54;
    uint32_t nwkAddr = 1864;
    Ptr<LoraDeviceAddressGenerator> addrGen =
        CreateObject<LoraDeviceAddressGenerator>(nwkId, nwkAddr);

    macHelper.SetAddressGenerator(addrGen);

    // End devices
    phyHelper.SetDeviceType(LoraPhyHelper::ED);
    macHelper.SetDeviceType(LorawanMacHelper::ED_A);
    NetDeviceContainer endDeviceDevs = helper.Install(phyHelper, macHelper, endDevices);

    // Gateways
    phyHelper.SetDeviceType(LoraPhyHelper::GW);
    macHelper.SetDeviceType(LorawanMacHelper::GW);
    NetDeviceContainer gatewayDevs = helper.Install(phyHelper, macHelper, gateways);

    NS_LOG_INFO("Completed LoRa configuration");

    /************************************
     *  Energy model for end devices    *
     ************************************/

    DeviceEnergyModelContainer energyModels = SetupLoraEnergyModel(endDevices, endDeviceDevs, energyFile);

    /*********************************************
     *  Install applications on the end devices  *
     *********************************************/

    PeriodicSenderHelper appHelper;
    appHelper.SetPeriod(Seconds(intervalSec));
    appHelper.SetPacketSize(payloadBytes);

    // Ptr<UniformRandomVariable> jitter = CreateObject<UniformRandomVariable>();
    // jitter->SetAttribute("Min", DoubleValue(0.0));
    // jitter->SetAttribute("Max", DoubleValue(0.0));

    for (uint32_t i = 0; i < nDevices; ++i)
    {
        ApplicationContainer app = appHelper.Install(endDevices.Get(i));
        double start = 2 + i * 0.25;
        app.Start(Seconds(start));
        app.Stop(Seconds(simTimeSec * 0.9));
    }

    /**************************
     *  Create network server *
     **************************/

    Ptr<Node> networkServer = CreateObject<Node>();

    Ptr<ListPositionAllocator> serverAllocator = CreateObject<ListPositionAllocator>();
    // Place network server in center on top of gateway at some height
    for (uint32_t i = 0; i < nGateways; ++i)
    {
        serverAllocator->Add(Vector(distance / 2, distance / 2, 2.0));
    }

    mobility.SetPositionAllocator(serverAllocator);
    mobility.Install(networkServer);

    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", StringValue("100Mbps"));
    p2p.SetChannelAttribute("Delay", StringValue("2ms"));

    P2PGwRegistration_t gwRegistration;
    for (auto gw = gateways.Begin(); gw != gateways.End(); ++gw)
    {
        NetDeviceContainer container = p2p.Install(networkServer, *gw);
        Ptr<PointToPointNetDevice> serverP2PNetDev =
            DynamicCast<PointToPointNetDevice>(container.Get(0));
        gwRegistration.emplace_back(serverP2PNetDev, *gw);
    }

    nsHelper.SetGatewaysP2P(gwRegistration);
    nsHelper.SetEndDevices(endDevices);
    nsHelper.Install(networkServer);

    forHelper.Install(gateways);

    /***********************
     *  Install trace hooks *
     ***********************/

    // Gateway reception trace
    for (auto node = gateways.Begin(); node != gateways.End(); ++node)
    {
        Ptr<LoraNetDevice> dev = DynamicCast<LoraNetDevice>((*node)->GetDevice(0));
        dev->GetPhy()->TraceConnectWithoutContext(
            "ReceivedPacket",
            MakeCallback(&OnPacketReceptionCallback));
    }

    // End-device transmission trace
    for (auto node = endDevices.Begin(); node != endDevices.End(); ++node)
    {
        Ptr<LoraNetDevice> dev = DynamicCast<LoraNetDevice>((*node)->GetDevice(0));
        dev->GetPhy()->TraceConnectWithoutContext(
            "StartSending",
            MakeCallback(&OnTransmissionCallback));
    }

    // Set spreading factors adaptively based on distance
    std::vector<int> spreadingFactors = LorawanMacHelper::SetSpreadingFactorsUp(endDevices, gateways, channel);
    for (int sf : spreadingFactors)
    {
        std::cout << "SF: " << sf << std::endl;
    }

    /****************
     *  Simulation  *
     ****************/

    Simulator::Stop(Seconds(simTimeSec));
    NS_LOG_INFO("Running simulation...");
    Simulator::Run();

    // -------------- Metrics computation --------------

    // Energy: sum over all device energy models
    double totalEnergyConsumedJ = 0.0;
    uint32_t nodeId = 0;
    for (DeviceEnergyModelContainer::Iterator it = energyModels.Begin(); it != energyModels.End(); ++it, ++nodeId)
    {
        double energyConsumed = (*it)->GetTotalEnergyConsumption();
        totalEnergyConsumedJ += energyConsumed;
        g_energyCsv << Simulator::Now().GetSeconds() << ","
                << nodeId << ","
                << 300.0 - energyConsumed << std::endl;
    }

    if (g_energyCsv.is_open())
    {
        g_energyCsv.close();
    }

    uint64_t packetLoss = 0;
    if (g_packetsSent >= g_packetsReceived)
    {
        packetLoss = g_packetsSent - g_packetsReceived;
    }
    double pdr = (g_packetsSent > 0)
                     ? static_cast<double>(g_packetsReceived) /
                           static_cast<double>(g_packetsSent)
                     : 0.0;

    // -------------- Write metadata --------------

    {
        std::ofstream meta(metaFile.c_str());
        meta << "{\n";
        meta << "  \"experimentName\": \"" << experimentName << "\",\n";
        meta << "  \"technology\": \"lora\",\n";
        meta << "  \"nDevices\": " << nDevices << ",\n";
        meta << "  \"distance\": " << distance << ",\n";
        meta << "  \"simTimeSec\": " << simTimeSec << ",\n";
        meta << "  \"intervalSec\": " << intervalSec << ",\n";
        meta << "  \"payloadBytes\": " << payloadBytes << ",\n";
        meta << "  \"environment\": \"" << environment << "\",\n";
        meta << "  \"seed\": " << runSeed << "\n";
        meta << "}\n";
    }

    // -------------- Write metrics --------------

    {
        std::ofstream metrics(metricsFile.c_str());
        metrics << "{\n";
        metrics << "  \"packetsSent\": " << g_packetsSent << ",\n";
        metrics << "  \"packetsReceived\": " << g_packetsReceived << ",\n";
        metrics << "  \"bytesPerPacket\": " << payloadBytes << ",\n";
        metrics << "  \"packetLoss\": " << packetLoss << ",\n";
        metrics << "  \"packetDeliveryRatio\": " << pdr << ",\n";
        metrics << "  \"totalEnergyConsumedJ\": " << totalEnergyConsumedJ << ",\n";
        metrics << "  \"avgEnergyConsumedPerNodeJ\": "
                << (nDevices > 0
                        ? (totalEnergyConsumedJ / static_cast<double>(nDevices))
                        : 0.0)
                << ",\n";

        // ----------------------------------------------------------------------
        // Serialize per-node packets SENT
        // ----------------------------------------------------------------------
        metrics << "  \"nodePacketsSent\": {\n";
        {
            size_t idx = 0;
            const size_t last = nodePacketsSent.size();

            for (const auto& entry : nodePacketsSent)
            {
                int nodeId = entry.first;
                int count = entry.second;

                metrics << "    \"" << nodeId << "\": " << count;
                if (++idx < last) metrics << ",";
                metrics << "\n";
            }
        }
        metrics << "  },\n";

        // ----------------------------------------------------------------------
        // Serialize per-node packets RECEIVED
        // ----------------------------------------------------------------------
        metrics << "  \"nodePacketsReceived\": {\n";
        {
            size_t idx = 0;
            const size_t last = nodePacketsReceived.size();

            for (const auto& entry : nodePacketsReceived)
            {
                int nodeId = entry.first;
                int count = entry.second;

                metrics << "    \"" << nodeId << "\": " << count;
                if (++idx < last) metrics << ",";
                metrics << "\n";
            }
        }
        metrics << "  },\n";

        // ----------------------------------------------------------------------
        // Serialize per-node average latency
        // ----------------------------------------------------------------------
        metrics << "  \"nodeAverageLatency\": {\n";
        {
            size_t idx = 0;
            const size_t last = nodeLatencies.size();

            for (const auto& entry : nodeLatencies)
            {
                int nodeId = entry.first;
                std::vector<double> latencies = entry.second;

                // for (double element : latencies) {
                //     std::cout << element << " ";
                // }
                // std::cout << std::endl;

                double sum = std::accumulate(latencies.begin(), latencies.end(), 0.0);
                double averageLatency = sum / latencies.size();

                metrics << "    \"" << nodeId << "\": " << averageLatency;
                if (++idx < last) metrics << ",";
                metrics << "\n";
            }
        }
        metrics << "  }\n";

        metrics << "}\n";   // end object
    }

    Simulator::Destroy();

    std::cout << "Simulation complete.\n";
    std::cout << "Packets sent: " << g_packetsSent
              << ", received: " << g_packetsReceived
              << ", lost: " << packetLoss
              << ", PDR (Packet Delivery Ratio): " << pdr << std::endl;
    std::cout << "Total energy consumed (J): " << totalEnergyConsumedJ << std::endl;

    return 0;
}
