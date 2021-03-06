#include "StreamDeckSDK/EPLJSONUtils.h"
#include "StreamDeckSDK/ESDCommon.h"
#include "DiscordClient.h"

#include <curl/curl.h>
#include <uuid/uuid.h>
#include <functional>
#include <memory>
#include <sstream>

namespace {
bool haveInitializedCurl = false;

std::string urlencode(CURL* curl, const std::string& in) {
  auto encoded = curl_easy_escape(curl, in.c_str(), in.length());
  std::string ret(encoded);
  curl_free(encoded);
  return ret;
}

class AtScopeExit {
 public:
  AtScopeExit(std::function<void()> cb) {
    mCallback = cb;
  }

  ~AtScopeExit() {
    mCallback();
  }

 private:
  std::function<void()> mCallback;
};

#define AT_SCOPE_EXIT(callback) \
  AtScopeExit AT_SCOPE_EXIT_##__COUNTER__(callback)

size_t
curl_write_to_std_string(char* ptr, size_t size, size_t nmemb, void* userdata) {
  std::string* out = reinterpret_cast<std::string*>(userdata);
  *out += std::string(ptr, nmemb);
  return nmemb;
}
}// namespace

std::string DiscordClient::getNextNonce() {
  uuid_t uuid;
  uuid_generate(uuid);
  uuid_string_t uuidString;
  uuid_unparse(uuid, uuidString);
  return std::string(uuidString);
}

DiscordClient::Credentials DiscordClient::getOAuthCredentials(
  const std::string& grantType,
  const std::string& secretType,
  const std::string& secret) {
  if (!haveInitializedCurl) {
    curl_global_init(CURL_GLOBAL_ALL);
    haveInitializedCurl = true;
  }

  CURL* curl = curl_easy_init();
  AT_SCOPE_EXIT([=]() { curl_easy_cleanup(curl); });
  curl_easy_setopt(
    curl, CURLOPT_URL, "https://discordapp.com/api/oauth2/token");
  std::stringstream ss;
  ss << "grant_type=" << urlencode(curl, grantType) << "&"
     << urlencode(curl, secretType) << "=" << urlencode(curl, secret)
     << "&client_id=" << urlencode(curl, mAppId)
     << "&client_secret=" << urlencode(curl, mAppSecret)
     << "&scope=" << urlencode(curl, "rpc");
  const auto postData = ss.str();
  DebugPrint("Sending to discord www api: %s", postData.c_str());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postData.c_str());

  std::string response;
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &curl_write_to_std_string);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

  auto result = curl_easy_perform(curl);
  if (result != CURLE_OK) {
    return Credentials{};
  }

  // I tried using HttpQueryInfo to get Content-Length;
  DebugPrint("HTTP response from discord: %s", response.c_str());
  Credentials out = mCredentials;
  const json parsed = json::parse(response);
  out.accessToken = EPLJSONUtils::GetStringByName(parsed, "access_token");
  out.refreshToken = EPLJSONUtils::GetStringByName(parsed, "refresh_token");
  return out;
}
