#ifndef LDDSFRAMEWORK_SQLITEDURABILITYSTORE_H_
#define LDDSFRAMEWORK_SQLITEDURABILITYSTORE_H_

#include <cstdint>
#include <chrono>
#include <cstring>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <Windows.h>
#endif

namespace LDdsFramework {

class SqliteDurabilityStore final
{
public:
    SqliteDurabilityStore() noexcept
        : m_db(nullptr)
        , m_domainId(0)
        , m_historyDepth(1)
    {
    }

    ~SqliteDurabilityStore() noexcept
    {
        close();
    }

    SqliteDurabilityStore(const SqliteDurabilityStore &) = delete;
    SqliteDurabilityStore & operator=(const SqliteDurabilityStore &) = delete;

    bool open(
        const std::string & dbPath,
        uint32_t domainId,
        size_t historyDepth,
        std::string * errorMessage = nullptr)
    {
        close();
        m_domainId = domainId;
        m_historyDepth = historyDepth == 0 ? 1 : historyDepth;
        m_dbPath = dbPath;
        if (m_dbPath.empty())
        {
            m_dbPath = "build/ldds_domain_" + std::to_string(domainId) + ".sqlite";
        }

        std::string apiError;
        if (!ensureApiLoaded(apiError))
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = apiError;
            }
            return false;
        }

        std::filesystem::path path(m_dbPath);
        if (path.has_parent_path())
        {
            std::error_code ec;
            std::filesystem::create_directories(path.parent_path(), ec);
        }

        if (api().sqlite3_open_v2(
                m_dbPath.c_str(),
                &m_db,
                SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                nullptr) != SQLITE_OK)
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = "sqlite open failed: " + lastErrorMessage();
            }
            close();
            return false;
        }

        if (!execSql("PRAGMA journal_mode=WAL;", errorMessage) ||
            !execSql("PRAGMA synchronous=NORMAL;", errorMessage) ||
            !execSql(
                "CREATE TABLE IF NOT EXISTS topic_history ("
                " id INTEGER PRIMARY KEY AUTOINCREMENT,"
                " domain_id INTEGER NOT NULL,"
                " topic INTEGER NOT NULL,"
                " seq INTEGER NOT NULL,"
                " data BLOB NOT NULL,"
                " data_type TEXT,"
                " created_at_ms INTEGER NOT NULL"
                ");",
                errorMessage) ||
            !execSql(
                "CREATE INDEX IF NOT EXISTS idx_topic_history_domain_topic_id "
                "ON topic_history(domain_id, topic, id);",
                errorMessage))
        {
            close();
            return false;
        }

        return true;
    }

    void close() noexcept
    {
        if (m_db != nullptr)
        {
            (void)api().sqlite3_close_v2(m_db);
            m_db = nullptr;
        }
    }

    bool isOpen() const noexcept
    {
        return m_db != nullptr;
    }

    void setHistoryDepth(size_t historyDepth) noexcept
    {
        m_historyDepth = historyDepth == 0 ? 1 : historyDepth;
    }

    bool append(
        int topic,
        const std::vector<uint8_t> & data,
        const std::string & dataType,
        uint64_t sequence,
        std::string * errorMessage = nullptr)
    {
        if (m_db == nullptr || topic <= 0 || data.empty())
        {
            return false;
        }

        sqlite3_stmt * stmt = nullptr;
        const char * insertSql =
            "INSERT INTO topic_history(domain_id, topic, seq, data, data_type, created_at_ms) "
            "VALUES(?, ?, ?, ?, ?, ?);";
        if (api().sqlite3_prepare_v2(m_db, insertSql, -1, &stmt, nullptr) != SQLITE_OK)
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = "sqlite prepare insert failed: " + lastErrorMessage();
            }
            return false;
        }

        const int64_t nowMs = static_cast<int64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch())
                .count());

        const sqlite3_destructor_type transient = sqliteTransient();
        (void)api().sqlite3_bind_int(stmt, 1, static_cast<int>(m_domainId));
        (void)api().sqlite3_bind_int(stmt, 2, topic);
        (void)api().sqlite3_bind_int64(stmt, 3, static_cast<int64_t>(sequence));
        (void)api().sqlite3_bind_blob(
            stmt,
            4,
            data.data(),
            static_cast<int>(data.size()),
            transient);
        if (dataType.empty())
        {
            (void)api().sqlite3_bind_text(stmt, 5, "", 0, transient);
        }
        else
        {
            (void)api().sqlite3_bind_text(
                stmt,
                5,
                dataType.c_str(),
                static_cast<int>(dataType.size()),
                transient);
        }
        (void)api().sqlite3_bind_int64(stmt, 6, nowMs);

        const int stepResult = api().sqlite3_step(stmt);
        (void)api().sqlite3_finalize(stmt);
        if (stepResult != SQLITE_DONE)
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = "sqlite insert failed: " + lastErrorMessage();
            }
            return false;
        }

        return compactTopic(topic, errorMessage);
    }

    bool loadRecent(
        size_t historyDepth,
        std::map<int, std::deque<std::vector<uint8_t>>> & topicCache,
        std::map<int, std::string> & topicDataTypes,
        std::string * errorMessage = nullptr)
    {
        if (m_db == nullptr)
        {
            return false;
        }

        const size_t effectiveDepth = historyDepth == 0 ? 1 : historyDepth;
        sqlite3_stmt * stmt = nullptr;
        const char * querySql =
            "SELECT topic, data, data_type FROM topic_history "
            "WHERE domain_id=? ORDER BY id ASC;";
        if (api().sqlite3_prepare_v2(m_db, querySql, -1, &stmt, nullptr) != SQLITE_OK)
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = "sqlite prepare load failed: " + lastErrorMessage();
            }
            return false;
        }

        (void)api().sqlite3_bind_int(stmt, 1, static_cast<int>(m_domainId));

        topicCache.clear();
        topicDataTypes.clear();

        while (true)
        {
            const int stepResult = api().sqlite3_step(stmt);
            if (stepResult == SQLITE_DONE)
            {
                break;
            }
            if (stepResult != SQLITE_ROW)
            {
                (void)api().sqlite3_finalize(stmt);
                if (errorMessage != nullptr)
                {
                    *errorMessage = "sqlite load step failed: " + lastErrorMessage();
                }
                return false;
            }

            const int topic = api().sqlite3_column_int(stmt, 0);
            const int blobSize = api().sqlite3_column_bytes(stmt, 1);
            const void * blob = api().sqlite3_column_blob(stmt, 1);
            const unsigned char * text = api().sqlite3_column_text(stmt, 2);
            if (topic <= 0 || blob == nullptr || blobSize <= 0)
            {
                continue;
            }

            std::vector<uint8_t> payload(static_cast<size_t>(blobSize), 0U);
            std::memcpy(payload.data(), blob, static_cast<size_t>(blobSize));
            auto & queue = topicCache[topic];
            queue.push_back(std::move(payload));
            while (queue.size() > effectiveDepth)
            {
                queue.pop_front();
            }

            if (text != nullptr && *text != '\0')
            {
                topicDataTypes[topic] = reinterpret_cast<const char *>(text);
            }
        }

        (void)api().sqlite3_finalize(stmt);
        return true;
    }

