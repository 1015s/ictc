#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("TcpDoWirelessScenario");

void RttTracer(Time oldRtt, Time newRtt)
{
    static std::ofstream rttFile("rtt-wireless-router-do.csv", std::ios::out | std::ios::app);
    static double startTime = Simulator::Now().GetSeconds();

    double currentTime = Simulator::Now().GetSeconds() - startTime;
    double rttValue = newRtt.GetSeconds();

    rttFile << currentTime << "," << rttValue << std::endl;
}

void SetupRttTracer(Ptr<Node> node)
{
    Config::ConnectWithoutContext("/NodeList/0/$ns3::TcpL4Protocol/SocketList/0/RTT", MakeCallback(&RttTracer));
}

void ThroughputTracer(Ptr<Application> sinkApp)
{
    static std::ofstream throughputFile("throughput-wireless-router-do.csv", std::ios::out | std::ios::app);
    static double lastTotalRx = 0;
    static double lastTime = Simulator::Now().GetSeconds();

    double currentTime = Simulator::Now().GetSeconds();
    Ptr<PacketSink> sink = DynamicCast<PacketSink>(sinkApp);
    double currentTotalRx = sink->GetTotalRx();

    double timeInterval = currentTime - lastTime;
    if (timeInterval == 0)
    {
        timeInterval = 1.0;
    }

    double throughput = (currentTotalRx - lastTotalRx) * 8 / (1e6 * timeInterval); // Mbps로 변환
    throughputFile << currentTime << "," << throughput << std::endl;

    NS_LOG_UNCOND("Time: " << currentTime << "s, Throughput: " << throughput << " Mbps");

    lastTotalRx = currentTotalRx;
    lastTime = currentTime;

    Simulator::Schedule(Seconds(1.0), &ThroughputTracer, sinkApp);
}

