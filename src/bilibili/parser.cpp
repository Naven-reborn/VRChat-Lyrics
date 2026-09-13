#include "parser.h"
#include "../net/winhttp_client.h"
#include "json.hpp"
#include <Windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#pragma comment(lib,"bcrypt.lib")

namespace bilibili {
using Json=nlohmann::json;
static const char* Headers=
    "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 Chrome/131.0.0.0 Safari/537.36\r\n"
    "Referer: https://www.bilibili.com/\r\nAccept: application/json\r\n";
static std::string Str(const Json& j,const char* key,const std::string& fallback="") {
    auto p=j.find(key);return p!=j.end() && p->is_string()?p->get<std::string>():fallback;
}
static int64_t Num(const Json& j,const char* key,int64_t fallback=0) {
    auto p=j.find(key);return p!=j.end() && p->is_number_integer()?p->get<int64_t>():fallback;
}
static std::string Match(const std::string& s,const char* expression,int group=1) {
    std::smatch m;return std::regex_search(s,m,std::regex(expression))?m[group].str():"";
}
static std::string Host(const std::string& s) {
    std::string h=Match(s,R"(^https?://([^/:?#]+))");
    std::transform(h.begin(),h.end(),h.begin(),[](unsigned char c){return (char)std::tolower(c);});return h;
}
static bool BiliHost(const std::string& url) {
    const auto h=Host(url);return h=="b23.tv" || h=="bilibili.com" || (h.size()>13 && h.compare(h.size()-13,13,".bilibili.com")==0);
}
static bool MediaUrl(const std::string& url) {
    const auto h=Host(url);
    for(const char* base:{"bilivideo.com","bilivideo.cn","bilibili.com","akamaized.net","cloudfront.net","mcdn.bilivideo.cn"}) {
        const std::string b=base;
        if(h==b || (h.size()>b.size() && h.compare(h.size()-b.size(),b.size(),b)==0 && h[h.size()-b.size()-1]=='.'))return true;
    }
    return false;
}
static int Positive(const std::string& s) {
    if(s.empty() || s.size()>9)return 0;
    try {return std::stoi(s);}catch(...){return 0;}
}
std::string QualityLabel(int q,bool live) {
    if(live){switch(q){case 80:return "流畅";case 150:return "高清";case 250:return "超清";case 400:return "蓝光";case 10000:return "原画";case 15000:return "2K";case 20000:return "4K";case 30000:return "杜比";}}
    else {switch(q){case 16:return "360P";case 32:return "480P";case 64:return "720P";case 74:return "720P60";case 80:return "1080P";case 112:return "1080P 高码率";case 116:return "1080P60";case 120:return "4K";case 125:return "HDR";case 126:return "杜比视界";case 127:return "8K";}}
    return q==0?"自动":std::to_string(q);
}
static void Fail(ParseResult& r,ErrorCode e,const std::string& message){r.ok=false;r.error=e;r.message=message;}
static bool GetJson(const std::string& url,const Transport& t,Json& j,ParseResult& r,bool check_code=true) {
    std::string body;int status=0;
    const bool ok=t.get?t.get(url,Headers,body,status):net::HttpGet(url,Headers,body,status);
    r.http_status=status;
    if(!ok){Fail(r,ErrorCode::Network,"网络请求失败，HTTP "+std::to_string(status)+"；请检查网络或稍后重试。");return false;}
    j=Json::parse(body,nullptr,false);
    if(j.is_discarded() || !j.is_object()){Fail(r,ErrorCode::JsonInvalid,"接口没有返回有效 JSON。");return false;}
    r.api_code=(int)Num(j,"code",-1);
    if(check_code && r.api_code!=0){
        const bool restricted=r.api_code==-101 || r.api_code==-403 || r.api_code==-10403 || r.api_code==-352 || r.api_code==-401;
        Fail(r,restricted?ErrorCode::Restricted:ErrorCode::Api,
            "Bilibili "+std::to_string(r.api_code)+"："+Str(j,"message",Str(j,"msg","请求被拒绝")));
        return false;
    }
    return true;
}
static void AddQuality(ParseResult& r,int q,const std::string& label) {
    if(q<=0)return;
    for(auto& old:r.qualities)if(old.id==q)return;
    r.qualities.push_back({q,label.empty()?QualityLabel(q,r.live):label});
}
static void AddStream(ParseResult& r,std::string url,const std::string& label,const std::string& format) {
    if(url.rfind("//",0)==0)url="https:"+url;
    if(!MediaUrl(url) || url.size()>8192)return;
    for(const auto& s:r.streams)if(s.url==url)return;
    r.streams.push_back({url,label,format});
}
static std::string Md5(const std::string& value) {
    BCRYPT_ALG_HANDLE algorithm=nullptr;unsigned char bytes[16];
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_MD5_ALGORITHM,nullptr,0)<0)return {};
    auto status=BCryptHash(algorithm,nullptr,0,(PUCHAR)value.data(),(ULONG)value.size(),bytes,16);
    BCryptCloseAlgorithmProvider(algorithm,0);if(status<0)return {};
    const char* digits="0123456789abcdef";std::string result;
    for(auto b:bytes){result+=digits[b>>4];result+=digits[b&15];}return result;
}
static std::string WbiQuery(const std::string& bvid,int64_t cid,int quality,const Transport& t) {
    Json nav;ParseResult error;
    if(!GetJson("https://api.bilibili.com/x/web-interface/nav",t,nav,error,false))return {};
    if(!nav.contains("data") || !nav["data"].is_object())return {};
    const Json img=nav["data"].value("wbi_img",Json::object());
    const std::string keys=Match(Str(img,"img_url"),R"(/([a-zA-Z0-9]+)\.[a-z]+)")+
        Match(Str(img,"sub_url"),R"(/([a-zA-Z0-9]+)\.[a-z]+)");
    if(keys.size()!=64)return {};
    constexpr int indices[]={46,47,18,2,53,8,23,32,15,50,10,31,58,3,45,35,27,43,5,49,33,9,42,19,29,28,14,39,12,38,41,13};
    std::string mix;for(int i:indices)mix+=keys[i];
    const auto stamp=std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    // All parameter values here are validated alphanumeric/numeric. Sorted as WBI requires.
    const std::string query="bvid="+bvid+"&cid="+std::to_string(cid)+"&fnval=1&platform=html5&qn="+std::to_string(quality)+"&wts="+std::to_string(stamp);
    return query+"&w_rid="+Md5(query+mix);
}
static void Finish(ParseResult& r,const ParseOptions& options) {
    if(r.streams.empty()){Fail(r,ErrorCode::NoStream,"没有返回可用的完整视频地址。");return;}
    r.url=r.streams.front().url;r.format=r.streams.front().format;r.node=Host(r.url);
    r.quality=QualityLabel(r.actual_quality,r.live);
    r.ok=true;r.error=ErrorCode::None;r.api_code=0;r.message.clear();
    if(options.quality && r.actual_quality!=options.quality)
        r.warning+="所选 "+QualityLabel(options.quality,r.live)+" 未返回，实际为 "+r.quality+"；更高清晰度可能需要登录或会员。";
}
static ParseResult ParseLive(const std::string& room,const ParseOptions& options,const Transport& t) {
    ParseResult r;r.live=true;r.page=0;r.requested_quality=options.quality;r.source_url="https://live.bilibili.com/"+room;
    Json j;
    if(!GetJson("https://api.live.bilibili.com/room/v1/Room/room_init?id="+room,t,j,r))return r;
    const auto data=j.value("data",Json::object());
    const auto id=Num(data,"room_id");
    if(data.value("encrypted",false) || data.value("is_locked",false)){Fail(r,ErrorCode::Restricted,"直播间有访问限制，无法解析。");return r;}
    if(Num(data,"live_status")!=1){Fail(r,ErrorCode::Offline,"该直播间尚未开播或已下播。");return r;}
    if(id<=0){Fail(r,ErrorCode::Api,"直播间编号无效。");return r;}
    r.title="Bilibili 直播 · "+std::to_string(id);
    const int q=options.quality?options.quality:10000;
    const std::string api="https://api.live.bilibili.com/xlive/web-room/v2/index/getRoomPlayInfo?room_id="+std::to_string(id)+
        "&protocol=0,1&format=0,1,2&codec=0&qn="+std::to_string(q)+"&platform=web&ptype=8";
    if(!GetJson(api,t,j,r))return r;
    const Json info=j.value("data",Json::object());
    if(Num(info,"live_status")!=1){Fail(r,ErrorCode::Offline,"该直播间已下播。");return r;}
    const Json play=info.value("playurl_info",Json::object()).value("playurl",Json::object());
    std::map<int,std::string> names;
    for(const auto& a:play.value("g_qn_desc",Json::array()))names[(int)Num(a,"qn")]=Str(a,"desc");
    // Prefer AVC HLS/TS; FLV is a visible alternative, not a silently substituted codec.
    for(const char* want:{"ts","flv","fmp4"})
        for(const auto& protocol:play.value("stream",Json::array()))
            for(const auto& format:protocol.value("format",Json::array())){
                if(Str(format,"format_name")!=want)continue;
                for(const auto& codec:format.value("codec",Json::array())){
                    if(Str(codec,"codec_name")!="avc")continue;
                    for(const auto& a:codec.value("accept_qn",Json::array()))if(a.is_number_integer())AddQuality(r,a.get<int>(),names[a.get<int>()]);
                    if(!r.actual_quality)r.actual_quality=(int)Num(codec,"current_qn");
                    int backup=0;
                    for(const auto& u:codec.value("url_info",Json::array())){
                        std::string name=std::string(want)=="ts"?"HLS":std::string(want)=="flv"?"FLV":"HLS fMP4";
                        AddStream(r,Str(u,"host")+Str(codec,"base_url")+Str(u,"extra"),name+(backup++?" · 备用":" · 主线路"),name);
                    }
                }
            }
    r.warning="直播地址会过期；若播放失败可切换线路或重新解析。播放器需要支持 HLS/FLV。";
    Finish(r,options);return r;
}
ParseResult Parse(const std::string& raw,const ParseOptions& options,const Transport& t) {
    ParseResult r;
    r.requested_quality=options.quality;
    try {
        if(raw.size()>8192){Fail(r,ErrorCode::NoBv,"输入过长。");return r;}
        std::string input=raw;
        const int input_page=Positive(Match(input,R"((?:[?&#]|^)p(?:age)?=(\d+))"));
        std::string url=Match(input,R"((https?://[A-Za-z0-9./_?=&%+#:~-]+))");
        if(url.empty()){
            auto hostpos=input.find("b23.tv/");
            if(hostpos!=std::string::npos && (hostpos==0 || !std::isalnum((unsigned char)input[hostpos-1])))
                url="https://"+Match(input.substr(hostpos),R"((b23\.tv/[A-Za-z0-9/?=&%+#_-]+))");
            if(url.empty() && input.rfind("live.bilibili.com/",0)==0)url="https://"+input;
        }
        if(!url.empty() && !BiliHost(url)){Fail(r,ErrorCode::NoBv,"仅支持 bilibili.com 视频或直播链接、BV/AV 号和 b23.tv 短链。");return r;}
        for(int hop=0;Host(url)=="b23.tv" && hop<5;++hop){
            std::string target;int status=0;
            const bool ok=t.redirect?t.redirect(url,Headers,target,status):net::ResolveRedirect(url,Headers,target,status);
            if(target.rfind("//",0)==0)target="https:"+target;
            if(!ok || status<300 || status>=400 || !BiliHost(target)){Fail(r,ErrorCode::ShortlinkFailed,"短链跳转失败或目标不是 Bilibili。");return r;}
            url=target;
        }
        if(Host(url)=="b23.tv"){Fail(r,ErrorCode::ShortlinkFailed,"短链跳转次数过多。");return r;}
        if(!url.empty())input=url;
        if(Host(url)=="live.bilibili.com"){
            auto room=Match(url,R"(live\.bilibili\.com/(?:blanc/)?([0-9]{1,12})(?:[/?#]|$))");
            if(room.empty()){Fail(r,ErrorCode::NoBv,"请粘贴完整的直播间链接。");return r;}
            return ParseLive(room,options,t);
        }
        r.bvid=Match(input,R"((BV[A-Za-z0-9]{10})(?:[^A-Za-z0-9]|$))");
        const std::string aid=Match(input,R"((?:^|[/\s])av([0-9]{1,12})(?:[/?#\s]|$))");
        if(r.bvid.empty() && aid.empty()){Fail(r,ErrorCode::NoBv,"未找到 BV/AV 号；请粘贴视频或直播间链接。");return r;}
        Json j;
        if(!GetJson("https://api.bilibili.com/x/web-interface/view?"+(r.bvid.empty()?"aid="+aid:"bvid="+r.bvid),t,j,r))return r;
        const Json data=j.value("data",Json::object());
        r.bvid=Str(data,"bvid",r.bvid);r.title=Str(data,"title");
        for(const auto& p:data.value("pages",Json::array())){
            if(r.pages.size()>=2000)break;
            const int number=(int)Num(p,"page");const auto cid=Num(p,"cid");
            if(number>0 && cid>0)r.pages.push_back({number,cid,Str(p,"part"),(int)Num(p,"duration")});
        }
        if(r.pages.empty() && Num(data,"cid")>0)r.pages.push_back({1,Num(data,"cid"),r.title,0});
        r.page=options.page>0?options.page:input_page>0?input_page:std::max(1,Positive(Match(input,R"((?:[?&#]|^)p(?:age)?=(\d+))")));
        const auto selected=std::find_if(r.pages.begin(),r.pages.end(),[&](const Page& p){return p.number==r.page;});
        if(selected==r.pages.end()){Fail(r,ErrorCode::InvalidPage,"所选分集不存在，请从分集列表重新选择。");return r;}
        if(r.pages.size()>1)r.title+=" · P"+std::to_string(r.page)+" "+selected->title;
        r.source_url="https://www.bilibili.com/video/"+r.bvid+"/?p="+std::to_string(r.page);
        const int q=options.quality?options.quality:80;
        std::string endpoint="https://api.bilibili.com/x/player/playurl?bvid="+r.bvid+"&cid="+std::to_string(selected->cid)+
            "&qn="+std::to_string(q)+"&fnval=1&platform=html5";
        if(!GetJson(endpoint,t,j,r)){
            // Deprecated-parameter errors may use the signed endpoint. Do not
            // rotate endpoints/proxies to defeat login, risk or region restrictions.
            if(r.error!=ErrorCode::Api || (r.api_code!=-400 && r.api_code!=-404))return r;
            const auto query=WbiQuery(r.bvid,selected->cid,q,t);
            if(query.empty() || !GetJson("https://api.bilibili.com/x/player/wbi/playurl?"+query,t,j,r))return r;
        }
        const Json play=j.value("data",Json::object());
        r.actual_quality=(int)Num(play,"quality");
        const Json accepted=play.value("accept_quality",Json::array()), descriptions=play.value("accept_description",Json::array());
        for(size_t i=0;i<accepted.size();++i)if(accepted[i].is_number_integer())
            AddQuality(r,accepted[i].get<int>(),i<descriptions.size()&&descriptions[i].is_string()?descriptions[i].get<std::string>():"");
        AddQuality(r,r.actual_quality,"");
        const Json durl=play.value("durl",Json::array());
        int segment=0;
        for(const auto& d:durl){
            ++segment;std::string u=Str(d,"url");
            const std::string path=u.substr(0,u.find('?'));
            const std::string format=path.find(".flv")!=std::string::npos?"FLV":"MP4";
            std::string label=durl.size()>1?"视频段 "+std::to_string(segment):"主线路";
            AddStream(r,u,label,format);
            int backup=0;
            for(const auto& b:d.value("backup_url",Json::array()))if(b.is_string())AddStream(r,b.get<std::string>(),label+" · 备用 "+std::to_string(++backup),format);
        }
        if(durl.size()>1)r.warning="此视频由 "+std::to_string(durl.size())+" 段组成；下方链接每次只包含所选视频段，不能当成完整合集。";
        if(r.streams.empty() && play.contains("dash")){
            Fail(r,ErrorCode::SeparateStreams,"平台仅返回音视频分离流，无法作为 VRChat 的单一完整链接；请降低清晰度或使用原视频页面。");return r;
        }
        Finish(r,options);
    } catch(const std::exception&) {Fail(r,ErrorCode::JsonInvalid,"接口字段发生变化或数据格式错误，请稍后重试。");}
    return r;
}
}
