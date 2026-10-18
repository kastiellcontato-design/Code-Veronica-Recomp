#define NOMINMAX
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include "../cv_keycap_style.h"
#include <vector>
#include <cstdio>

int wmain(int argc, wchar_t** argv) {
    if(argc!=2) return 2;
    ULONG_PTR token; Gdiplus::GdiplusStartupInput init;
    if(Gdiplus::GdiplusStartup(&token,&init,nullptr)!=Gdiplus::Ok) return 3;
    int result=0;
    {
        using namespace Gdiplus;
        Bitmap canvas(1280,900,PixelFormat32bppARGB);
        Graphics g(&canvas); g.Clear(Color(255,18,17,19));
        g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
        Font title(L"Segoe UI",30,FontStyleBold,UnitPixel), text(L"Segoe UI",17,FontStyleRegular,UnitPixel);
        SolidBrush white(Color(255,240,240,242)),dim(Color(255,162,164,171));
        SolidBrush red(Color(255,205,44,50));
        g.DrawString(L"CODE: VERONICA  /  TECLADO",-1,&title,PointF(42,25),&white);
        g.DrawString(L"Novo desenho compartilhado pelo launcher e pelo jogo",-1,&text,PointF(44,72),&dim);
        g.FillRectangle(&red,44,109,1192,2);
        g.DrawString(L"Amostras em tamanho real: launcher (21 px) e jogo (32 px)",-1,&text,PointF(44,130),&dim);
        std::vector<std::wstring> examples={L"K",L"E",L"Q",L"1",L"W",L"S",L"A",L"D",L"R",L"I"};
        for(int i=0;i<(int)examples.size();++i){
            const float x=46+i*113.0f;
            cv_keycap::draw(g,examples[i],x,174,21,21);
            g.DrawString(examples[i].c_str(),-1,&text,PointF(x+28,173),&white);
            cv_keycap::draw(g,examples[i],x,216,32,32);
        }
        g.DrawString(L"Letras, numeros e teclas de funcao",-1,&text,PointF(44,276),&dim);
        std::vector<std::wstring> keys;
        for(wchar_t c=L'A';c<=L'Z';++c)keys.emplace_back(1,c);
        for(wchar_t c=L'0';c<=L'9';++c)keys.emplace_back(1,c);
        for(int i=1;i<=12;++i)keys.push_back(L"F"+std::to_wstring(i));
        for(int i=0;i<(int)keys.size();++i) cv_keycap::draw(g,keys[i],46+(i%16)*74.0f,316+(i/16)*65.0f,48,48);
        g.DrawString(L"Teclas especiais e direcionais",-1,&text,PointF(44,525),&dim);
        std::vector<std::wstring> special={L"Esc",L"Tab",L"Ctrl",L"Alt",L"Shift",L"Enter",L"Space",L"BkSp",L"Home",L"End",L"PgUp",L"PgDn",L"Ins",L"Del",L"Caps",L"Num0",L"LMB",L"RMB",L"MMB",L"\x2190",L"\x2191",L"\x2193",L"\x2192",L"Win"};
        for(int i=0;i<(int)special.size();++i)cv_keycap::draw(g,special[i],46+(i%12)*99.0f,568+(i/12)*72.0f,78,48);
        g.DrawString(L"No manual: o mesmo estilo, com destaque para as teclas mapeadas",-1,&text,PointF(44,735),&dim);
        for(int i=0;i<4;++i) cv_keycap::paint(g,examples[i],46+i*68.0f,778,48,48,29,true);
        g.DrawString(L"Xbox e PlayStation mantidos. Os icones acompanham o seu mapeamento.",-1,&text,PointF(44,853),&dim);
        UINT bytes=0,count=0; GetImageEncodersSize(&count,&bytes);
        std::vector<unsigned char> enc(bytes); auto* codecs=(ImageCodecInfo*)enc.data(); GetImageEncoders(count,bytes,codecs);
        bool saved=false; for(UINT i=0;i<count;++i)if(wcscmp(codecs[i].MimeType,L"image/png")==0){saved=canvas.Save(argv[1],&codecs[i].Clsid,nullptr)==Ok;break;}
        result=saved?0:4;
    }
    // Shared raster cache is process-local; process exit releases its bitmaps.
    if(!result)std::puts("Keyboard icon preview saved.");
    return result;
}
