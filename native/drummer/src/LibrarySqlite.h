#pragma once
#include <juce_core/juce_core.h>
#include <sqlite3.h>
#include <functional>
#include <stdexcept>

namespace drummer::sql {
struct Database {
    sqlite3* handle = nullptr;
    explicit Database(const juce::File& file) {
        if (sqlite3_open_v2(file.getFullPathName().toRawUTF8(), &handle, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX, nullptr) != SQLITE_OK) {
            const std::string error = handle ? sqlite3_errmsg(handle) : "Cannot open pattern catalog";
            if (handle) sqlite3_close(handle);
            throw std::runtime_error(error);
        }
        sqlite3_busy_timeout(handle, 5000);
        exec("PRAGMA foreign_keys=ON");
    }
    ~Database() { sqlite3_close(handle); }
    Database(const Database&) = delete;
    void exec(const char* query) {
        char* error = nullptr;
        if (sqlite3_exec(handle, query, nullptr, nullptr, &error) != SQLITE_OK) {
            std::string message = error ? error : sqlite3_errmsg(handle);
            sqlite3_free(error);
            throw std::runtime_error(message);
        }
    }
};
struct Statement {
    sqlite3_stmt* handle = nullptr;
    Database& db;
    Statement(Database& owner, const char* query) : db(owner) {
        if (sqlite3_prepare_v2(db.handle, query, -1, &handle, nullptr) != SQLITE_OK) throw std::runtime_error(sqlite3_errmsg(db.handle));
    }
    ~Statement() { sqlite3_finalize(handle); }
    void reset() { sqlite3_reset(handle); sqlite3_clear_bindings(handle); }
    void text(int index, const juce::String& value) { check(sqlite3_bind_text(handle, index, value.toRawUTF8(), value.getNumBytesAsUTF8(), SQLITE_TRANSIENT)); }
    void integer(int index, int64_t value) { check(sqlite3_bind_int64(handle, index, value)); }
    void real(int index, double value) { check(sqlite3_bind_double(handle, index, value)); }
    void blob(int index, const juce::MemoryBlock& bytes) { check(sqlite3_bind_blob64(handle, index, bytes.getData(), bytes.getSize(), SQLITE_TRANSIENT)); }
    bool row() {
        const int result = sqlite3_step(handle);
        if (result != SQLITE_ROW && result != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(db.handle));
        return result == SQLITE_ROW;
    }
    void done() { if (row()) throw std::runtime_error("Unexpected catalog result"); }
    int64_t integer(int column) const { return sqlite3_column_int64(handle, column); }
    double real(int column) const { return sqlite3_column_double(handle, column); }
    juce::String text(int column) const { return juce::String::fromUTF8(reinterpret_cast<const char*>(sqlite3_column_text(handle, column)), sqlite3_column_bytes(handle, column)); }
    juce::MemoryBlock blob(int column) const { return {sqlite3_column_blob(handle, column), (size_t) sqlite3_column_bytes(handle, column)}; }
private:
    void check(int result) { if (result != SQLITE_OK) throw std::runtime_error(sqlite3_errmsg(db.handle)); }
};
struct Transaction {
    Database& db;
    bool committed = false;
    explicit Transaction(Database& owner) : db(owner) { db.exec("BEGIN IMMEDIATE"); }
    // Initial worker admission can wait for a one-time catalog index build.
    // Cancellation happens before a lease or transaction has been acquired.
    Transaction(Database& owner,const std::function<bool()>& cancelled,const std::function<void()>& waiting={}) : db(owner) {
        struct Restore {Database& db;~Restore(){sqlite3_busy_timeout(db.handle,5000);}} restore{db};
        sqlite3_busy_timeout(db.handle,0);
        const auto begin=juce::Time::getMillisecondCounterHiRes();bool announced=false;
        for(;;){
            if(cancelled&&cancelled())throw std::runtime_error("Catalog operation paused while waiting for another instance");
            const int result=sqlite3_exec(db.handle,"BEGIN IMMEDIATE",nullptr,nullptr,nullptr);
            if(result==SQLITE_OK)return;
            if(result!=SQLITE_BUSY&&result!=SQLITE_LOCKED)throw std::runtime_error(sqlite3_errmsg(db.handle));
            if(juce::Time::getMillisecondCounterHiRes()-begin>=180000)throw std::runtime_error("Another instance is still preparing the catalog; retry this operation");
            if(!announced){announced=true;if(waiting)waiting();}
            juce::Thread::sleep(50);
        }
    }
    ~Transaction() { if (!committed) sqlite3_exec(db.handle, "ROLLBACK", nullptr, nullptr, nullptr); }
    void commit() { db.exec("COMMIT"); committed = true; }
};
// A sealed schema needs no write admission when another instance opens it.
// Advance the component version whenever its initialization/migration changes.
inline bool schemaReady(Database& db,const char* component,int supported=1) {
    Statement table(db,"SELECT 1 FROM sqlite_master WHERE type='table' AND name='catalog_schema'");if(!table.row())return false;
    Statement row(db,"SELECT version FROM catalog_schema WHERE component=?");row.text(1,component);if(!row.row())return false;
    if(row.integer(0)>supported)throw std::runtime_error("This catalog schema requires a newer instrument");
    return row.integer(0)==supported;
}
inline void sealSchema(Database& db,const char* component,int version=1) {
    db.exec("CREATE TABLE IF NOT EXISTS catalog_schema(component TEXT PRIMARY KEY,version INTEGER NOT NULL)");
    Statement row(db,"INSERT OR REPLACE INTO catalog_schema VALUES(?,?)");row.text(1,component);row.integer(2,version);row.done();
}
}
