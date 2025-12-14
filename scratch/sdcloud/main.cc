#include "ns3/core-module.h"
#include "ns3/energy-module.h"
#include "ns3/basic-energy-source.h"
#include "ns3/basic-energy-source-helper.h"
#include "ns3/wifi-radio-energy-model-helper.h"
#include "ns3/network-module.h"
#include "ns3/mobility-module.h"
#include "ns3/wifi-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/lora-radio-energy-model.h"
#include "ns3/lora-radio-energy-model-helper.h"
#include "ns3/end-device-lora-phy.h"
#include "ns3/end-device-lorawan-mac.h"
#include "ns3/gateway-lora-phy.h"
#include "ns3/gateway-lorawan-mac.h"
#include "ns3/lora-helper.h"
#include "ns3/periodic-sender-helper.h"
#include "ns3/mesh-helper.h"
#include "ns3/mesh-module.h"


using namespace ns3;
using namespace energy;
using namespace lorawan;

static std::ofstream g_energyCsv;

// static void RemainingEnergyTrace(uint32_t nodeId, double oldValue, double newValue)
// {
    // nodeEnergyRemaining[nodeId] = newValue;
    // g_energyCsv << Simulator::Now().GetSeconds() << ","
    //             << nodeId << ","
    //             << newValue << std::endl;
// }

DeviceEnergyModelContainer SetupEnergyModel(
    NodeContainer& nodes, 
    NetDeviceContainer& devices, 
    const std::string& filename, 
    const std::string& technology, 
    const std::string& topology)
{
    g_energyCsv.open(filename);
    g_energyCsv << "time,node,remaining_energy_joules\n";

    BasicEnergySourceHelper sourceHelper;
    sourceHelper.Set("BasicEnergySourceInitialEnergyJ", DoubleValue(300.0));

    EnergySourceContainer sources = sourceHelper.Install(nodes);

    DeviceEnergyModelContainer models;

    if (technology == "wifi")
    {
        if (topology == "mesh")
        {
            NetDeviceContainer wifiIfaces;

            for (uint32_t i = 0; i < devices.GetN(); i++) {
                Ptr<MeshPointDevice> mp = DynamicCast<MeshPointDevice>(devices.Get(i));
                for (uint32_t j = 0; j < mp->GetNInterfaces(); j++) {
                    // wifiIfaces.Add(mp->GetInterface(j));  // WifiNetDevice
                    for (auto interface : mp->GetInterfaces())
                    {
                        // std::cout << interface->GetInstanceTypeId().GetName() << std::endl;
                        wifiIfaces.Add(interface);
                    }
                }
            }
            WifiRadioEnergyModelHelper wifiEnergy;
            wifiEnergy.Set("IdleCurrentA", DoubleValue(0.0704));      // The default radio Idle current in Ampere.
            wifiEnergy.Set("CcaBusyCurrentA", DoubleValue(0.0868));   // The default radio CCA Busy State current in Ampere.
            wifiEnergy.Set("TxCurrentA", DoubleValue(0.381));   // The radio Tx current in Ampere.
            wifiEnergy.Set("RxCurrentA", DoubleValue(0.130));   // The radio Rx current in Ampere.
            wifiEnergy.Set("SwitchingCurrentA", DoubleValue(0.0868)); // The default radio Channel Switch current in Ampere.
            wifiEnergy.Set("SleepCurrentA", DoubleValue(0.0000010));     // The radio Sleep current in Ampere.
            models = wifiEnergy.Install(wifiIfaces, sources);
        }
        else
        {
            WifiRadioEnergyModelHelper wifiEnergy;
            wifiEnergy.Set("IdleCurrentA", DoubleValue(0.0704));      // The default radio Idle current in Ampere.
            wifiEnergy.Set("CcaBusyCurrentA", DoubleValue(0.0868));   // The default radio CCA Busy State current in Ampere.
            wifiEnergy.Set("TxCurrentA", DoubleValue(0.246));   // The radio Tx current in Ampere.
            wifiEnergy.Set("RxCurrentA", DoubleValue(0.100));   // The radio Rx current in Ampere.
            wifiEnergy.Set("SwitchingCurrentA", DoubleValue(0.0868)); // The default radio Channel Switch current in Ampere.
            wifiEnergy.Set("SleepCurrentA", DoubleValue(0.0000010));     // The radio Sleep current in Ampere.
            models = wifiEnergy.Install(devices, sources);
        }
    }
    else
    {
        NS_FATAL_ERROR("Unsupported technology: " << technology);
    }

    // for (uint32_t i = 0; i < sources.GetN(); i++)
    // {
    //     Ptr<EnergySource> src = sources.Get(i);
    //     src->TraceConnectWithoutContext(
    //         "RemainingEnergy",
    //         MakeBoundCallback(&RemainingEnergyTrace, i)
    //     );
    // }

    return models;
}

