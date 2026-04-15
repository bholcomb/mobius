#include <mobius/mobius_plugin.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <system_error>
#include <vector>

#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include "miniz/miniz.c"
#include "microtar/microtar.c"

#if __has_include(<zlib.h>)
#include <zlib.h>
#define MOBIUS_COMPRESSION_HAS_ZLIB 1
#else
#define MOBIUS_COMPRESSION_HAS_ZLIB 0
#endif

#if __has_include(<zstd.h>)
#include <zstd.h>
#define MOBIUS_COMPRESSION_HAS_ZSTD 1
#else
#define MOBIUS_COMPRESSION_HAS_ZSTD 0
#endif

namespace {

namespace fs = std::filesystem;

constexpr size_t kChunkSize = 64 * 1024;

struct FormatInfo {
    std::string format;
    std::string kind;
    std::string archive_format;
    std::string compression_format;
    bool supported = false;
};

struct CommonOptions {
    std::string format;
    std::string overwrite = "error";
    int compression_level = -1;
    std::string root_in_archive;
    std::string tar_compression;
    int strip_components = 0;
};

struct ArchiveEntry {
    std::string path;
    std::string type;
    uint64_t size = 0;
    uint64_t compressed_size = 0;
    bool has_compressed_size = false;
    uint32_t mode = 0;
    bool has_mode = false;
    uint64_t mtime = 0;
    bool has_mtime = false;
};

struct LocalInputEntry {
    fs::path disk_path;
    std::string archive_path;
    bool is_dir = false;
};

struct ScopedTempFile {
    fs::path path;
    ~ScopedTempFile() {
        if (!path.empty()) {
            std::error_code ec;
            fs::remove(path, ec);
        }
    }
};

static bool file_exists(const std::string& path) {
    struct stat st;
    return ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

static bool path_exists(const std::string& path) {
    std::error_code ec;
    return fs::exists(fs::u8path(path), ec);
}

static uint64_t file_size_or_zero(const std::string& path) {
    struct stat st;
    if (::stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) return 0;
    return static_cast<uint64_t>(st.st_size);
}

static std::string lower_copy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return s;
}

static bool ends_with(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

static std::string to_generic_string(const fs::path& path) {
    return path.generic_string();
}

static std::string ensure_trailing_slash(std::string s) {
    if (!s.empty() && s.back() != '/') s.push_back('/');
    return s;
}

static std::string join_archive_path(const std::string& left, const std::string& right) {
    if (left.empty()) return right;
    if (right.empty()) return left;
    if (left.back() == '/') return left + right;
    return left + "/" + right;
}

static bool create_parent_directories(const fs::path& path, std::string& error) {
    std::error_code ec;
    fs::path parent = path.parent_path();
    if (parent.empty()) return true;
    fs::create_directories(parent, ec);
    if (ec) {
        error = "failed creating parent directories";
        return false;
    }
    return true;
}

static bool create_directory_tree(const fs::path& path, std::string& error) {
    std::error_code ec;
    fs::create_directories(path, ec);
    if (ec) {
        error = "failed creating directory";
        return false;
    }
    return true;
}

static bool write_entire_file(const std::string& path, const std::vector<unsigned char>& data, std::string& error) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        error = "unable to open output file";
        return false;
    }
    if (!data.empty()) {
        out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        if (!out) {
            error = "failed writing output file";
            return false;
        }
    }
    return true;
}

static bool read_prefix(const std::string& path, std::vector<unsigned char>& out, size_t max_bytes) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out.resize(max_bytes);
    in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(max_bytes));
    out.resize(static_cast<size_t>(in.gcount()));
    return true;
}

static bool read_entire_file(const std::string& path, std::vector<unsigned char>& out, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "unable to open input file";
        return false;
    }
    in.seekg(0, std::ios::end);
    std::streamoff end = in.tellg();
    if (end < 0) {
        error = "unable to determine input file size";
        return false;
    }
    in.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(end));
    if (end > 0) {
        in.read(reinterpret_cast<char*>(out.data()), end);
        if (!in) {
            error = "failed reading input file";
            return false;
        }
    }
    return true;
}

static FormatInfo format_from_name(const std::string& raw) {
    FormatInfo info;
    std::string fmt = lower_copy(raw);
    if (fmt == "mz") fmt = "zip";

    if (fmt == "zip") {
        info.format = "zip";
        info.kind = "archive";
        info.archive_format = "zip";
        info.supported = false;
    } else if (fmt == "tar") {
        info.format = "tar";
        info.kind = "archive";
        info.archive_format = "tar";
        info.supported = true;
    } else if (fmt == "tar.gz" || fmt == "tgz") {
        info.format = "tar.gz";
        info.kind = "archive";
        info.archive_format = "tar";
        info.compression_format = "gzip";
        info.supported = true;
    } else if (fmt == "tar.zst" || fmt == "tzst") {
        info.format = "tar.zst";
        info.kind = "archive";
        info.archive_format = "tar";
        info.compression_format = "zstd";
        info.supported = true;
    } else if (fmt == "gzip" || fmt == "gz") {
        info.format = "gzip";
        info.kind = "compressed_stream";
        info.compression_format = "gzip";
        info.supported = MOBIUS_COMPRESSION_HAS_ZLIB;
    } else if (fmt == "zstd" || fmt == "zst") {
        info.format = "zstd";
        info.kind = "compressed_stream";
        info.compression_format = "zstd";
        info.supported = MOBIUS_COMPRESSION_HAS_ZSTD;
    }
    return info;
}

static FormatInfo infer_format_from_path(const std::string& path) {
    std::string lower = lower_copy(path);
    if (ends_with(lower, ".tar.gz") || ends_with(lower, ".tgz")) return format_from_name("tar.gz");
    if (ends_with(lower, ".tar.zst") || ends_with(lower, ".tzst")) return format_from_name("tar.zst");
    if (ends_with(lower, ".zip") || ends_with(lower, ".mz")) return format_from_name("zip");
    if (ends_with(lower, ".tar")) return format_from_name("tar");
    if (ends_with(lower, ".gz")) return format_from_name("gzip");
    if (ends_with(lower, ".zst")) return format_from_name("zstd");
    return {};
}

