
#include "Archive.h"
#include "common.h"
#include "libs/minizip/unzip.h"
#include "libs/minizip/zip.h"
#include <ctime>
#include <filesystem>

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

  auto time = std::tm{
    static_cast<int>(info.tmu_date.tm_sec),
    static_cast<int>(info.tmu_date.tm_min),
    static_cast<int>(info.tmu_date.tm_hour),
    static_cast<int>(info.tmu_date.tm_mday),
    static_cast<int>(info.tmu_date.tm_mon),
    static_cast<int>(info.tmu_date.tm_year) - 1900,
    0, 0, -1
  };
  return std::mktime(&time);
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

bool ArchiveWriter::open(std::filesystem::path filename) {
  if (m_zip)
    return false;

  m_filename = std::move(filename);

#if defined(_WIN32)
  auto filefunc = zlib_filefunc64_def{ };
  ::fill_win32_filefunc64W(&filefunc);
  m_zip = ::zipOpen2_64(m_filename.wstring().c_str(),
    APPEND_STATUS_CREATE, nullptr, &filefunc);
# else
  m_zip = ::zipOpen(m_filename.c_str(), APPEND_STATUS_CREATE);
#endif
  if (!m_zip)
    return false;
  
  start_thread();
  return true;
}

ArchiveWriter::~ArchiveWriter() {
  close();
}

void ArchiveWriter::move_on_close(std::filesystem::path filename, bool overwrite) {
  m_move_on_close = absolute(filename);
  m_overwrite = overwrite;
}

bool ArchiveWriter::close() {
  if (!m_zip)
    return true;

  finish_thread();

  ::zipClose(m_zip, nullptr);
  m_zip = nullptr;

  if (m_move_on_close.empty())
    return true;

  const auto move_on_close = resolve_collision(m_move_on_close, m_overwrite ?
    MoveCollisionResolution::overwrite : MoveCollisionResolution::rename);
  if (move_on_close.empty())
    return false;

  return move_file(m_filename, move_on_close);
}

bool ArchiveWriter::contains(const std::string& filename) const {
  return (m_filenames.count(filename) > 0);
}

bool ArchiveWriter::write(const std::string& filename, ByteView data, 
    time_t modification_time) {
  if (!update_filenames(filename))
    return false;

  return do_write(filename, data, modification_time);
}

void ArchiveWriter::async_write(const std::string& filename, ByteView data,
    time_t modification_time, std::function<void(bool)>&& on_complete) {
  if (!update_filenames(filename))
    return on_complete(false);

  auto tasks_lock = std::unique_lock(m_tasks_mutex);
  m_tasks.emplace_back(
    [this, filename, data, modification_time, 
     on_complete = std::move(on_complete)]() {
      on_complete(do_write(filename, data, modification_time));
    });
  if (m_tasks.size() == 1) {
    tasks_lock.unlock();
    m_tasks_signal.notify_one();
  }
}

bool ArchiveWriter::update_filenames(const std::string& filename) {
  if (starts_with(filename, "/"))
    return false;
  
  if (m_filenames.count(filename) > 0)
    return false;
  m_filenames.insert(filename);
  return true;
}

bool ArchiveWriter::do_write(const std::string& filename, ByteView data, 
    time_t modification_time) {
  auto lock = std::lock_guard(m_zip_mutex);
  if (!m_zip)
    return false;

  if (!modification_time)
    modification_time = std::time(nullptr);
  const auto local_time = std::localtime(&modification_time);
  const auto info = zip_fileinfo{
    tm_zip{
      static_cast<uInt>(local_time->tm_sec),
      static_cast<uInt>(local_time->tm_min),
      static_cast<uInt>(local_time->tm_hour),
      static_cast<uInt>(local_time->tm_mday),
      static_cast<uInt>(local_time->tm_mon),
      static_cast<uInt>(local_time->tm_year) + 1900
    },
    0, 0, 0,
  };
  if (::zipOpenNewFileInZip(m_zip, filename.c_str(),
      &info, nullptr, 0, nullptr, 0, nullptr, Z_DEFLATED, 
      is_likely_compressible(filename) ?
        Z_DEFAULT_COMPRESSION : Z_NO_COMPRESSION) != ZIP_OK)
    return false;
  
  ::zipWriteInFileInZip(m_zip, data.data(),
    static_cast<unsigned int>(data.size()));
  ::zipCloseFileInZip(m_zip);
  return true;
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
