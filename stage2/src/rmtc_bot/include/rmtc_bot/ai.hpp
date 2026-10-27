#pragma once
#include "rmtc_bot/core.hpp"
#include <curl/curl.h>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace rmtc {
struct AiConfig {
  std::string url, model;
  std::string provider{"gateway"};
  int timeout_ms{12000}, budget{90}, max_tokens{2500};
  bool event_revisions{false};
};
struct AiResult {
  std::string session, error;
  int tick{}, http_status{};
  long elapsed_ms{};
  std::string model;
  size_t reasoning_chars{};
  Json usage;
  bool success{};
  Json parsed;
  Json input; // Public request data only; never includes the bearer token.
  std::string request_key;
  bool repair_attempt{};
};
Json decode_model(const std::string &content);
class AiClient {
public:
  explicit AiClient(AiConfig config);
  ~AiClient();
  void reset(const std::string &session, const std::string &token);
  bool submit(const Frame &f, const Events &events, const std::string &text,
              const std::string &source, bool qualification=false);
  bool repair_schema(const AiResult &failed,const Frame &current,const Events &events,
                     const std::string &diagnostic);
  std::vector<AiResult> drain();
private:
  struct Job { std::string session, token, key; int tick; Json input;bool repair{}; };
  AiResult request(const Job &job);
  void run();
  AiConfig config_;
  CURL *curl_{}; // Used only by the worker; retains DNS/TLS/connection caches between jobs.
  std::mutex mutex_;
  std::condition_variable cv_;
  std::thread worker_;
  bool stopping_{false}, exhausted_{false};
  std::string session_, token_;
  int successes_{0}, attempts_{0};
  std::deque<Job> jobs_;
  std::vector<AiResult> results_;
  std::set<std::string> seen_;
  std::set<std::string> repairs_;
};
} // namespace rmtc
