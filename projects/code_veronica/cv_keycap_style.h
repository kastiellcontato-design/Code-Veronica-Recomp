#pragma once
/* Shared, vector-drawn keyboard artwork. Controller art and mappings are untouched. */
#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <gdiplus.h>

namespace cv_keycap {
inline const wchar_t* font() { return L"Segoe UI"; }
inline void rounded(Gdiplus::GraphicsPath& p, float x, float y, float w, float h, float r) {
    r = std::min(r, std::min(w, h) * 0.5f);
    p.AddArc(x,y,2*r,2*r,180,90); p.AddArc(x+w-2*r,y,2*r,2*r,270,90);
    p.AddArc(x+w-2*r,y+h-2*r,2*r,2*r,0,90); p.AddArc(x,y+h-2*r,2*r,2*r,90,90);
    p.CloseFigure();
}
inline void paint(Gdiplus::Graphics& g, const std::wstring& label,
                  float x, float y, float w, float h, float px, bool lit = false) {
    using namespace Gdiplus;
    if (w < 4 || h < 4) return;
    const auto state = g.Save();
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(PixelOffsetModeHighQuality);
    const float u = std::min(w,h), stroke = std::max(0.65f,u*0.032f);
    const float r = u*0.18f, depth = std::max(1.0f,h*0.075f);
    GraphicsPath base; rounded(base,x+stroke/2,y+stroke/2,w-stroke,h-stroke,r);
    LinearGradientBrush body(PointF(x,y),PointF(x,y+h),Color(255,170,179,190),Color(255,93,104,119));
    g.FillPath(&body,&base);
    Pen edge(lit ? Color(255,226,169,68) : Color(255,60,69,82),stroke);
    g.DrawPath(&edge,&base);
    const float inset = stroke + u*0.025f;
    const RectF face(x+inset,y+inset,w-2*inset,h-2*inset-depth);
    GraphicsPath top; rounded(top,face.X,face.Y,face.Width,face.Height,std::max(1.0f,r-inset));
    LinearGradientBrush fill(PointF(x,face.Y),PointF(x,face.Y+face.Height),Color(255,249,250,252),Color(255,211,218,227));
    g.FillPath(&fill,&top);
    Pen highlight(Color(180,255,255,255),stroke*0.7f); g.DrawPath(&highlight,&top);
    if (!label.empty()) {
        FontFamily family(font());
        GraphicsPath glyph;
        StringFormat format(StringFormat::GenericTypographic());
        format.SetFormatFlags(StringFormatFlagsNoWrap | StringFormatFlagsNoClip);
        const float em = std::max(5.0f, std::min(px,h*0.62f));
        glyph.AddString(label.c_str(),-1,&family,FontStyleBold,em,PointF(0,0),&format);
        RectF bounds; glyph.GetBounds(&bounds);
        if (bounds.Width > 0 && bounds.Height > 0) {
            const float availableW = std::max(1.0f,face.Width-u*0.15f);
            const float availableH = std::max(1.0f,face.Height-u*0.20f);
            const float fit = std::min(1.0f,std::min(availableW/bounds.Width,availableH/bounds.Height));
            Matrix position(fit,0,0,fit,
                face.X+(face.Width-bounds.Width*fit)*0.5f-bounds.X*fit,
                face.Y+(face.Height-bounds.Height*fit)*0.5f-bounds.Y*fit);
            glyph.Transform(&position);
            SolidBrush ink(Color(255,34,42,53)); g.FillPath(&ink,&glyph);
        }
    }
    g.Restore(state);
}
/* The launcher's 21px badges use the same artwork rendered at 4x. Retain the
 * tiny bitmaps: repainting the launcher never rasterizes the letters again. */
inline void draw(Gdiplus::Graphics& g, const std::wstring& label, float x,float y,float w,float h) {
    using namespace Gdiplus;
    using Key = std::tuple<std::wstring,int,int>;
    static std::map<Key,std::unique_ptr<Bitmap>> cache;
    const int W=std::max(1,(int)std::ceil(w*4)), H=std::max(1,(int)std::ceil(h*4));
    Key key(label,W,H); auto it=cache.find(key);
    if (it==cache.end()) {
        auto image=std::make_unique<Bitmap>(W,H,PixelFormat32bppPARGB);
        { Graphics high(image.get()); high.Clear(Color(0,0,0,0)); high.ScaleTransform(4,4);
          paint(high,label,0.75f,0.75f,w-1.5f,h-1.5f,h*0.60f); }
        it=cache.emplace(std::move(key),std::move(image)).first;
    }
    auto saved=g.Save(); g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    g.SetPixelOffsetMode(PixelOffsetModeHighQuality);
    ImageAttributes attrs; attrs.SetWrapMode(WrapModeTileFlipXY);
    g.DrawImage(it->second.get(),RectF(x,y,w,h),0,0,(REAL)W,(REAL)H,UnitPixel,&attrs);
    g.Restore(saved);
}
}
