
#include "Archive.h"
#include "common.h"
#include "libs/minizip/unzip.h"
#include "libs/minizip/zip.h"
#include "LossyCompressor.h"
#include <ctime>
#include <filesystem>
#include <string>

#if defined(_WIN32)
# if !defined(UNICODE)
#   error "please compile with UNICODE (see iowin32.c)"
# endif
# include "libs/minizip/iowin32.h"
#endif

namespace {
  enum class MoveCollisionResolution { fail, overwrite, rename };

  bool is_likely_compressible(std::string_view filename) {
    const auto extension = get_file_extension(filename);
    return (!iequals_any(extension,
      "jpg", "jpeg", "png", "gif", "webp", "otf", "woff", "woff2"));
  }

  std::filesystem::path resolve_collision(const std::filesystem::path& target,
      MoveCollisionResolution collision_resolution) {

    auto error = std::error_code{ };
    if (!std::filesystem::exists(target, error) && !error)
      return target;

    switch (collision_resolution) {
      case MoveCollisionResolution::fail:
        break;

      case MoveCollisionResolution::overwrite:
        std::filesystem::remove(target, error);
        if (!error)
          return target;
        break;

      case MoveCollisionResolution::rename:
        for (auto i = 2; i < 100; ++i) {
          auto renamed = target;
          renamed += " [" + std::to_string(i) + "]";
          if (!std::filesystem::exists(renamed, error) && !error)
            return renamed;
        }
        break;
    }
    return { };
  }

  bool move_file(const std::filesystem::path& source, const std::filesystem::path& target) {
    auto error = std::error_code{ };
    std::filesystem::rename(source, target, error);
    if (error) {
      std::filesystem::copy(source, target, error);
      if (error)
        return false;
      std::filesystem::remove(source, error);
    }
    return true;
  }

  tm_zip to_tm_zip(time_t time) {
    const auto local_time = std::localtime(&time);
    return tm_zip{
      static_cast<uInt>(local_time->tm_sec),
      static_cast<uInt>(local_time->tm_min),
      static_cast<uInt>(local_time->tm_hour),
      static_cast<uInt>(local_time->tm_mday),
      static_cast<uInt>(local_time->tm_mon),
      static_cast<uInt>(local_time->tm_year) + 1900
    };
  }

  time_t to_time_t(tm_unz tmu_date) {
    auto time = std::tm{ };
    time.tm_sec = static_cast<int>(tmu_date.tm_sec);
    time.tm_min = static_cast<int>(tmu_date.tm_min);
    time.tm_hour = static_cast<int>(tmu_date.tm_hour);
    time.tm_mday = static_cast<int>(tmu_date.tm_mday);
    time.tm_mon = static_cast<int>(tmu_date.tm_mon);
    time.tm_year = static_cast<int>(tmu_date.tm_year) - 1900;
    time.tm_isdst = -1;
    return std::mktime(&time);
  }
} // namespace

ArchiveReader::~ArchiveReader() {
  close();
}

bool ArchiveReader::open(const std::filesystem::path& filename) {
  close();

  m_filename = filename;
  if (auto unzip = acquire_context()) {
    return_context(unzip);
    return true;
  }
  return false;
}

void* ArchiveReader::acquire_context() const {
  auto lock = std::lock_guard(m_mutex);

  if (!m_unzip_contexts.empty()) {
    auto unzip = m_unzip_contexts.back();
    m_unzip_contexts.pop_back();
    return unzip;
  }

#if defined(_WIN32)
  auto filefunc = zlib_filefunc64_def{ };
  ::fill_win32_filefunc64W(&filefunc);
  return ::unzOpen2_64(m_filename.wstring().c_str(), &filefunc);
#else
  return ::unzOpen64(m_filename.c_str());
#endif
}

void ArchiveReader::return_context(void* context) const {
  auto lock = std::lock_guard(m_mutex);
  m_unzip_contexts.push_back(context);
}

void ArchiveReader::close() {
  auto lock = std::lock_guard(m_mutex);
  for (auto* unzip : m_unzip_contexts)
    ::unzClose(unzip);
  m_unzip_contexts.clear();
}

