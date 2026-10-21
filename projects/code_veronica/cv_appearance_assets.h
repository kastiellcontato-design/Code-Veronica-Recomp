#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
namespace cv_appearance {
struct Resource { std::string name; uint32_t type=0; std::vector<uint8_t> bytes; };
struct PlayerAsset {
    std::vector<uint8_t> model, motion, translation;
    std::vector<Resource> resources;
    uint32_t body_nodes=0;
};
struct PreviewVertex {float x,y,z,nx,ny,nz,u,v;};
struct PreviewTriangle {PreviewVertex vertices[3];uint32_t texture;};
struct PreviewTexture {uint32_t width=0,height=0;std::vector<uint32_t> argb;};
struct PreviewModel {std::vector<PreviewTriangle> triangles;std::vector<PreviewTexture> textures;};
bool make_preview(const PlayerAsset& asset,PreviewModel& out,std::string& error);
bool add_preview_glasses(const PlayerAsset& asset,PreviewModel& out,std::string& error);
bool add_preview_hands(const PlayerAsset& asset,const std::vector<Resource>& hands,PreviewModel& out,std::string& error);
bool load_archive_resources(const std::filesystem::path& archive,std::vector<Resource>& out,std::string& error);
// Host-only decoding. No writes to guest memory, archive files, or actor identity.
bool load_player_asset(const std::filesystem::path& archive,PlayerAsset& out,std::string& error);
bool compatible_player_skin(const PlayerAsset& original,const PlayerAsset& skin,std::string& error);
bool compatible_claire_variant(const PlayerAsset& original,const PlayerAsset& variant,std::string& error);
}
