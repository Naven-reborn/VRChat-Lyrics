#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace bilibili {
enum class ErrorCode { None, NoBv, ShortlinkFailed, Network, Api, NoStream,
    JsonInvalid, InvalidPage, Offline, Restricted, SeparateStreams };
struct Page { int number=1; int64_t cid=0; std::string title; int duration=0; };
struct Quality { int id=0; std::string label; };
struct Stream { std::string url, label, format; };
struct ParseOptions { int page=0; int quality=0; };
struct ParseResult {
    bool ok=false, live=false;
    ErrorCode error=ErrorCode::None;
    int api_code=0, http_status=0;
    std::string bvid,title,url,format,quality,node,message,warning,source_url;
    int page=1, actual_quality=0, requested_quality=0;
    std::vector<Page> pages;
    std::vector<Quality> qualities;
    std::vector<Stream> streams;
};
// Test seam; normal calls use WinHTTP. No cookies or credentials are collected.
struct Transport {
    std::function<bool(const std::string&,const std::string&,std::string&,int&)> get;
    std::function<bool(const std::string&,const std::string&,std::string&,int&)> redirect;
};
ParseResult Parse(const std::string& input, const ParseOptions& options={}, const Transport& transport={});
std::string QualityLabel(int quality,bool live=false);
}
