#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "tcp-do.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("TcpDoTest");

// RTT를 추적하고 파일에 기록하는 함수
void RttTracer(Time oldRtt, Time newRtt)
{
    static std::ofstream rttFile("rtt-oscillation-do.csv", std::ios::out | std::ios::app);
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
    double simulationTime = 60.0;

    // 로그 활성화
    LogComponentEnable("TcpDoTest", LOG_LEVEL_INFO);

    // 노드 생성
    NodeContainer nodes;
    nodes.Create(2);

    // 포인트 투 포인트 링크 설정 (변동성이 큰 지연 시간 적용)
    PointToPointHelper pointToPoint;
    pointToPoint.SetDeviceAttribute("DataRate", StringValue("5Mbps"));
    pointToPoint.SetChannelAttribute("Delay", StringValue("2ms"));

    // 패킷 손실 모델 추가
    Ptr<RateErrorModel> errorModel = CreateObject<RateErrorModel>();
    errorModel->SetAttribute("ErrorRate", DoubleValue(0.01));  // 1% 패킷 손실

    NetDeviceContainer devices = pointToPoint.Install(nodes);
    devices.Get(0)->SetAttribute("ReceiveErrorModel", PointerValue(errorModel));

    // 인터넷 스택 설치
    InternetStackHelper stack;
    stack.Install(nodes);

    // IP 주소 설정
    Ipv4AddressHelper address;
    address.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer interfaces = address.Assign(devices);

    // TCP-do 설정
    TypeId tcpDoTypeId = TypeId::LookupByName("ns3::TcpDo");
    Config::Set("/NodeList/*/$ns3::TcpL4Protocol/SocketType", TypeIdValue(tcpDoTypeId));

    // 애플리케이션 설정 (TCP 트래픽 생성)
    uint16_t sinkPort = 8080;
    Address sinkAddress(InetSocketAddress(interfaces.GetAddress(1), sinkPort));
    PacketSinkHelper packetSinkHelper("ns3::TcpSocketFactory", sinkAddress);
    ApplicationContainer sinkApp = packetSinkHelper.Install(nodes.Get(1));
    sinkApp.Start(Seconds(0.0));
    sinkApp.Stop(Seconds(simulationTime));

    // 다중 OnOffHelper 인스턴스를 사용하여 혼잡 유도
    for (int i = 0; i < 5; ++i)
    {
        OnOffHelper onOffHelper("ns3::TcpSocketFactory", sinkAddress);
        onOffHelper.SetAttribute("DataRate", StringValue("5Mbps"));
        onOffHelper.SetAttribute("PacketSize", UintegerValue(1024));
        ApplicationContainer clientApp = onOffHelper.Install(nodes.Get(0));
        clientApp.Start(Seconds(1.0 + i * 5)); // 흐름 시작 시간을 다르게 설정
        clientApp.Stop(Seconds(simulationTime));
    }

    // RTT 콜백을 연결하는 이벤트 추가
    Simulator::Schedule(Seconds(1.1), &SetupRttTracer, nodes.Get(0));

    // 시뮬레이션 실행
    Simulator::Stop(Seconds(simulationTime));
    Simulator::Run();
    Simulator::Destroy();

    return 0;
}
