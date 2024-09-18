#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("TcpCubicSimulation");

void RttTracer(Time oldRtt, Time newRtt)
{
    static std::ofstream rttFile("rtt-tcpcubic-wired.csv", std::ios::out | std::ios::app);
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
    static std::ofstream throughputFile("throughput-tcpcubic-wired.csv", std::ios::out | std::ios::app);
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

    LogComponentEnable("TcpCubicSimulation", LOG_LEVEL_INFO);

    // TCP Do 사용 설정
    TypeId tcpTypeId = TypeId::LookupByName("ns3::TcpCubic");
    Config::SetDefault("ns3::TcpL4Protocol::SocketType", TypeIdValue(tcpTypeId));

    // 네트워크 노드 생성
    NodeContainer sender, receiver, routerNode;
    sender.Create(1);
    receiver.Create(1);
    routerNode.Create(1);

    Ptr<UniformRandomVariable> delayVar = CreateObject<UniformRandomVariable>();
    delayVar->SetAttribute("Min", DoubleValue(0.5)); // 최소 지연 시간
    delayVar->SetAttribute("Max", DoubleValue(1.5)); // 최대 지연 시간
    delayVar->SetStream(1);

    // 링크 설정: 더 높은 대역폭과 지연 시간 설정
    PointToPointHelper pointToPoint;
    pointToPoint.SetDeviceAttribute("DataRate", StringValue("1Gbps")); // 대역폭 크게 설정
    pointToPoint.SetChannelAttribute("Delay", TimeValue(MilliSeconds(delayVar->GetValue())));


    // 공유 링크 설정: router에서 receiver까지
    PointToPointHelper sharedLink;
    sharedLink.SetDeviceAttribute("DataRate", StringValue("1Gbps")); // 대역폭 크게 설정
    sharedLink.SetChannelAttribute("Delay", TimeValue(MilliSeconds(delayVar->GetValue())));


    // sender에서 router까지의 링크
    NetDeviceContainer senderToRouter = pointToPoint.Install(sender.Get(0), routerNode.Get(0));

    // router에서 receiver까지의 공유 링크 설정
    NetDeviceContainer routerToReceiver = sharedLink.Install(routerNode.Get(0), receiver.Get(0));

    // 인터넷 스택 설치
    InternetStackHelper stack;
    stack.Install(sender);
    stack.Install(receiver);
    stack.Install(routerNode);

    // IP 주소 할당
    Ipv4AddressHelper address;
    address.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer senderRouterInterfaces = address.Assign(senderToRouter);

    address.SetBase("10.1.2.0", "255.255.255.0");
    Ipv4InterfaceContainer routerReceiverInterfaces = address.Assign(routerToReceiver);

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    // sender 전용 수신 애플리케이션 설정 (별도 포트 사용)
    uint16_t senderSinkPort = 8080;
    Address senderSinkAddress(InetSocketAddress(routerReceiverInterfaces.GetAddress(1), senderSinkPort));
    PacketSinkHelper senderPacketSinkHelper("ns3::TcpSocketFactory", senderSinkAddress);
    ApplicationContainer senderSinkApp = senderPacketSinkHelper.Install(receiver.Get(0));
    senderSinkApp.Start(Seconds(0.0));
    senderSinkApp.Stop(Seconds(simulationTime));

    // sender 애플리케이션 설정: 더 높은 송신 속도 설정
    OnOffHelper onOffHelper("ns3::TcpSocketFactory", senderSinkAddress);
    onOffHelper.SetAttribute("DataRate", StringValue("1Gbps")); // 높은 송신 속도 설정
    onOffHelper.SetAttribute("PacketSize", UintegerValue(1024));

    // 일정한 트래픽 패턴 설정
    onOffHelper.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=0.1]"));
    onOffHelper.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0.1]")); // 계속 켜져 있음

    ApplicationContainer clientApp = onOffHelper.Install(sender.Get(0));
    clientApp.Start(Seconds(1.0));
    clientApp.Stop(Seconds(simulationTime));

    // RTT 및 sender의 throughput 측정 시작
    Simulator::Schedule(Seconds(1.1), &SetupRttTracer, sender.Get(0));
    Simulator::Schedule(Seconds(1.1), &ThroughputTracer, senderSinkApp.Get(0)); // sender의 throughput 측정

    Simulator::Stop(Seconds(simulationTime));
    Simulator::Run();
    Simulator::Destroy();

    return 0;
}
