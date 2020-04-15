#pragma once

#include "common.h"
#include <optional>

class LossyCompressor {
public:
  std::optional<ByteVector> try_compress(ByteView data);

private:
  int m_skip_files_below{ 100 << 10 };
  int m_max_image_width{ 1280 };
  int m_max_image_height{ 720 };
};
