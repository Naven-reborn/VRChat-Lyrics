// Test-build-only offscreen integration renderer. No OSC, audio, network, desktop
// capture or user configuration is touched. PNGs come from our D3D render target.
#include "ui_test.h"
#include "menu/menu.h"
#include "menu/lyric_motion.h"
#include "menu/ui_motion.h"
#include "menu/title_glitch.h"
#include "bilibili/parser_checks.h"
#include "util/image.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "backends/imgui_impl_dx11.h"
#include <d3d11.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <stdexcept>
#include <cstring>

using Microsoft::WRL::ComPtr;
namespace host {
static void Check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
static void SavePng(ID3D11Device* dev, ID3D11DeviceContext* ctx, ID3D11Texture2D* source,
                    const std::filesystem::path& path) {
    D3D11_TEXTURE2D_DESC desc; source->GetDesc(&desc);
    desc.Usage=D3D11_USAGE_STAGING; desc.BindFlags=0; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> stage;
    Check(SUCCEEDED(dev->CreateTexture2D(&desc,nullptr,&stage)),"staging texture");
    ctx->CopyResource(stage.Get(),source);
    D3D11_MAPPED_SUBRESOURCE map{};
    Check(SUCCEEDED(ctx->Map(stage.Get(),0,D3D11_MAP_READ,0,&map)),"readback");
    std::vector<unsigned char> pixels(desc.Width*desc.Height*4);
    for (UINT y=0; y<desc.Height; ++y) {
        memcpy(pixels.data()+y*desc.Width*4,(unsigned char*)map.pData+y*map.RowPitch,desc.Width*4);
        for (UINT x=0;x<desc.Width;++x) pixels[(y*desc.Width+x)*4+3]=255;
    }
    ctx->Unmap(stage.Get(),0);
    ComPtr<IWICImagingFactory> factory;
    Check(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory))),"WIC");
    ComPtr<IWICStream> stream; factory->CreateStream(&stream);
    Check(SUCCEEDED(stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE)),"PNG path");
    ComPtr<IWICBitmapEncoder> encoder; factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder);
    encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache);
    ComPtr<IWICBitmapFrameEncode> frame; encoder->CreateNewFrame(&frame,nullptr);
    frame->Initialize(nullptr); frame->SetSize(desc.Width,desc.Height);
    WICPixelFormatGUID format=GUID_WICPixelFormat32bppRGBA;
    Check(SUCCEEDED(frame->SetPixelFormat(&format)),"PNG format");
    if (IsEqualGUID(format, GUID_WICPixelFormat32bppBGRA)) {
        for (size_t p=0;p<pixels.size();p+=4) std::swap(pixels[p],pixels[p+2]);
    } else Check(IsEqualGUID(format,GUID_WICPixelFormat32bppRGBA),"unexpected PNG pixel format");
    Check(SUCCEEDED(frame->WritePixels(desc.Height,desc.Width*4,(UINT)pixels.size(),pixels.data())),"PNG pixels");
    Check(SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit()),"PNG commit");
}

