// Compile the actual UI with identical UTF-8 options and render both languages.
#include "app.cpp"
#include <iostream>
#include <vector>
#include <stdexcept>
void require(bool ok){if(!ok)throw std::runtime_error("UI localization regression");}
int wmain(int argc,wchar_t** argv){
 require(argc==2);
 require(std::wstring(L"Extração")==L"Extra\u00e7\u00e3o");
 require(std::wstring(L"Português")==L"Portugu\u00eas");
 require(std::wstring(L"… ·")==L"\u2026 \u00b7");
 require(pkg_error_pt("Cannot open selected PKG.")==L"N\u00e3o foi poss\u00edvel abrir o PKG selecionado.");
 require(pkg_error_pt("A folder for this version already exists at the destination. Choose another folder; existing files will not be overwritten.")!=pkg_error_pt("unknown"));
 require(pkg_error_pt("AES stream length mismatch.")!=pkg_error_pt("unknown"));
 require(pkg_error_pt("Absolute or empty package path.")!=pkg_error_pt("unknown"));
 require(pkg_error_pt("Cancelled.")!=pkg_error_pt("unknown"));
 require(pkg_error_pt("Cannot create extracted file.")!=pkg_error_pt("unknown"));
 require(pkg_error_pt("Cannot create temporary extraction folder.")!=pkg_error_pt("unknown"));
 require(pkg_error_pt("Cannot finalize extraction; destination may already exist.")!=pkg_error_pt("unknown"));
 require(pkg_error_pt("Cannot finish extracted file.")!=pkg_error_pt("unknown"));
 require(pkg_error_pt("Cannot open selected PKG.")!=pkg_error_pt("unknown"));
 require(pkg_error_pt("Cannot read PKG file.")!=pkg_error_pt("unknown"));
 require(pkg_error_pt("Choose an existing destination folder.")!=pkg_error_pt("unknown"));
 require(pkg_error_pt("Destination contains a link or junction; choose a regular folder.")!=pkg_error_pt("unknown"));
 require(pkg_error_pt("Disk write failed during extraction.")!=pkg_error_pt("unknown"));
 require(pkg_error_pt("Duplicate or ambiguous filename in PKG.")!=pkg_error_pt("unknown"));
 require(pkg_error_pt("Encrypted PKG range is invalid.")!=pkg_error_pt("unknown"));
 require(pkg_error_pt("Insufficient free disk space at destination.")!=pkg_error_pt("unknown"));
 require(pkg_error_pt("Invalid PARAM.SFO.")!=pkg_error_pt("unknown"));
 require(pkg_error_pt("Invalid PKG content ID.")!=pkg_error_pt("unknown"));
 require(pkg_error_pt("Invalid PKG data table.")!=pkg_error_pt("unknown"));
 require(pkg_error_pt("Invalid PKG file size or offset.")!=pkg_error_pt("unknown"));
 require(pkg_error_pt("Invalid PKG filename range.")!=pkg_error_pt("unknown"));
 require(pkg_error_pt("Invalid SFO key.")!=pkg_error_pt("unknown"));
 require(pkg_error_pt("Invalid SFO table.")!=pkg_error_pt("unknown"));
 require(pkg_error_pt("Invalid SFO value.")!=pkg_error_pt("unknown"));
 require(pkg_error_pt("Invalid UTF-8 package filename.")!=pkg_error_pt("unknown"));
 require(pkg_error_pt("Invalid package filename.")!=pkg_error_pt("unknown"));
 require(pkg_error_pt("Only retail PS3 game PKGs are supported.")!=pkg_error_pt("unknown"));
 require(pkg_error_pt("Only the base PSN game is supported, not updates or add-ons.")!=pkg_error_pt("unknown"));
 require(pkg_error_pt("Only the base PSN release version 01.00 is supported; this is not a compatible base game.")!=pkg_error_pt("unknown"));
 require(pkg_error_pt("PKG and PARAM.SFO title IDs do not match.")!=pkg_error_pt("unknown"));
 require(pkg_error_pt("PKG does not contain PARAM.SFO and USRDIR/EBOOT.BIN.")!=pkg_error_pt("unknown"));
 require(pkg_error_pt("PKG file conflicts with a directory.")!=pkg_error_pt("unknown"));
 require(pkg_error_pt("PKG header is missing.")!=pkg_error_pt("unknown"));
 require(pkg_error_pt("PKG is incomplete or truncated.")!=pkg_error_pt("unknown"));
 require(pkg_error_pt("PKG size does not match its header; download or copy may be incomplete.")!=pkg_error_pt("unknown"));
 require(pkg_error_pt("PKG size overflow.")!=pkg_error_pt("unknown"));
 require(pkg_error_pt("Package filename is too long.")!=pkg_error_pt("unknown"));
 require(pkg_error_pt("Reserved Windows package filename.")!=pkg_error_pt("unknown"));
 require(pkg_error_pt("This PKG is not one of the three supported Code Veronica releases.")!=pkg_error_pt("unknown"));
 require(pkg_error_pt("This file is not a PS3 PKG.")!=pkg_error_pt("unknown"));
 require(pkg_error_pt("Truncated PARAM.SFO.")!=pkg_error_pt("unknown"));
 require(pkg_error_pt("Truncated package metadata.")!=pkg_error_pt("unknown"));
 require(pkg_error_pt("Unsafe package path component.")!=pkg_error_pt("unknown"));
 require(pkg_error_pt("Unsupported PKG file entry type.")!=pkg_error_pt("unknown"));
 require(pkg_error_pt("Unsupported Windows package filename.")!=pkg_error_pt("unknown"));
 require(pkg_error_pt("Unterminated SFO key.")!=pkg_error_pt("unknown"));
 require(pkg_error_pt("Windows encryption operation failed.")!=pkg_error_pt("unknown"));
 GdiplusStartupInput input;require(GdiplusStartup(&gdip,&input,nullptr)==Ok);load_banner();
 HDC dc=CreateCompatibleDC(nullptr);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=820;info.bmiHeader.biHeight=-600;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;void* bits=nullptr;
 HBITMAP bmp=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);auto old=SelectObject(dc,bmp);
 UINT size=0,count=0;GetImageEncodersSize(&count,&size);std::vector<BYTE> bytes(size);auto codecs=(ImageCodecInfo*)bytes.data();GetImageEncoders(count,size,codecs);CLSID png{};for(UINT i=0;i<count;++i)if(wcscmp(codecs[i].MimeType,L"image/png")==0)png=codecs[i].Clsid;
 for(bool en:{false,true}){english=en;require(std::wstring(tr(L"Extrair PKG",L"Extract PKG"))==(en?L"Extract PKG":L"Extrair PKG"));state(L"",0);paint(dc);Bitmap image(bmp,nullptr);auto file=std::filesystem::path(argv[1])/(en?L"extractor-en.png":L"extractor-pt.png");require(image.Save(file.c_str(),&png,nullptr)==Ok);}
 // Exercise the actual click handler: labels select their own language.
 english=true;proc(nullptr,WM_LBUTTONUP,0,MAKELPARAM(50,525));require(!english);
 proc(nullptr,WM_LBUTTONUP,0,MAKELPARAM(210,525));require(english);
 busy=true;proc(nullptr,WM_LBUTTONUP,0,MAKELPARAM(50,525));require(english);busy=false;
 SelectObject(dc,old);DeleteObject(bmp);DeleteDC(dc);banner.reset();GdiplusShutdown(gdip);
 std::cout<<"UTF-8 accents, punctuation, PT/EN selection, extraction lock and actual UI rendering: PASS\n";
}