static FormatInfo detect_format_from_magic(const std::string& path) {
    std::vector<unsigned char> prefix;
    if (!read_prefix(path, prefix, 512)) return {};

    if (prefix.size() >= 4 && prefix[0] == 0x50 && prefix[1] == 0x4B &&
        (prefix[2] == 0x03 || prefix[2] == 0x05 || prefix[2] == 0x07) &&
        (prefix[3] == 0x04 || prefix[3] == 0x06 || prefix[3] == 0x08)) {
        return format_from_name("zip");
    }
    if (prefix.size() >= 2 && prefix[0] == 0x1F && prefix[1] == 0x8B) {
        return format_from_name("gzip");
    }
    if (prefix.size() >= 4 && prefix[0] == 0x28 && prefix[1] == 0xB5 &&
        prefix[2] == 0x2F && prefix[3] == 0xFD) {
        return format_from_name("zstd");
    }
    if (prefix.size() >= 262 &&
        std::memcmp(prefix.data() + 257, "ustar", 5) == 0) {
        return format_from_name("tar");
    }
    return {};
}

static FormatInfo resolve_format(const std::string& path, const std::string& override_format) {
    if (!override_format.empty()) return format_from_name(override_format);

    FormatInfo by_path = infer_format_from_path(path);
    FormatInfo by_magic = detect_format_from_magic(path);

    if (!by_path.format.empty() && !by_magic.format.empty()) {
        if (by_path.format == "tar.gz" && by_magic.format == "gzip") return by_path;
        if (by_path.format == "tar.zst" && by_magic.format == "zstd") return by_path;
        return by_magic;
    }
    if (!by_path.format.empty()) return by_path;
    return by_magic;
}

static void push_optional_string_field(MobiusState* state, int table_idx, const char* key, const std::string& value) {
    if (value.empty()) mobius_stack_pushNil(state);
    else mobius_stack_pushString(state, value.c_str());
    mobius_stack_setTableField(state, table_idx, key);
}

static void push_optional_uint_field(MobiusState* state, int table_idx, const char* key, uint64_t value, bool present) {
    if (present) mobius_stack_pushUInt64(state, value);
    else mobius_stack_pushNil(state);
    mobius_stack_setTableField(state, table_idx, key);
}

static int push_inspect_table(MobiusState* state, const std::string& path, const FormatInfo& info,
                              uint64_t entry_count = 0, bool has_entry_count = false,
                              uint64_t uncompressed_size = 0, bool has_uncompressed_size = false) {
    mobius_stack_pushNewTable(state, 8);
    int tbl = mobius_stack_size(state) - 1;
    push_optional_string_field(state, tbl, "format", info.format);
    push_optional_string_field(state, tbl, "kind", info.kind);
    push_optional_string_field(state, tbl, "archive_format", info.archive_format);
    push_optional_string_field(state, tbl, "compression_format", info.compression_format);
    push_optional_uint_field(state, tbl, "entry_count", entry_count, has_entry_count);
    push_optional_uint_field(state, tbl, "compressed_size", file_size_or_zero(path), true);
    push_optional_uint_field(state, tbl, "uncompressed_size", uncompressed_size, has_uncompressed_size);
    mobius_stack_pushBool(state, info.supported);
    mobius_stack_setTableField(state, tbl, "supported");
    return 1;
}

static int push_summary_table(MobiusState* state, const std::string& format,
                              const std::string& output_path, uint64_t bytes_written, uint64_t files_written) {
    mobius_stack_pushNewTable(state, 5);
    int tbl = mobius_stack_size(state) - 1;
    mobius_stack_pushBool(state, true);
    mobius_stack_setTableField(state, tbl, "ok");
    mobius_stack_pushString(state, format.c_str());
    mobius_stack_setTableField(state, tbl, "format");
    mobius_stack_pushString(state, output_path.c_str());
    mobius_stack_setTableField(state, tbl, "output_path");
    mobius_stack_pushUInt64(state, files_written);
    mobius_stack_setTableField(state, tbl, "files_written");
    mobius_stack_pushUInt64(state, bytes_written);
    mobius_stack_setTableField(state, tbl, "bytes_written");
    return 1;
}

static int push_entry_list(MobiusState* state, const std::vector<ArchiveEntry>& entries) {
    mobius_stack_pushNewArray(state, entries.size());
    int arr_idx = mobius_stack_size(state) - 1;
    for (const ArchiveEntry& entry : entries) {
        mobius_stack_pushNewTable(state, 8);
        int tbl = mobius_stack_size(state) - 1;
        mobius_stack_pushString(state, entry.path.c_str());
        mobius_stack_setTableField(state, tbl, "path");
        mobius_stack_pushString(state, entry.type.c_str());
        mobius_stack_setTableField(state, tbl, "type");
        mobius_stack_pushUInt64(state, entry.size);
        mobius_stack_setTableField(state, tbl, "size");
        push_optional_uint_field(state, tbl, "compressed_size", entry.compressed_size, entry.has_compressed_size);
        push_optional_uint_field(state, tbl, "mode", entry.mode, entry.has_mode);
        push_optional_uint_field(state, tbl, "mtime", entry.mtime, entry.has_mtime);
        mobius_stack_arrayPush(state, arr_idx);
    }
    return 1;
}

static bool table_string_field(MobiusState* state, int table_idx, const char* key, std::string& out) {
    mobius_stack_getTableField(state, table_idx, key);
    bool ok = false;
    if (mobius_stack_isString(state, -1)) {
        out = mobius_stack_asString(state, -1);
        ok = true;
    } else if (mobius_stack_isNil(state, -1)) {
        out.clear();
        ok = true;
    }
    mobius_stack_pop(state, 1);
    return ok;
}

static bool table_int_field(MobiusState* state, int table_idx, const char* key, int& out, bool* was_present = nullptr) {
    mobius_stack_getTableField(state, table_idx, key);
    bool ok = false;
    bool present = false;
    if (mobius_stack_isNumber(state, -1)) {
        out = static_cast<int>(mobius_stack_asInt64(state, -1));
        ok = true;
        present = true;
    } else if (mobius_stack_isNil(state, -1)) {
        ok = true;
    }
    mobius_stack_pop(state, 1);
    if (was_present) *was_present = present;
    return ok;
}

