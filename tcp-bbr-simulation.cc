#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("TcpBbrSimulation");

void RttTracer(Time oldRtt, Time newRtt)
{
    static std::ofstream rttFile("rtt-tcpbbr-wireless.csv", std::ios::out | std::ios::app);
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
    static std::ofstream throughputFile("throughput-tcpbbr-wireless.csv", std::ios::out | std::ios::app);
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

    LogComponentEnable("TcpBbrSimulation", LOG_LEVEL_INFO);

    // TCP Do 사용 설정
    TypeId tcpTypeId = TypeId::LookupByName("ns3::TcpBbr");
    Config::SetDefault("ns3::TcpL4Protocol::SocketType", TypeIdValue(tcpTypeId));

    // 네트워크 노드 생성
    NodeContainer sender, receiver, routerNode;
    sender.Create(1);
    receiver.Create(1);
    routerNode.Create(1);

    NodeContainer trafficSenders;
    trafficSenders.Create(9);

    // 링크 설정
    PointToPointHelper pointToPoint;
    pointToPoint.SetDeviceAttribute("DataRate", StringValue("1Gbps"));

    // NormalRandomVariable을 사용하여 지연 시간 설정
    Ptr<NormalRandomVariable> delayVar = CreateObject<NormalRandomVariable>();
    delayVar->SetAttribute("Mean", DoubleValue(0.5)); // 평균 10ms
    delayVar->SetAttribute("Variance", DoubleValue(0.2)); // 분산 2ms
    pointToPoint.SetChannelAttribute("Delay", TimeValue(MilliSeconds(delayVar->GetValue())));

    // 공유 링크 설정: router에서 receiver까지
    PointToPointHelper sharedLink;
    sharedLink.SetDeviceAttribute("DataRate", StringValue("1Gbps"));
    sharedLink.SetChannelAttribute("Delay", StringValue("1ms"));

    // 패킷 손실을 유발하는 ErrorModel 설정
    Ptr<RateErrorModel> em = CreateObject<RateErrorModel>();
    em->SetAttribute("ErrorRate", DoubleValue(0.001)); // 0.1% 패킷 손실 확률
    em->SetAttribute("ErrorUnit", StringValue("ERROR_UNIT_PACKET"));

    // sender에서 router까지의 링크
    NetDeviceContainer senderToRouter = pointToPoint.Install(sender.Get(0), routerNode.Get(0));

    // trafficSenders에서 router까지의 링크
    NetDeviceContainer trafficSenderToRouter[9];
    for (uint32_t i = 0; i < trafficSenders.GetN(); ++i)
    {
        trafficSenderToRouter[i] = pointToPoint.Install(trafficSenders.Get(i), routerNode.Get(0));
    }

    // router에서 receiver까지의 공유 링크에 ErrorModel 추가
    NetDeviceContainer routerToReceiver = sharedLink.Install(routerNode.Get(0), receiver.Get(0));
    routerToReceiver.Get(1)->SetAttribute("ReceiveErrorModel", PointerValue(em)); // 링크에 패킷 손실 모델 적용

    // 인터넷 스택 설치
    InternetStackHelper stack;
    stack.Install(sender);
    stack.Install(receiver);
    stack.Install(routerNode);
    stack.Install(trafficSenders);

    // IP 주소 할당
    Ipv4AddressHelper address;
    address.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer senderRouterInterfaces = address.Assign(senderToRouter);

    for (uint32_t i = 0; i < trafficSenders.GetN(); ++i)
    {
        std::ostringstream subnet;
        subnet << "10.1." << i + 2 << ".0";
        address.SetBase(subnet.str().c_str(), "255.255.255.0");
        address.Assign(trafficSenderToRouter[i]);
    }

    address.SetBase("10.1.100.0", "255.255.255.0");
    Ipv4InterfaceContainer routerReceiverInterfaces = address.Assign(routerToReceiver);

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    // sender 전용 수신 애플리케이션 설정 (별도 포트 사용)
    uint16_t senderSinkPort = 8080;
    Address senderSinkAddress(InetSocketAddress(routerReceiverInterfaces.GetAddress(1), senderSinkPort));
    PacketSinkHelper senderPacketSinkHelper("ns3::TcpSocketFactory", senderSinkAddress);
    ApplicationContainer senderSinkApp = senderPacketSinkHelper.Install(receiver.Get(0));
    senderSinkApp.Start(Seconds(0.0));
    senderSinkApp.Stop(Seconds(simulationTime));

    // sender 애플리케이션 설정
    OnOffHelper onOffHelper("ns3::TcpSocketFactory", senderSinkAddress);
    onOffHelper.SetAttribute("DataRate", StringValue("300Mbps"));
    onOffHelper.SetAttribute("PacketSize", UintegerValue(1024));

    // 변동성을 위한 랜덤 On/Off 시간 설정
    onOffHelper.SetAttribute("OnTime", StringValue("ns3::ExponentialRandomVariable[Mean=0.1]"));
    onOffHelper.SetAttribute("OffTime", StringValue("ns3::ExponentialRandomVariable[Mean=0.2]"));

    ApplicationContainer clientApp = onOffHelper.Install(sender.Get(0));
    clientApp.Start(Seconds(1.0));
    clientApp.Stop(Seconds(simulationTime));

    // trafficSenders 애플리케이션 설정 (다른 포트 사용)
    uint16_t trafficSinkPort = 8081;
    Address trafficSinkAddress(InetSocketAddress(routerReceiverInterfaces.GetAddress(1), trafficSinkPort));
    PacketSinkHelper trafficPacketSinkHelper("ns3::TcpSocketFactory", trafficSinkAddress);
    ApplicationContainer trafficSinkApp = trafficPacketSinkHelper.Install(receiver.Get(0));
    trafficSinkApp.Start(Seconds(0.0));
    trafficSinkApp.Stop(Seconds(simulationTime));

    // 랜덤 트래픽 패턴 설정
    for (uint32_t i = 0; i < trafficSenders.GetN(); ++i)
    {
        OnOffHelper trafficOnOffHelper("ns3::TcpSocketFactory", trafficSinkAddress);
        trafficOnOffHelper.SetAttribute("DataRate", StringValue("300Mbps"));
        trafficOnOffHelper.SetAttribute("PacketSize", UintegerValue(1024));

        // 모든 노드에서 동일한 데이터 전송 속도를 사용하지만, On/Off 시간을 다르게 설정하여 변동성 추가
        trafficOnOffHelper.SetAttribute("OnTime", StringValue("ns3::ExponentialRandomVariable[Mean=0.1]"));
        trafficOnOffHelper.SetAttribute("OffTime", StringValue("ns3::ExponentialRandomVariable[Mean=0.2]"));

        ApplicationContainer trafficApp = trafficOnOffHelper.Install(trafficSenders.Get(i));

        // 각 트래픽 노드의 시작 시간을 랜덤하게 설정
        Ptr<UniformRandomVariable> startVar = CreateObject<UniformRandomVariable>();
        startVar->SetAttribute("Min", DoubleValue(0.0));
        startVar->SetAttribute("Max", DoubleValue(1.0));
        startVar->SetStream(i + 1);
        trafficApp.Start(Seconds(1.0 + startVar->GetValue()));
        trafficApp.Stop(Seconds(simulationTime));
    }

    // RTT 및 sender의 throughput 측정 시작
    Simulator::Schedule(Seconds(1.1), &SetupRttTracer, sender.Get(0));
    Simulator::Schedule(Seconds(1.1), &ThroughputTracer, senderSinkApp.Get(0)); // sender의 throughput 측정

    Simulator::Stop(Seconds(simulationTime));
    Simulator::Run();
    Simulator::Destroy();

    return 0;
}
