#pragma once

#include <cstddef>
#include <cstdint>

namespace ZapretConnectivity
{

bool ProbeDiscord();
bool ProbeYouTube();
bool ProbeTelegram();
bool ProbeTelegramMtProxy(const char* host, int port, const uint8_t secret[16], int timeoutMs = 3500);
bool ProbeHttpsUrl(const char* url, int timeoutMs = 5000);
int MeasureIcmpPingMs();
int MeasureIcmpPingMs(const char* host, int timeoutMs = 1200);

}  // namespace ZapretConnectivity