static bool parse_common_options(MobiusState* state, int idx, CommonOptions& options, std::string& error) {
    if (idx < 0 || mobius_stack_isNil(state, idx)) return true;
    if (!mobius_stack_isTable(state, idx)) {
        error = "options must be a table";
        return false;
    }

    if (!table_string_field(state, idx, "format", options.format)) {
        error = "options.format must be a string";
        return false;
    }
    if (!table_string_field(state, idx, "overwrite", options.overwrite)) {
        error = "options.overwrite must be a string";
        return false;
    }
    if (options.overwrite.empty()) options.overwrite = "error";

    bool level_present = false;
    if (!table_int_field(state, idx, "compression_level", options.compression_level, &level_present)) {
        error = "options.compression_level must be a number";
        return false;
    }

    if (!table_string_field(state, idx, "root_in_archive", options.root_in_archive)) {
        error = "options.root_in_archive must be a string";
        return false;
    }

    if (!table_int_field(state, idx, "strip_components", options.strip_components)) {
        error = "options.strip_components must be a number";
        return false;
    }

    mobius_stack_getTableField(state, idx, "tar");
    if (mobius_stack_isTable(state, -1)) {
        if (!table_string_field(state, mobius_stack_size(state) - 1, "compression", options.tar_compression)) {
            mobius_stack_pop(state, 1);
            error = "options.tar.compression must be a string";
            return false;
        }
    } else if (!mobius_stack_isNil(state, -1)) {
        mobius_stack_pop(state, 1);
        error = "options.tar must be a table";
        return false;
    }
    mobius_stack_pop(state, 1);

    return true;
}

static bool ensure_can_write(const std::string& path, const CommonOptions& options, std::string& error) {
    if (!file_exists(path)) return true;
    if (options.overwrite == "replace") return true;
    if (options.overwrite == "skip") {
        error = "output file already exists and skip mode is not supported for this operation";
        return false;
    }
    error = "output file already exists";
    return false;
}

#if MOBIUS_COMPRESSION_HAS_ZLIB
static bool gzip_compress_file(const std::string& input_path, const std::string& output_path,
                               int level, std::string& error);
static bool gzip_decompress_file(const std::string& input_path, const std::string& output_path,
                                 std::string& error);
#endif

#if MOBIUS_COMPRESSION_HAS_ZSTD
static bool zstd_compress_file(const std::string& input_path, const std::string& output_path,
                               int level, std::string& error);
static bool zstd_decompress_file(const std::string& input_path, const std::string& output_path,
                                 std::string& error);
#endif

static bool is_within_root(const fs::path& root, const fs::path& candidate) {
    fs::path root_norm = root.lexically_normal();
    fs::path cand_norm = candidate.lexically_normal();
    auto r_it = root_norm.begin();
    auto c_it = cand_norm.begin();
    for (; r_it != root_norm.end() && c_it != cand_norm.end(); ++r_it, ++c_it) {
        if (*r_it != *c_it) return false;
    }
    return r_it == root_norm.end();
}

static bool normalize_archive_member_path(const std::string& raw, int strip_components,
                                          std::string& out, std::string& error) {
    std::string normalized = raw;
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    while (normalized.size() >= 2 && normalized[0] == '.' && normalized[1] == '/') {
        normalized.erase(0, 2);
    }

    fs::path path = fs::path(normalized).lexically_normal();
    std::vector<std::string> components;
    for (const auto& part : path) {
        std::string piece = part.generic_string();
        if (piece.empty() || piece == ".") continue;
        if (piece == "..") {
            error = "archive entry uses parent directory traversal";
            return false;
        }
        if (piece == "/" || piece == "\\") {
            error = "archive entry uses an absolute path";
            return false;
        }
        components.push_back(piece);
    }

    if (path.is_absolute()) {
        error = "archive entry uses an absolute path";
        return false;
    }

    if (strip_components < 0) strip_components = 0;
    if (static_cast<size_t>(strip_components) >= components.size()) {
        out.clear();
        return true;
    }

    std::ostringstream oss;
    for (size_t i = static_cast<size_t>(strip_components); i < components.size(); ++i) {
        if (i > static_cast<size_t>(strip_components)) oss << '/';
        oss << components[i];
    }
    out = oss.str();
    return true;
}

static bool archive_member_destination(const fs::path& destination_root, const std::string& archive_path,
                                       int strip_components, fs::path& out, std::string& error) {
    std::string normalized;
    if (!normalize_archive_member_path(archive_path, strip_components, normalized, error)) return false;
    if (normalized.empty()) {
        out.clear();
        return true;
    }
    out = (destination_root / fs::path(normalized)).lexically_normal();
    if (!is_within_root(destination_root, out)) {
        error = "archive entry escapes destination directory";
        return false;
    }
    return true;
}

static std::string unique_temp_path(const std::string& suffix) {
    auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    for (int attempt = 0; attempt < 1024; ++attempt) {
        fs::path candidate = fs::temp_directory_path() /
            ("mobius_compression_" + std::to_string(now) + "_" + std::to_string(attempt) + suffix);
        std::error_code ec;
        if (!fs::exists(candidate, ec)) return candidate.string();
    }
    return (fs::temp_directory_path() / ("mobius_compression_fallback" + suffix)).string();
}

static bool collect_input_entries(const std::vector<std::string>& inputs, const CommonOptions& options,
                                  std::vector<LocalInputEntry>& entries, std::string& error) {
    for (const std::string& raw_input : inputs) {
        fs::path input = fs::u8path(raw_input);
        std::error_code ec;
        if (!fs::exists(input, ec)) {
            error = "input path not found: " + raw_input;
            return false;
        }

        fs::path normalized = input.lexically_normal();
        std::string base = normalized.filename().generic_string();
        if (base.empty()) {
            error = "could not derive archive name for input path: " + raw_input;
            return false;
        }

        std::string archive_root = base;
        if (!options.root_in_archive.empty()) {
            archive_root = join_archive_path(options.root_in_archive, archive_root);
        }

        if (fs::is_directory(normalized, ec)) {
            entries.push_back({normalized, ensure_trailing_slash(archive_root), true});
            for (fs::recursive_directory_iterator it(normalized, ec), end; !ec && it != end; it.increment(ec)) {
                fs::path rel = fs::relative(it->path(), normalized, ec);
                if (ec) {
                    error = "failed computing relative path while archiving";
                    return false;
                }
                std::string archive_path = join_archive_path(archive_root, rel.generic_string());
                bool is_dir = it->is_directory(ec);
                if (ec) {
                    error = "failed reading input entry type";
                    return false;
                }
                if (is_dir) archive_path = ensure_trailing_slash(archive_path);
                entries.push_back({it->path(), archive_path, is_dir});
            }
        } else if (fs::is_regular_file(normalized, ec)) {
            entries.push_back({normalized, archive_root, false});
        } else {
            error = "unsupported input path type: " + raw_input;
            return false;
        }
    }
    return true;
}

