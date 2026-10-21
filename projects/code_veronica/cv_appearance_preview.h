#pragma once
#include "cv_appearance_assets.h"
#include "rsx_ui_batch.h"
namespace cv_appearance {
// Genuine orthographic 3D render of local game geometry; host menu only.
// Caller controls cadence; no gameplay actor/camera state is touched.
void render_preview(const PreviewModel& model,float yaw,std::vector<rsx_ui_quad>& quads,
                    float x,float y,float width,float height);
}
