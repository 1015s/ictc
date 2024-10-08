#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("TcpDoFrequencyTest");

// RTT를 추적하고 파일에 기록하는 함수
void RttTracer(Time oldRtt, Time newRtt)
{
    static std::ofstream rttFile("rtt-high-congestion-do.csv", std::ios::out | std::ios::app);
    static double startTime = Simulator::Now().GetSeconds();

    double currentTime = Simulator::Now().GetSeconds() - startTime;
    double rttValue = newRtt.GetSeconds();

    rttFile << currentTime << "," << rttValue << std::endl;

    // NS_LOG_UNCOND("Time: " << currentTime << "s, RTT: " << rttValue << "s");
}

// Throughput을 추적하고 파일에 기록하는 함수
void ThroughputTracer(Ptr<Application> sinkApp)
{
    static std::ofstream throughputFile("throughput-high-congestion-do.csv", std::ios::out | std::ios::app);
    static double lastTotalRx = 0;
    static double lastTime = Simulator::Now().GetSeconds();
    
    double currentTime = Simulator::Now().GetSeconds();
    Ptr<PacketSink> sink = DynamicCast<PacketSink>(sinkApp);
    double currentTotalRx = sink->GetTotalRx();
    
    double timeInterval = currentTime - lastTime;

    // 첫 번째 호출 시 timeInterval이 0일 경우 1초로 설정
    if (timeInterval == 0)
    {
        timeInterval = 1.0;
    }

    double throughput = (currentTotalRx - lastTotalRx) * 8 / (1e6 * timeInterval); // Mbps로 변환
    throughputFile << currentTime << "," << throughput << std::endl;

    NS_LOG_UNCOND("Time: " << currentTime << "s, Throughput: " << throughput << " Mbps");

    // 상태 갱신
    lastTotalRx = currentTotalRx;
    lastTime = currentTime;

    // 1초마다 Throughput 측정
    Simulator::Schedule(Seconds(1.0), &ThroughputTracer, sinkApp);
}


// RTT 추적기를 설정하는 함수
void SetupRttTracer(Ptr<Node> node)
{
    Config::ConnectWithoutContext("/NodeList/0/$ns3::TcpL4Protocol/SocketList/0/RTT", MakeCallback(&RttTracer));
}

int main(int argc, char *argv[])
{
    double simulationTime = 20.0;  // 시뮬레이션 시간을 20초로 증가

    // 로그 활성화
    LogComponentEnable("TcpDoFrequencyTest", LOG_LEVEL_INFO);

    // 노드 생성
    NodeContainer nodes;
    nodes.Create(2);

    // 포인트 투 포인트 링크 설정 (중간 대역폭, 가변 지연)
    PointToPointHelper pointToPoint;
    pointToPoint.SetDeviceAttribute("DataRate", StringValue("1Gbps"));

    // 가변 지연을 추가하기 위해 UniformRandomVariable 사용
    Ptr<UniformRandomVariable> uv = CreateObject<UniformRandomVariable>();
    uv->SetAttribute("Min", DoubleValue(20)); // 최소 지연 20ms (더 낮게 설정)
    uv->SetAttribute("Max", DoubleValue(80)); // 최대 지연 80ms (더 높게 설정)
    uv->SetStream(1);

    pointToPoint.SetChannelAttribute("Delay", TimeValue(MilliSeconds(uv->GetValue())));

    // 패킷 손실을 도입하기 위한 에러 모델 설정
    Ptr<RateErrorModel> em = CreateObject<RateErrorModel>();
    em->SetAttribute("ErrorRate", DoubleValue(0.01)); // 1%의 패킷 손실률 설정

    NetDeviceContainer devices = pointToPoint.Install(nodes);

    // 인터넷 스택 설치
    InternetStackHelper stack;
    stack.Install(nodes);

    // IP 주소 설정
    Ipv4AddressHelper address;
    address.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer interfaces = address.Assign(devices);

    // TCP-DO 설정
    TypeId tcpDoTypeId = TypeId::LookupByName("ns3::TcpDo");
    Config::Set("/NodeList/*/$ns3::TcpL4Protocol/SocketType", TypeIdValue(tcpDoTypeId));

    // 애플리케이션 설정 (TCP 트래픽 생성)
    uint16_t sinkPort = 8080;
    Address sinkAddress(InetSocketAddress(interfaces.GetAddress(1), sinkPort));
    PacketSinkHelper packetSinkHelper("ns3::TcpSocketFactory", sinkAddress);
    ApplicationContainer sinkApp = packetSinkHelper.Install(nodes.Get(1));
    sinkApp.Start(Seconds(0.0));
    sinkApp.Stop(Seconds(simulationTime));

    // 혼잡 유도를 위해 OnOffHelper를 사용하여 간헐적으로 높은 트래픽 발생
    OnOffHelper onOffHelper("ns3::TcpSocketFactory", sinkAddress);
    onOffHelper.SetAttribute("DataRate", StringValue("2Gbps")); // 더 높은 데이터 속도
    onOffHelper.SetAttribute("PacketSize", UintegerValue(1500));
    onOffHelper.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1.0]"));
    onOffHelper.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0.0]"));
    ApplicationContainer clientApp = onOffHelper.Install(nodes.Get(0));
    clientApp.Start(Seconds(1.0));
    clientApp.Stop(Seconds(simulationTime));

    // RTT 콜백을 연결하는 이벤트 추가
    Simulator::Schedule(Seconds(1.1), &SetupRttTracer, nodes.Get(0));

    // Throughput 콜백을 연결하는 이벤트 추가
    Simulator::Schedule(Seconds(1.1), &ThroughputTracer, sinkApp.Get(0));

    // 시뮬레이션 실행
    Simulator::Stop(Seconds(simulationTime));
    Simulator::Run();
    Simulator::Destroy();

    return 0;
}