static mz_uint miniz_level_from_options(const CommonOptions& options) {
    if (options.compression_level < 0) return MZ_DEFAULT_LEVEL;
    return static_cast<mz_uint>(std::clamp(options.compression_level, 0, 10));
}

static bool write_zip_archive(const std::string& output_path, const std::vector<LocalInputEntry>& entries,
                              const CommonOptions& options, uint64_t& files_written, std::string& error) {
    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof(zip));
    if (!mz_zip_writer_init_file_v2(&zip, output_path.c_str(), 0, MZ_ZIP_FLAG_WRITE_ZIP64)) {
        error = mz_zip_get_error_string(mz_zip_get_last_error(&zip));
        return false;
    }

    mz_uint level = miniz_level_from_options(options);
    static const char kEmpty = '\0';
    for (const LocalInputEntry& entry : entries) {
        bool ok = false;
        if (entry.is_dir) {
            std::string dir_name = ensure_trailing_slash(entry.archive_path);
            ok = mz_zip_writer_add_mem(&zip, dir_name.c_str(), &kEmpty, 0, MZ_NO_COMPRESSION);
        } else {
            ok = mz_zip_writer_add_file(&zip, entry.archive_path.c_str(), entry.disk_path.string().c_str(),
                                        nullptr, 0, level);
        }
        if (!ok) {
            error = mz_zip_get_error_string(mz_zip_get_last_error(&zip));
            mz_zip_writer_end(&zip);
            return false;
        }
        files_written++;
    }

    if (!mz_zip_writer_finalize_archive(&zip)) {
        error = mz_zip_get_error_string(mz_zip_get_last_error(&zip));
        mz_zip_writer_end(&zip);
        return false;
    }
    if (!mz_zip_writer_end(&zip)) {
        error = mz_zip_get_error_string(mz_zip_get_last_error(&zip));
        return false;
    }
    return true;
}

static bool list_zip_archive(const std::string& path, std::vector<ArchiveEntry>& entries,
                             uint64_t* total_uncompressed, std::string& error) {
    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof(zip));
    if (!mz_zip_reader_init_file(&zip, path.c_str(), 0)) {
        error = mz_zip_get_error_string(mz_zip_get_last_error(&zip));
        return false;
    }

    mz_uint count = mz_zip_reader_get_num_files(&zip);
    uint64_t total = 0;
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&zip, i, &stat)) {
            error = mz_zip_get_error_string(mz_zip_get_last_error(&zip));
            mz_zip_reader_end(&zip);
            return false;
        }
        ArchiveEntry entry;
        entry.path = stat.m_filename;
        if (!entry.path.empty() && entry.path.back() == '/') entry.path.pop_back();
        entry.type = stat.m_is_directory ? "directory" : "file";
        entry.size = static_cast<uint64_t>(stat.m_uncomp_size);
        entry.compressed_size = static_cast<uint64_t>(stat.m_comp_size);
        entry.has_compressed_size = true;
        entry.mode = static_cast<uint32_t>(stat.m_external_attr >> 16);
        entry.has_mode = entry.mode != 0;
#ifndef MINIZ_NO_TIME
        entry.mtime = static_cast<uint64_t>(stat.m_time);
        entry.has_mtime = true;
#endif
        entries.push_back(entry);
        total += entry.size;
    }
    mz_zip_reader_end(&zip);
    if (total_uncompressed) *total_uncompressed = total;
    return true;
}

static bool extract_zip_archive(const std::string& path, const std::string& destination,
                                const CommonOptions& options, uint64_t& files_written, std::string& error) {
    fs::path destination_root = fs::u8path(destination).lexically_normal();
    if (!create_directory_tree(destination_root, error)) return false;

    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof(zip));
    if (!mz_zip_reader_init_file(&zip, path.c_str(), 0)) {
        error = mz_zip_get_error_string(mz_zip_get_last_error(&zip));
        return false;
    }

    mz_uint count = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&zip, i, &stat)) {
            error = mz_zip_get_error_string(mz_zip_get_last_error(&zip));
            mz_zip_reader_end(&zip);
            return false;
        }
        fs::path dest_path;
        if (!archive_member_destination(destination_root, stat.m_filename, options.strip_components, dest_path, error)) {
            mz_zip_reader_end(&zip);
            return false;
        }
        if (dest_path.empty()) continue;

        bool is_dir = stat.m_is_directory;
        if (is_dir) {
            if (!create_directory_tree(dest_path, error)) {
                mz_zip_reader_end(&zip);
                return false;
            }
            files_written++;
            continue;
        }

        if (!create_parent_directories(dest_path, error)) {
            mz_zip_reader_end(&zip);
            return false;
        }
        if (path_exists(dest_path.string()) && options.overwrite != "replace") {
            error = "destination file already exists";
            mz_zip_reader_end(&zip);
            return false;
        }
        if (!mz_zip_reader_extract_to_file(&zip, i, dest_path.string().c_str(), 0)) {
            error = mz_zip_get_error_string(mz_zip_get_last_error(&zip));
            mz_zip_reader_end(&zip);
            return false;
        }
        files_written++;
    }

    mz_zip_reader_end(&zip);
    return true;
}

static bool write_tar_archive(const std::string& output_path, const std::vector<LocalInputEntry>& entries,
                              uint64_t& files_written, std::string& error) {
    mtar_t tar;
    int rc = mtar_open(&tar, output_path.c_str(), "w");
    if (rc != MTAR_ESUCCESS) {
        error = mtar_strerror(rc);
        return false;
    }

    std::vector<char> buffer(kChunkSize);
    for (const LocalInputEntry& entry : entries) {
        if (entry.is_dir) {
            rc = mtar_write_dir_header(&tar, ensure_trailing_slash(entry.archive_path).c_str());
            if (rc != MTAR_ESUCCESS) {
                error = mtar_strerror(rc);
                mtar_close(&tar);
                return false;
            }
            files_written++;
            continue;
        }

        uint64_t size = file_size_or_zero(entry.disk_path.string());
        if (size > std::numeric_limits<unsigned>::max()) {
            error = "microtar backend currently supports files up to 4GiB";
            mtar_close(&tar);
            return false;
        }

        rc = mtar_write_file_header(&tar, entry.archive_path.c_str(), static_cast<unsigned>(size));
        if (rc != MTAR_ESUCCESS) {
            error = mtar_strerror(rc);
            mtar_close(&tar);
            return false;
        }

        std::ifstream in(entry.disk_path, std::ios::binary);
        if (!in) {
            error = "unable to open input file while writing tar archive";
            mtar_close(&tar);
            return false;
        }

        while (in) {
            in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            std::streamsize got = in.gcount();
            if (got <= 0) break;
            rc = mtar_write_data(&tar, buffer.data(), static_cast<unsigned>(got));
            if (rc != MTAR_ESUCCESS) {
                error = mtar_strerror(rc);
                mtar_close(&tar);
                return false;
            }
        }
        files_written++;
    }

    rc = mtar_finalize(&tar);
    if (rc != MTAR_ESUCCESS) {
        error = mtar_strerror(rc);
        mtar_close(&tar);
        return false;
    }
    mtar_close(&tar);
    return true;
}

