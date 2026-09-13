#pragma once
#include "parser.h"
#include "../net/winhttp_client.h"
#include <fstream>
#include <stdexcept>

namespace bilibili {
inline void ParserChecks(std::ostream& report) {
    auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    const std::string view=R"({"code":0,"data":{"bvid":"BV1xx411c7mu","title":"Fixture","pages":[{"page":1,"cid":101,"part":"First"},{"page":2,"cid":102,"part":"Second"}]}})";
    std::string play=R"({"code":0,"data":{"quality":16,"accept_quality":[80,64,16],"accept_description":["1080P","720P","360P"],"durl":[{"url":"https://test.bilivideo.com/movie.mp4?token=fixture","backup_url":["https://backup.bilivideo.com/movie.mp4?token=fixture"]}]}})";
    std::string requested;
    int gets=0;
    Transport transport;
    transport.get=[&](const std::string& url,const std::string&,std::string& body,int& status){
        ++gets;status=200;
        if(url.find("/view?")!=std::string::npos)body=view;
        else {requested=url;body=play;}return true;
    };
    transport.redirect=[](const std::string&,const std::string&,std::string& target,int& status){
        status=302;target="https://www.bilibili.com/video/BV1xx411c7mu/?p=2";return true;
    };
    auto result=Parse("https://www.bilibili.com/video/BV1xx411c7mu/?p=2",{0,64},transport);
    check(result.ok&&result.page==2&&result.pages.size()==2,"Bili page list");
    check(requested.find("cid=102")!=std::string::npos&&requested.find("qn=64")!=std::string::npos,"Bili selected page/quality request");
    check(result.actual_quality==16&&!result.warning.empty()&&result.streams.size()==2,"Bili quality downgrade / backup");
    check(Parse("BV1xx411c7mu",{3,0},transport).error==ErrorCode::InvalidPage,"Bili invalid page must not clamp silently");
    check(Parse("分享：https://b23.tv/fixture?p=1",{},transport).page==1,"Bili explicit p=1 beats redirect p=2");
    check(Parse("分享：https://b23.tv/fixture",{},transport).page==2,"Bili shared short link");
    const int previous_gets=gets;
    check(!Parse("https://bilibili.com.evil.example/video/BV1xx411c7mu",{},transport).ok&&gets==previous_gets,"Bili rejects unrelated hosts");
    check(!Parse("file:///private",{},transport).ok,"Bili invalid input");
    play=R"({"code":0,"data":{"quality":80,"dash":{"video":[{"baseUrl":"https://test.bilivideo.com/video.m4s"}]}}})";
    check(Parse("BV1xx411c7mu",{},transport).error==ErrorCode::SeparateStreams,"Bili must not return silent DASH as muxed");
    play=R"({"code":-403,"message":"Access denied"})";
    check(Parse("BV1xx411c7mu",{},transport).error==ErrorCode::Restricted,"Bili access restriction surfaced");
    play="not json";
    check(Parse("BV1xx411c7mu",{},transport).error==ErrorCode::JsonInvalid,"Bili malformed JSON");
    transport.get=[](const std::string&,const std::string&,std::string& body,int& status){status=200;body=R"({"code":0,"data":{"room_id":123,"live_status":0}})";return true;};
    check(Parse("https://live.bilibili.com/6",{},transport).error==ErrorCode::Offline,"Bili offline room");
    check(QualityLabel(112)=="1080P 高码率","Bili quality 112 is not 1440P");
    report<<"PASS: Bili page/quality selection, downgrade, backup, short links, invalid page, hostile host, DASH guard, restriction, malformed JSON, offline\n";
}
inline int NetworkChecks() {
    std::ofstream log("bilibili-network-checks.txt");
    int failed=0;
    for(const auto& sample:std::vector<std::pair<std::string,ParseOptions>>{
        {"BV1xx411c7mu",{0,80}}, {"BV1xx411c7mu",{0,64}},
        {"https://www.bilibili.com/video/BV1bK411W797/?p=2",{0,16}},
        {"https://live.bilibili.com/6",{0,250}}}){
        const auto result=Parse(sample.first,sample.second);
        log<<sample.first<<" qn="<<sample.second.quality<<" ok="<<result.ok<<" page="<<result.page
            <<" pages="<<result.pages.size()<<" actual="<<result.actual_quality<<" format="<<result.format
            <<" streams="<<result.streams.size()<<" error="<<result.message<<'\n';
        if(!result.ok)++failed;
        if(result.ok&&result.live){
            std::string body;int status=0;
            const bool hls=net::HttpGet(result.url,"",body,status)&&body.rfind("#EXTM3U",0)==0;
            log<<"HLS manifest without Referer: "<<hls<<" HTTP "<<status<<'\n';if(!hls)++failed;
        }
        if(result.ok&&!result.live){
            std::string body;int status=0;
            const bool reachable=net::HttpGet(result.url,"Range: bytes=0-511\r\n",body,status)&&status==206;
            log<<"Media range without Referer: "<<reachable<<" HTTP "<<status<<" bytes="<<body.size()<<'\n';if(!reachable)++failed;
        }
    }
    return failed?1:0;
}
}