int main(int argc, char *argv[])
{
    double simulationTime = 20.0;

    LogComponentEnable("TcpDoWirelessScenario", LOG_LEVEL_INFO);

    NodeContainer sender, receiver, routerNode;
    sender.Create(1);
    receiver.Create(1);
    routerNode.Create(1);

    NodeContainer trafficSenders;
    trafficSenders.Create(59);

    PointToPointHelper pointToPoint;
    pointToPoint.SetDeviceAttribute("DataRate", StringValue("1Gbps"));
    //pointToPoint.SetChannelAttribute("Delay", StringValue("2ms")); // 링크 지연 시간을 2ms로 변경하여 테스트
    pointToPoint.SetQueue("ns3::DropTailQueue", "MaxSize", StringValue("1000p"));

    // 랜덤 지연 생성: 고정된 시드를 사용하여 평균이 0.5ms인 지연 값
    Ptr<UniformRandomVariable> randomDelay = CreateObject<UniformRandomVariable>();
    randomDelay->SetAttribute("Min", DoubleValue(0.5));
    randomDelay->SetAttribute("Max", DoubleValue(1.0));  // 0.5ms ~ 1.5ms 사이의 랜덤 지연
    randomDelay->SetStream(1);  // 스트림 번호를 설정하여 항상 동일한 난수 시퀀스를 생성

    NetDeviceContainer senderToRouter = pointToPoint.Install(sender.Get(0), routerNode.Get(0));
    NetDeviceContainer routerToReceiver = pointToPoint.Install(routerNode.Get(0), receiver.Get(0));

    // 각 링크의 채널에 랜덤 지연 설정
    Ptr<PointToPointChannel> channel1 = DynamicCast<PointToPointChannel>(senderToRouter.Get(0)->GetChannel());
    channel1->SetAttribute("Delay", TimeValue(MicroSeconds(randomDelay->GetValue() * 1000)));

    Ptr<PointToPointChannel> channel2 = DynamicCast<PointToPointChannel>(routerToReceiver.Get(0)->GetChannel());
    channel2->SetAttribute("Delay", TimeValue(MicroSeconds(randomDelay->GetValue() * 1000)));

    // Error model to induce packet loss on the main link
    Ptr<RateErrorModel> em = CreateObject<RateErrorModel>();
    em->SetAttribute("ErrorRate", DoubleValue(0.000001)); // 오류율을 높여 패킷 손실 증가
    routerToReceiver.Get(1)->SetAttribute("ReceiveErrorModel", PointerValue(em));

    InternetStackHelper stack;
    stack.Install(sender);
    stack.Install(receiver);
    stack.Install(routerNode);
    stack.Install(trafficSenders);

    Ipv4AddressHelper address;
    address.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer senderRouterInterfaces = address.Assign(senderToRouter);

    address.SetBase("10.1.2.0", "255.255.255.0");
    Ipv4InterfaceContainer routerReceiverInterfaces = address.Assign(routerToReceiver);

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    TypeId tcpDoTypeId = TypeId::LookupByName("ns3::TcpDo");
    Config::Set("/NodeList/*/$ns3::TcpL4Protocol/SocketType", TypeIdValue(tcpDoTypeId));

    uint16_t sinkPort = 8080;
    Address sinkAddress(InetSocketAddress(routerReceiverInterfaces.GetAddress(1), sinkPort));
    PacketSinkHelper packetSinkHelper("ns3::TcpSocketFactory", sinkAddress);
    ApplicationContainer sinkApp = packetSinkHelper.Install(receiver.Get(0));
    sinkApp.Start(Seconds(0.0));
    sinkApp.Stop(Seconds(simulationTime));

    OnOffHelper onOffHelper("ns3::TcpSocketFactory", sinkAddress);
    onOffHelper.SetAttribute("DataRate", StringValue("500Mbps"));
    onOffHelper.SetAttribute("PacketSize", UintegerValue(1024));
    onOffHelper.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=0.5]"));
    onOffHelper.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0.5]"));
    ApplicationContainer clientApp = onOffHelper.Install(sender.Get(0));
    clientApp.Start(Seconds(1.0));
    clientApp.Stop(Seconds(simulationTime));

    // Traffic to Router only
    uint16_t routerSinkPort = 8081;
    Address routerSinkAddress(InetSocketAddress(senderRouterInterfaces.GetAddress(1), routerSinkPort));
    PacketSinkHelper routerPacketSinkHelper("ns3::TcpSocketFactory", routerSinkAddress);
    ApplicationContainer routerSinkApp = routerPacketSinkHelper.Install(routerNode.Get(0));
    routerSinkApp.Start(Seconds(0.0));
    routerSinkApp.Stop(Seconds(simulationTime));

    for (uint32_t i = 0; i < trafficSenders.GetN(); ++i)
    {
        OnOffHelper trafficOnOffHelper("ns3::TcpSocketFactory", routerSinkAddress);
        trafficOnOffHelper.SetAttribute("DataRate", StringValue("500Mbps"));
        trafficOnOffHelper.SetAttribute("PacketSize", UintegerValue(1024));

        Ptr<ExponentialRandomVariable> onTimeVar = CreateObject<ExponentialRandomVariable>();
        onTimeVar->SetAttribute("Mean", DoubleValue(0.5));
        trafficOnOffHelper.SetAttribute("OnTime", StringValue("ns3::ExponentialRandomVariable[Mean=0.5]"));

        Ptr<ExponentialRandomVariable> offTimeVar = CreateObject<ExponentialRandomVariable>();
        offTimeVar->SetAttribute("Mean", DoubleValue(0.5));
        trafficOnOffHelper.SetAttribute("OffTime", StringValue("ns3::ExponentialRandomVariable[Mean=0.5]"));
        ApplicationContainer trafficApp = trafficOnOffHelper.Install(trafficSenders.Get(i));

        Ptr<UniformRandomVariable> startVar = CreateObject<UniformRandomVariable>();
        startVar->SetAttribute("Min", DoubleValue(0.0));
        startVar->SetAttribute("Max", DoubleValue(1.0));
        startVar->SetStream(1);
        trafficApp.Start(Seconds(1.0 + startVar->GetValue())); // 랜덤 시작 시간
        trafficApp.Stop(Seconds(simulationTime));
    }

    Simulator::Schedule(Seconds(1.1), &SetupRttTracer, sender.Get(0));
    Simulator::Schedule(Seconds(1.1), &ThroughputTracer, sinkApp.Get(0));

    Simulator::Stop(Seconds(simulationTime));
    Simulator::Run();
    Simulator::Destroy();

    return 0;
}