static bool list_tar_archive(const std::string& path, std::vector<ArchiveEntry>& entries,
                             uint64_t* total_uncompressed, std::string& error) {
    mtar_t tar;
    int rc = mtar_open(&tar, path.c_str(), "r");
    if (rc != MTAR_ESUCCESS) {
        error = mtar_strerror(rc);
        return false;
    }

    uint64_t total = 0;
    while (true) {
        mtar_header_t h;
        rc = mtar_read_header(&tar, &h);
        if (rc == MTAR_ENULLRECORD) break;
        if (rc != MTAR_ESUCCESS) {
            error = mtar_strerror(rc);
            mtar_close(&tar);
            return false;
        }

        ArchiveEntry entry;
        entry.path = h.name;
        if (!entry.path.empty() && entry.path.back() == '/') entry.path.pop_back();
        entry.type = (h.type == MTAR_TDIR) ? "directory" : "file";
        entry.size = h.size;
        entry.mode = h.mode;
        entry.has_mode = true;
        entry.mtime = h.mtime;
        entry.has_mtime = true;
        entries.push_back(entry);
        total += entry.size;

        rc = mtar_next(&tar);
        if (rc == MTAR_ENULLRECORD) break;
        if (rc != MTAR_ESUCCESS) {
            error = mtar_strerror(rc);
            mtar_close(&tar);
            return false;
        }
    }

    mtar_close(&tar);
    if (total_uncompressed) *total_uncompressed = total;
    return true;
}

static bool extract_tar_archive(const std::string& path, const std::string& destination,
                                const CommonOptions& options, uint64_t& files_written, std::string& error) {
    fs::path destination_root = fs::u8path(destination).lexically_normal();
    if (!create_directory_tree(destination_root, error)) return false;

    mtar_t tar;
    int rc = mtar_open(&tar, path.c_str(), "r");
    if (rc != MTAR_ESUCCESS) {
        error = mtar_strerror(rc);
        return false;
    }

    std::vector<char> buffer(kChunkSize);
    while (true) {
        mtar_header_t h;
        rc = mtar_read_header(&tar, &h);
        if (rc == MTAR_ENULLRECORD) break;
        if (rc != MTAR_ESUCCESS) {
            error = mtar_strerror(rc);
            mtar_close(&tar);
            return false;
        }

        fs::path dest_path;
        if (!archive_member_destination(destination_root, h.name, options.strip_components, dest_path, error)) {
            mtar_close(&tar);
            return false;
        }

        if (h.type == MTAR_TDIR) {
            if (!dest_path.empty() && !create_directory_tree(dest_path, error)) {
                mtar_close(&tar);
                return false;
            }
            files_written++;
            rc = mtar_next(&tar);
            if (rc == MTAR_ENULLRECORD) break;
            if (rc != MTAR_ESUCCESS) {
                error = mtar_strerror(rc);
                mtar_close(&tar);
                return false;
            }
            continue;
        }

        if (h.type != MTAR_TREG) {
            error = "tar extraction currently supports only regular files and directories";
            mtar_close(&tar);
            return false;
        }

        if (dest_path.empty()) {
            rc = mtar_next(&tar);
            if (rc == MTAR_ENULLRECORD) break;
            if (rc != MTAR_ESUCCESS) {
                error = mtar_strerror(rc);
                mtar_close(&tar);
                return false;
            }
            continue;
        }

        if (!create_parent_directories(dest_path, error)) {
            mtar_close(&tar);
            return false;
        }
        if (path_exists(dest_path.string()) && options.overwrite != "replace") {
            error = "destination file already exists";
            mtar_close(&tar);
            return false;
        }

        std::ofstream out(dest_path, std::ios::binary | std::ios::trunc);
        if (!out) {
            error = "unable to open extracted output file";
            mtar_close(&tar);
            return false;
        }

        uint64_t remaining = h.size;
        while (remaining > 0) {
            unsigned chunk = static_cast<unsigned>(std::min<uint64_t>(remaining, buffer.size()));
            rc = mtar_read_data(&tar, buffer.data(), chunk);
            if (rc != MTAR_ESUCCESS) {
                error = mtar_strerror(rc);
                mtar_close(&tar);
                return false;
            }
            out.write(buffer.data(), chunk);
            if (!out) {
                error = "failed writing extracted file";
                mtar_close(&tar);
                return false;
            }
            remaining -= chunk;
        }
        files_written++;

        rc = mtar_next(&tar);
        if (rc == MTAR_ENULLRECORD) break;
        if (rc != MTAR_ESUCCESS) {
            error = mtar_strerror(rc);
            mtar_close(&tar);
            return false;
        }
    }

    mtar_close(&tar);
    return true;
}

static bool list_archive_entries(const std::string& path, const FormatInfo& info,
                                 std::vector<ArchiveEntry>& entries, uint64_t* total_uncompressed,
                                 std::string& error);

static bool extract_archive(const std::string& path, const std::string& destination, const FormatInfo& info,
                            const CommonOptions& options, uint64_t& files_written, std::string& error);

static bool create_archive(const std::string& output_path, const std::vector<LocalInputEntry>& entries,
                           const FormatInfo& info, const CommonOptions& options, uint64_t& files_written,
                           std::string& error);

static bool with_temp_tar_from_compressed(const std::string& path, const FormatInfo& info, ScopedTempFile& temp_tar,
                                          std::string& error) {
    temp_tar.path = fs::u8path(unique_temp_path(".tar"));
    if (info.format == "tar.gz") {
#if MOBIUS_COMPRESSION_HAS_ZLIB
        return gzip_decompress_file(path, temp_tar.path.string(), error);
#else
        error = "gzip support is unavailable in this build";
        return false;
#endif
    }
    if (info.format == "tar.zst") {
#if MOBIUS_COMPRESSION_HAS_ZSTD
        return zstd_decompress_file(path, temp_tar.path.string(), error);
#else
        error = "zstd support is unavailable in this build";
        return false;
#endif
    }
    error = "unsupported compressed tar format";
    return false;
}

