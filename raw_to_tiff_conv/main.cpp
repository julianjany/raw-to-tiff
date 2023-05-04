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

int main() {
  LibRaw ip{};
  auto const raw_directory_path{
      std::filesystem::path{"..\\..\\photographyblog"}};
  auto const raw_files{std::filesystem::directory_iterator{raw_directory_path}};
  size_t const scale_factor{2U};
  static constexpr size_t channel_count{3U};

  for (auto const& raw_filename : raw_files) {
    ip.open_file(raw_filename.path().c_str());
    ip.unpack();

    {
      auto const& sizes{ip.imgdata.sizes};
      auto const& imgdata{ip.imgdata};
      auto const& rawdata{ip.imgdata.rawdata};
      size_t const tiff_width{sizes.width / scale_factor};
      size_t const tiff_height{sizes.height / scale_factor};
      size_t const tiff_pixelcount{tiff_width * tiff_height};
      auto tiff_data{std::vector<uint16_t>(tiff_pixelcount * channel_count, 0)};

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
              uint16_t const raw_value{rawdata.raw_image[raw_pixel_id]};

              int const rgb_id{get_rgb_id(ip, raw_row, raw_col)};
              accumulator[rgb_id].sum += raw_value;
              accumulator[rgb_id].count++;
            }
          }
          size_t const tiff_pixel_id{(tiff_row * tiff_width + tiff_col) *
                                     channel_count};
          for (size_t channel_id{0}; channel_id < channel_count; ++channel_id) {
            if (accumulator[channel_id].count != 0) {
              tiff_data[tiff_pixel_id + channel_id] =
                  accumulator[channel_id].sum / accumulator[channel_id].count;
            }
          }
        }
      }

      TinyTIFFWriterFile* tif = TinyTIFFWriter_open(
          "myfile.tif", 16, TinyTIFFWriter_UInt, channel_count, tiff_width,
          tiff_height, TinyTIFFWriter_RGB);
      TinyTIFFWriter_writeImage(tif, tiff_data.data());
      TinyTIFFWriter_close(tif);

      std::cout << raw_filename << ": " << ip.imgdata.color.raw_bps << "bps\n";
      std::cout << raw_filename << ": " << ip.imgdata.idata.cdesc << "colors\n";

      return 0;
    }

    ip.recycle();
  }
}