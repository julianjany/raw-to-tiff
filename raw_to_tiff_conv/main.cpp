#include <libraw.h>
#include <tinytiffwriter.h>

#include <array>
#include <filesystem>
#include <iostream>
#include <vector>

struct accumulator_t {
  uint64_t sum{0};
  uint64_t count{0};
};

int get_rgb_id(LibRaw& ip, int const row, int const col) {
  switch (ip.imgdata.idata.cdesc[ip.COLOR(row, col)]) {
    case 'R':
      return 0;
    case 'G':
      return 1;
    case 'B':
      return 2;

    default:
      return -1;
  }
}

char get_rgb_char(LibRaw& ip, int const row, int const col) {
  return ip.imgdata.idata.cdesc[ip.COLOR(row, col)];
}

std::string get_metadata_string(libraw_data_t const& imgdata,
                                size_t const scale_factor) {
  auto const cam_mul{
      std::format(R"("cam_mul": [ {}, {}, {}, {} ])", imgdata.color.cam_mul[0],
                  imgdata.color.cam_mul[1], imgdata.color.cam_mul[2],
                  imgdata.color.cam_mul[3])};

  auto const cblack{std::format(
      R"("cblack": [ {}, {}, {}, {}, {}, {}, {}, {}, {}, {} ])",
      imgdata.color.cblack[0], imgdata.color.cblack[1], imgdata.color.cblack[2],
      imgdata.color.cblack[3], imgdata.color.cblack[4], imgdata.color.cblack[5],
      imgdata.color.cblack[6], imgdata.color.cblack[7], imgdata.color.cblack[8],
      imgdata.color.cblack[9])};

  auto const rgb_cam{std::format(
      R"("rgb_cam": [
  [ {}, {}, {}, {} ],
  [ {}, {}, {}, {} ],
  [ {}, {}, {}, {} ]
])",
      imgdata.color.rgb_cam[0][0], imgdata.color.rgb_cam[0][1],
      imgdata.color.rgb_cam[0][2], imgdata.color.rgb_cam[0][3],
      imgdata.color.rgb_cam[1][0], imgdata.color.rgb_cam[1][1],
      imgdata.color.rgb_cam[1][2], imgdata.color.rgb_cam[1][3],
      imgdata.color.rgb_cam[2][0], imgdata.color.rgb_cam[2][1],
      imgdata.color.rgb_cam[2][2], imgdata.color.rgb_cam[2][3])};

  auto const metadata{std::format(
      R"({{
  "raw_bps": {},
  "iso_speed": {},
  "scale_factor": {},
  "camera": "{} {}",
  "lens": "{} {}",
  "cc_params": {{
  {},
  {},
  {}
  }}
}})",
      imgdata.color.raw_bps, imgdata.other.iso_speed, scale_factor,
      imgdata.idata.normalized_make, imgdata.idata.normalized_model,
      imgdata.lens.LensMake, imgdata.lens.Lens, cam_mul, cblack, rgb_cam)};

  return metadata;
}

int main(int argc, char const* argv[]) {
  if (argc < 2) {
    std::cout << "supply a filename\n";
  }

  LibRaw ip{};
  auto const raw_directory_path{std::filesystem::path{argv[1]}};
  auto const raw_files{std::filesystem::directory_iterator{raw_directory_path}};

  for (auto const& raw_filepath : raw_files) {
    auto const raw_filename_ext{raw_filepath.path().filename().string()};
    auto const file_ext_id{raw_filename_ext.find_last_of('.')};
    if (file_ext_id == std::string::npos) {
      continue;
    }
    auto const file_ext{raw_filename_ext.substr(file_ext_id)};
    if (file_ext != ".ARW") {
      // std::cout << "ignored " << raw_filepath;
      continue;
    }

    ip.open_file(raw_filepath.path().c_str());
    ip.unpack();

    {  // process single image
      auto const& sizes{ip.imgdata.sizes};
      auto const& imgdata{ip.imgdata};
      auto const& rawdata{ip.imgdata.rawdata};
      size_t const tiff_width{sizes.width};
      size_t const tiff_height{sizes.height};
      size_t const tiff_pixelcount{tiff_width * tiff_height};
      auto tiff_data{std::vector<uint16_t>(tiff_pixelcount, 0)};

      // std::cout << "whole sensor (top left)\n";
      // for (size_t raw_row{0}; raw_row < 4; ++raw_row) {
      //   for (size_t raw_col{0}; raw_col < 4; ++raw_col) {
      //     std::cout << get_rgb_char(ip, raw_row, raw_col);
      //   }
      //   std::cout << '\n';
      // }

      // std::cout << "sensor visible area (top left)\n";
      // for (size_t raw_row{0}; raw_row < 4; ++raw_row) {
      //   for (size_t raw_col{0}; raw_col < 4; ++raw_col) {
      //     std::cout << get_rgb_char(ip, raw_row + sizes.top_margin,
      //                               raw_col + sizes.left_margin);
      //   }
      //   std::cout << '\n';
      // }

      auto const raw_shift{static_cast<uint16_t>(16U - imgdata.color.raw_bps)};

      for (size_t tiff_row{0}; tiff_row < tiff_height; ++tiff_row) {
        for (size_t tiff_col{0}; tiff_col < tiff_width; ++tiff_col) {
          size_t const raw_row{tiff_row + sizes.top_margin};
          size_t const raw_col{tiff_col + sizes.left_margin};
          size_t const raw_pixel_id{raw_row * sizes.raw_width + raw_col};

          auto const raw_value{static_cast<uint16_t>(
              rawdata.raw_image[raw_pixel_id] << raw_shift)};

          size_t const tiff_pixel_id{tiff_row * tiff_width + tiff_col};
          tiff_data[tiff_pixel_id] = raw_value;
        }
      }

      {  // store as tiff
        auto const raw_filename_ext{raw_filepath.path().filename().string()};
        auto const raw_filename{
            raw_filename_ext.substr(0, raw_filename_ext.find_last_of('.'))};
        auto const tiff_filename{std::format("{}.tiff", raw_filename)};
        TinyTIFFWriterFile* p_tiff = TinyTIFFWriter_open(
            tiff_filename.c_str(), 16, TinyTIFFWriter_UInt, 1U, tiff_width,
            tiff_height, TinyTIFFWriter_Greyscale);
        TinyTIFFWriter_writeImage(p_tiff, tiff_data.data());
        auto const metadata{get_metadata_string(imgdata, 1)};
        TinyTIFFWriter_close_withdescription(p_tiff, metadata.c_str());
        std::cout << std::format("processed {}\n", raw_filename_ext);
      }
    }

    ip.recycle();
  }
}