static bool list_archive_entries(const std::string& path, const FormatInfo& info,
                                 std::vector<ArchiveEntry>& entries, uint64_t* total_uncompressed,
                                 std::string& error) {
    if (info.format == "zip") return list_zip_archive(path, entries, total_uncompressed, error);
    if (info.format == "tar") return list_tar_archive(path, entries, total_uncompressed, error);
    if (info.format == "tar.gz" || info.format == "tar.zst") {
        ScopedTempFile temp_tar;
        if (!with_temp_tar_from_compressed(path, info, temp_tar, error)) return false;
        return list_tar_archive(temp_tar.path.string(), entries, total_uncompressed, error);
    }
    error = "format does not support archive listing";
    return false;
}

static bool extract_archive(const std::string& path, const std::string& destination, const FormatInfo& info,
                            const CommonOptions& options, uint64_t& files_written, std::string& error) {
    if (info.format == "zip") return extract_zip_archive(path, destination, options, files_written, error);
    if (info.format == "tar") return extract_tar_archive(path, destination, options, files_written, error);
    if (info.format == "tar.gz" || info.format == "tar.zst") {
        ScopedTempFile temp_tar;
        if (!with_temp_tar_from_compressed(path, info, temp_tar, error)) return false;
        return extract_tar_archive(temp_tar.path.string(), destination, options, files_written, error);
    }
    error = "format does not support archive extraction";
    return false;
}

static bool create_archive(const std::string& output_path, const std::vector<LocalInputEntry>& entries,
                           const FormatInfo& info, const CommonOptions& options, uint64_t& files_written,
                           std::string& error) {
    if (info.format == "zip") return write_zip_archive(output_path, entries, options, files_written, error);
    if (info.format == "tar") return write_tar_archive(output_path, entries, files_written, error);
    if (info.format == "tar.gz" || info.format == "tar.zst") {
        ScopedTempFile temp_tar;
        temp_tar.path = fs::u8path(unique_temp_path(".tar"));
        uint64_t temp_files_written = 0;
        if (!write_tar_archive(temp_tar.path.string(), entries, temp_files_written, error)) return false;
        files_written = temp_files_written;
        if (info.format == "tar.gz") {
#if MOBIUS_COMPRESSION_HAS_ZLIB
            return gzip_compress_file(temp_tar.path.string(), output_path, options.compression_level, error);
#else
            error = "gzip support is unavailable in this build";
            return false;
#endif
        }
        if (info.format == "tar.zst") {
#if MOBIUS_COMPRESSION_HAS_ZSTD
            return zstd_compress_file(temp_tar.path.string(), output_path, options.compression_level, error);
#else
            error = "zstd support is unavailable in this build";
            return false;
#endif
        }
    }
    error = "format does not support archive creation";
    return false;
}

#if MOBIUS_COMPRESSION_HAS_ZLIB
static bool gzip_compress_file(const std::string& input_path, const std::string& output_path,
                               int level, std::string& error) {
    std::ifstream in(input_path, std::ios::binary);
    if (!in) {
        error = "unable to open input file";
        return false;
    }
    gzFile out = gzopen(output_path.c_str(), "wb");
    if (!out) {
        error = "unable to open output file";
        return false;
    }
    if (level >= 0) gzsetparams(out, std::max(0, std::min(level, 9)), Z_DEFAULT_STRATEGY);

    std::vector<char> buf(kChunkSize);
    while (in) {
        in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        std::streamsize got = in.gcount();
        if (got <= 0) break;
        int written = gzwrite(out, buf.data(), static_cast<unsigned int>(got));
        if (written == 0) {
            int err_no = Z_OK;
            const char* msg = gzerror(out, &err_no);
            error = msg ? msg : "gzip write failed";
            gzclose(out);
            return false;
        }
    }
    if (gzclose(out) != Z_OK) {
        error = "failed closing gzip output stream";
        return false;
    }
    return true;
}

static bool gzip_decompress_file(const std::string& input_path, const std::string& output_path,
                                 std::string& error) {
    gzFile in = gzopen(input_path.c_str(), "rb");
    if (!in) {
        error = "unable to open gzip input file";
        return false;
    }
    std::ofstream out(output_path, std::ios::binary | std::ios::trunc);
    if (!out) {
        gzclose(in);
        error = "unable to open output file";
        return false;
    }

    std::vector<char> buf(kChunkSize);
    while (true) {
        int read_n = gzread(in, buf.data(), static_cast<unsigned int>(buf.size()));
        if (read_n < 0) {
            int err_no = Z_OK;
            const char* msg = gzerror(in, &err_no);
            error = msg ? msg : "gzip read failed";
            gzclose(in);
            return false;
        }
        if (read_n == 0) break;
        out.write(buf.data(), read_n);
        if (!out) {
            gzclose(in);
            error = "failed writing decompressed output";
            return false;
        }
    }
    gzclose(in);
    return true;
}
#endif

#if MOBIUS_COMPRESSION_HAS_ZSTD
static bool zstd_compress_file(const std::string& input_path, const std::string& output_path,
                               int level, std::string& error) {
    std::vector<unsigned char> input;
    if (!read_entire_file(input_path, input, error)) return false;

    int actual_level = level >= 0 ? level : ZSTD_CLEVEL_DEFAULT;
    size_t bound = ZSTD_compressBound(input.size());
    std::vector<unsigned char> out(bound);
    size_t compressed = ZSTD_compress(out.data(), bound, input.data(), input.size(), actual_level);
    if (ZSTD_isError(compressed)) {
        error = ZSTD_getErrorName(compressed);
        return false;
    }

    std::ofstream file(output_path, std::ios::binary | std::ios::trunc);
    if (!file) {
        error = "unable to open output file";
        return false;
    }
    file.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(compressed));
    if (!file) {
        error = "failed writing zstd output";
        return false;
    }
    return true;
}