private:
    struct sqlite3;
    struct sqlite3_stmt;
    using sqlite3_destructor_type = void (*)(void *);

    struct SqliteApi
    {
        bool loaded = false;
        void * module = nullptr;

        int (*sqlite3_open_v2)(const char *, sqlite3 **, int, const char *) = nullptr;
        int (*sqlite3_close_v2)(sqlite3 *) = nullptr;
        int (*sqlite3_exec)(sqlite3 *, const char *, int (*)(void *, int, char **, char **), void *, char **) = nullptr;
        void (*sqlite3_free)(void *) = nullptr;
        const char * (*sqlite3_errmsg)(sqlite3 *) = nullptr;
        int (*sqlite3_prepare_v2)(sqlite3 *, const char *, int, sqlite3_stmt **, const char **) = nullptr;
        int (*sqlite3_bind_int)(sqlite3_stmt *, int, int) = nullptr;
        int (*sqlite3_bind_int64)(sqlite3_stmt *, int, int64_t) = nullptr;
        int (*sqlite3_bind_blob)(sqlite3_stmt *, int, const void *, int, sqlite3_destructor_type) = nullptr;
        int (*sqlite3_bind_text)(sqlite3_stmt *, int, const char *, int, sqlite3_destructor_type) = nullptr;
        int (*sqlite3_step)(sqlite3_stmt *) = nullptr;
        int (*sqlite3_finalize)(sqlite3_stmt *) = nullptr;
        int (*sqlite3_column_int)(sqlite3_stmt *, int) = nullptr;
        const unsigned char * (*sqlite3_column_text)(sqlite3_stmt *, int) = nullptr;
        const void * (*sqlite3_column_blob)(sqlite3_stmt *, int) = nullptr;
        int (*sqlite3_column_bytes)(sqlite3_stmt *, int) = nullptr;
    };

    static constexpr int SQLITE_OK = 0;
    static constexpr int SQLITE_ROW = 100;
    static constexpr int SQLITE_DONE = 101;
    static constexpr int SQLITE_OPEN_READWRITE = 0x00000002;
    static constexpr int SQLITE_OPEN_CREATE = 0x00000004;
    static constexpr int SQLITE_OPEN_FULLMUTEX = 0x00010000;

    static SqliteApi & api()
    {
        static SqliteApi g_api;
        return g_api;
    }

    static sqlite3_destructor_type sqliteTransient()
    {
        return reinterpret_cast<sqlite3_destructor_type>(-1);
    }

    bool ensureApiLoaded(std::string & errorMessage)
    {
        SqliteApi & a = api();
        if (a.loaded)
        {
            return true;
        }

#if defined(_WIN32)
        using ModuleHandle = void *;
        auto loadLibrary = [](const char * name) -> ModuleHandle {
            return reinterpret_cast<ModuleHandle>(::LoadLibraryA(name));
        };
        auto getSymbol = [](ModuleHandle module, const char * symbol) -> void * {
            return reinterpret_cast<void *>(::GetProcAddress(reinterpret_cast<HMODULE>(module), symbol));
        };

        ModuleHandle module = loadLibrary("winsqlite3.dll");
        if (module == nullptr)
        {
            module = loadLibrary("sqlite3.dll");
        }
        if (module == nullptr)
        {
            errorMessage = "sqlite runtime not found (winsqlite3.dll/sqlite3.dll)";
            return false;
        }

        a.module = module;
#define LDDS_SQLITE_LOAD(symbol)                                                          \
    do                                                                                    \
    {                                                                                     \
        a.symbol = reinterpret_cast<decltype(a.symbol)>(getSymbol(module, #symbol));     \
        if (a.symbol == nullptr)                                                          \
        {                                                                                 \
            errorMessage = std::string("sqlite symbol missing: ") + #symbol;             \
            return false;                                                                 \
        }                                                                                 \
    } while (false)

        LDDS_SQLITE_LOAD(sqlite3_open_v2);
        LDDS_SQLITE_LOAD(sqlite3_close_v2);
        LDDS_SQLITE_LOAD(sqlite3_exec);
        LDDS_SQLITE_LOAD(sqlite3_free);
        LDDS_SQLITE_LOAD(sqlite3_errmsg);
        LDDS_SQLITE_LOAD(sqlite3_prepare_v2);
        LDDS_SQLITE_LOAD(sqlite3_bind_int);
        LDDS_SQLITE_LOAD(sqlite3_bind_int64);
        LDDS_SQLITE_LOAD(sqlite3_bind_blob);
        LDDS_SQLITE_LOAD(sqlite3_bind_text);
        LDDS_SQLITE_LOAD(sqlite3_step);
        LDDS_SQLITE_LOAD(sqlite3_finalize);
        LDDS_SQLITE_LOAD(sqlite3_column_int);
        LDDS_SQLITE_LOAD(sqlite3_column_text);
        LDDS_SQLITE_LOAD(sqlite3_column_blob);
        LDDS_SQLITE_LOAD(sqlite3_column_bytes);
#undef LDDS_SQLITE_LOAD
#else
        errorMessage = "sqlite runtime loading is only implemented on Windows";
        return false;
#endif

        a.loaded = true;
        return true;
    }

    bool execSql(const char * sql, std::string * errorMessage)
    {
        if (m_db == nullptr || sql == nullptr)
        {
            return false;
        }

        char * sqliteError = nullptr;
        const int rc = api().sqlite3_exec(m_db, sql, nullptr, nullptr, &sqliteError);
        if (rc != SQLITE_OK)
        {
            if (errorMessage != nullptr)
            {
                if (sqliteError != nullptr)
                {
                    *errorMessage = sqliteError;
                }
                else
                {
                    *errorMessage = "sqlite exec failed";
                }
            }
            if (sqliteError != nullptr)
            {
                api().sqlite3_free(sqliteError);
            }
            return false;
        }

        if (sqliteError != nullptr)
        {
            api().sqlite3_free(sqliteError);
        }
        return true;
    }

    bool compactTopic(int topic, std::string * errorMessage)
    {
        if (m_db == nullptr || topic <= 0)
        {
            return false;
        }

        sqlite3_stmt * stmt = nullptr;
        const char * cleanupSql =
            "DELETE FROM topic_history WHERE domain_id=? AND topic=? "
            "AND id NOT IN ("
            "  SELECT id FROM topic_history WHERE domain_id=? AND topic=? ORDER BY id DESC LIMIT ?"
            ");";

        if (api().sqlite3_prepare_v2(m_db, cleanupSql, -1, &stmt, nullptr) != SQLITE_OK)
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = "sqlite prepare cleanup failed: " + lastErrorMessage();
            }
            return false;
        }

        (void)api().sqlite3_bind_int(stmt, 1, static_cast<int>(m_domainId));
        (void)api().sqlite3_bind_int(stmt, 2, topic);
        (void)api().sqlite3_bind_int(stmt, 3, static_cast<int>(m_domainId));
        (void)api().sqlite3_bind_int(stmt, 4, topic);
        (void)api().sqlite3_bind_int(stmt, 5, static_cast<int>(m_historyDepth));
        const int stepResult = api().sqlite3_step(stmt);
        (void)api().sqlite3_finalize(stmt);

        if (stepResult != SQLITE_DONE)
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = "sqlite cleanup failed: " + lastErrorMessage();
            }
            return false;
        }
        return true;
    }

    std::string lastErrorMessage() const
    {
        if (m_db == nullptr || api().sqlite3_errmsg == nullptr)
        {
            return "unknown sqlite error";
        }
        const char * msg = api().sqlite3_errmsg(m_db);
        return (msg != nullptr) ? std::string(msg) : std::string("unknown sqlite error");
    }

private:
    sqlite3 * m_db;
    uint32_t m_domainId;
    size_t m_historyDepth;
    std::string m_dbPath;
};

} // namespace LDdsFramework

#endif // LDDSFRAMEWORK_SQLITEDURABILITYSTORE_H_
