
#include "LossyCompressor.h"
#include <memory>

#if defined(FreeImage_FOUND)
#include <FreeImage.h>

namespace {
  class FreeImageBitmap {
  private:
    FIBITMAP* m_bitmap{ };
  public:
    FreeImageBitmap() = default;
    explicit FreeImageBitmap(FIBITMAP* bitmap)
      : m_bitmap(bitmap) {
    }
    FreeImageBitmap(FreeImageBitmap&& rhs) noexcept
      : m_bitmap(std::exchange(rhs.m_bitmap, nullptr)) {
    }
    FreeImageBitmap& operator=(FreeImageBitmap&& rhs) noexcept {
      auto tmp = std::move(rhs);
      std::swap(m_bitmap, tmp.m_bitmap);
      return *this;
    }
    ~FreeImageBitmap() {
      if (m_bitmap)
        FreeImage_Unload(m_bitmap);
    }
    operator FIBITMAP*() const { return m_bitmap; }
  };

  void init_freeimage() {
    struct StaticInit {
      StaticInit() { FreeImage_Initialise(); }
      ~StaticInit() { FreeImage_DeInitialise(); }
    };
    static const StaticInit s_static_init;
  }

  int get_load_flags(FREE_IMAGE_FORMAT image_format) {
    auto load_flags = 0;
    if (image_format == FIF_JPEG)
      load_flags |= JPEG_ACCURATE;
    return load_flags;
  }

  FreeImageBitmap load_image(ByteView data, FREE_IMAGE_FORMAT image_format) {
    init_freeimage();

    auto stream = std::unique_ptr<FIMEMORY, decltype(&FreeImage_CloseMemory)>(
      FreeImage_OpenMemory(
        reinterpret_cast<BYTE*>(const_cast<std::byte*>(data.data())),
        static_cast<DWORD>(data.size())),
      &FreeImage_CloseMemory);

    return FreeImageBitmap(FreeImage_LoadFromMemory(
      image_format, stream.get(), get_load_flags(image_format)));
  }

  std::pair<FreeImageBitmap, FREE_IMAGE_FORMAT> load_image(ByteView data) {
    for (auto image_format : { FIF_JPEG, FIF_PNG, FIF_WEBP, FIF_BMP })
      if (auto image = load_image(data, image_format))
        return std::make_pair(std::move(image), image_format);
    return { };
  }

  int get_save_flags(FREE_IMAGE_FORMAT image_format) {
    auto save_flags = 0;
    if (image_format == FIF_JPEG)
      save_flags |= (JPEG_QUALITYGOOD | JPEG_SUBSAMPLING_420);
    return save_flags;
  }

  FreeImageBitmap resize_image(const FreeImageBitmap& image, double factor) {
    return FreeImageBitmap(FreeImage_Rescale(image,
        static_cast<int>(FreeImage_GetWidth(image) * factor),
        static_cast<int>(FreeImage_GetHeight(image) * factor)));
  }

  FreeImageBitmap convert_to_rgb24(const FreeImageBitmap& image) {
    return FreeImageBitmap(FreeImage_ConvertTo24Bits(image));
  }

  ByteVector save_image(const FreeImageBitmap& image, FREE_IMAGE_FORMAT image_format) {
    auto stream = std::unique_ptr<FIMEMORY, decltype(&FreeImage_CloseMemory)>(
      FreeImage_OpenMemory(), &FreeImage_CloseMemory);
    if (!FreeImage_SaveToMemory(image_format, image,
        stream.get(), get_save_flags(image_format)))
      return { };
    auto data = std::add_pointer_t<BYTE>{ };
    auto size = DWORD{ };
    if (!FreeImage_AcquireMemory(stream.get(), &data, &size))
      return { };
    return { reinterpret_cast<std::byte*>(data), reinterpret_cast<std::byte*>(data) + size };
  }
} // namespace

#endif // FreeImage_FOUND

std::optional<ByteVector> LossyCompressor::try_compress(ByteView data) {
  if (data.size() < m_skip_files_below)
    return { };

#if defined(FreeImage_FOUND)
  auto [image, original_format] = load_image(data);
  if (!image)
    return { };
  const auto original = static_cast<void*>(image);

  // convert to RGB
  if (FreeImage_IsTransparent(image))
    return { };
  if (FreeImage_GetBPP(image) != 24)
    image = convert_to_rgb24(image);

  // limit dimensions
  const auto size = std::max(
    FreeImage_GetWidth(image) / static_cast<double>(m_max_image_width),
    FreeImage_GetHeight(image) / static_cast<double>(m_max_image_height));
  if (size > 1.0)
    image = resize_image(image, 1.0 / size);

  // check if anything changed
  if (image == original && original_format == FIF_JPEG)
    return { };

  // compress to JPEG
  auto compressed = save_image(image, FIF_JPEG);

  // check if size was reduced
  if (!compressed.empty() &&
      compressed.size() < static_cast<size_t>(data.size()))
    return compressed;
#endif // FreeImage_FOUND

  return { };
}
