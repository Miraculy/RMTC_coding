#pragma once
#include "rmtc_bot/core.hpp"
#include <condition_variable>
#include <mutex>
#include <thread>

namespace rmtc {
struct AiConfig {
  std::string url, model;
  int timeout_ms{12000}, budget{90}, max_tokens{2500};
};
struct AiResult {
  std::string session, error;
  int tick{}, http_status{};
  long elapsed_ms{};
  bool success{};
  Json parsed;
};
Json decode_model(const std::string &content);
class AiClient {
public:
  explicit AiClient(AiConfig config);
  ~AiClient();
  void reset(const std::string &session, const std::string &token);
  bool submit(const Frame &f, const Events &events, const std::string &text,
              const std::string &source, bool qualification=false);
  std::vector<AiResult> drain();
private:
  struct Job { std::string session, token, key; int tick; Json input; };
  AiResult request(const Job &job);
  void run();
  AiConfig config_;
  std::mutex mutex_;
  std::condition_variable cv_;
  std::thread worker_;
  bool stopping_{false}, exhausted_{false};
  std::string session_, token_;
  int successes_{0}, attempts_{0};
  std::deque<Job> jobs_;
  std::vector<AiResult> results_;
  std::set<std::string> seen_;
};
} // namespace rmtc
