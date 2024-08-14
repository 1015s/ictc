#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("TcpVegasCongestionTest1Gbps");

// RTT를 추적하고 파일에 기록하는 함수
void RttTracer(Time oldRtt, Time newRtt)
{
    static std::ofstream rttFile("rtt-oscillation-vegas-1Gbps.csv", std::ios::out | std::ios::app);
    static double startTime = Simulator::Now().GetSeconds();

    double currentTime = Simulator::Now().GetSeconds() - startTime;
    double rttValue = newRtt.GetSeconds();

    rttFile << currentTime << "," << rttValue << std::endl;

    NS_LOG_UNCOND("Time: " << currentTime << "s, RTT: " << rttValue << "s");
}

// RTT 추적기를 설정하는 함수
void SetupRttTracer(Ptr<Node> node)
{
    Config::ConnectWithoutContext("/NodeList/0/$ns3::TcpL4Protocol/SocketList/0/RTT", MakeCallback(&RttTracer));
}

int main(int argc, char *argv[])
{
    double simulationTime = 10.0;

    // 로그 활성화
    LogComponentEnable("TcpVegasCongestionTest1Gbps", LOG_LEVEL_INFO);

    // 노드 생성
    NodeContainer nodes;
    nodes.Create(2);

    // 포인트 투 포인트 링크 설정 (중간 대역폭, 고정 지연)
    PointToPointHelper pointToPoint;
    pointToPoint.SetDeviceAttribute("DataRate", StringValue("1Gbps"));
    pointToPoint.SetChannelAttribute("Delay", StringValue("50ms"));

    NetDeviceContainer devices = pointToPoint.Install(nodes);

    // 인터넷 스택 설치
    InternetStackHelper stack;
    stack.Install(nodes);

    // IP 주소 설정
    Ipv4AddressHelper address;
    address.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer interfaces = address.Assign(devices);

    // TCP-BBR 설정
    TypeId tcpVegasTypeId = TypeId::LookupByName("ns3::TcpVegas");
    Config::Set("/NodeList/*/$ns3::TcpL4Protocol/SocketType", TypeIdValue(tcpVegasTypeId));

    // 애플리케이션 설정 (TCP 트래픽 생성)
    uint16_t sinkPort = 8080;
    Address sinkAddress(InetSocketAddress(interfaces.GetAddress(1), sinkPort));
    PacketSinkHelper packetSinkHelper("ns3::TcpSocketFactory", sinkAddress);
    ApplicationContainer sinkApp = packetSinkHelper.Install(nodes.Get(1));
    sinkApp.Start(Seconds(0.0));
    sinkApp.Stop(Seconds(simulationTime));

    // 혼잡 유도를 위해 OnOffHelper를 사용하여 간헐적으로 높은 트래픽 발생
    OnOffHelper onOffHelper("ns3::TcpSocketFactory", sinkAddress);
    onOffHelper.SetAttribute("DataRate", StringValue("0.5Gbps"));
    onOffHelper.SetAttribute("PacketSize", UintegerValue(1024));
    onOffHelper.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=0.3]"));
    onOffHelper.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0.7]"));
    ApplicationContainer clientApp = onOffHelper.Install(nodes.Get(0));
    clientApp.Start(Seconds(1.0));
    clientApp.Stop(Seconds(simulationTime));

    // RTT 콜백을 연결하는 이벤트 추가
    Simulator::Schedule(Seconds(1.1), &SetupRttTracer, nodes.Get(0));

    // 시뮬레이션 실행
    Simulator::Stop(Seconds(simulationTime));
    Simulator::Run();
    Simulator::Destroy();

    return 0;
}
