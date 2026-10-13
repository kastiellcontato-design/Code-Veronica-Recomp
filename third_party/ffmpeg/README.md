# FFmpeg runtime for Code Veronica

Headers: FFmpeg 7.1, https://ffmpeg.org/releases/ffmpeg-7.1.tar.xz
Shared libraries: FFmpeg n7.1.5, originally bundled with the local PySide6 installation.
`avcodec_license()` reports LGPL version 2.1 or later. The decoder uses the public
FFmpeg API and checks libavcodec major version 61 before opening it.
Only actual decoded frames generate video PICOUT messages.

Runtime libraries are copied beside the executable by CMake. Source and license:
https://ffmpeg.org/ and https://github.com/FFmpeg/FFmpeg/tree/n7.1.5

SHA256:
- avcodec-61.dll: 3823070d38dc7dce277ccec165f99bbc02ad96bacc9afde1a00818e9d8350eb6
- avutil-59.dll: 9d744dc6d44189a690d30b491b53aec1a9822a59aeb7b6f79253d219c0f0862d
- swresample-5.dll: 2f709b4d0ca771b22f2fa61ce26355c85f49a61203de041e30acb499cc2ad11a
- swscale-8.dll: 91fec4aa86bcac65778f8974ceea58fa1bf8696ad693fc4b478a7e2d43ade6bb
