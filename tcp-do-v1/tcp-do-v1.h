#ifndef TCP_DO_V1_H
#define TCP_DO_V1_H

#include "ns3/tcp-vegas.h"
#include <deque>

namespace ns3 {

class TcpDo : public TcpVegas
{
public:
    static TypeId GetTypeId(void);
    TcpDo();
    TcpDo(const TcpDo& sock);
    virtual ~TcpDo();
    virtual std::string GetName() const override;

    // 혼잡 제어 관련 메서드
    virtual void PktsAcked(Ptr<TcpSocketState> tcb, uint32_t segmentsAcked, const Time& rtt) override;
    virtual void IncreaseWindow(Ptr<TcpSocketState> tcb, uint32_t segmentsAcked) override;
    void Retransmit(Ptr<TcpSocketState> tcb); // 재전송 감지 메서드

protected:
    void CalculateOscillationFrequency(const Time& rtt);
    void HandleRetransmission(Ptr<TcpSocketState> tcb);

private:
    double m_congestionThreshold;
    double m_lastOscillationFrequency;
    size_t m_oscillationCount;
    size_t m_maxRttHistorySize;
    Time m_timeWindow;
    std::deque<Time> m_rttHistory;
    bool m_retransmitDetected; // 재전송 감지를 위한 플래그
    bool m_fastRecovery;             // 빠른 복구 모드 플래그
    uint32_t m_recoveryCwnd;         // 복구 모드에서 사용할 창 크기
};

} // namespace ns3

#endif /* TCP_DO_V1_H */
