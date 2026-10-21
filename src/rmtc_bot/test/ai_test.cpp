#include "rmtc_bot/ai.hpp"
#include <gtest/gtest.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <sstream>
using namespace rmtc;
namespace {
struct Reply { int status; std::string content; int delay_ms; };
// Deterministic HTTP fixture for transport tests; it does not implement an LLM.
class HttpFixture {
public:
  explicit HttpFixture(std::vector<Reply> replies):replies_(std::move(replies)) {
    fd_=socket(AF_INET,SOCK_STREAM,0);
    if(fd_<0)throw std::runtime_error("socket");
    sockaddr_in addr{};addr.sin_family=AF_INET;addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);addr.sin_port=0;
    if(bind(fd_,reinterpret_cast<sockaddr*>(&addr),sizeof(addr)) || listen(fd_,8))throw std::runtime_error("bind/listen");
    socklen_t len=sizeof(addr);getsockname(fd_,reinterpret_cast<sockaddr*>(&addr),&len);port=ntohs(addr.sin_port);
    thread_=std::thread([this]{serve();});
  }
  ~HttpFixture(){stop_=true;shutdown(fd_,SHUT_RDWR);if(thread_.joinable())thread_.join();close(fd_);}
  std::string url()const{return "http://127.0.0.1:"+std::to_string(port)+"/v1/chat/completions";}
  std::atomic<int> hits{0};int port{};
private:
  void serve() {
    while(!stop_) {
      int client=accept(fd_,nullptr,nullptr);if(client<0)return;
      timeval timeout{2,0};setsockopt(client,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
      std::string raw;char buf[4096];size_t required=0, header_end=std::string::npos;
      while(true) {
        auto n=recv(client,buf,sizeof(buf),0);if(n<=0)break;raw.append(buf,static_cast<size_t>(n));
        header_end=raw.find("\r\n\r\n");
        if(header_end!=std::string::npos) {
          std::istringstream headers(raw.substr(0,header_end));std::string line;
          while(std::getline(headers,line))if(line.rfind("Content-Length:",0)==0)required=std::stoul(line.substr(15));
          if(raw.size()>=header_end+4+required)break;
        }
      }
      int index=hits.fetch_add(1);auto reply=replies_[std::min(static_cast<size_t>(index),replies_.size()-1)];
      std::this_thread::sleep_for(std::chrono::milliseconds(reply.delay_ms));
      std::string body=reply.content;
      if(body=="structured") {
        try {
          auto req=Json::parse(raw.substr(header_end+4));auto input=Json::parse(req["messages"][1]["content"].get<std::string>());
          Json out{{"session_id",input["session_id"]},{"events",Json::array()},{"effects",Json::array()}};
          body=Json{{"choices",Json::array({{{"message",{{"content",out.dump()}}}}})}}.dump();
        } catch(...) {body="{}";}
      }
      std::string response="HTTP/1.1 "+std::to_string(reply.status)+" Test\r\nContent-Type: application/json\r\nContent-Length: "+std::to_string(body.size())+"\r\nConnection: close\r\n\r\n"+body;
      size_t offset=0;while(offset<response.size()) {auto n=send(client,response.data()+offset,response.size()-offset,MSG_NOSIGNAL);if(n<=0)break;offset+=static_cast<size_t>(n);}
      close(client);
    }
  }
  int fd_{};std::vector<Reply> replies_;std::atomic<bool> stop_{false};std::thread thread_;
};
std::vector<AiResult> await_result(AiClient &client,int expected=1) {
  std::vector<AiResult> all;auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);
  while(std::chrono::steady_clock::now()<end && static_cast<int>(all.size())<expected) {
    auto rows=client.drain();all.insert(all.end(),rows.begin(),rows.end());std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return all;
}
Frame ai_frame(){Frame f;f.session="http-test";f.width=f.height=24;return f;}
}
TEST(Ai, ParsesPlainAndFencedJsonButRejectsSurroundingProse) {
  EXPECT_EQ(decode_model(" {\"events\":[]} ")["events"].size(),0u);
  EXPECT_EQ(decode_model("```json\n{\"events\":[]}\n```\n")["events"].size(),0u);
  EXPECT_THROW(decode_model("Advice: {\"events\":[]}"),std::exception);
  EXPECT_THROW(decode_model("```json\n{}\n``` trailing"),std::exception);
}
TEST(Ai, RejectsInvalidTransportConfiguration) {
  AiConfig c;c.url="file:///etc/passwd";EXPECT_THROW(AiClient client(c),std::exception);
  c.url="http://127.0.0.1:8080/v1/chat/completions";c.timeout_ms=0;EXPECT_THROW(AiClient client(c),std::exception);
}
TEST(Ai, MissingTokenDoesNotEnqueueRequests) {
  AiConfig c;c.url="http://127.0.0.1:1/v1/chat/completions";AiClient client(c);
  Frame f;f.session="test";client.reset(f.session,"");
  EXPECT_FALSE(client.submit(f,Events{},"公告","perception"));EXPECT_TRUE(client.drain().empty());
}
TEST(Ai, DelayedHttpDoesNotBlockSubmitAndReturnsStructuredContent) {
  HttpFixture server({{200,"structured",200}});AiConfig config;config.url=server.url();AiClient client(config);
  auto f=ai_frame();client.reset(f.session,"test-token");
  auto begin=std::chrono::steady_clock::now();ASSERT_TRUE(client.submit(f,Events{},"event announcement","perception"));
  EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-begin).count(),50);
  auto rows=await_result(client);ASSERT_EQ(rows.size(),1u);EXPECT_TRUE(rows[0].success);EXPECT_EQ(rows[0].parsed["session_id"],f.session);
  EXPECT_GE(rows[0].elapsed_ms,190);EXPECT_EQ(server.hits,1);
}
TEST(Ai, QuotaExhaustionStopsFurtherCalls) {
  HttpFixture server({{429,R"({"error":{"code":"quota_exceeded"}})",0}});AiConfig config;config.url=server.url();AiClient client(config);
  auto f=ai_frame();client.reset(f.session,"test-token");client.submit(f,Events{},"first","perception");
  auto rows=await_result(client);ASSERT_EQ(rows.size(),1u);EXPECT_EQ(rows[0].error,"quota_exceeded");
  EXPECT_FALSE(client.submit(f,Events{},"second","perception"));EXPECT_EQ(server.hits,1);
}
TEST(Ai, RetriesTransientUpstreamFailureWithBackoff) {
  HttpFixture server({{502,R"({"error":{"code":"upstream_unavailable"}})",0},{200,"structured",0}});
  AiConfig config;config.url=server.url();AiClient client(config);auto f=ai_frame();client.reset(f.session,"test-token");client.submit(f,Events{},"first","perception");
  auto rows=await_result(client,2);ASSERT_EQ(rows.size(),2u);EXPECT_FALSE(rows[0].success);EXPECT_TRUE(rows[1].success);EXPECT_EQ(server.hits,2);
}
TEST(Ai, HttpSuccessWithUnusableContentStillConsumesBudget) {
  HttpFixture server({{200,R"({"choices":[{"message":{"content":"not JSON"}}]})",0}});
  AiConfig config;config.url=server.url();config.budget=1;AiClient client(config);auto f=ai_frame();client.reset(f.session,"test-token");client.submit(f,Events{},"first","perception");
  auto rows=await_result(client);ASSERT_EQ(rows.size(),1u);EXPECT_TRUE(rows[0].success);EXPECT_EQ(rows[0].error,"model_content_invalid");
  EXPECT_FALSE(client.submit(f,Events{},"second","perception"));
}
TEST(Ai, InFlightResultFromPreviousSessionIsDiscarded) {
  HttpFixture server({{200,"structured",150}});AiConfig config;config.url=server.url();AiClient client(config);
  auto f=ai_frame();client.reset(f.session,"test-token");client.submit(f,Events{},"old","perception");
  auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
  while(server.hits==0 && std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(5));
  ASSERT_EQ(server.hits,1);f.session="next-match";client.reset(f.session,"next-token");client.submit(f,Events{},"new","perception");
  auto rows=await_result(client);ASSERT_EQ(rows.size(),1u);EXPECT_EQ(rows[0].session,f.session);EXPECT_EQ(rows[0].parsed["session_id"],f.session);
}
