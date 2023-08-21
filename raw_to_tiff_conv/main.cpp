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
  size_t const scale_factor{2U};
  static constexpr size_t channel_count{3U};

  for (auto const& raw_filename : raw_files) {
    ip.open_file(raw_filename.path().c_str());
    ip.unpack();

    {  // process single image
      auto const& sizes{ip.imgdata.sizes};
      auto const& imgdata{ip.imgdata};
      auto const& rawdata{ip.imgdata.rawdata};
      size_t const tiff_width{sizes.width / scale_factor};
      size_t const tiff_height{sizes.height / scale_factor};
      size_t const tiff_pixelcount{tiff_width * tiff_height};
      auto tiff_data{std::vector<uint16_t>(tiff_pixelcount * channel_count, 0)};

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

      for (size_t tiff_row{0}; tiff_row < tiff_height; ++tiff_row) {
        for (size_t tiff_col{0}; tiff_col < tiff_width; ++tiff_col) {
          // accumulate raw values for 'scale-region'
          auto accumulator{std::array<accumulator_t, 3>{}};
          for (size_t raw_row_offset{0}; raw_row_offset < scale_factor;
               ++raw_row_offset) {
            for (size_t raw_col_offset{0}; raw_col_offset < scale_factor;
                 ++raw_col_offset) {
              size_t const raw_row{tiff_row * scale_factor + raw_row_offset +
                                   sizes.top_margin};
              size_t const raw_col{tiff_col * scale_factor + raw_col_offset +
                                   sizes.left_margin};
              size_t const raw_pixel_id{raw_row * sizes.raw_width + raw_col};

              auto const raw_shift{
                  static_cast<uint16_t>(16U - imgdata.color.raw_bps)};
              auto const raw_value{static_cast<uint16_t>(
                  rawdata.raw_image[raw_pixel_id] << raw_shift)};

              int const rgb_id{get_rgb_id(ip, raw_row, raw_col)};
              accumulator[rgb_id].sum += raw_value;
              accumulator[rgb_id].count++;
            }
          }
          size_t const tiff_pixel_id{(tiff_row * tiff_width + tiff_col) *
                                     channel_count};
          for (size_t channel_id{0}; channel_id < channel_count; ++channel_id) {
            if (accumulator[channel_id].count != 0) {
              tiff_data[tiff_pixel_id + channel_id] = static_cast<uint16_t>(
                  accumulator[channel_id].sum / accumulator[channel_id].count);
            }
          }
        }
      }

      {  // store as tiff
        auto const raw_filename_ext{raw_filename.path().filename().string()};
        auto const raw_filename{
            raw_filename_ext.substr(0, raw_filename_ext.find_last_of('.'))};
        auto const tiff_filename{std::format("{}.tiff", raw_filename)};
        TinyTIFFWriterFile* p_tiff = TinyTIFFWriter_open(
            tiff_filename.c_str(), 16, TinyTIFFWriter_UInt, channel_count,
            tiff_width, tiff_height, TinyTIFFWriter_RGB);
        TinyTIFFWriter_writeImage(p_tiff, tiff_data.data());
        auto const metadata{get_metadata_string(imgdata, scale_factor)};
        TinyTIFFWriter_close_withdescription(p_tiff, metadata.c_str());
        std::cout << std::format("processed {}\n", raw_filename_ext);
      }
    }

    ip.recycle();
  }
}