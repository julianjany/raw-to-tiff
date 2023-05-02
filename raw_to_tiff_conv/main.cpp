#include <libraw.h>
#include <tinytiffwriter.h>

#include <filesystem>
#include <iostream>

int main() {
  LibRaw ip{};
  auto const raw_directory_path{
      std::filesystem::path{"..\\..\\imagingresource"}};
  auto const raw_files{std::filesystem::directory_iterator{raw_directory_path}};

  for (auto const& raw_filename : raw_files) {
    ip.open_file(raw_filename.path().c_str());
    ip.unpack();
    auto const& sizes = ip.imgdata.sizes;
    size_t const tiff_width{sizes.width / 2U};
    size_t const tiff_height{sizes.height / 2U};
    size_t const tiff_pixelcount{tiff_width * tiff_height};
    auto tiff_data{std::vector<uint16_t>(tiff_pixelcount * 3U, 0)};
    auto tiff_green_first{std::vector<bool>(tiff_pixelcount, true)};

    for (size_t row{sizes.top_margin};
         row < (size_t)sizes.height + sizes.top_margin; ++row) {
      for (size_t col{sizes.left_margin};
           col < (size_t)sizes.width + sizes.left_margin; ++col) {
        size_t const tiff_row{(row - sizes.top_margin) / 2};
        size_t const tiff_col{(col - sizes.left_margin) / 2};
        size_t const tiff_pixel_id{(tiff_row * tiff_width + tiff_col) * 3U};
        size_t const raw_pixel_id{row * sizes.raw_width + col};
        uint16_t const raw_value{ip.imgdata.rawdata.raw_image[raw_pixel_id]};

        switch (ip.imgdata.idata.cdesc[ip.COLOR(row, col)]) {
          case 'R':
            tiff_data[tiff_pixel_id] = raw_value;
            break;
          case 'G': {
            if (tiff_green_first[tiff_pixel_id / 3]) {
              tiff_data[tiff_pixel_id + 1] = raw_value;
              tiff_green_first[tiff_pixel_id / 3] = false;
            } else {
              uint16_t const prev{tiff_data[tiff_pixel_id + 1]};
              uint32_t const sum{static_cast<uint32_t>(prev) + raw_value};
              tiff_data[tiff_pixel_id + 1] = static_cast<uint16_t>(sum / 2);
            }
          } break;
          case 'B':
            tiff_data[tiff_pixel_id + 2] = raw_value;
            break;
          default:
            break;
        }
      }
    }

    TinyTIFFWriterFile* tif =
        TinyTIFFWriter_open("myfil.tif", 16, TinyTIFFWriter_UInt, 3, tiff_width,
                            tiff_height, TinyTIFFWriter_RGB);
    TinyTIFFWriter_writeImage(tif, tiff_data.data());
    TinyTIFFWriter_close(tif);

    std::cout << raw_filename << ": " << ip.imgdata.color.raw_bps << "bps\n";
    std::cout << raw_filename << ": " << ip.imgdata.idata.cdesc << "colors\n";

    return 0;

    ip.recycle();
  }
}