YansWifiChannelHelper BuildEnvironmentChannel(const std::string& env)
{
    YansWifiChannelHelper chan;
    chan.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");

    if (env == "field")
    {
        chan.AddPropagationLoss("ns3::LogDistancePropagationLossModel",
                                "Exponent", DoubleValue(2.0));
        chan.AddPropagationLoss("ns3::NakagamiPropagationLossModel",
                                "m0", DoubleValue(2.5),
                                "m1", DoubleValue(2.5),
                                "m2", DoubleValue(2.5));
    }
    else if (env == "forest")
    {
        chan.AddPropagationLoss("ns3::LogDistancePropagationLossModel",
                                "Exponent", DoubleValue(3.5));
        chan.AddPropagationLoss("ns3::NakagamiPropagationLossModel",
                                "m0", DoubleValue(1.0),
                                "m1", DoubleValue(1.0),
                                "m2", DoubleValue(1.0));
    }
    else if (env == "mountain")
    {
        chan.AddPropagationLoss("ns3::TwoRayGroundPropagationLossModel");
        chan.AddPropagationLoss("ns3::NakagamiPropagationLossModel",
                                "m0", DoubleValue(1.5),
                                "m1", DoubleValue(1.5),
                                "m2", DoubleValue(1.5));
    }
    else
    {
        NS_FATAL_ERROR("Unknown environment " << env);
    }

    return chan;
}

std::tuple<NetDeviceContainer, NetDeviceContainer, Ipv4InterfaceContainer, Ipv4InterfaceContainer> 
InstallWifi(
    NodeContainer& sensors, 
    NodeContainer& gateway, 
    const std::string& environment, 
    double txPowerDbm)
{
    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211ax); // esp32-c5 supports
    // wifi.SetRemoteStationManager("ns3::IdealWifiManager");  // represents an optimistic upper bound assuming ideal rate adaptation

    // Because ESP32-C5 rate adaptation is firmware-defined and undocumented, we model Wi-Fi using fixed HE MCS values via ConstantRateWifiManager
    // This captures embedded-device behavior more faithfully than Linux-derived rate adaptation algorithms
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                 "DataMode",
                                 StringValue("HeMcs0"),
                                 "ControlMode",
                                 StringValue("HeMcs0"));

    // Build environment-specific propagation models
    YansWifiChannelHelper chan = BuildEnvironmentChannel(environment);

    YansWifiPhyHelper phy = YansWifiPhyHelper();
    phy.SetChannel(chan.Create());
    phy.Set("TxPowerStart", DoubleValue(txPowerDbm));
    phy.Set("TxPowerEnd", DoubleValue(txPowerDbm));
    phy.Set("ChannelSettings", StringValue("{0, 0, BAND_2_4GHZ, 0}"));

    Ssid ssid = Ssid("sdcloud-wifi");
    WifiMacHelper mac;

    mac.SetType(
        "ns3::StaWifiMac",
        "Ssid", SsidValue(ssid),
        "ActiveProbing", BooleanValue(false)
    );
    NetDeviceContainer staDevs = wifi.Install(phy, mac, sensors);

    mac.SetType(
        "ns3::ApWifiMac",
        "Ssid", SsidValue(ssid)
    );
    NetDeviceContainer apDev = wifi.Install(phy, mac, gateway);

    Ipv4AddressHelper wifiAddr;
    wifiAddr.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer staIf = wifiAddr.Assign(staDevs);
    Ipv4InterfaceContainer apIf  = wifiAddr.Assign(apDev);

    return std::make_tuple(staDevs, apDev, staIf, apIf);
}

