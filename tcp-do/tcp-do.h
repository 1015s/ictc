#ifndef TCP_DO_H
#define TCP_DO_H

#include "ns3/tcp-congestion-ops.h"
#include "ns3/tcp-socket-state.h"

namespace ns3 {

class TcpDo : public TcpCongestionOps
{
public:
  static TypeId GetTypeId (void);
  TcpDo (void);
  virtual ~TcpDo (void);

  virtual std::string GetName () const override;
  virtual void IncreaseWindow (Ptr<TcpSocketState> tcb, uint32_t segmentsAcked) override;
  virtual uint32_t GetSsThresh (Ptr<const TcpSocketState> tcb, uint32_t bytesInFlight) override;
  virtual Ptr<TcpCongestionOps> Fork() override;  // Fork 메서드 구현

private:
  double m_prevRtt;                // 이전 RTT 값
  double m_oscillationThreshold;   // 오실레이션을 감지하기 위한 임계값
  double m_congestionWindowDelta;  // 혼잡 윈도우 변화량
};

} // namespace ns3

#endif // TCP_DO_H