static bool zstd_decompress_file(const std::string& input_path, const std::string& output_path,
                                 std::string& error) {
    std::vector<unsigned char> input;
    if (!read_entire_file(input_path, input, error)) return false;

    ZSTD_DCtx* dctx = ZSTD_createDCtx();
    if (!dctx) {
        error = "failed to create zstd decompressor";
        return false;
    }

    std::ofstream out(output_path, std::ios::binary | std::ios::trunc);
    if (!out) {
        ZSTD_freeDCtx(dctx);
        error = "unable to open output file";
        return false;
    }

    ZSTD_inBuffer in_buf = {input.data(), input.size(), 0};
    std::vector<unsigned char> out_buf_mem(kChunkSize);

    while (in_buf.pos < in_buf.size) {
        ZSTD_outBuffer out_buf = {out_buf_mem.data(), out_buf_mem.size(), 0};
        size_t rc = ZSTD_decompressStream(dctx, &out_buf, &in_buf);
        if (ZSTD_isError(rc)) {
            ZSTD_freeDCtx(dctx);
            error = ZSTD_getErrorName(rc);
            return false;
        }
        out.write(reinterpret_cast<const char*>(out_buf.dst), static_cast<std::streamsize>(out_buf.pos));
        if (!out) {
            ZSTD_freeDCtx(dctx);
            error = "failed writing decompressed output";
            return false;
        }
    }

    ZSTD_freeDCtx(dctx);
    return true;
}
#endif

static int compression_inspect_native(MobiusState* state, int arg_count) {
    if (arg_count < 1 || arg_count > 2) {
        return mobius_error(state, "__inspect_native() expects path and optional options");
    }
    if (!mobius_stack_isString(state, -arg_count)) {
        return mobius_error(state, "__inspect_native() expects a string path");
    }

    std::string error;
    CommonOptions options;
    if (arg_count == 2 && !parse_common_options(state, -1, options, error)) {
        return mobius_error(state, error.c_str());
    }

    std::string path = mobius_stack_asString(state, -arg_count);
    mobius_stack_pop(state, arg_count);
    if (!file_exists(path)) {
        return mobius_error(state, "__inspect_native() input file not found");
    }

    FormatInfo info = resolve_format(path, options.format);
    if (info.kind == "archive") {
        std::vector<ArchiveEntry> entries;
        uint64_t total = 0;
        if (!list_archive_entries(path, info, entries, &total, error)) {
            return mobius_error(state, error.c_str());
        }
        return push_inspect_table(state, path, info, entries.size(), true, total, true);
    }
    return push_inspect_table(state, path, info);
}

static int compression_list_native(MobiusState* state, int arg_count) {
    if (arg_count < 1 || arg_count > 2) {
        return mobius_error(state, "__list_native() expects path and optional options");
    }
    if (!mobius_stack_isString(state, -arg_count)) {
        return mobius_error(state, "__list_native() expects a string path");
    }

    std::string error;
    CommonOptions options;
    if (arg_count == 2 && !parse_common_options(state, -1, options, error)) {
        return mobius_error(state, error.c_str());
    }

    std::string path = mobius_stack_asString(state, -arg_count);
    mobius_stack_pop(state, arg_count);
    if (!file_exists(path)) {
        return mobius_error(state, "__list_native() input file not found");
    }

    FormatInfo info = resolve_format(path, options.format);
    if (info.kind != "archive") {
        return mobius_error(state, "__list_native() is only valid for archive formats");
    }

    std::vector<ArchiveEntry> entries;
    if (!list_archive_entries(path, info, entries, nullptr, error)) {
        return mobius_error(state, error.c_str());
    }
    return push_entry_list(state, entries);
}

static int compression_extract_native(MobiusState* state, int arg_count) {
    if (arg_count < 2 || arg_count > 3) {
        return mobius_error(state, "__extract_native() expects path, destination, and optional options");
    }
    if (!mobius_stack_isString(state, -arg_count) || !mobius_stack_isString(state, -arg_count + 1)) {
        return mobius_error(state, "__extract_native() expects string paths");
    }

    std::string error;
    CommonOptions options;
    if (arg_count == 3 && !parse_common_options(state, -1, options, error)) {
        return mobius_error(state, error.c_str());
    }

    std::string path = mobius_stack_asString(state, -arg_count);
    std::string destination = mobius_stack_asString(state, -arg_count + 1);
    mobius_stack_pop(state, arg_count);

    if (!file_exists(path)) {
        return mobius_error(state, "__extract_native() input file not found");
    }

    FormatInfo info = resolve_format(path, options.format);
    if (info.kind != "archive") {
        return mobius_error(state, "__extract_native() is only valid for archive formats");
    }

    uint64_t files_written = 0;
    if (!extract_archive(path, destination, info, options, files_written, error)) {
        return mobius_error(state, error.c_str());
    }
    return push_summary_table(state, info.format, destination, 0, files_written);
}

static int compression_create_native(MobiusState* state, int arg_count) {
    if (arg_count < 2 || arg_count > 3) {
        return mobius_error(state, "__create_native() expects output_path, inputs, and optional options");
    }
    if (!mobius_stack_isString(state, -arg_count) || !mobius_stack_isArray(state, -arg_count + 1)) {
        return mobius_error(state, "__create_native() expects a string output_path and array inputs");
    }

    std::string error;
    CommonOptions options;
    if (arg_count == 3 && !parse_common_options(state, -1, options, error)) {
        return mobius_error(state, error.c_str());
    }

    std::string output_path = mobius_stack_asString(state, -arg_count);
    int inputs_idx = -arg_count + 1;
    std::vector<std::string> inputs;
    size_t input_count = mobius_stack_getArrayLength(state, inputs_idx);
    inputs.reserve(input_count);
    for (size_t i = 0; i < input_count; ++i) {
        mobius_stack_getArrayElement(state, inputs_idx, i);
        if (!mobius_stack_isString(state, -1)) {
            mobius_stack_pop(state, arg_count + 1);
            return mobius_error(state, "__create_native() inputs must contain only strings");
        }
        inputs.emplace_back(mobius_stack_asString(state, -1));
        mobius_stack_pop(state, 1);
    }
    mobius_stack_pop(state, arg_count);

    if (inputs.empty()) {
        return mobius_error(state, "__create_native() requires at least one input path");
    }
    if (!ensure_can_write(output_path, options, error)) {
        return mobius_error(state, error.c_str());
    }

    FormatInfo info = resolve_format(output_path, options.format);
    if (info.format == "tar" && !options.tar_compression.empty()) {
        if (lower_copy(options.tar_compression) == "gzip") info = format_from_name("tar.gz");
        else if (lower_copy(options.tar_compression) == "zstd") info = format_from_name("tar.zst");
    }
    if (info.kind != "archive") {
        return mobius_error(state, "__create_native() is only valid for archive formats");
    }

    std::vector<LocalInputEntry> entries;
    if (!collect_input_entries(inputs, options, entries, error)) {
        return mobius_error(state, error.c_str());
    }

    uint64_t files_written = 0;
    if (!create_archive(output_path, entries, info, options, files_written, error)) {
        return mobius_error(state, error.c_str());
    }
    return push_summary_table(state, info.format, output_path, file_size_or_zero(output_path), files_written);
}