std::tuple<NetDeviceContainer, NetDeviceContainer, Ipv4InterfaceContainer, Ipv4InterfaceContainer> 
InstallMeshWifi(
    NodeContainer& sensors, 
    NodeContainer& gateway, 
    const std::string& environment, 
    double txPowerDbm)
{
    NodeContainer meshNodes;
    meshNodes.Add(sensors);
    meshNodes.Add(gateway);
    // Build environment-specific propagation models
    YansWifiChannelHelper chan = BuildEnvironmentChannel(environment);

    YansWifiPhyHelper phy = YansWifiPhyHelper();
    phy.SetChannel(chan.Create());
    phy.Set("TxPowerStart", DoubleValue(txPowerDbm));
    phy.Set("TxPowerEnd", DoubleValue(txPowerDbm));

    /*
     * Create mesh helper and set stack installer to it
     * Stack installer creates all needed protocols and install them to
     * mesh point device
     */
    MeshHelper mesh = MeshHelper::Default();
    mesh.SetStandard(WIFI_STANDARD_80211a);
    mesh.SetStackInstaller("ns3::Dot11sStack");
    mesh.SetSpreadInterfaceChannels(MeshHelper::ZERO_CHANNEL);
    mesh.SetMacType("RandomStart", TimeValue(Seconds(0.5)));
    mesh.SetNumberOfInterfaces(1);

    NetDeviceContainer meshDevices = mesh.Install(phy, meshNodes);
    mesh.AssignStreams(meshDevices, 0);

    NetDeviceContainer staDevs;
    for (uint32_t i = 0; i < meshDevices.GetN() - 1; ++i)
    {
        staDevs.Add(meshDevices.Get(i));
    }

    NetDeviceContainer apDev;
    apDev.Add(meshDevices.Get(meshDevices.GetN() - 1));

    Ipv4AddressHelper wifiAddr;
    wifiAddr.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer staIf = wifiAddr.Assign(staDevs);
    Ipv4InterfaceContainer apIf = wifiAddr.Assign(apDev);

    return std::make_tuple(staDevs, apDev, staIf, apIf);
}

std::tuple<NetDeviceContainer, Ipv4InterfaceContainer> InstallP2P(NodeContainer& gateway, NodeContainer& cloud)
{
    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", StringValue("100Mbps"));
    p2p.SetChannelAttribute("Delay", StringValue("2ms"));
    NetDeviceContainer p2pDevs = p2p.Install(gateway.Get(0), cloud.Get(0));

    Ipv4AddressHelper p2pAddr;
    p2pAddr.SetBase("10.2.1.0", "255.255.255.0");
    Ipv4InterfaceContainer p2pIf = p2pAddr.Assign(p2pDevs);

    return std::make_tuple(p2pDevs, p2pIf);
}