time_t ArchiveReader::get_modification_time(const std::string& filename) const {
  auto unzip = acquire_context();
  if (!unzip)
    return { };
  auto guard = std::shared_ptr<void>(nullptr, [&](auto) { return_context(unzip); });

  if (::unzLocateFile(unzip, filename.c_str(), 0) != UNZ_OK)
    return { };

  auto info = unz_file_info{ };
  ::unzGetCurrentFileInfo(unzip, &info,
    nullptr, 0, nullptr, 0, nullptr, 0);

  return to_time_t(info.tmu_date);
}

ByteVector ArchiveReader::read(const std::string& filename) const {
  auto unzip = acquire_context();
  if (!unzip)
    return { };
  auto guard = std::shared_ptr<void>(nullptr, [&](auto) { return_context(unzip); });

  if (::unzLocateFile(unzip, filename.c_str(), 0) != UNZ_OK ||
      ::unzOpenCurrentFile(unzip) != UNZ_OK)
    return { };

  auto info = unz_file_info{ };
  ::unzGetCurrentFileInfo(unzip, &info,
    nullptr, 0, nullptr, 0, nullptr, 0);
  auto buffer = ByteVector(info.uncompressed_size);
  ::unzReadCurrentFile(unzip, buffer.data(),
    static_cast<unsigned int>(info.uncompressed_size));
  ::unzCloseCurrentFile(unzip);

  return buffer;
}

void ArchiveReader::for_each_file(const std::function<void(std::string)>& callback) const {
  auto unzip = acquire_context();
  if (!unzip)
    return;
  auto guard = std::shared_ptr<void>(nullptr, [&](auto) { return_context(unzip); });

  auto filename = std::vector<char>(255 + 1);
  for (::unzGoToFirstFile(unzip); ::unzGoToNextFile(unzip) == UNZ_OK; ) {
    ::unzGetCurrentFileInfo(unzip, nullptr,
      filename.data(), filename.size(), nullptr, 0, nullptr, 0);
    callback(filename.data());
  }
}

//-------------------------------------------------------------------------

ArchiveWriter::ArchiveWriter() = default;

ArchiveWriter::~ArchiveWriter() {
  close();
}

void ArchiveWriter::set_lossy_compressor(
    std::unique_ptr<LossyCompressor> lossy_compressor) {
  auto lock = std::lock_guard(m_zip_mutex);
  m_lossy_compressor = std::move(lossy_compressor);
}

bool ArchiveWriter::open(std::filesystem::path filename) {
  if (!m_filename.empty() || filename.empty())
    return false;

  m_filename = std::move(filename);
  if (!reopen(false))
    return false;

  start_thread();
  return true;
}

void ArchiveWriter::move_on_close(std::filesystem::path filename, bool overwrite) {
  m_move_on_close = absolute(filename);
  m_overwrite = overwrite;
}

bool ArchiveWriter::close() {
  if (m_filename.empty())
    return false;

  finish_thread();
  do_close();

  auto filename = std::exchange(m_filename, { });
  if (!m_move_on_close.empty()) {
    const auto move_on_close = resolve_collision(m_move_on_close, m_overwrite ?
      MoveCollisionResolution::overwrite : MoveCollisionResolution::rename);
    if (move_on_close.empty())
      return false;
    return move_file(filename, move_on_close);
  }
  return true;
}

bool ArchiveWriter::contains(const std::string& filename) const {
  return (m_filenames.count(filename) > 0);
}

bool ArchiveWriter::write(const std::string& filename, ByteView data,
    time_t modification_time, bool allow_lossy_compression) {
  if (!update_filenames(filename))
    return false;

  return do_write(filename, data, modification_time, allow_lossy_compression);
}

void ArchiveWriter::async_write(const std::string& filename, ByteView data,
    time_t modification_time, bool allow_lossy_compression,
    std::function<void(bool)>&& on_complete) {

  if (!update_filenames(filename))
    return on_complete(false);

  insert_task([this, filename, data, modification_time,
      allow_lossy_compression, on_complete = std::move(on_complete)]() {
    on_complete(do_write(filename, data, modification_time, allow_lossy_compression));
  });
}

void ArchiveWriter::async_read(const std::string& filename,
    std::function<void(ByteVector, time_t)>&& on_complete) {
  insert_task([this, filename, on_complete = std::move(on_complete)]() {
    auto [data, time] = do_read(filename);
    on_complete(std::move(data), time);
  });
}

bool ArchiveWriter::update_filenames(const std::string& filename) {
  if (filename.empty() || starts_with(filename, "/"))
    return false;
  if (m_filenames.count(filename) > 0)
    return false;
  m_filenames.insert(filename);
  return true;
}