static int compression_compress_native(MobiusState* state, int arg_count) {
    if (arg_count < 2 || arg_count > 3) {
        return mobius_error(state, "__compress_native() expects input_path, output_path, and optional options");
    }
    if (!mobius_stack_isString(state, -arg_count) || !mobius_stack_isString(state, -arg_count + 1)) {
        return mobius_error(state, "__compress_native() expects string paths");
    }

    std::string error;
    CommonOptions options;
    if (arg_count == 3 && !parse_common_options(state, -1, options, error)) {
        return mobius_error(state, error.c_str());
    }

    std::string input_path = mobius_stack_asString(state, -arg_count);
    std::string output_path = mobius_stack_asString(state, -arg_count + 1);
    mobius_stack_pop(state, arg_count);

    if (!file_exists(input_path)) {
        return mobius_error(state, "__compress_native() input file not found");
    }
    if (!ensure_can_write(output_path, options, error)) {
        return mobius_error(state, error.c_str());
    }

    FormatInfo info = resolve_format(output_path, options.format);
    if (info.format.empty()) {
        return mobius_error(state, "__compress_native() could not infer format; pass options.format");
    }

    if (info.format == "gzip") {
#if MOBIUS_COMPRESSION_HAS_ZLIB
        if (!gzip_compress_file(input_path, output_path, options.compression_level, error)) {
            return mobius_error(state, error.c_str());
        }
        return push_summary_table(state, "gzip", output_path, file_size_or_zero(output_path), 1);
#else
        return mobius_error(state, "__compress_native() gzip support is unavailable in this build");
#endif
    }

    if (info.format == "zstd") {
#if MOBIUS_COMPRESSION_HAS_ZSTD
        if (!zstd_compress_file(input_path, output_path, options.compression_level, error)) {
            return mobius_error(state, error.c_str());
        }
        return push_summary_table(state, "zstd", output_path, file_size_or_zero(output_path), 1);
#else
        return mobius_error(state, "__compress_native() zstd support is unavailable in this build");
#endif
    }

    return mobius_error(state, "__compress_native() supports only gzip and zstd stream formats");
}

static int compression_decompress_native(MobiusState* state, int arg_count) {
    if (arg_count < 2 || arg_count > 3) {
        return mobius_error(state, "__decompress_native() expects input_path, output_path, and optional options");
    }
    if (!mobius_stack_isString(state, -arg_count) || !mobius_stack_isString(state, -arg_count + 1)) {
        return mobius_error(state, "__decompress_native() expects string paths");
    }

    std::string error;
    CommonOptions options;
    if (arg_count == 3 && !parse_common_options(state, -1, options, error)) {
        return mobius_error(state, error.c_str());
    }

    std::string input_path = mobius_stack_asString(state, -arg_count);
    std::string output_path = mobius_stack_asString(state, -arg_count + 1);
    mobius_stack_pop(state, arg_count);

    if (!file_exists(input_path)) {
        return mobius_error(state, "__decompress_native() input file not found");
    }
    if (!ensure_can_write(output_path, options, error)) {
        return mobius_error(state, error.c_str());
    }

    FormatInfo info = resolve_format(input_path, options.format);
    if (info.format.empty()) {
        return mobius_error(state, "__decompress_native() could not infer format; pass options.format");
    }

    if (info.format == "gzip") {
#if MOBIUS_COMPRESSION_HAS_ZLIB
        if (!gzip_decompress_file(input_path, output_path, error)) {
            return mobius_error(state, error.c_str());
        }
        return push_summary_table(state, "gzip", output_path, file_size_or_zero(output_path), 1);
#else
        return mobius_error(state, "__decompress_native() gzip support is unavailable in this build");
#endif
    }

    if (info.format == "zstd") {
#if MOBIUS_COMPRESSION_HAS_ZSTD
        if (!zstd_decompress_file(input_path, output_path, error)) {
            return mobius_error(state, error.c_str());
        }
        return push_summary_table(state, "zstd", output_path, file_size_or_zero(output_path), 1);
#else
        return mobius_error(state, "__decompress_native() zstd support is unavailable in this build");
#endif
    }

    return mobius_error(state, "__decompress_native() supports only gzip and zstd stream formats");
}

static int init_compression_plugin(MobiusState* /*state*/) { return 0; }
static void cleanup_compression_plugin(void) {}

static MobiusPluginFunction compression_functions[] = {
    {"__inspect_native",    compression_inspect_native,    SIZE_MAX, MOBIUS_VAL_TABLE,  "Internal archive/compression inspection helper"},
    {"__list_native",       compression_list_native,       SIZE_MAX, MOBIUS_VAL_ARRAY,   "Internal archive entry listing helper"},
    {"__extract_native",    compression_extract_native,    SIZE_MAX, MOBIUS_VAL_TABLE,   "Internal archive extraction helper"},
    {"__create_native",     compression_create_native,     SIZE_MAX, MOBIUS_VAL_TABLE,   "Internal archive creation helper"},
    {"__compress_native",   compression_compress_native,   SIZE_MAX, MOBIUS_VAL_TABLE,   "Internal stream compression helper"},
    {"__decompress_native", compression_decompress_native, SIZE_MAX, MOBIUS_VAL_TABLE,   "Internal stream decompression helper"},
};

static MobiusPlugin compression_plugin = {
    .metadata = {
        .name = "compression",
        .version = "0.1.0",
        .description = "Compression and archive primitives",
        .author = "Mobius Team",
        .api_version = MOBIUS_PLUGIN_API_VERSION,
        .license = "MIT"
    },
    .functions = compression_functions,
    .function_count = sizeof(compression_functions) / sizeof(compression_functions[0]),
    .init_plugin = init_compression_plugin,
    .cleanup_plugin = cleanup_compression_plugin,
    .post_init = nullptr,
};

}  // namespace

extern "C" MOBIUS_PLUGIN_EXPORT MobiusPlugin* mobius_plugin_info(void) {
    return &compression_plugin;
}
