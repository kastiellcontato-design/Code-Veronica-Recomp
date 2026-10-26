#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shellapi.h>
#include <gdiplus.h>
#include <atomic>
#include <algorithm>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include "pkg_core.h"
#include "error_translation.h"
using namespace Gdiplus;
namespace {
HWND window,pkg_edit,destination_edit;HFONT font;bool english=false;std::atomic<bool> busy=false,cancelled=false;std::thread worker;std::mutex state_mutex;
std::atomic<bool> progress_dirty=false;
std::wstring status,result;double fraction=0;std::unique_ptr<Bitmap> banner;ULONG_PTR gdip=0;
constexpr UINT done_message=WM_APP+1;
const wchar_t* tr(const wchar_t* pt,const wchar_t* en){return english?en:pt;}
std::wstring widen(const std::string& s){int n=MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),nullptr,0);std::wstring out(n,0);MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),out.data(),n);return out;}
std::wstring value(HWND h){int n=GetWindowTextLengthW(h);std::wstring out(n+1,0);GetWindowTextW(h,out.data(),n+1);out.resize(n);return out;}
void state(const std::wstring& s,double f){std::lock_guard<std::mutex> lock(state_mutex);if(status!=s||fraction!=f){status=s;fraction=f;progress_dirty.store(true);}}
void label(Graphics& g,const wchar_t* text,float x,float y,float size,Color color,float width=750){
 FontFamily family(L"Segoe UI");Font f(&family,size,FontStyleRegular,UnitPixel);SolidBrush b(color);StringFormat sf;sf.SetTrimming(StringTrimmingEllipsisCharacter);g.DrawString(text,-1,&f,RectF(x,y,width,60),&sf,&b);
}
void draw_button(Graphics& g,int x,int y,int width,const wchar_t* text,bool accent=false){SolidBrush fill(accent?Color(255,133,23,35):Color(255,39,35,33));g.FillRectangle(&fill,x,y,width,38);Pen border(Color(255,102,87,65));g.DrawRectangle(&border,x,y,width,38);label(g,text,float(x+14),float(y+8),15,Color(255,233,224,205),float(width-20));}
void paint(HDC dc){
 Bitmap buffer(820,600);Graphics g(&buffer);g.SetSmoothingMode(SmoothingModeAntiAlias);g.Clear(Color(255,15,15,16));
 if(banner){g.DrawImage(banner.get(),260,0,560,190);SolidBrush tint(Color(125,0,0,0));g.FillRectangle(&tint,260,0,560,190);}
 SolidBrush panel(Color(255,15,15,16));g.FillRectangle(&panel,0,0,285,190);SolidBrush red(Color(255,157,31,43));g.FillRectangle(&red,0,0,820,3);
 FontFamily serif(L"Georgia");Font title(&serif,32,FontStyleBold,UnitPixel);SolidBrush ivory(Color(255,234,224,202));g.DrawString(L"VERONICA",-1,&title,PointF(28,34),&ivory);
 label(g,L"PKG EXTRACTOR",30,79,20,Color(255,195,57,65));label(g,tr(L"Extração local · CPU",L"Local extraction · CPU"),30,121,13,Color(255,176,162,139));
 label(g,tr(L"Prepare seu jogo para o Veronica Port",L"Prepare your game for Veronica Port"),30,194,21,Color(255,235,225,205));
 label(g,tr(L"Arraste um PKG aqui ou selecione o arquivo abaixo.",L"Drop a PKG here or select a file below."),30,228,14,Color(255,173,164,150));
 label(g,tr(L"ARQUIVO PKG",L"PKG FILE"),30,266,12,Color(255,183,164,128));draw_button(g,654,288,134,tr(L"Selecionar…",L"Browse…"));
 label(g,tr(L"PASTA DE DESTINO",L"DESTINATION FOLDER"),30,340,12,Color(255,183,164,128));draw_button(g,654,362,134,tr(L"Escolher…",L"Choose…"));
 std::wstring s;double f;{std::lock_guard<std::mutex> lock(state_mutex);s=status;f=fraction;}
 if(s.empty())s=tr(L"Compatível: NPEB00553 · NPUB30467 · NPJB00135 | versão 01.00",L"Supported: NPEB00553 · NPUB30467 · NPJB00135 | version 01.00");
 label(g,s.c_str(),30,414,14,Color(255,217,207,184),758);
 SolidBrush track(Color(255,43,39,37));g.FillRectangle(&track,30,478,758,6);g.FillRectangle(&red,30,478,int(758*std::clamp(f,0.0,1.0)),6);
 draw_button(g,30,510,150,L"Português",!english);draw_button(g,194,510,150,L"English",english);draw_button(g,600,510,188,busy?tr(L"Cancelar",L"Cancel"):tr(L"Extrair PKG",L"Extract PKG"),true);
 if(!busy&&!result.empty())draw_button(g,376,510,208,tr(L"Abrir pasta",L"Open folder"));
 label(g,tr(L"Arquivos originais preservados. Nenhum jogo ou licença acompanha a ferramenta.",L"Original files stay unchanged. No game or license is bundled."),30,564,11,Color(255,144,135,120));
 Graphics screen(dc);screen.DrawImage(&buffer,0,0);
}
std::wstring choose_file(){wchar_t file[32768]{};OPENFILENAMEW ofn{sizeof ofn};ofn.hwndOwner=window;ofn.lpstrFile=file;ofn.nMaxFile=32768;ofn.lpstrTitle=tr(L"Selecionar arquivo PKG",L"Select PKG file");ofn.lpstrFilter=tr(L"Pacote de PlayStation 3 (*.pkg)\0*.pkg\0\0",L"PlayStation 3 package (*.pkg)\0*.pkg\0\0");ofn.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;return GetOpenFileNameW(&ofn)?file:L"";}
std::wstring choose_folder(){IFileOpenDialog* dialog=nullptr;std::wstring selected;if(SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog)))){dialog->SetTitle(tr(L"Escolher pasta de destino",L"Choose destination folder"));DWORD flags;dialog->GetOptions(&flags);dialog->SetOptions(flags|FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM|FOS_PATHMUSTEXIST);if(SUCCEEDED(dialog->Show(window))){IShellItem* item=nullptr;if(SUCCEEDED(dialog->GetResult(&item))){PWSTR path=nullptr;if(SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&path))){selected=path;CoTaskMemFree(path);}item->Release();}}dialog->Release();}return selected;}
void start(){
 if(busy){cancelled=true;return;}auto pkg=value(pkg_edit),destination=value(destination_edit);if(pkg.empty()||destination.empty()){state(tr(L"Selecione o PKG e a pasta de destino.",L"Select the PKG and destination folder."),0);return;}
 if(worker.joinable())worker.join();result.clear();cancelled=false;busy=true;EnableWindow(pkg_edit,FALSE);EnableWindow(destination_edit,FALSE);state(tr(L"Conferindo o pacote…",L"Checking package…"),0);const bool en=english;
 worker=std::thread([pkg,destination,en]{
  try{auto output=veronica_pkg::extract(pkg,destination,cancelled,[en](const veronica_pkg::Progress& p){double f=p.total?double(p.done)/p.total:1;state((en?L"Extracting ":L"Extraindo ")+std::to_wstring(int(f*100))+L"%  ·  "+p.file,f);});
   {std::lock_guard<std::mutex> lock(state_mutex);result=output.wstring();status=(en?L"Done! Add this folder to the launcher: ":L"Concluído! Adicione esta pasta ao launcher: ")+output.filename().wstring();fraction=1;}
  }catch(const std::exception& e){bool cancel=cancelled.load();state(cancel?(en?L"Cancelled. Incomplete files were removed.":L"Cancelado. Os arquivos incompletos foram removidos."):(en?L"Extraction failed. See the message.":L"Não foi possível extrair. Veja a mensagem."),0);if(!cancel){auto text=en?widen(e.what()):pkg_error_pt(e.what());PostMessageW(window,done_message,1,(LPARAM)new std::wstring(text));return;}}
  PostMessageW(window,done_message,0,0);
 });
}
LRESULT CALLBACK proc(HWND h,UINT msg,WPARAM w,LPARAM l){
 switch(msg){
 case WM_PAINT:{PAINTSTRUCT p;HDC dc=BeginPaint(h,&p);paint(dc);EndPaint(h,&p);return 0;}
 case WM_ERASEBKGND:return 1;
 case WM_CTLCOLOREDIT:SetTextColor((HDC)w,RGB(235,225,205));SetBkColor((HDC)w,RGB(30,28,28));{static HBRUSH brush=CreateSolidBrush(RGB(30,28,28));return (LRESULT)brush;}
 // Poll only the coalesced progress flag. Idle windows do not repaint.
 // Progress never invalidates the native edit controls or their borders.
 case WM_TIMER:if(progress_dirty.exchange(false)){RECT progress{30,410,790,490};InvalidateRect(h,&progress,FALSE);}return 0;
 case WM_DROPFILES:if(!busy){HDROP drop=(HDROP)w;wchar_t path[32768];if(DragQueryFileW(drop,0,path,32768)){SetWindowTextW(pkg_edit,path);state(L"",0);}DragFinish(drop);}else DragFinish((HDROP)w);return 0;
 case WM_LBUTTONUP:{int x=LOWORD(l),y=HIWORD(l);if(y>=510&&y<548){if(x>=600)start();else if(x>=30&&x<180&&!busy){english=false;state(L"",0);}else if(x>=194&&x<344&&!busy){english=true;state(L"",0);}else if(x>=376&&x<584&&!busy&&!result.empty())ShellExecuteW(h,L"open",result.c_str(),nullptr,nullptr,SW_SHOWNORMAL);}else if(!busy&&x>=654&&x<788){if(y>=288&&y<326){auto p=choose_file();if(!p.empty())SetWindowTextW(pkg_edit,p.c_str());}else if(y>=362&&y<400){auto p=choose_folder();if(!p.empty())SetWindowTextW(destination_edit,p.c_str());}}InvalidateRect(h,nullptr,FALSE);return 0;}
 case done_message:busy=false;EnableWindow(pkg_edit,TRUE);EnableWindow(destination_edit,TRUE);if(w){std::unique_ptr<std::wstring> text((std::wstring*)l);MessageBoxW(h,text->c_str(),L"Veronica PKG Extractor v1.0.1",MB_OK|MB_ICONERROR);}InvalidateRect(h,nullptr,FALSE);return 0;
 case WM_CLOSE:if(busy){cancelled=true;state(tr(L"Cancelando… aguarde a limpeza.",L"Cancelling… waiting for cleanup."),0);return 0;}if(worker.joinable())worker.join();DestroyWindow(h);return 0;
 case WM_DESTROY:PostQuitMessage(0);return 0;
 }return DefWindowProcW(h,msg,w,l);
}
void load_banner(){HRSRC resource=FindResourceW(nullptr,MAKEINTRESOURCEW(101),RT_RCDATA);if(!resource)return;DWORD n=SizeofResource(nullptr,resource);HGLOBAL data=LoadResource(nullptr,resource);if(!data)return;HGLOBAL copy=GlobalAlloc(GMEM_MOVEABLE,n);if(!copy)return;void* p=GlobalLock(copy);memcpy(p,LockResource(data),n);GlobalUnlock(copy);IStream* stream=nullptr;if(SUCCEEDED(CreateStreamOnHGlobal(copy,TRUE,&stream))){std::unique_ptr<Bitmap> image(Bitmap::FromStream(stream));if(image&&image->GetLastStatus()==Ok)banner.reset(image->Clone(0,0,image->GetWidth(),image->GetHeight(),PixelFormat32bppARGB));stream->Release();}else GlobalFree(copy);}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int){
 SetProcessDPIAware();CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);english=PRIMARYLANGID(GetUserDefaultUILanguage())!=LANG_PORTUGUESE;GdiplusStartupInput startup;GdiplusStartup(&gdip,&startup,nullptr);load_banner();
 WNDCLASSEXW cls{sizeof cls};cls.lpfnWndProc=proc;cls.hInstance=instance;cls.lpszClassName=L"VeronicaPkgExtractor";cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(1));RegisterClassExW(&cls);
 RECT rect{0,0,820,600};AdjustWindowRect(&rect,WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,FALSE);
 window=CreateWindowExW(WS_EX_ACCEPTFILES,cls.lpszClassName,L"Veronica PKG Extractor v1.0.1",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,rect.right-rect.left,rect.bottom-rect.top,nullptr,nullptr,instance,nullptr);
 if(!window)return 1;font=CreateFontW(-16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
 pkg_edit=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL|WS_TABSTOP,30,288,608,38,window,(HMENU)1,instance,nullptr);destination_edit=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL|WS_TABSTOP,30,362,608,38,window,(HMENU)2,instance,nullptr);SendMessageW(pkg_edit,WM_SETFONT,(WPARAM)font,TRUE);SendMessageW(destination_edit,WM_SETFONT,(WPARAM)font,TRUE);
 wchar_t self[32768];GetModuleFileNameW(nullptr,self,32768);SetWindowTextW(destination_edit,std::filesystem::path(self).parent_path().c_str());int argc=0;LPWSTR* argv=CommandLineToArgvW(GetCommandLineW(),&argc);if(argc>1)SetWindowTextW(pkg_edit,argv[1]);LocalFree(argv);
 ShowWindow(window,SW_SHOWNORMAL);SetTimer(window,1,100,nullptr);MSG msg;while(GetMessageW(&msg,nullptr,0,0)>0){if(!IsDialogMessageW(window,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
 banner.reset();DeleteObject(font);GdiplusShutdown(gdip);CoUninitialize();return 0;
}