int RunUiCapture(const wchar_t* args) {
    const auto output=std::filesystem::current_path()/"captures";
    std::filesystem::create_directories(output);
    std::ofstream report(output/"checks.txt");
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    try {
        bilibili::ParserChecks(report);
        for(int cycle=0;cycle<20;++cycle){
            const double start=cycle*3.0;
            Check(menu::motion::TitleGlitch(start+.3,12345).strength>.99f,"glitch active second");
            Check(menu::motion::TitleGlitch(start+1.01,12345).strength==0 &&
                menu::motion::TitleGlitch(start+2.99,12345).strength==0,"two clean seconds");
            const auto pattern=menu::motion::TitleGlitch(start+.65,12345);
            float previous=1.f;
            for(int i=66;i<100;++i){
                auto f=menu::motion::TitleGlitch(start+i/100.0,12345);
                Check(f.strength<=previous+.00001f,"glitch recovery monotonic");
                Check(f.count==pattern.count,"recovery freezes tear count");
                for(int n=0;n<f.count;++n)Check(f.tears[n].y==pattern.tears[n].y,"recovery freezes tear positions");
                previous=f.strength;
            }
        }
        int patterns_with_tears=0;
        for(int i=0;i<120;++i){
            auto f=menu::motion::TitleGlitch((i/8)*3.0+.1+(i%8)*.07,98123);
            Check(f.count>=0 && f.count<=2,"sparse tear limit");
            patterns_with_tears+=f.count>0;
            for(int n=0;n<f.count;++n)Check(f.tears[n].x+f.tears[n].width<=1.f && f.tears[n].height<.07f,"local thin tears");
        }
        Check(patterns_with_tears>0 && patterns_with_tears<120,"tear count varies including clean intervals");
        Check(menu::motion::TitleGlitch(.3,1,false).strength==0,"glitch disabled with interface motion");
        Check(menu::motion::TitleGlitch(.3,1).jitter!=menu::motion::TitleGlitch(3.3,1).jitter,"different cycles use different patterns");
        report<<"PASS: 3s glitch cycle, 1s active, sparse random tears, frozen monotonic recovery and reduced motion\n";
        for(int hz:{30,60,144}){
            menu::motion::SoftPress spring;
            for(int i=0;i<hz;++i)spring.Tick(true,1.f/hz);
            Check(std::abs(spring.scale-.975f)<.0001f,"soft press compression");
            float max_scale=0;
            for(int i=0;i<hz;++i)max_scale=std::max(max_scale,spring.Tick(false,1.f/hz));
            Check(spring.scale==1.f && max_scale>1.f && max_scale<=1.007f,"soft press rebound");
        }
        report<<"PASS: approved 2.5% soft press and bounded rebound at 30/60/144 Hz\n";
        menu::LyricMotion motion;
        motion.Update("song","first",1000,1.f/60,true);
        Check(motion.outgoing.empty() && motion.Progress()==1.f,"first row must not fly in");
        motion.Update("song","second",1100,1.f/60,true);
        Check(motion.outgoing=="first" && motion.Progress()<1.f,"row transition");
        for(int i=0;i<45;++i) motion.Update("song","second",1100,1.f/60,true);
        Check(motion.outgoing.empty(),"transition settles while paused");
        motion.Update("song","seek",9000,1.f/60,true);
        Check(motion.outgoing.empty(),"seek snap");
        motion.Update("new song","new",0,1.f/60,true);
        Check(motion.outgoing.empty(),"song reset");
        motion.Update("new song","reduced",100,1.f/60,false);
        Check(motion.Progress()==1.f && motion.outgoing.empty(),"reduced motion");
        report << "PASS: first row / transition / pause / seek / song reset / reduced motion\n";
        for (int hz : {30,60,144}) {
            menu::motion::Tween t;t.Reset(0.f);
            float previous=0;
            for(int i=0;i<hz;++i) {
                float v=t.Tick(1.f,1.f/hz,.20f);
                Check(v>=previous && v<=1.f,"monotonic UI tween");previous=v;
            }
            Check(t.value==1.f,"UI tween settles at every frame rate");
            t.Tick(0.f,1.f/hz,.2f); const float before=t.value;
            Check(t.Tick(1.f,0.f,.2f)==before,"continuous tween reversal");
            Check(t.Tick(0.f,.001f,.2f,false)==0.f,"UI reduced motion snap");
        }
        report<<"PASS: finite UI motion at 30/60/144 Hz, reversal, reduced motion\n";

        ComPtr<ID3D11Device> dev; ComPtr<ID3D11DeviceContext> ctx;
        D3D_FEATURE_LEVEL level;
        Check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,
            D3D11_SDK_VERSION,&dev,&level,&ctx)),"D3D WARP");
        ImGui::CreateContext();
        auto& io=ImGui::GetIO(); io.IniFilename=nullptr; io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
        menu::ui_scale=1.f; menu::LoadFontsAndStyle();
        Check(ImGui_ImplDX11_Init(dev.Get(),ctx.Get()),"DX11 backend");
        menu::State state; state.language=i18n::Lang::SC; state.theme=menu::Theme::Blur;
        state.blur_opacity=55; menu::SnapTheme(state.theme,state.blur_opacity);
        state.np_detected=true; state.np_playing=true; state.np_has_lyrics=true;
        state.np_track_key="test-money"; state.np_pos_ms=103000; state.np_dur_ms=233000; state.np_source=1;
        strcpy_s(state.np_title,"Money"); strcpy_s(state.np_artist,"The Drums"); strcpy_s(state.np_album,"Portamento");
        strcpy_s(state.np_ncm_id,"19526900");
        strcpy_s(state.np_current_line,"No I don't have any money\n没有 我没钱");
        state.fmt_with_lyrics.enabled[0]=false; state.fmt_with_lyrics.enabled[1]=false;
        // Optional user-supplied cover is a fixture, never bundled album artwork.
        ComPtr<ID3D11ShaderResourceView> cover;
        std::ifstream art("cover.png",std::ios::binary);
        if(art) { std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(art)),{});
            cover.Attach(util::CreateCircularTexture(dev.Get(),bytes.data(),bytes.size(),160,false));
            state.cover_square_srv=cover.Get(); }
        int width=1000,height=680;
        ComPtr<ID3D11Texture2D> target; ComPtr<ID3D11RenderTargetView> rtv;
        auto resize=[&](int w,int h) {
            width=w;height=h; rtv.Reset();target.Reset();
            D3D11_TEXTURE2D_DESC d{}; d.Width=w;d.Height=h;d.MipLevels=1;d.ArraySize=1;
            d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;d.SampleDesc.Count=1;d.BindFlags=D3D11_BIND_RENDER_TARGET;
            Check(SUCCEEDED(dev->CreateTexture2D(&d,nullptr,&target)),"target");
            Check(SUCCEEDED(dev->CreateRenderTargetView(target.Get(),nullptr,&rtv)),"render view");
        };
        resize(width,height);
        auto render=[&](const char* filename=nullptr) {
            io.DisplaySize=ImVec2((float)width,(float)height); io.DeltaTime=1.f/60;
            ImGui_ImplDX11_NewFrame(); ImGui::NewFrame(); menu::Draw(state,width,height); ImGui::Render();
            const float clear[]={.18f,.19f,.20f,1};
            ID3D11RenderTargetView* view=rtv.Get();ctx->OMSetRenderTargets(1,&view,nullptr);
            ctx->ClearRenderTargetView(view,clear); ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            if(filename) SavePng(dev.Get(),ctx.Get(),target.Get(),output/filename);
        };
        for(int i=0;i<8;++i) render(); render("01-lyrics.png");
        const float cover_start=state.cover_angle;
        for(int i=0;i<12;++i)render();
        Check(state.cover_angle>cover_start,"album rotates during playback");
        state.np_playing=false;const float paused_angle=state.cover_angle;render();
        Check(state.cover_angle==paused_angle,"album rotation pauses with music");
        state.np_playing=true;state.ui_motion=false;render();
        Check(state.cover_angle==paused_angle,"album respects reduced interface motion");
        state.ui_motion=true;
        Check(menu::font_logo->LegacySize==24.f && menu::font_logo->CalcTextSizeA(24.f,FLT_MAX,0.f,"VRC LYRICS").x<190.f,
            "larger wordmark fits centered sidebar");
        report<<"PASS: album rotation/play/pause/reduced motion and centered 24px wordmark\n";
        strcpy_s(state.np_current_line,"动画测试：下一句歌词\n换行时平滑上移与淡入");
        for(int i=0;i<36;++i) { state.np_pos_ms+=16; char filename[64];
            sprintf_s(filename,"motion-%02d.png",i); render(filename); }
        state.np_playing=false;render("02-paused.png");
        strcpy_s(state.np_current_line,"这是一段用于检查窄窗口自动换行的很长的歌词，文字不应该遮住进度条或发送按钮。\nThis is a long translation used to check wrapping and the available space.");
        state.lyric_motion=false;resize(780,720);for(int i=0;i<8;++i)render();render("03-narrow-long.png");
        resize(1000,680);state.np_detected=false;state.np_has_lyrics=false;state.np_current_line[0]=0;
        state.np_track_key.clear();state.np_pos_ms=0;state.np_dur_ms=0;state.cover_square_srv=nullptr;
        for(int i=0;i<8;++i)render();render("04-empty.png");
        // Feed input into this private ImGui context, never Windows SendInput.
        state.np_detected=true;state.np_has_lyrics=true;state.np_playing=true;state.np_track_key="test-money";
        state.np_pos_ms=103000;state.np_dur_ms=233000;
        strcpy_s(state.np_current_line,"No I don't have any money\n没有 我没钱");
        for(int i=0;i<8;++i)render();
        io.AddMousePosEvent(870,540);render();io.AddMouseButtonEvent(0,true);render();io.AddMouseButtonEvent(0,false);render();
        Check(state.service_running,"send button click");
        io.AddMouseButtonEvent(0,true);render();io.AddMouseButtonEvent(0,false);render();
        Check(!state.service_running,"stop button click");
        report<<"PASS: start / stop click (local state only, no network)\n";
        io.AddMousePosEvent(860,590);render();io.AddMouseButtonEvent(0,true);render();io.AddMouseButtonEvent(0,false);render();
        Check(!state.send_while_paused,"pause toggle click");
        state.send_while_paused=true;
        io.AddMousePosEvent(680,638);render();io.AddMouseButtonEvent(0,true);render();io.AddMouseButtonEvent(0,false);render();
        Check(state.current_tab==menu::Tab::Settings,"format link navigation");
        for(int i=0;i<60;++i)render();render("05-format-settings.png");
        const auto* content=ImGui::FindWindowByName("##content");
        Check(content && content->Scroll.y>100,"format scroll anchor");
        report<<"PASS: format link navigates to Settings and scrolls to format builder\n";
        state.current_tab=menu::Tab::Lyrics;state.theme=menu::Theme::Light;menu::SnapTheme(state.theme);
        for(int i=0;i<8;++i)render();render("06-light.png");
        state.theme=menu::Theme::Dark;menu::SnapTheme(state.theme);for(int i=0;i<8;++i)render();render("07-dark.png");
        for(int tab=0;tab<5;++tab) {
            io.AddMousePosEvent(75,72.f+tab*40.f);render();
            io.AddMouseButtonEvent(0,true);render();io.AddMouseButtonEvent(0,false);render();
            Check((int)state.current_tab==tab,"sidebar navigation");
            float layout_width=-1.f;
            for(int i=0;i<30;++i) {
                render();
                const auto* page=ImGui::FindWindowByName("##content");
                if(layout_width<0)layout_width=page->WorkRect.GetWidth();
                Check(std::abs(page->WorkRect.GetWidth()-layout_width)<.01f,"page transition must not reflow content");
                if(tab==1 && (i==0||i==5||i==12||i==29)) {
                    char frame_name[64];sprintf_s(frame_name,"page-transition-%02d.png",i);
                    SavePng(dev.Get(),ctx.Get(),target.Get(),output/frame_name);
                }
            }
        }
        menu::interface_motion_enabled=true;menu::SnapTheme(menu::Theme::Dark);
        menu::BeginThemeTransition(menu::Theme::Dark,menu::Theme::Light,55);
        menu::TickThemeTransition(.08f);
        const ImVec4 before=menu::col::bg_content;
        menu::BeginThemeTransition(menu::Theme::Light,menu::Theme::Blur,55);
        Check(std::abs(before.x-menu::col::bg_content.x)<.001f && std::abs(before.y-menu::col::bg_content.y)<.001f,
            "rapid theme reversal continuity");
        menu::TickThemeTransition(1.f);
        state.theme=menu::Theme::Blur; menu::SnapTheme(state.theme);
        state.current_tab=menu::Tab::Lyrics;render();state.current_tab=menu::Tab::Settings;
        for(int i=0;i<40;++i)render();
        io.AddMousePosEvent(500,143);render();io.AddMouseButtonEvent(0,true);render();io.AddMouseButtonEvent(0,false);render();
        for(int i=0;i<15;++i)render();render("08-dropdown.png");
        auto open_panels=[] {
            int count=0;
            for(const auto* window:GImGui->Windows)
                if(window->Active && strstr(window->Name,"##nlcombo_panel_")) ++count;
            return count;
        };
        Check(open_panels()==1,"dropdown opens");
        io.AddKeyEvent(ImGuiKey_Escape,true);render();io.AddKeyEvent(ImGuiKey_Escape,false);
        for(int i=0;i<15;++i)render();
        Check(open_panels()==0,"dropdown Escape closes completely");
        report<<"PASS: dropdown open and Escape close\n";
        state.ui_motion=false;state.current_tab=menu::Tab::Lyrics;state.tab_transition=0;render();
        Check(state.tab_transition==1.f,"UI motion disabled page");
        report<<"PASS: fixed page width, rapid theme retarget, reduced UI motion\n";
        state.current_tab=menu::Tab::Video;state.tab_transition=1;
        strcpy_s(state.video_input,"https://www.bilibili.com/video/BV1xx411c7mu/");
        state.video_input_changed=false;state.video_status=2;
        state.video_result.ok=true;state.video_result.title="视频解析示例 · P2 第二集";
        state.video_result.page=2;state.video_page_index=1;
        state.video_result.quality="1080P";state.video_result.actual_quality=80;
        state.video_result.pages={{1,101,"第一集",60},{2,102,"第二集",90}};
        state.video_result.qualities={{80,"1080P"},{64,"720P"},{16,"360P"}};
        state.video_result.streams={{"https://test.bilivideo.com/fixture.mp4","主线路","MP4"},
            {"https://backup.bilivideo.com/fixture.mp4","备用线路","MP4"}};
        state.video_result.source_url=state.video_input;
        strcpy_s(state.video_result_url,"https://test.bilivideo.com/fixture.mp4");
        for(int i=0;i<25;++i)render();render("09-video-options.png");
        state.ui_motion=true;
        for(int i=3;i<=120;++i)state.video_result.pages.push_back({i,100+i,"分集 "+std::to_string(i),90});
        state.video_page_index=97;
        for(int i=0;i<8;++i)render();
        io.AddMousePosEvent(500,190);render();io.AddMouseButtonEvent(0,true);render();io.AddMouseButtonEvent(0,false);render();
        for(int i=0;i<18;++i)render();render("11-glass-episodes.png");
        ImGuiWindow* list=nullptr;
        for(auto* window:GImGui->Windows)if(window->Active && strstr(window->Name,"##nlcombo_panel_"))list=window;
        Check(list && list->Size.y<=267 && list->Pos.y>=0 && list->Pos.y+list->Size.y<=height,"episode popup bounded");
        Check(list->ScrollMax.y>2000 && list->Scroll.y>2000,"long list scrolls to current episode");
        Check(menu::col::bg_popup.w<1.f && menu::col::bg_popup.x>.15f,"glass uses distinct translucent material");
        const float before_scroll=list->Scroll.y;
        io.AddMousePosEvent(list->Pos.x+100,list->Pos.y+100);render();io.AddMouseWheelEvent(0,-3);render();
        for(int i=0;i<10;++i)render();
        Check(list->Scroll.y>before_scroll,"wheel scroll in long dropdown");
        const int old_episode=state.video_page_index;
        io.AddMousePosEvent(list->Pos.x+100,list->Pos.y+60);render();
        io.AddMouseButtonEvent(0,true);render();io.AddMouseButtonEvent(0,false);render();
        Check(state.video_page_index!=old_episode && state.video_result_url[0]==0 && state.video_status==0,"custom episode selection invalidates old URL");
        for(int i=0;i<15;++i)render();
        io.AddMousePosEvent(500,190);render();io.AddMouseButtonEvent(0,true);render();io.AddMouseButtonEvent(0,false);render();
        for(int i=0;i<15;++i)render();
        for(const auto theme:{menu::Theme::Dark,menu::Theme::Light}){
            state.theme=theme;menu::SnapTheme(theme);for(int i=0;i<5;++i)render();
            Check(menu::col::bg_popup.w==1.f,"solid themes stay opaque");
            render(theme==menu::Theme::Dark?"12-dark-episodes.png":"13-light-episodes.png");
        }
        state.theme=menu::Theme::Blur;menu::SnapTheme(state.theme);
        io.AddKeyEvent(ImGuiKey_Escape,true);render();io.AddKeyEvent(ImGuiKey_Escape,false);
        for(int i=0;i<15;++i)render();Check(open_panels()==0,"episode popup closes");
        report<<"PASS: shared custom video dropdown, 120 episodes, initial selection reveal, wheel, theme materials and Escape\n";
        state.video_status=3;state.video_result.message="该直播间尚未开播或已下播。";
        state.video_result.live=true;state.video_result.pages.clear();state.video_result.qualities.clear();
        for(int i=0;i<8;++i)render();render("10-live-offline.png");
        report<<"PASS: pause toggle and all five sidebar destinations\n";
        report<<"PASS: offscreen renders, light/dark/acrylic palette, long text, empty, paused\n";
        state.current_tab=menu::Tab::Lyrics;state.ui_motion=true;state.tab_transition=1;
        const double sample_times[]={.16,.50,.70,.85,.97,1.1,2.9,3.16};
        for(int i=0;i<8;++i){
            GImGui->Time=sample_times[i]-1.0/60.0;
            char filename[64];sprintf_s(filename,"title-glitch-%02d.png",i);
            render(filename);
        }
        ImGui_ImplDX11_Shutdown();ImGui::DestroyContext();
        return 0;
    } catch(const std::exception& e) { report<<"FAIL: "<<e.what()<<'\n'; return 1; }
}
}
