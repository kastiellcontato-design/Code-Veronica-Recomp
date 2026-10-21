#pragma once
#include "cv_appearance_preview.h"
#include "cv_appearance_catalog.h"
#include <chrono>
namespace cv_appearance {
// Owned by the guest menu thread. Selection is a preview index, not a change
// to CV_SRT_CHARACTER or a claim that the gameplay swap has been applied.
class AppearancePreview {
public:
 bool open(const std::filesystem::path& system_directory);
 void select(unsigned index);
 void append(std::vector<rsx_ui_quad>& out,float x,float y,float w,float h);
 const std::string& error() const {return error_;}
 unsigned selection() const {return selected_;}
 const PlayerAsset* asset(unsigned index) {return load(index)?assets_+index:nullptr;}
private:
 bool load(unsigned index);
 std::filesystem::path expanded_directory_;
 PlayerAsset assets_[appearance_count];PreviewModel models_[appearance_count];std::vector<rsx_ui_quad> pixels_;
 std::string error_;std::filesystem::path directory_;unsigned selected_=0;
 bool ready_=false;float px_=0,py_=0,pw_=0,ph_=0;
 std::chrono::steady_clock::time_point epoch_{},last_{};
};
}