int main(int argc, char *argv[])
{
    // ---------------- Parameters (overridable by CLI) ----------------
    uint32_t nDevices     = 16;
    double   distance  = 30.0;
    uint32_t payloadBytes = 128;
    double   intervalSec  = 30.0;
    double   simTimeSec   = 300.0;
    uint16_t serverPort   = 9;
    double   txPowerDbm   = 15.0;
    bool     verbose      = false;

    std::string experimentName = "default";
    std::string environment = "field"; // field | forest | mountain
    std::string technology = "wifi";  // currently only wifi supported | ble | lora
    std::string topology = "star";  // star | mesh
    uint32_t runSeed = 1;

    CommandLine cmd;
    // USE SAME NUMBER OF DEVICES ON ALL SIMULATIONS
    cmd.AddValue("nDevices", "Number of SDcloud sensor nodes", nDevices);
    cmd.AddValue("distance", "Size of grid", distance);
    cmd.AddValue("payloadBytes", "UDP payload size (bytes)", payloadBytes);
    cmd.AddValue("intervalSec", "Send interval (seconds)", intervalSec);
    cmd.AddValue("simTimeSec", "Simulation time (seconds)", simTimeSec);
    cmd.AddValue("serverPort", "UDP server port on cloud", serverPort);
    cmd.AddValue("txPowerDbm", "Wi-Fi TX power (dBm)", txPowerDbm);
    cmd.AddValue("verbose", "Enable UdpClient/Server INFO logs", verbose);

    cmd.AddValue("experimentName", "Experiment folder name", experimentName);
    cmd.AddValue("environment", "Environment: field | forest | mountain", environment);
    cmd.AddValue("technology", "Technology: wifi (BLE/LoRa future)", technology);
    cmd.AddValue("topology", "Topology: star | mesh", topology);
    cmd.AddValue("runSeed", "Run number / RNG seed", runSeed);

    cmd.Parse(argc, argv);

    if (technology != "wifi")
    {
        NS_FATAL_ERROR("Only wifi is implemented at the moment.");
    }

    if (topology == "mesh")
    {
        txPowerDbm = 17.0;
    }

    long root = static_cast<long>(sqrt(nDevices));
    if (root * root != nDevices)
    {
        NS_FATAL_ERROR("Number of devices must be perfect square.");
    }

    RngSeedManager::SetSeed(1);
    RngSeedManager::SetRun(runSeed);

    std::string timestamp = std::to_string(time(NULL));

    std::string outDir = 
        "results/" + experimentName + 
        "/run_" + std::to_string(runSeed) + "_" + timestamp + "/";

    system(("mkdir -p " + outDir).c_str());

    std::string flowmonFile = outDir + "flowmon.xml";
    std::string energyFile = outDir + "energy.csv";
    std::string metaFile = outDir + "metadata.json";


    if (verbose)
    {
        LogComponentEnable("UdpClient", LOG_LEVEL_INFO);
        LogComponentEnable("UdpServer", LOG_LEVEL_INFO);
    }

    // ---------------- Nodes ----------------
    NodeContainer sensors;
    sensors.Create(nDevices);

    NodeContainer gateway;
    gateway.Create(1);

    NodeContainer cloud;
    cloud.Create(1);

    // ---------------- Mobility ----------------
    MobilityHelper mobility;

    // spacing so that the outermost nodes lie exactly on distance × distance
    // assumes perfect square number of nodes
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
    mobility.Install(sensors);

    for (uint32_t i = 0; i < sensors.GetN(); i++)
    {
        Ptr<MobilityModel> mm = sensors.Get(i)->GetObject<MobilityModel>();
        auto pos = mm->GetPosition();
        sensors.Get(i)->GetObject<MobilityModel>()->SetPosition(Vector(pos.x, pos.y, 1.5));
        // auto raisedPos = sensors.Get(i)->GetObject<MobilityModel>()->GetPosition();
        // std::cout << "Sensor " << i << " at " << raisedPos.x << "," << raisedPos.y << "," << raisedPos.z << "\n";
    }

    mobility.Install(gateway);
    gateway.Get(0)->GetObject<MobilityModel>()->SetPosition(Vector(distance / 2.0, distance / 2.0, 2.0));

    // auto gatewayPos = gateway.Get(0)->GetObject<MobilityModel>()->GetPosition();
    // std::cout << "Gateway at " << gatewayPos.x << "," << gatewayPos.y << "," << gatewayPos.z << "\n";

    mobility.Install(cloud);
    cloud.Get(0)->GetObject<MobilityModel>()->SetPosition(Vector(distance / 2.0, distance / 2.0, 2.0));

    // auto cloudPos = cloud.Get(0)->GetObject<MobilityModel>()->GetPosition();
    // std::cout << "Cloud at " << cloudPos.x << "," << cloudPos.y << "," << cloudPos.z << "\n";

    if (technology != "wifi")
    {
        NS_FATAL_ERROR("Unsupported technology: " << technology);
    }
    
    // ---------------- Internet (IPv4) ----------------)
    InternetStackHelper stack;
    stack.Install(sensors);
    stack.Install(gateway);
    stack.Install(cloud);

    // ---------------- Wi-Fi (sensors STA <-> gateway AP) ----------------
    NetDeviceContainer staDevs, apDev;
    Ipv4InterfaceContainer staIf, apIf;
    if (topology == "star")
    {
        std::tie(staDevs, apDev, staIf, apIf) = InstallWifi(sensors, gateway, environment, txPowerDbm);
    }
    else
    {
        std::tie(staDevs, apDev, staIf, apIf) = InstallMeshWifi(sensors, gateway, environment, txPowerDbm);
    }

    // ---------------- P2P backhaul: gateway <-> cloud ----------------
    // auto [p2pDevs, p2pIf] = InstallP2P(gateway, cloud);

    if (topology != "mesh")
    {
        Ipv4GlobalRoutingHelper::PopulateRoutingTables();
    }

    // ---------------- Energy Model Setup ----------------
    NodeContainer energyNodes;
    energyNodes.Add(sensors);

    DeviceEnergyModelContainer deviceEnergyModels = SetupEnergyModel(energyNodes, staDevs, energyFile, technology, topology);

    // ---------------- Applications ----------------
    UdpServerHelper server(serverPort);
    // ApplicationContainer serverApps = server.Install(cloud.Get(0));
    ApplicationContainer serverApps = server.Install(gateway.Get(0));
    serverApps.Start(Seconds(1.0));
    serverApps.Stop(Seconds(simTimeSec - 1));

    // Ipv4Address serverAddr = p2pIf.GetAddress(1);
    Ipv4Address serverAddr = apIf.GetAddress(0);
    UdpClientHelper client(serverAddr, serverPort);
    client.SetAttribute("MaxPackets", UintegerValue(0));
    client.SetAttribute("Interval", TimeValue(Seconds(intervalSec)));
    client.SetAttribute("PacketSize", UintegerValue(payloadBytes));

    Ptr<UniformRandomVariable> jitter = CreateObject<UniformRandomVariable>();
    jitter->SetAttribute("Min", DoubleValue(0.0));
    jitter->SetAttribute("Max", DoubleValue(0.5));

    for (uint32_t i = 0; i < nDevices; ++i)
    {
        ApplicationContainer apps = client.Install(sensors.Get(i));
        // double start = 2.0 + i * 0.25;
        double start = 2.0 + jitter->GetValue();
        apps.Start(Seconds(start));
        apps.Stop(Seconds(simTimeSec - 2.0));
    }


    // ---------------- Flow Monitor ----------------
    FlowMonitorHelper fmHelper;
    Ptr<FlowMonitor> monitor = fmHelper.InstallAll();

    // ---------------- Run ----------------
    Simulator::Stop(Seconds(simTimeSec));
    Simulator::Run();

    monitor->SerializeToXmlFile(flowmonFile, false, false);

    uint32_t nodeId = 0;
    for (auto iter = deviceEnergyModels.Begin(); iter != deviceEnergyModels.End(); iter++, ++nodeId)
    {
        double energyConsumed = (*iter)->GetTotalEnergyConsumption();
        // NS_LOG_UNCOND("End of simulation ("
        //               << Simulator::Now().GetSeconds()
        //               << "s) Total energy consumed by radio = " << energyConsumed << "J");
        g_energyCsv << Simulator::Now().GetSeconds() << ","
                << nodeId << ","
                << 300.0 - energyConsumed << std::endl;
    }

    std::ofstream meta(metaFile);
    meta << "{\n";
    meta << "  \"experimentName\": \"" << experimentName << "\",\n";
    meta << "  \"environment\": \"" << environment << "\",\n";
    meta << "  \"technology\": \"" << ((topology == "mesh") ? topology : technology) << "\",\n";
    meta << "  \"distance\": " << distance << ",\n";
    meta << "  \"nDevices\": " << nDevices << ",\n";
    meta << "  \"txPowerDbm\": " << txPowerDbm << ",\n";
    meta << "  \"simTimeSec\": " << simTimeSec << ",\n";
    meta << "  \"payloadBytes\": " << payloadBytes << ",\n";
    meta << "  \"intervalSec\": " << intervalSec << ",\n";
    meta << "  \"seed\": " << runSeed << "\n";
    meta << "}\n";
    meta.close();

    Simulator::Destroy();

    std::cout << "Simulation complete.\n";
    return 0;
}
