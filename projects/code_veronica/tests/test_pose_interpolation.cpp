#include "rsx_pose_interpolation.h"
#include <cassert>
#include <cstdio>
#include <vector>
#include <chrono>

static void camera(float* m) {
    memset(m,0,32*sizeof(float));m[0]=m[5]=m[10]=m[15]=1;m[19]=m[26]=1;
}
static bool near(float a,float b) {return fabsf(a-b)<.0001f;}
struct Fixture {
    RsxPoseHistory h{};
    RsxPoseVertex v[32]{};float matrices[8*32]{};
    RsxPoseDraw d[8]{};RsxPoseFrame f{v,matrices,d,32,1,1,1};
    Fixture() {
        assert(rsx_pose_init(&h,32,8));
        for(int i=0;i<8;i++)camera(matrices+i*32);
        for(int i=0;i<32;i++){v[i].xyz[0]=float(i);v[i].stamp=1;v[i].uv=123;}
        d[0]={1,0,3,0,0};
    }
    ~Fixture(){rsx_pose_destroy(&h);}
    void move(unsigned first,float amount) {for(unsigned i=first;i<first+3;i++)v[i].xyz[0]+=amount;}
    float x(unsigned draw=0,unsigned vertex=0) {float xyz[3];rsx_pose_vertex(&h,draw,vertex,xyz);return xyz[0];}
};
static void cadence_and_relocation() {
    Fixture a;
    assert(!rsx_pose_prepare(&a.h,&a.f,0));assert(a.h.reason==RSX_POSE_WARMUP);
    a.move(0,1);auto before=a.v[0];
    assert(rsx_pose_prepare(&a.h,&a.f,1./30));assert(near(a.x(),.5f));
    assert(a.h.reason==RSX_POSE_NEW&&a.h.matched==1);
    rsx_pose_prepare(&a.h,&a.f,1./20);assert(near(a.x(),1));
    assert(a.h.reason==RSX_POSE_REPEAT&&memcmp(&before,&a.v[0],sizeof before)==0);
    // Move CPU bank/ranges without losing the mesh's two poses.
    for(int i=0;i<3;i++){a.v[i+10]=a.v[i];a.v[i+10].xyz[0]+=1;}
    a.d[0].first=10;a.d[0].cb=2;
    assert(rsx_pose_prepare(&a.h,&a.f,2./30));assert(near(a.x(),1.5f));
    rsx_pose_prepare(&a.h,&a.f,2./30+1./60);assert(near(a.x(),2));
    // 60 visible samples follow a steady 30 Hz motion at its original speed.
    for(int step=3;step<303;step++) {
        a.move(10,1);double t=step/30.;
        assert(rsx_pose_prepare(&a.h,&a.f,t));assert(near(a.x(),float(step)-.5f));
        rsx_pose_prepare(&a.h,&a.f,t+1./60);assert(near(a.x(),float(step)));
    }
}
static void partial_lists_and_independent_tracks() {
    Fixture a;a.f.nd=2;a.d[1]={2,4,3,1,0};
    rsx_pose_prepare(&a.h,&a.f,0);a.move(0,1);a.move(4,1);
    assert(rsx_pose_prepare(&a.h,&a.f,1./30));assert(a.h.matched==2);
    // Reorder and insert an unrelated draw half-way through an animation.
    a.d[0]={3,8,3,3,0};a.d[1]={2,4,3,1,0};a.d[2]={1,0,3,0,0};a.f.nd=3;
    assert(rsx_pose_prepare(&a.h,&a.f,1./30+1./120));
    assert(a.h.matched==2&&near(a.x(1),4.75f)&&near(a.x(2),.75f));
    assert(!rsx_pose_draw_ready(&a.h,0));
    // Culling one mesh and changing another does not reset the first track.
    a.d[0]=a.d[2];a.d[1]={9,8,3,3,0};a.f.nd=2;
    rsx_pose_prepare(&a.h,&a.f,1./20);assert(near(a.x(),1));assert(a.h.matched==1);
    a.move(0,1);a.move(8,.25f);
    assert(rsx_pose_prepare(&a.h,&a.f,2./30));assert(near(a.x(),1.5f));
    a.move(8,.25f); // unrelated pose updates at a different time
    assert(rsx_pose_prepare(&a.h,&a.f,2./30+1./120));assert(near(a.x(),1.75f));
}
static void nearest_instances_and_ambiguity() {
    Fixture a;a.f.nd=2;a.d[1]={1,10,3,1,0};
    rsx_pose_prepare(&a.h,&a.f,0);a.move(0,1);a.move(10,-1);
    a.d[0]={1,10,3,1,0};a.d[1]={1,0,3,0,0};
    assert(rsx_pose_prepare(&a.h,&a.f,1./30));
    assert(a.h.matched==2&&near(a.x(0),9.5f)&&near(a.x(1),.5f));
    // Two distinct instances equally close: never guess which one moved.
    Fixture b;b.f.nd=2;b.d[1]={1,4,3,1,0};
    for(int i=0;i<3;i++){b.v[i].xyz[0]=float(i)-1;b.v[i+4].xyz[0]=float(i)+1;}
    rsx_pose_prepare(&b.h,&b.f,0);b.f.nd=1;
    for(int i=0;i<3;i++)b.v[i].xyz[0]=float(i);
    assert(!rsx_pose_prepare(&b.h,&b.f,1./30));assert(b.h.ambiguous==1&&!rsx_pose_draw_ready(&b.h,0));
    // Same local-space mesh at different transforms: use transform proximity.
    Fixture c;c.f.nd=2;c.d[1]={1,4,3,1,0};
    for(int i=0;i<3;i++)c.v[i+4]=c.v[i];c.matrices[32+28]=4;
    rsx_pose_prepare(&c.h,&c.f,0);c.matrices[28]=.5f;c.matrices[32+28]=4.5f;
    c.d[0]={1,4,3,1,0};c.d[1]={1,0,3,0,0};
    assert(rsx_pose_prepare(&c.h,&c.f,1./30)&&c.h.matched==2&&c.h.ambiguous==0);
    float matrix[32];rsx_pose_camera(&c.h,0,matrix);assert(near(matrix[28],4.25f));
}
static void discontinuities_and_invalid_input() {
    Fixture a;rsx_pose_prepare(&a.h,&a.f,0);
    a.move(0,100);assert(!rsx_pose_prepare(&a.h,&a.f,.03));assert(a.h.reason==RSX_POSE_CUT);
    a.matrices[0]=0;a.matrices[2]=-1;a.matrices[8]=1;a.matrices[10]=0;
    assert(!rsx_pose_prepare(&a.h,&a.f,.06));assert(a.h.reason==RSX_POSE_CUT);
    a.matrices[28]=20;assert(!rsx_pose_prepare(&a.h,&a.f,.09)); // translation-only cut
    a.f.scene=2;assert(!rsx_pose_prepare(&a.h,&a.f,.12));assert(a.h.reason==RSX_POSE_LAYOUT);
    a.move(0,1);assert(!rsx_pose_prepare(&a.h,&a.f,1));assert(a.h.reason==RSX_POSE_LAYOUT);
    assert(!rsx_pose_prepare(&a.h,&a.f,.5));assert(a.h.reason==RSX_POSE_LAYOUT);
    a.v[0].xyz[0]=NAN;assert(!rsx_pose_prepare(&a.h,&a.f,.51));assert(a.h.reason==RSX_POSE_INVALID);a.v[0].xyz[0]=1;
    a.v[0].stamp=0;assert(!rsx_pose_prepare(&a.h,&a.f,.52));assert(a.h.reason==RSX_POSE_INVALID);a.v[0].stamp=1;
    a.d[0].count=33;assert(!rsx_pose_prepare(&a.h,&a.f,.53));assert(a.h.reason==RSX_POSE_INVALID);a.d[0].count=3;
    a.f.scene=0;assert(!rsx_pose_prepare(&a.h,&a.f,.54));assert(a.h.reason==RSX_POSE_OFF);
    a.f.scene=1;assert(!rsx_pose_prepare(&a.h,&a.f,INFINITY));assert(a.h.reason==RSX_POSE_INVALID);
}
static void aliases_and_budgets() {
    float output[3]{},x[3]={1,2,3},y[3]={4,5,6};uint32_t state=0;
    rsx_pose_merge(output,&state,x,3);assert(state==1&&!memcmp(output,x,sizeof x));
    rsx_pose_merge(output,&state,x,3);assert(state==1);
    rsx_pose_merge(output,&state,y,3);assert(state==2);
    rsx_pose_merge(output,&state,x,3);assert(state==2);
    Fixture shared;shared.f.nd=2;shared.d[1]={2,4,3,1,0};
    rsx_pose_prepare(&shared.h,&shared.f,0);shared.move(0,1);shared.d[1].first=0;
    assert(rsx_pose_prepare(&shared.h,&shared.f,1./30));
    float pose_a[3],pose_b[3];rsx_pose_vertex(&shared.h,0,0,pose_a);rsx_pose_vertex(&shared.h,1,0,pose_b);
    state=0;rsx_pose_merge(output,&state,pose_a,3);rsx_pose_merge(output,&state,pose_b,3);assert(state==2);
    // Aliased input ranges are packed independently. Exhaustion skips only
    // the over-budget draw, with no read/write beyond the snapshot capacity.
    Fixture a;a.f.nd=8;
    for(int i=0;i<8;i++)a.d[i]={uint64_t(i+1),0,6,0,0};
    rsx_pose_prepare(&a.h,&a.f,0);assert(a.h.cur.nv==30&&a.h.capped==3);
    a.move(0,1);rsx_pose_prepare(&a.h,&a.f,1./30);assert(a.h.cur.nv==30&&a.h.capped==3);
    assert(!rsx_pose_draw_ready(&a.h,7));
    // Pathological same-key buckets have bounded work, never quadratic scans.
    RsxPoseHistory h{};assert(rsx_pose_init(&h,600,200));
    std::vector<RsxPoseVertex> v(600);std::vector<RsxPoseDraw> d(200);float cam[32];camera(cam);
    for(unsigned i=0;i<200;i++) {
        d[i]={1,i*3,3,0,0};
        for(unsigned k=0;k<3;k++){v[i*3+k].stamp=1;v[i*3+k].xyz[0]=float(i*20+k);}
    }
    RsxPoseFrame f{v.data(),cam,d.data(),600,200,1,1};rsx_pose_prepare(&h,&f,0);
    for(auto& p:v)p.xyz[0]+=.1f;
    rsx_pose_prepare(&h,&f,1./30);assert(h.capped>0);rsx_pose_destroy(&h);
}
static void captured_camera_layout() {
    // c0..c7 from F12 frame_92, gameplay room 010D; not an invented identity matrix.
    const float m[32]={-.0735f,-.4504f,.8898f,0, 0,.8922f,.4516f,0,
        -.9973f,.0332f,-.0656f,0, 0,0,0,1,
        0,0,1.0602f,1, 0,1,0,0, -1,0,0,0, -1.9954f,14.5532f,43.4509f,42.2321f};
    assert(rsx_pose_camera_valid(m));assert(!rsx_pose_camera_cut(m,m));
    Fixture a;memcpy(a.matrices,m,sizeof m);rsx_pose_prepare(&a.h,&a.f,0);
    a.move(0,.2f);a.matrices[28]+=.1f;
    assert(rsx_pose_prepare(&a.h,&a.f,1./30));float out[32];rsx_pose_camera(&a.h,0,out);
    assert(near(out[28],m[28]+.05f));
}
static void coherent_multipass() {
    // Capture regression: depth has cmask=0, lighting blend=1/cmask=4,
    // material blend=0/cmask=F. Different VS/UV/pass keys cannot justify
    // drawing these copies of one surface at different positions.
    Fixture a;a.f.nd=2;a.d[1]={2,4,3,1,0};
    for(unsigned k=0;k<3;k++)a.v[k+4]=a.v[k];
    rsx_pose_prepare(&a.h,&a.f,0);
    a.move(0,1);a.move(4,1);
    for(unsigned k=0;k<3;k++)a.v[k+8]=a.v[k];
    a.f.nd=3;a.d[2]={3,8,3,2,0}; // a newly visible lighting/material copy
    for(unsigned cb=0;cb<3;cb++)a.matrices[cb*32+28]=.2f;
    assert(rsx_pose_prepare(&a.h,&a.f,1./30));assert(!rsx_pose_draw_ready(&a.h,2));
    float depth[3];rsx_pose_vertex(&a.h,0,0,depth);
    assert(!near(depth[0],a.v[8].xyz[0])); // reproduces the pre-fix misalignment
    assert(rsx_pose_cohere(&a.h,&a.f)&&a.h.coherent>=4);
    float canonical_xyz[3],canonical_camera[32];
    rsx_pose_vertex(&a.h,(unsigned)a.h.vertex_sources[0],0,canonical_xyz);
    rsx_pose_camera(&a.h,(unsigned)a.h.camera_sources[0],canonical_camera);
    assert(near(canonical_xyz[0],.5f)&&near(canonical_camera[28],.1f));
    for(unsigned pass=0;pass<3;pass++) {
        assert(a.h.vertex_sources[pass]>=0&&a.h.camera_sources[pass]>=0);
        float xyz[3],matrix[32];
        rsx_pose_vertex(&a.h,(unsigned)a.h.vertex_sources[pass],0,xyz);
        rsx_pose_camera(&a.h,(unsigned)a.h.camera_sources[pass],matrix);
        assert(!memcmp(xyz,canonical_xyz,sizeof xyz)&&!memcmp(matrix,canonical_camera,sizeof matrix));
    }
    // A late veto through a shared upload range must reach every surface
    // copy, including a differently-sized overlapping draw transitively.
    a.f.nd=4;a.d[3]={77,2,3,3,0};
    rsx_pose_prepare(&a.h,&a.f,.04);assert(rsx_pose_cohere(&a.h,&a.f));
    uint32_t vertex_states[32],camera_states[8];
    for(auto& state:vertex_states)state=1;for(auto& state:camera_states)state=1;
    vertex_states[3]=2;camera_states[0]=2;
    rsx_pose_protect_aliases(&a.h,&a.f,vertex_states,camera_states);
    for(unsigned pass=0;pass<3;pass++)for(unsigned k=0;k<3;k++)assert(vertex_states[a.d[pass].first+k]==2);
    assert(vertex_states[11]==1&&camera_states[0]==2&&camera_states[1]==2&&camera_states[2]==2&&camera_states[3]==1);
    // Important observed case: no vertex movement, only c7 changes.
    Fixture camera_only;camera_only.f.nd=1;rsx_pose_prepare(&camera_only.h,&camera_only.f,0);
    camera_only.matrices[28]=1;camera_only.matrices[32+28]=1;
    camera_only.f.nd=2;camera_only.d[1]={99,4,3,1,0}; // unrelated new object, same camera
    assert(rsx_pose_prepare(&camera_only.h,&camera_only.f,1./30));
    assert(rsx_pose_cohere(&camera_only.h,&camera_only.f));
    assert(camera_only.h.vertex_sources[0]<0&&camera_only.h.camera_sources[1]>=0);
    float m[32];rsx_pose_camera(&camera_only.h,(unsigned)camera_only.h.camera_sources[1],m);assert(near(m[28],.5f));
    // Distinct shadow/reflection camera must stay separate.
    camera_only.d[2]={100,8,3,2,0};camera_only.matrices[64+28]=30;camera_only.f.nd=3;
    rsx_pose_prepare(&camera_only.h,&camera_only.f,.04);assert(rsx_pose_cohere(&camera_only.h,&camera_only.f));
    assert(camera_only.h.camera_sources[2]<0);
    // Conflicting histories snap ALL copies together, then recover next pose.
    Fixture conflict;conflict.f.nd=2;conflict.d[1]={2,4,3,1,0};
    for(unsigned k=0;k<3;k++)conflict.v[k+4].xyz[0]=float(k)+2;
    rsx_pose_prepare(&conflict.h,&conflict.f,0);
    for(unsigned k=0;k<3;k++)conflict.v[k].xyz[0]=conflict.v[k+4].xyz[0]=float(k)+1;
    assert(rsx_pose_prepare(&conflict.h,&conflict.f,1./30));assert(rsx_pose_cohere(&conflict.h,&conflict.f));
    assert(conflict.h.coherence_conflicts==1&&conflict.h.vertex_sources[0]<0&&conflict.h.vertex_sources[1]<0);
    conflict.move(0,1);conflict.move(4,1);
    rsx_pose_prepare(&conflict.h,&conflict.f,2./30);assert(rsx_pose_cohere(&conflict.h,&conflict.f));
    assert(conflict.h.coherence_conflicts==0&&conflict.h.vertex_sources[0]>=0&&conflict.h.vertex_sources[1]>=0);
}
static void workload() {
    constexpr unsigned draws=4000,vertices=draws*12;
    RsxPoseHistory h{};assert(rsx_pose_init(&h,vertices,draws));
    std::vector<RsxPoseVertex> v(vertices);std::vector<RsxPoseDraw> d(draws);float cam[32];camera(cam);
    std::vector<uint32_t> vertex_states(vertices,1),camera_states(draws,1);
    for(unsigned i=0;i<draws;i++) {
        d[i]={uint64_t(i+1),i*12,12,0,0};
        for(unsigned k=0;k<12;k++){auto& p=v[i*12+k];p.stamp=1;p.xyz[0]=float(k);}
    }
    RsxPoseFrame f{v.data(),cam,d.data(),vertices,draws,1,1};rsx_pose_prepare(&h,&f,0);
    const auto start=std::chrono::steady_clock::now();
    for(int frame=1;frame<=120;frame++) {
        if(frame%2)for(auto& p:v)p.xyz[0]+=.01f;
        if(rsx_pose_prepare(&h,&f,frame/60.)){assert(rsx_pose_cohere(&h,&f));rsx_pose_protect_aliases(&h,&f,vertex_states.data(),camera_states.data());}
        assert(h.matched==draws&&h.capped==0);
    }
    double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/120;
    std::printf("CPU history workload: %u draws, %u vertices, %.3f ms/present\n",draws,vertices,ms);
    rsx_pose_destroy(&h);
}
static void shared_animation_edges() {
    // Two triangles share an edge, but use different material/UV keys and
    // independent upload ranges. This is NOT a whole-draw multipass copy.
    Fixture a;a.f.nd=2;a.d[0]={1,0,3,0,0};a.d[1]={2,3,3,1,0};
    const float positions[6][3]={{0,0,2},{1,0,2},{0,1,2},{1,0,2},{1,1,2},{0,1,2}};
    for(unsigned v=0;v<6;v++)memcpy(a.v[v].xyz,positions[v],3*sizeof(float));
    rsx_pose_prepare(&a.h,&a.f,0);
    for(unsigned v=0;v<6;v++)a.v[v].xyz[0]+=1;
    a.d[1].key=9; // second patch has no history after a layout/material change
    assert(rsx_pose_prepare(&a.h,&a.f,1./30));assert(rsx_pose_cohere(&a.h,&a.f));
    float output[32*3]{},baseline[32*3]{};uint32_t states[32]{},cameras[8]{};uint64_t recipes[32];
    for(unsigned v=0;v<32;v++)recipes[v]=UINT64_MAX;
    for(unsigned i=0;i<2;i++)for(unsigned k=0;k<3;k++) {
        unsigned v=a.d[i].first+k;int32_t source=a.h.vertex_sources[i];
        if(source>=0)rsx_pose_vertex(&a.h,(unsigned)source,k,output+v*3);else memcpy(output+v*3,a.v[v].xyz,3*sizeof(float));
        states[v]=1;if(source>=0)recipes[v]=(uint64_t((unsigned)source)<<32)|(a.h.cur.draws[source].first+k);
    }
    assert(!near(output[1*3],output[3*3])); // v3's shared edge is visibly open
    memcpy(baseline,output,sizeof output);
    rsx_pose_protect_aliases(&a.h,&a.f,states,cameras);
    assert(rsx_pose_weld_recipe(&a.h,&a.f,output,states,recipes));
    assert(recipes[1]==recipes[3]&&recipes[2]==recipes[5]&&recipes[3]!=UINT64_MAX&&recipes[4]==UINT64_MAX);
    for(unsigned v=0;v<6;v++)if(recipes[v]!=UINT64_MAX) {
        unsigned source=(unsigned)(recipes[v]>>32),at=(unsigned)recipes[v];float xyz[3];
        rsx_pose_vertex(&a.h,source,at-a.h.cur.draws[source].first,xyz);assert(!memcmp(xyz,output+v*3,sizeof xyz));
    }
    assert(!memcmp(output+1*3,output+3*3,3*sizeof(float))&&!memcmp(output+2*3,output+5*3,3*sizeof(float)));
    assert(near(output[3*3],1.5f)&&a.h.seams==2); // preserve the intermediate motion
    assert(near(output[4*3],2)); // an unrelated point does not borrow another pose
    // Conflicting phases snap the duplicate point together, without opening it.
    memcpy(output,baseline,sizeof output);output[3*3]-=.25f;
    assert(rsx_pose_weld_recipe(&a.h,&a.f,output,states,recipes));
    assert(recipes[1]==UINT64_MAX&&recipes[3]==UINT64_MAX);
    assert(a.h.seam_conflicts==1&&!memcmp(output+1*3,output+3*3,3*sizeof(float)));
    assert(near(output[1*3],a.v[1].xyz[0]));
    // A protected duplicate remains original everywhere, including its seam.
    memcpy(output,baseline,sizeof output);for(auto& state:states)state=1;states[3]=2;
    assert(rsx_pose_weld(&a.h,&a.f,output,states));
    assert(states[1]==2&&states[3]==2&&!memcmp(output+1*3,output+3*3,3*sizeof(float)));
    // Nearby points are NOT welded; neither are the same points in a different camera.
    for(auto& state:states)state=1;a.v[3].xyz[0]+=.0001f;
    memcpy(output,baseline,sizeof output);assert(rsx_pose_weld(&a.h,&a.f,output,states));
    assert(!near(output[1*3],output[3*3]));a.v[3].xyz[0]-=.0001f;
    a.matrices[32+28]=30;rsx_pose_prepare(&a.h,&a.f,.04);assert(rsx_pose_cohere(&a.h,&a.f));
    for(auto& state:states)state=1;memcpy(output,baseline,sizeof output);assert(rsx_pose_weld(&a.h,&a.f,output,states));
    assert(!near(output[1*3],output[3*3]));
}
static void connected_seam_fallback_preserves_triangles() {
    for(bool reverse:{false,true})for(bool alias:{false,true}) {
        Fixture a;a.f.nd=3;a.f.nv=9;
        a.d[0]={1,0,3,0,0};a.d[1]={2,3,3,0,0};a.d[2]={3,6,3,0,0};
        if(reverse){auto d=a.d[0];a.d[0]=a.d[2];a.d[2]=d;}
        const float xyz[9][3]={{0,0,2},{1,0,2},{0,1,2},{1,0,2},{1,1,2},{0,1,2},
                              {10,0,2},{11,0,2},{10,1,2}};
        for(unsigned v=0;v<9;v++)memcpy(a.v[v].xyz,xyz[v],12);
        rsx_pose_prepare(&a.h,&a.f,0);for(unsigned v=0;v<9;v++)a.v[v].xyz[0]+=1;
        assert(rsx_pose_prepare(&a.h,&a.f,1./30));assert(rsx_pose_cohere(&a.h,&a.f));
        float out[27];uint32_t states[9];uint64_t recipes[9];
        for(unsigned v=0;v<9;v++){memcpy(out+v*3,a.v[v].xyz,12);out[v*3]-=.5f;states[v]=1;recipes[v]=UINT64_MAX;}
        if(alias)states[3]=2;else out[3*3]-=.25f;
        assert(rsx_pose_weld_recipe(&a.h,&a.f,out,states,recipes));
        assert(a.h.seam_components==1&&a.h.seam_veto_vertices>0);
        // A conflicting point must not leave its triangle half interpolated.
        for(unsigned v=0;v<6;v++){assert(states[v]==2);assert(!memcmp(out+v*3,a.v[v].xyz,12));assert(recipes[v]==UINT64_MAX);}
        // A disconnected mesh keeps its exact midpoint and native state intact.
        for(unsigned v=6;v<9;v++){assert(states[v]==1);assert(near(out[v*3],a.v[v].xyz[0]-.5f));}
        for(unsigned v=0;v<9;v++)assert(near(a.v[v].xyz[0],xyz[v][0]+1));
    }
}
static void shared_upload_across_cameras(bool reverse) {
    // One uploaded triangle is reused in two views (for example a shadow).
    // Adjacent patches have separate uploads in each view and lost history.
    // A shared upload must link BOTH sets of duplicated edge points.
    Fixture a;a.f.nd=4;
    a.d[0]={1,0,3,0,0};a.d[1]={2,0,3,1,0};
    a.d[2]={3,3,3,0,0};a.d[3]={4,6,3,1,0};a.matrices[32+28]=5;
    if(reverse)for(unsigned i=0;i<2;i++){auto tmp=a.d[i];a.d[i]=a.d[3-i];a.d[3-i]=tmp;}
    const float p[6][3]={{0,0,2},{1,0,2},{0,1,2},{1,0,2},{1,1,2},{0,1,2}};
    for(unsigned v=0;v<6;v++)memcpy(a.v[v].xyz,p[v],3*sizeof(float));
    for(unsigned v=0;v<3;v++)a.v[v+6]=a.v[v+3];
    rsx_pose_prepare(&a.h,&a.f,0);
    for(unsigned v=0;v<9;v++)a.v[v].xyz[0]+=1;
    for(auto& d:a.d)if(d.key==3||d.key==4)d.key+=30;
    assert(rsx_pose_prepare(&a.h,&a.f,1./30));assert(rsx_pose_cohere(&a.h,&a.f));
    float out[32*3]{};uint32_t states[32]{},cams[8]{};
    for(unsigned i=0;i<4;i++)for(unsigned k=0;k<3;k++) {
        unsigned v=a.d[i].first+k;float xyz[3];int32_t source=a.h.vertex_sources[i];
        if(source>=0)rsx_pose_vertex(&a.h,(unsigned)source,k,xyz);else memcpy(xyz,a.v[v].xyz,sizeof xyz);
        rsx_pose_merge(out+v*3,states+v,xyz,3);
    }
    rsx_pose_protect_aliases(&a.h,&a.f,states,cams);
    assert(rsx_pose_weld(&a.h,&a.f,out,states));
    for(unsigned v:{3u,6u})assert(!memcmp(out+3,out+v*3,3*sizeof(float)));
    for(unsigned v:{5u,8u})assert(!memcmp(out+6,out+v*3,3*sizeof(float)));
    assert(near(out[3],1.5f));
    // A veto must reach copies in every view through the shared point.
    states[6]=2;
    assert(rsx_pose_weld(&a.h,&a.f,out,states));
    for(unsigned v:{1u,3u,6u})assert(states[v]==2&&near(out[v*3],a.v[v].xyz[0]));
}
static void weld_limits_leave_proposals_untouched() {
    // One upload reused by four views needs more point/view nodes than
    // upload vertices. Exhausting the optional graph must be atomic.
    Fixture a;rsx_pose_destroy(&a.h);assert(rsx_pose_init(&a.h,9,8));a.f.nv=9;a.f.nd=4;
    for(unsigned i=0;i<4;i++){a.d[i]={uint64_t(i+1),0,3,i,0};a.matrices[i*32+28]=float(i*20);}
    rsx_pose_prepare(&a.h,&a.f,0);assert(rsx_pose_cohere(&a.h,&a.f));
    float out[32*3]{},before[32*3]{};uint32_t states[32]{},before_states[32]{};
    for(unsigned v=0;v<3;v++){out[v*3]=a.v[v].xyz[0]-.5f;states[v]=1;}
    memcpy(before,out,sizeof out);memcpy(before_states,states,sizeof states);
    assert(!rsx_pose_weld(&a.h,&a.f,out,states));
    assert(!memcmp(out,before,sizeof out)&&!memcmp(states,before_states,sizeof states));
    // Deliberately collide >128 distinct points in one bucket. The caller
    // can retain the original full frame; no partially edited proposals.
    RsxPoseHistory h{};assert(rsx_pose_init(&h,160,1));h.buckets=1;
    std::vector<RsxPoseVertex> v(160);std::vector<float> result(160*3,42),saved=result;
    std::vector<uint32_t> status(160,1),saved_status=status;float cam[32];camera(cam);
    for(unsigned i=0;i<160;i++){v[i].xyz[0]=float(i);v[i].stamp=1;}
    RsxPoseDraw d{1,0,160,0,0};RsxPoseFrame f{v.data(),cam,&d,160,1,1,1};
    rsx_pose_prepare(&h,&f,0);assert(rsx_pose_cohere(&h,&f));
    assert(!rsx_pose_weld(&h,&f,result.data(),status.data()));
    assert(result==saved&&status==saved_status);rsx_pose_destroy(&h);
}
static void lighter_projection_matches_current_light() {
    // F12 v5 frame_91: CPU shadow VS26 used c7=39.2381,3.71998,3.39271,4.44995,
    // while palette VS33 and FP light position still described the current
    // light: c262=38.8953,3.71517,3.42015,4.47582. One atlas had two origins.
    const float shadow[32]={-.991311f,-.010356f,.131133f,0, 0,.996896f,.0787283f,0,
        -.131542f,.0780442f,-.988234f,0, 0,0,0,1,
        0,0,1.06024f,1, 0,1,0,0, -1,0,0,0,
        38.8953f,3.71517f,3.42015f,4.47582f};
    Fixture a;a.f.nd=3;a.d[1]={2,4,3,1,0};a.d[2]={3,8,3,2,0};
    assert(rsx_pose_fragment_flags(0x0C121A16u)==0&&rsx_pose_fragment_flags(0)==0);
    a.d[0].flags=rsx_pose_fragment_flags(0x7EA4C4E7u);assert(a.d[0].flags==RSX_POSE_CURRENT_LIGHT_CAMERA);
    memcpy(a.matrices,shadow,sizeof shadow);memcpy(a.matrices+32,shadow,sizeof shadow);
    a.matrices[28]=39.5809f;a.matrices[32+28]=39.5809f;
    rsx_pose_prepare(&a.h,&a.f,0);
    memcpy(a.matrices,shadow,sizeof shadow);memcpy(a.matrices+32,shadow,sizeof shadow);
    a.move(0,.2f);a.move(4,.2f);a.move(8,.2f);a.matrices[64+28]=1;
    assert(rsx_pose_prepare(&a.h,&a.f,1./30));assert(rsx_pose_cohere(&a.h,&a.f));
    // All copies of this light projection must keep CURRENT constants until
    // light-reader FP uniforms and palette projections are interpolated too.
    assert(a.h.camera_sources[0]<0&&a.h.camera_sources[1]<0);
    assert(a.h.vertex_sources[0]>=0&&near(a.x(),.1f)); // vertices still animate
    float main_camera[32];assert(a.h.camera_sources[2]>=0);
    rsx_pose_camera(&a.h,(unsigned)a.h.camera_sources[2],main_camera);assert(near(main_camera[28],.5f));
    // A newly appeared shadow copy with no history must also lock the group.
    a.d[0].flags=0;a.d[1].key=99;a.d[1].flags=RSX_POSE_CURRENT_LIGHT_CAMERA;
    assert(rsx_pose_prepare(&a.h,&a.f,1./30+1./120));assert(!rsx_pose_draw_ready(&a.h,1));
    assert(rsx_pose_cohere(&a.h,&a.f)&&a.h.camera_sources[0]<0&&a.h.camera_sources[1]<0);
}
static void animated_normals_are_not_identity() {
    // Actual frame_91/skinned VS10: a14 decoded for lighting; a15 is UV.
    float attributes[16][4]{};
    attributes[0][0]=-1.51557f;attributes[0][1]=.18108f;attributes[0][2]=1.38353f;
    attributes[14][0]=.0941176f;attributes[14][1]=.701961f;attributes[14][2]=.701961f;
    attributes[15][0]=.998535f;attributes[15][1]=.614746f;
    RsxPoseVertex old{},moved{},retopologized{};rsx_pose_record_vertex(&old,attributes,1);
    attributes[0][0]+=.1f;attributes[14][0]=.32549f;attributes[14][1]=.192157f;
    rsx_pose_record_vertex(&moved,attributes,2);
    assert(old.uv==moved.uv&&moved.stamp==2&&near(moved.xyz[0],old.xyz[0]+.1f));
    attributes[15][0]=.881348f;rsx_pose_record_vertex(&retopologized,attributes,3);
    assert(old.uv!=retopologized.uv);
    Fixture a;for(unsigned k=0;k<3;k++)a.v[k]=old;
    a.f.stamp=1;a.d[0].key=old.uv;rsx_pose_prepare(&a.h,&a.f,0);
    for(unsigned k=0;k<3;k++)a.v[k]=moved;
    a.f.stamp=2;a.d[0].key=moved.uv;
    assert(rsx_pose_prepare(&a.h,&a.f,1./30)&&a.h.matched==1);
    assert(near(a.x(),old.xyz[0]+.05f)); // animation retains its midpoint/history
}
static void projection_axis_discontinuities() {
    // Equal total projection energy does NOT mean compatible projections.
    // A mirror/sign change cancels this axis at alpha=.5 in v6.
    Fixture a;a.matrices[16]=1;rsx_pose_prepare(&a.h,&a.f,0);
    float previous[32];memcpy(previous,a.matrices,sizeof previous);
    a.matrices[16]=-1;a.move(0,.2f);
    assert(rsx_pose_camera_valid(a.matrices));
    assert(rsx_pose_camera_cut(previous,a.matrices));
    assert(!rsx_pose_prepare(&a.h,&a.f,1./30)&&!rsx_pose_draw_ready(&a.h,0));
    // Projection strength can move from one axis to another without changing
    // its total norm; collapsing one axis must also start a fresh history.
    Fixture b;b.matrices[16]=1;b.matrices[20]=1;b.matrices[27]=1;
    rsx_pose_prepare(&b.h,&b.f,0);memcpy(previous,b.matrices,sizeof previous);
    b.matrices[16]=0;b.matrices[19]=0;b.matrices[20]=2;b.move(0,.2f);
    assert(rsx_pose_camera_valid(b.matrices));
    assert(rsx_pose_camera_cut(previous,b.matrices));
    assert(!rsx_pose_prepare(&b.h,&b.f,1./30));
    // After a discontinuity, the next ordinary pose recovers immediately.
    b.move(0,.2f);assert(rsx_pose_prepare(&b.h,&b.f,2./30));assert(near(b.x(),.3f));
    // Small legitimate changes in projection retain smooth motion.
    Fixture smooth;smooth.matrices[16]=1;rsx_pose_prepare(&smooth.h,&smooth.f,0);
    smooth.matrices[16]=1.01f;smooth.move(0,.2f);
    assert(rsx_pose_prepare(&smooth.h,&smooth.f,1./30)&&near(smooth.x(),.1f));
}
static void malformed_ranges_and_numeric_limits() {
    Fixture a;
    for(unsigned field=0;field<5;field++) {
        a.f={a.v,a.matrices,a.d,32,1,1,1};a.d[0]={1,0,3,0,0};
        if(field==0)a.d[0].first=UINT32_MAX;
        if(field==1)a.d[0].count=UINT32_MAX;
        if(field==2)a.d[0].cb=UINT32_MAX;
        if(field==3)a.f.nv=UINT32_MAX;
        if(field==4)a.f.nd=UINT32_MAX;
        assert(!rsx_pose_prepare(&a.h,&a.f,field/30.));assert(a.h.reason==RSX_POSE_INVALID);
    }
    a.f={a.v,a.matrices,a.d,32,1,1,1};a.d[0]={1,0,3,0,0};
    for(float bad:{NAN,INFINITY,-INFINITY,1000001.f,-1000001.f}) {
        a.v[0].xyz[0]=bad;assert(!rsx_pose_prepare(&a.h,&a.f,1));assert(a.h.reason==RSX_POSE_INVALID);
        a.v[0].xyz[0]=0;a.matrices[28]=bad;
        assert(!rsx_pose_prepare(&a.h,&a.f,1));assert(a.h.reason==RSX_POSE_INVALID);a.matrices[28]=0;
    }
    a.v[0].xyz[0]=1000000.f;assert(!rsx_pose_prepare(&a.h,&a.f,2));
    a.v[0].xyz[0]=999998.f;assert(rsx_pose_prepare(&a.h,&a.f,2+1./30));
    assert(isfinite(a.x())&&a.x()>=999998.f&&a.x()<=1000000.f);
}
static void bounded_motion_soak() {
    Fixture a;uint32_t rng=0x5481ad3u;unsigned interpolated=0,discontinuities=0;
    for(unsigned frame=0;frame<10000;frame++) {
        if(frame%2==0) {
            rng=rng*1664525u+1013904223u;
            float step=(float((rng>>16)&255)/255.f-.5f)*.2f;
            a.move(0,step);
            if(frame%202==0){a.move(0,20);discontinuities++;}
            if(frame%514==0)a.f.scene++;
            if(frame%382==0)a.d[0].key++;
            if(frame%658==0)a.matrices[16]=a.matrices[16]==1?-1.f:1.f;
        }
        RsxPoseVertex untouched[32];memcpy(untouched,a.v,sizeof untouched);
        int apply=rsx_pose_prepare(&a.h,&a.f,frame/60.);
        assert(!memcmp(untouched,a.v,sizeof untouched));
        if(!apply)continue;
        assert(rsx_pose_cohere(&a.h,&a.f));assert(rsx_pose_draw_ready(&a.h,0));
        float t=a.h.cur.tracks[0].alpha;assert(isfinite(t)&&t>=.5f&&t<1);
        for(unsigned v=0;v<3;v++) {
            float out[3];rsx_pose_vertex(&a.h,0,v,out);
            const float* old=a.h.cur.old_xyz+(a.h.cur.draws[0].first+v)*3;
            for(unsigned k=0;k<3;k++) {
                float current=a.v[v].xyz[k],lo=old[k]<current?old[k]:current,hi=old[k]>current?old[k]:current;
                assert(isfinite(out[k])&&out[k]>=lo&&out[k]<=hi);
            }
        }
        interpolated++;
    }
    assert(interpolated>4000&&discontinuities>40);
    std::printf("PASS stability: 10000 frames, %u intermediate poses, discontinuities and bounded vertices\n",interpolated);
}
static void bounded_search_blocks() {
    {Fixture full;full.f.nd=8;
     for(unsigned i=0;i<8;i++)full.d[i]={1,0,8,0,0};
     rsx_pose_prepare(&full.h,&full.f,0);assert(full.h.cur.nv==32&&full.h.capped==4);
     full.move(0,.5f);rsx_pose_prepare(&full.h,&full.f,1./30);
     assert(full.h.cur.nv==32&&full.h.capped==4);}
    // Exactly 128 visits is allowed; a 129th invalidates any earlier match.
    // Blocks must count skipped candidates too, including hash collisions.
    for(unsigned count:{1u,7u,8u,9u,127u,128u,129u,257u}) {
        RsxPoseHistory h{};assert(rsx_pose_init(&h,count*3,count));
        std::vector<RsxPoseVertex> v(count*3);std::vector<RsxPoseDraw> d(count);
        float cam[32];camera(cam);
        for(unsigned i=0;i<count;i++) {
            d[i]={1,i*3,3,0,0};
            for(unsigned k=0;k<3;k++){v[i*3+k].stamp=1;v[i*3+k].xyz[0]=float(i*32+k);}
        }
        RsxPoseFrame f{v.data(),cam,d.data(),count*3,count,1,1};
        rsx_pose_prepare(&h,&f,0);for(auto& p:v)p.xyz[0]+=.5f;
        int apply=rsx_pose_prepare(&h,&f,1./30);
        if(count<=128) {
            assert(apply&&h.matched==count&&h.capped==0&&h.ambiguous==0);
            for(unsigned i=0;i<count;i++) {
                float xyz[3];rsx_pose_vertex(&h,i,0,xyz);assert(xyz[0]==float(i*32)+.25f);
            }
        }else assert(!apply&&h.matched==0&&h.capped==count);
        rsx_pose_destroy(&h);
    }
    // Strict radius boundary and float rounding at large/small coordinates.
    // A block may reject only if EVERY contained candidate would be rejected.
    for(float origin:{-1000000.f,-8.f,-0.f,0.f,8.f,1000000.f}) {
        RsxPoseBlock block{};
        for(unsigned k=0;k<3;k++){block.lo[k]=origin;block.hi[k]=origin+.03125f;}
        for(float delta:{-8.f,nextafterf(-8.f,-INFINITY),nextafterf(-8.f,0.f),
                         0.f,nextafterf(8.f,0.f),8.f,nextafterf(8.f,INFINITY),20.f}) {
            float query[3]={origin+delta,origin,origin};
            if(!rsx_pose_block_distant(&block,query))continue;
            for(unsigned corner=0;corner<8;corner++) {
                float distance=0;
                for(unsigned k=0;k<3;k++) {
                    float xyz=corner&(1u<<k)?block.lo[k]:block.hi[k];
                    float difference=xyz-query[k];distance+=difference*difference;
                }
                assert(distance>64.f);
            }
        }
    }
}
static void camera_memo_low_reuse_and_recovery() {
    constexpr unsigned n=600;RsxPoseHistory h{};assert(rsx_pose_init(&h,n,n));
    std::vector<RsxPoseVertex> v(n);std::vector<RsxPoseDraw> d(n);std::vector<float> cameras(n*32);
    for(unsigned i=0;i<n;i++) {
        d[i]={uint64_t(i+1),i,1,i,0};v[i].stamp=1;v[i].xyz[0]=float(i)*16;
        camera(cameras.data()+i*32);cameras[i*32+28]=float(i)*.001f;
    }
    RsxPoseFrame f{v.data(),cameras.data(),d.data(),n,n,1,1};rsx_pose_prepare(&h,&f,0);
    for(unsigned i=0;i<n;i++)cameras[i*32+28]+=.01f;
    rsx_pose_prepare(&h,&f,1./30);
    assert(h.matched==n&&h.camera_memo_disabled&&h.camera_misses==512&&h.camera_uncached==88);
    // Returning to repeated matrices must recover, even if the last frame
    // disabled memo lookups. Matrix equivalence IDs must remain accurate.
    for(unsigned i=0;i<n;i++)camera(cameras.data()+i*32);
    rsx_pose_prepare(&h,&f,2./30);assert(h.matched==n);
    rsx_pose_prepare(&h,&f,3./30);
    assert(h.matched==n&&!h.camera_memo_disabled&&h.camera_misses==1&&h.camera_hits==599&&h.camera_uncached==0);
    rsx_pose_destroy(&h);
}
static void cached_camera_comparisons() {
    Fixture a;a.f.nd=4;
    for(unsigned i=0;i<4;i++)a.d[i]={uint64_t(i+1),i*4,3,i,0};
    a.matrices[2*32+28]=4;
    a.matrices[3*32+28]=-4;
    // Force both the canonical-matrix hash table and pair cache to collide.
    // Exact byte verification and replacement must retain the original result.
    a.h.buckets=1;a.h.memo_slots=1;
    rsx_pose_prepare(&a.h,&a.f,0);
    assert(a.h.cur.camera_ids[0]==a.h.cur.camera_ids[1]);
    assert(a.h.cur.camera_ids[0]!=a.h.cur.camera_ids[2]);
    assert(a.h.cur.camera_ids[2]!=a.h.cur.camera_ids[3]);
    for(unsigned i=0;i<4;i++){a.move(i*4,.25f);a.matrices[i*32+28]+=.5f;}
    // Epoch wrap cannot resurrect a result for different frame-local IDs.
    a.h.memo_epoch=UINT32_MAX;
    a.h.camera_memo[0]={0,0,1,777.f};
    assert(rsx_pose_prepare(&a.h,&a.f,1./30)&&a.h.matched==4);
    assert(a.h.memo_epoch==1&&a.h.camera_hits>=1&&a.h.camera_misses>=3);
    for(unsigned i=0;i<4;i++)assert(near(a.x(i),float(i*4)+.125f));
    // Constant-bank IDs can be reused with different bytes next frame.
    // They are not persistent object identities and MUST invalidate the memo.
    for(unsigned i=0;i<4;i++)a.matrices[i*32+28]+=100;
    assert(!rsx_pose_prepare(&a.h,&a.f,2./30)&&a.h.matched==0&&a.h.camera_misses>0);
    // Sampling index arithmetic is prepared once, including draws <16 and
    // draws whose unsampled vertices still require the exhaustive jump veto.
    for(unsigned count=1;count<=32;count++) {
        Fixture q;q.d[0].count=count;RsxPoseQuery sample;
        rsx_pose_query(&sample,&q.f,0,0);
        unsigned n=count<16?count:16;assert(sample.count==n);
        for(unsigned k=0;k<n;k++) {
            unsigned index=n==1?0:unsigned(uint64_t(k)*(count-1)/(n-1));
            assert(sample.indices[k]==index&&!memcmp(sample.xyz[k],q.v[index].xyz,3*sizeof(float)));
        }
    }
}
static void cached_position_comparisons() {
    Fixture a;a.f.nd=4;
    for(unsigned i=0;i<4;i++)a.d[i]={uint64_t(i+1),0,3,i,0};
    rsx_pose_prepare(&a.h,&a.f,0);
    for(unsigned i=1;i<4;i++)assert(a.h.cur.sample_ids[i]==a.h.cur.sample_ids[0]);
    a.move(0,.25f);
    assert(rsx_pose_prepare(&a.h,&a.f,1./30)&&a.h.matched==4);
    assert(a.h.position_misses==1&&a.h.position_hits==3);
    for(unsigned i=0;i<4;i++)assert(near(a.x(i),.125f));

    Fixture b;b.f.nd=4;b.h.buckets=1;b.h.memo_slots=1;
    for(unsigned i=0;i<4;i++)b.d[i]={uint64_t(i+1),i*4,3,i,0};
    rsx_pose_prepare(&b.h,&b.f,0);
    // Force a canonical hash collision. Exact coordinates must still differ.
    RsxPoseQuery q;rsx_pose_query(&q,&b.f,1,0);
    b.h.sample_heads[0]=0;b.h.sample_next[0]=-1;b.h.sample_hashes[0]=b.h.sample_hashes[1];
    assert(rsx_pose_sample_id(&b.h,1,&q)==1);
    for(unsigned i=0;i<4;i++)b.move(i*4,.25f);
    b.h.memo_epoch=UINT32_MAX;b.h.position_memo[0]={0,0,1,777.f};
    assert(rsx_pose_prepare(&b.h,&b.f,1./30)&&b.h.matched==4);
    assert(b.h.memo_epoch==1&&b.h.position_misses==4);
    for(unsigned i=0;i<4;i++)assert(near(b.x(i),float(i*4)+.125f));

    constexpr unsigned n=600;RsxPoseHistory h{};assert(rsx_pose_init(&h,n,n));
    std::vector<RsxPoseVertex> v(n);std::vector<RsxPoseDraw> d(n);std::vector<float> cameras(n*32);
    for(unsigned i=0;i<n;i++){d[i]={uint64_t(i+1),i,1,i,0};v[i].stamp=1;v[i].xyz[0]=float(i)*16;camera(cameras.data()+i*32);}
    RsxPoseFrame f{v.data(),cameras.data(),d.data(),n,n,1,1};
    rsx_pose_prepare(&h,&f,0);rsx_pose_prepare(&h,&f,1./30);
    assert(h.position_memo_disabled&&h.position_misses==512&&h.position_uncached==88);
    for(auto& vertex:v)vertex.xyz[0]=0;
    rsx_pose_prepare(&h,&f,2./30);rsx_pose_prepare(&h,&f,3./30);
    assert(h.matched==n&&!h.position_memo_disabled&&h.position_misses==1&&h.position_hits==599);
    rsx_pose_destroy(&h);
    puts("PASS position memo: exact sample IDs, hash/pair collisions, epoch wrap, reuse bypass and recovery");
}
static float scalar_sample_sum(const RsxPoseSamples& a,const RsxPoseSamples& b,unsigned n) {
    float sum=0;
    for(unsigned k=0;k<n;k++) {
        float d=0,delta=a.x[k]-b.x[k];d+=delta*delta;
        delta=a.y[k]-b.y[k];d+=delta*delta;delta=a.z[k]-b.z[k];d+=delta*delta;
        if(d>64.f)return -1;sum+=d;
    }
    return sum;
}
static void vector_samples_and_score_bounds() {
    uint32_t rng=0x63918572u;
    auto random=[&](){return rng=rng*1664525u+1013904223u;};
    for(unsigned test=0;test<32000;test++) {
        unsigned n=test%16+1;RsxPoseSamples a{},b{};
        for(unsigned k=0;k<n;k++) {
            float* axes_a[3]={a.x,a.y,a.z};float* axes_b[3]={b.x,b.y,b.z};
            for(unsigned t=0;t<3;t++) {
                axes_a[t][k]=(int(random()%2000)-1000)*.03125f;
                axes_b[t][k]=axes_a[t][k]+(int(random()%2001)-1000)/512.f;
            }
        }
        if(test%7==0) {
            unsigned k=n-1;
            a.x[k]=a.y[k]=a.z[k]=b.y[k]=b.z[k]=0;
            b.x[k]=test%3==0?8.f:test%3==1?nextafterf(8.f,0):nextafterf(8.f,INFINITY);
        }
        if(test%11==0)a.x[0]=-0.f,b.x[0]=0.f;
        float expected=scalar_sample_sum(a,b,n),actual=rsx_pose_sample_sum(&a,&b,n,1e30f);
        assert(!memcmp(&actual,&expected,sizeof(float)));
        float center=expected<0?0:expected/n;
        const float bounds[]={0,center,nextafterf(center,0),nextafterf(center,INFINITY),center-1e-6f,center+1e-6f};
        for(float bound:bounds) {
            float result=rsx_pose_sample_sum(&a,&b,n,bound);
            if(result==-2)assert(expected<0||expected/n-bound>1e-6f);
            else assert(!memcmp(&result,&expected,sizeof(float)));
        }
    }
    Fixture f;rsx_pose_prepare(&f.h,&f.f,0);f.move(0,1);rsx_pose_prepare(&f.h,&f.f,1./30);
    RsxPoseQuery query;rsx_pose_query(&query,&f.f,0,f.h.cur.camera_ids[0]);query.sample_id=f.h.cur.sample_ids[0];
    memset(f.h.position_memo,0,f.h.memo_slots*sizeof(RsxPoseCameraMemo));
    f.h.position_memo_disabled=1;
    assert(rsx_pose_position_score(&f.h,&f.h.prev,0,&query,0)==-2);
    f.h.position_memo_disabled=0;
    // A bound-specific rejection must not poison the reusable distance memo.
    assert(rsx_pose_position_score(&f.h,&f.h.prev,0,&query,1e30f)==3.f);
    puts("PASS vector samples: 32000 bit-exact sums, radius/epsilon boundaries, padding and non-poisoning score bounds");
}
static void validation_edges() {
    RsxPoseHistory h{};assert(rsx_pose_init(&h,4,4));
    RsxPoseVertex vertices[4]{};RsxPoseDraw draws[4]{};float matrices[128]{};
    for(unsigned i=0;i<4;i++) {
        vertices[i].stamp=1;draws[i]={1,i,1,i,0};
        float* c=matrices+i*32;c[0]=c[5]=c[10]=c[15]=c[19]=c[26]=1;
    }
    RsxPoseFrame f{vertices,matrices,draws,4,4,1,1};
    assert(rsx_pose_frame_valid(&h,&f));
    matrices[32+4]=NAN;assert(!rsx_pose_frame_valid(&h,&f));
    matrices[32+4]=0;matrices[3*32+19]=INFINITY;assert(!rsx_pose_frame_valid(&h,&f));
    matrices[3*32+19]=1;assert(rsx_pose_frame_valid(&h,&f));
    vertices[3].xyz[2]=NAN;assert(!rsx_pose_frame_valid(&h,&f));
    vertices[3].xyz[2]=0;draws[3].cb=4;assert(!rsx_pose_frame_valid(&h,&f));
    draws[3].cb=3;assert(rsx_pose_frame_valid(&h,&f));
    float a[35]{},b[35]{};
    for(unsigned n=0;n<=35;n++) {
        assert(rsx_pose_floats_equal(a,b,n));
        if(n){b[n-1]=-0.f;assert(rsx_pose_floats_equal(a,b,n));b[n-1]=NAN;assert(!rsx_pose_floats_equal(a,b,n));b[n-1]=1;assert(!rsx_pose_floats_equal(a,b,n));b[n-1]=0;}
    }
    rsx_pose_destroy(&h);
}
static void reused_vertex_validation() {
    Fixture f;f.f.nd=2;f.d[1]=f.d[0];f.d[1].cb=1;
    assert(rsx_pose_frame_valid(&f.h,&f.f));assert(f.h.validated_vertices==3&&f.h.reused_validations==3);
    f.v[2].xyz[1]=NAN;assert(!rsx_pose_frame_valid(&f.h,&f.f));f.v[2].xyz[1]=0;
    f.v[1].stamp=0;assert(!rsx_pose_frame_valid(&f.h,&f.f));f.v[1].stamp=f.f.stamp;
    f.v[2].xyz[2]=1000001.f;assert(!rsx_pose_frame_valid(&f.h,&f.f));f.v[2].xyz[2]=-0.f;
    assert(rsx_pose_frame_valid(&f.h,&f.f));assert(f.h.validated_vertices==3&&f.h.reused_validations==3);
    f.h.vertex_validation_cache_disabled=1;assert(rsx_pose_frame_valid(&f.h,&f.f));assert(f.h.validated_vertices==6&&!f.h.reused_validations);
    f.h.vertex_validation_cache_disabled=0;f.d[1].first=2;f.d[1].count=3;
    assert(rsx_pose_frame_valid(&f.h,&f.f));assert(f.h.validated_vertices==5&&f.h.reused_validations==1);
    f.d[1].first=31;assert(!rsx_pose_frame_valid(&f.h,&f.f));
}
static void block_score_lower_bounds() {
    uint32_t rng=0x35af881u;
    auto random=[&](){return rng=rng*1664525u+1013904223u;};
    unsigned rejected=0;
    for(unsigned test=0;test<20000;test++) {
        RsxPoseBlock block{};float q[3],points[8][3];
        for(int k=0;k<3;k++){q[k]=float(int(random()%1025)-512)*.03125f;block.lo[k]=INFINITY;block.hi[k]=-INFINITY;}
        for(int k=0;k<3;k++) {
            float center=float(int(random()%1025)-512)*.015625f;
            for(int i=0;i<8;i++){points[i][k]=q[k]+center+float(int(random()%17)-8)*.0000152587890625f;block.lo[k]=fminf(block.lo[k],points[i][k]);block.hi[k]=fmaxf(block.hi[k],points[i][k]);}
        }
        uint32_t n=1+random()%16;float first=0;
        for(int k=0;k<3;k++){float d=points[0][k]-q[k];first+=d*d;}
        float best=test%3==0?0:first/n;
        if(test%7==0)best=nextafterf(best-1e-6f,0.f);
        if(rsx_pose_block_cannot_win(&block,q,n,best)) {
            rejected++;
            for(int i=0;i<8;i++){float d=0;for(int k=0;k<3;k++){float x=points[i][k]-q[k];d+=x*x;}assert(d>64.f||d/n-best>1e-6f);}
        }
    }
    assert(rejected>1000);
    RsxPoseBlock edge{};float q[3]{};
    edge.lo[0]=edge.hi[0]=8.f;assert(!rsx_pose_block_cannot_win(&edge,q,16,4.f));
    edge.lo[0]=edge.hi[0]=nextafterf(8.f,INFINITY);assert(rsx_pose_block_cannot_win(&edge,q,16,1e30f));
    puts("PASS block bounds: 20000 conservative AABBs, best-score epsilon and radius boundaries");
}
static void capped_bucket_precheck() {
    for(unsigned n:{127u,128u,129u,256u})for(bool collisions:{false,true}) {
        RsxPoseHistory fast{},original{};
        assert(rsx_pose_init(&fast,256,256)&&rsx_pose_init(&original,256,256));original.cap_precheck_disabled=1;
        RsxPoseVertex vertex{{0,0,0},0,1};float matrices[256*32]{};camera(matrices);
        std::vector<RsxPoseDraw> draws(n);
        for(unsigned i=0;i<n;i++)draws[i]={collisions?uint64_t(513u*(1u+i%2)):1u,0,1,0,0};
        RsxPoseFrame f{&vertex,matrices,draws.data(),1,n,1,1};
        rsx_pose_prepare(&fast,&f,0);rsx_pose_prepare(&original,&f,0);
        rsx_pose_prepare(&fast,&f,1./60);rsx_pose_prepare(&original,&f,1./60);
        assert(fast.matched==original.matched&&fast.capped==original.capped);
        assert(fast.capped_searches_skipped==(n>128?n:0));
        if(n>128)assert(!fast.matched&&fast.capped==n&&!fast.search_candidates);
        assert(!memcmp(fast.cur.xyz,original.cur.xyz,fast.cur.nv*3*sizeof(float)));
        assert(!memcmp(fast.cur.old_xyz,original.cur.old_xyz,fast.cur.nv*3*sizeof(float)));
        f.scene=2;rsx_pose_prepare(&fast,&f,2./60);assert(!fast.capped_searches_skipped);
        rsx_pose_destroy(&fast);rsx_pose_destroy(&original);
    }
    puts("PASS capped buckets: boundary128, colliding keys, exact fallback, disable switch and scene invalidation");
}
int main() {
    capped_bucket_precheck();
    block_score_lower_bounds();
    reused_vertex_validation();
    validation_edges();
    vector_samples_and_score_bounds();
    cached_position_comparisons();
    camera_memo_low_reuse_and_recovery();
    cached_camera_comparisons();
    bounded_search_blocks();
    // Fast candidate filtering must not bypass the exhaustive jump check:
    // vertex 4 is NOT among the 16 sampled vertices of a 20-vertex draw.
    {Fixture jump;jump.d[0].count=20;rsx_pose_prepare(&jump.h,&jump.f,0);
     jump.v[4].xyz[0]+=100;assert(!rsx_pose_prepare(&jump.h,&jump.f,1./30));
     assert(!rsx_pose_draw_ready(&jump.h,0)&&jump.h.matched==0);}
    assert(rsx_pose_shader_inputs_known("float x=vf_buf.Load(0); VSOutput vf_main(VSInput input) { return o; }"));
    assert(!rsx_pose_shader_inputs_known("float x=vf_buf.Load(0); VSOutput vf_main(VSInput input) { return rsx_vtex0.Load(0); }"));
    assert(!rsx_pose_shader_inputs_known("VSOutput main(VSInput input) { return rsx_vtex0.SampleLevel(s,uv,0); }"));
    cadence_and_relocation();partial_lists_and_independent_tracks();nearest_instances_and_ambiguity();
    discontinuities_and_invalid_input();aliases_and_budgets();captured_camera_layout();coherent_multipass();shared_animation_edges();
    connected_seam_fallback_preserves_triangles();shared_upload_across_cameras(false);shared_upload_across_cameras(true);weld_limits_leave_proposals_untouched();lighter_projection_matches_current_light();animated_normals_are_not_identity();
    projection_axis_discontinuities();malformed_ranges_and_numeric_limits();bounded_motion_soak();workload();
    std::puts("PASS pose v21: connected seam/alias fallback preserves whole triangles and disconnected interpolation; conservative best-score block rejection; once-per-upload vertex validation and invalidation; GPU recipe provenance across seam propagation and conflicts; validated camera reuse, canonical hash reuse, numerical vector equality; exact position/matrix-pair memo, collision verification, epoch invalidation and prepared samples; bounded search, exhaustive jump checks, lighting policy, shared-view seams, 60 Hz cadence and 30 Hz speed");
}
