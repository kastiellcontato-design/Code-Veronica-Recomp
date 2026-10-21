#include "cv_appearance_preview.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace cv_appearance {
namespace {
constexpr unsigned W=512,H=592;
struct Screen {float x,y,z,u,v,light;};
float edge(const Screen& a,const Screen& b,float x,float y){return (x-a.x)*(b.y-a.y)-(y-a.y)*(b.x-a.x);}
uint32_t sample(const PreviewTexture& t,float u,float v,float light){
 if(!t.width||!t.height||t.argb.size()!=size_t(t.width)*t.height)return 0;
 // Repeat addressing with bilinear filtering, matching normal game texture
 // sampling rather than magnifying nearest-neighbour BC1 texels.
 const float fx=(u-std::floor(u))*t.width-.5f,fy=(v-std::floor(v))*t.height-.5f;
 const int x=int(std::floor(fx)),y=int(std::floor(fy));const float sx=fx-x,sy=fy-y;
 auto pixel=[&](int px,int py){px=(px%int(t.width)+int(t.width))%int(t.width);py=(py%int(t.height)+int(t.height))%int(t.height);return t.argb[size_t(py)*t.width+px];};
 const uint32_t colors[]={pixel(x,y),pixel(x+1,y),pixel(x,y+1),pixel(x+1,y+1)};
 const float weights[]={(1-sx)*(1-sy),sx*(1-sy),(1-sx)*sy,sx*sy};
 float alpha=0,channels[3]={};
 for(unsigned i=0;i<4;i++){const float a=float(colors[i]>>24)/255.f;alpha+=weights[i]*a;for(unsigned k=0;k<3;k++)channels[k]+=float((colors[i]>>(k*8))&255)*weights[i]*a;}
 if(alpha<.01f)return 0;
 uint32_t result=uint32_t(std::clamp(alpha*255.f,0.f,255.f))<<24;
 for(unsigned k=0;k<3;k++)result|=uint32_t(std::clamp(channels[k]/alpha*light,0.f,255.f))<<(k*8);
 return result;
}
}
void render_preview(const PreviewModel& model,float yaw,std::vector<rsx_ui_quad>& quads,float x,float y,float width,float height){
 if(model.triangles.empty()||!std::isfinite(yaw)||width<=0||height<=0)return;
 float lo[3]={INFINITY,INFINITY,INFINITY},hi[3]={-INFINITY,-INFINITY,-INFINITY};
 for(const auto& t:model.triangles)for(const auto& v:t.vertices){const float p[]={v.x,v.y,v.z};for(unsigned a=0;a<3;a++){lo[a]=std::min(lo[a],p[a]);hi[a]=std::max(hi[a],p[a]);}}
 const float radius=std::hypot(hi[0]-lo[0],hi[2]-lo[2])*.5f,body_height=hi[1]-lo[1];
 if(radius<=0||body_height<=0)return;
 const float scale=std::min((W-12)/(2*radius),(H-12)/body_height),cs=std::cos(yaw),sn=std::sin(yaw);
 std::vector<float> depth(size_t(W)*H,INFINITY);std::vector<uint32_t> image(size_t(W)*H,0);
 auto transform=[&](const PreviewVertex& v){
  const float px=v.x-(lo[0]+hi[0])*.5f,pz=v.z-(lo[2]+hi[2])*.5f;
  const float nx=v.nx*cs+v.nz*sn,nz=-v.nx*sn+v.nz*cs;
  const float nlen=std::sqrt(v.nx*v.nx+v.ny*v.ny+v.nz*v.nz);
  const float diffuse=nlen>1e-6f?std::max(0.f,(nx*-.4f+v.ny*.5f+nz*-.76f)/nlen):.5f;
  return Screen{W*.5f+(px*cs+pz*sn)*scale,H*.5f-(v.y-(lo[1]+hi[1])*.5f)*scale,-px*sn+pz*cs,v.u,v.v,.48f+.52f*diffuse};
 };
 for(const auto& t:model.triangles){
  if(t.texture>=model.textures.size())continue;Screen a=transform(t.vertices[0]),b=transform(t.vertices[1]),c=transform(t.vertices[2]);
  float area=edge(a,b,c.x,c.y);if(std::fabs(area)<1e-6f)continue;
  int x0=std::max(0,int(std::floor(std::min({a.x,b.x,c.x})))),x1=std::min(int(W)-1,int(std::ceil(std::max({a.x,b.x,c.x}))));
  int y0=std::max(0,int(std::floor(std::min({a.y,b.y,c.y})))),y1=std::min(int(H)-1,int(std::ceil(std::max({a.y,b.y,c.y}))));
  for(int py=y0;py<=y1;py++)for(int px=x0;px<=x1;px++){
   float wa=edge(b,c,px+.5f,py+.5f)/area,wb=edge(c,a,px+.5f,py+.5f)/area,wc=1-wa-wb;if(wa<0||wb<0||wc<0)continue;
   const size_t at=size_t(py)*W+px;const float z=wa*a.z+wb*b.z+wc*c.z;if(z>=depth[at])continue;
   uint32_t color=sample(model.textures[t.texture],wa*a.u+wb*b.u+wc*c.u,wa*a.v+wb*b.v+wc*c.v,wa*a.light+wb*b.light+wc*c.light);
   if(!color)continue;depth[at]=z;image[at]=color;
  }
 }
 // Horizontal runs keep the existing host UI protocol bounded to W*H quads.
 for(unsigned py=0;py<H;py++)for(unsigned px=0;px<W;){
  uint32_t color=image[size_t(py)*W+px];unsigned end=px+1;while(end<W&&image[size_t(py)*W+end]==color)end++;
  if(color)quads.push_back({x+float(px)*width/W,y+float(py)*height/H,float(end-px)*width/W,height/H,color,0});px=end;
 }
}
}
