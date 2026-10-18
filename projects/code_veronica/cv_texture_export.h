#pragma once
#include <filesystem>
#include <cstdint>
struct CvTextureExportStatus {
    bool running=false, finished=false, canceled=false, failed=false;
    uint32_t files=0,total=0,written=0,existing=0,unsupported=0,errors=0;
};
// Source: biocv_disc, or one archive for offline tests. Never writes to source.
bool cv_texture_export_start(const std::filesystem::path& source,const std::filesystem::path& output);
void cv_texture_export_cancel();
CvTextureExportStatus cv_texture_export_status();