void ArchiveWriter::do_close() {
  if (m_reading)
    ::unzClose(m_zip);
  else
    ::zipClose(m_zip, nullptr);
  m_zip = nullptr;
}

bool ArchiveWriter::reopen(bool for_reading) {
  if (m_zip && for_reading == m_reading)
    return true;
  const auto write_mode =
    (m_zip ? APPEND_STATUS_ADDINZIP : APPEND_STATUS_CREATE);
  do_close();
  m_reading = for_reading;

#if defined(_WIN32)
  auto filefunc = zlib_filefunc64_def{ };
  ::fill_win32_filefunc64W(&filefunc);
  m_zip = (m_reading ?
    ::unzOpen2_64(m_filename.wstring().c_str(), &filefunc) :
    ::zipOpen2_64(m_filename.wstring().c_str(),
      write_mode, nullptr, &filefunc));
# else
  m_zip = (m_reading ?
    ::unzOpen64(m_filename.c_str()) :
    ::zipOpen(m_filename.c_str(), write_mode));
#endif
  return (m_zip != nullptr);
}

bool ArchiveWriter::do_write(const std::string& filename, ByteView data,
    time_t modification_time, bool allow_lossy_compression) {
  auto lock = std::lock_guard(m_zip_mutex);
  if (!reopen(false))
    return false;

  auto lossless_compression = is_likely_compressible(filename);

  auto lossy_compressed_data = std::optional<ByteVector>();
  if (allow_lossy_compression && m_lossy_compressor) {
    lossy_compressed_data = m_lossy_compressor->try_compress(data);
    if (lossy_compressed_data.has_value()) {
      data = lossy_compressed_data.value();
      lossless_compression = false;
    }
  }

  if (!modification_time)
    modification_time = std::time(nullptr);
  const auto info = zip_fileinfo{
    to_tm_zip(modification_time),
    0, 0, 0,
  };
  if (::zipOpenNewFileInZip(m_zip, filename.c_str(),
      &info, nullptr, 0, nullptr, 0, nullptr,
      Z_DEFLATED, (lossless_compression ?
        Z_DEFAULT_COMPRESSION : Z_NO_COMPRESSION)) != ZIP_OK)
    return false;

  ::zipWriteInFileInZip(m_zip, data.data(),
    static_cast<unsigned int>(data.size()));
  ::zipCloseFileInZip(m_zip);
  return true;
}


std::pair<ByteVector, time_t> ArchiveWriter::do_read(const std::string& filename) {
  auto lock = std::lock_guard(m_zip_mutex);
  if (!reopen(true))
    return { };

  if (::unzLocateFile(m_zip, filename.c_str(), 0) != UNZ_OK ||
      ::unzOpenCurrentFile(m_zip) != UNZ_OK)
    return { };

  auto info = unz_file_info{ };
  ::unzGetCurrentFileInfo(m_zip, &info,
    nullptr, 0, nullptr, 0, nullptr, 0);
  auto buffer = ByteVector(info.uncompressed_size);
  ::unzReadCurrentFile(m_zip, buffer.data(),
    static_cast<unsigned int>(info.uncompressed_size));
  ::unzCloseCurrentFile(m_zip);

  return std::make_pair(std::move(buffer), to_time_t(info.tmu_date));
}

void ArchiveWriter::insert_task(std::function<void()>&& task) {
  auto tasks_lock = std::unique_lock(m_tasks_mutex);
  m_tasks.emplace_back(std::move(task));
  if (m_tasks.size() == 1) {
    tasks_lock.unlock();
    m_tasks_signal.notify_one();
  }
}

void ArchiveWriter::start_thread() {
  m_finish_thread = false;
  m_thread = std::thread(&ArchiveWriter::thread_func, this);
}

void ArchiveWriter::finish_thread() {
  auto lock = std::unique_lock(m_tasks_mutex);
  m_finish_thread = true;
  lock.unlock();
  m_tasks_signal.notify_one();
  if (m_thread.joinable())
    m_thread.join();
}

void ArchiveWriter::thread_func() {
  for (;;) {
    auto lock = std::unique_lock(m_tasks_mutex);
    m_tasks_signal.wait(lock,
      [&]() { return m_finish_thread || !m_tasks.empty(); });
    if (m_tasks.empty())
      break;
    auto task = std::move(m_tasks.front());
    m_tasks.pop_front();
    lock.unlock();
    task();
  }
}
