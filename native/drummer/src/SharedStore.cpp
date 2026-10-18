#include <drummer/SharedStore.h>
#include <drummer/PatchBank.h>
#include <sqlite3.h>
#include <map>
#include <set>
#include <stdexcept>
namespace drummer {
namespace {
struct Statement {
 sqlite3* db;sqlite3_stmt* p=nullptr;
 Statement(sqlite3* owner,const char* sql):db(owner){if(sqlite3_prepare_v2(db,sql,-1,&p,nullptr)!=SQLITE_OK)throw std::runtime_error(sqlite3_errmsg(db));}
 ~Statement(){sqlite3_finalize(p);}
 void text(int n,const juce::String&s){if(sqlite3_bind_text(p,n,s.toRawUTF8(),-1,SQLITE_TRANSIENT)!=SQLITE_OK)throw std::runtime_error(sqlite3_errmsg(db));}
 void integer(int n,int64_t x){if(sqlite3_bind_int64(p,n,x)!=SQLITE_OK)throw std::runtime_error(sqlite3_errmsg(db));}
 bool row(){const auto status=sqlite3_step(p);if(status!=SQLITE_ROW&&status!=SQLITE_DONE)throw std::runtime_error(sqlite3_errmsg(db));return status==SQLITE_ROW;}
 void done(){if(row())throw std::runtime_error("Unexpected shared settings result");}
};
juce::String text(sqlite3_stmt*p,int n){auto*s=sqlite3_column_text(p,n);return s?juce::String::fromUTF8((const char*)s):juce::String();}
juce::var fields(sqlite3* db,int64_t after=0){
 juce::Array<juce::var> result;Statement query(db,"SELECT key,value,revision FROM fields WHERE revision>? ORDER BY revision,key");query.integer(1,after);
 while(query.row())result.add(json::object({{"key",text(query.p,0)},{"value",juce::JSON::fromString(text(query.p,1))},{"revision",(juce::int64)sqlite3_column_int64(query.p,2)}}));
 return result;
}
int64_t advance(sqlite3* db){Statement update(db,"UPDATE clock SET revision=revision+1 WHERE id=1");update.done();Statement clock(db,"SELECT revision FROM clock WHERE id=1");clock.row();return sqlite3_column_int64(clock.p,0);}
juce::var put(sqlite3* db,const juce::String& key,const juce::var& value,int64_t revision){
 Statement row(db,"INSERT INTO fields VALUES(?,?,?) ON CONFLICT(key) DO UPDATE SET value=excluded.value,revision=excluded.revision");
 row.text(1,key);row.text(2,juce::JSON::toString(value,true));row.integer(3,revision);row.done();
 return json::object({{"key",key},{"value",value},{"revision",(juce::int64)revision}});
}
}
void SharedStore::exec(const char*sql){char* error=nullptr;if(sqlite3_exec(db,sql,nullptr,nullptr,&error)!=SQLITE_OK){std::string why=error?error:"Database error";sqlite3_free(error);throw std::runtime_error(why);}}
SharedStore::SharedStore(const juce::File& file){
 file.getParentDirectory().createDirectory();if(sqlite3_open(file.getFullPathName().toRawUTF8(),&db)!=SQLITE_OK)throw std::runtime_error("Cannot open shared settings");
 try{
  sqlite3_busy_timeout(db,2000);exec("PRAGMA journal_mode=WAL; CREATE TABLE IF NOT EXISTS fields(key TEXT PRIMARY KEY,value TEXT NOT NULL,revision INTEGER NOT NULL); CREATE TABLE IF NOT EXISTS leases(resource TEXT PRIMARY KEY,owner TEXT NOT NULL,expires INTEGER NOT NULL); CREATE TABLE IF NOT EXISTS clock(id INTEGER PRIMARY KEY CHECK(id=1),revision INTEGER NOT NULL); INSERT OR IGNORE INTO clock VALUES(1,0);");
  exec("BEGIN IMMEDIATE");const auto changes=reconcilePatchBank(fields(db));
  if(!changes.isEmpty()){const auto revision=advance(db);for(const auto& change:changes)put(db,json::get(change,"key").toString(),json::get(change,"value"),revision);}
  exec("COMMIT");
 }catch(...){sqlite3_exec(db,"ROLLBACK",nullptr,nullptr,nullptr);sqlite3_close(db);db=nullptr;throw;}
}
SharedStore::~SharedStore(){sqlite3_close(db);}
juce::var SharedStore::snapshot(int64_t after){std::lock_guard lock(mutex);return fields(db,after);}
bool SharedStore::lease(const juce::String& resource,const juce::String& owner,bool release){std::lock_guard lock(mutex);
 if(release){Statement q(db,"DELETE FROM leases WHERE resource=? AND owner=?");q.text(1,resource);q.text(2,owner);q.done();return true;}
 Statement q(db,"INSERT INTO leases VALUES(?,?,?) ON CONFLICT(resource) DO UPDATE SET owner=excluded.owner,expires=excluded.expires WHERE leases.owner=excluded.owner OR leases.expires<?");q.text(1,resource);q.text(2,owner);auto now=juce::Time::currentTimeMillis();q.integer(3,now+5000);q.integer(4,now);q.done();return sqlite3_changes(db)>0;
}
juce::var SharedStore::commit(const juce::var& operations){
 using namespace json;std::lock_guard lock(mutex);auto* list=operations.getArray();if(!list)throw std::runtime_error("Expected shared field operations");
 exec("BEGIN IMMEDIATE");try{
  std::map<juce::String,juce::var> changes;bool patchesChanged=false;
  for(const auto& op:*list){
   const auto key=get(op,"key").toString();if(key.isEmpty()||changes.contains(key))throw std::runtime_error("Shared field keys must be nonempty and distinct");
   if(key==programBankKey)throw std::runtime_error("MIDI program assignments are maintained with saved patches");
   Statement current(db,"SELECT revision FROM fields WHERE key=?");current.text(1,key);const auto revision=current.row()?sqlite3_column_int64(current.p,0):0;
   if(revision!=(int64_t)(juce::int64)get(op,"revision",0)){exec("ROLLBACK");return object({{"ok",false},{"conflict",key}});}
   auto value=get(op,"value").clone();
   if(key.startsWith("patch/")&&value.isObject()){
    Statement previous(db,"SELECT value FROM fields WHERE key=?");previous.text(1,key);
    if(previous.row()){
     const auto old=juce::JSON::parse(text(previous.p,0));const auto id=get(old,"id").toString(),incoming=get(value,"id").toString();
     if(id.isNotEmpty()){
      if(incoming.isNotEmpty()&&incoming!=factoryPatchId&&incoming!=id)throw std::runtime_error("Save a copy to create a new patch identity");
      set(value,"id",id);
     }
    }
   }
   changes.emplace(key,value);patchesChanged=patchesChanged||key.startsWith("patch/")||key==programBankKey;
  }
  if(patchesChanged){
   auto all=fields(db);auto* entries=all.getArray();
   for(const auto& [key,value]:changes){entries->removeIf([&](const auto& entry){return get(entry,"key").toString()==key;});entries->add(object({{"key",key},{"value",value}}));}
   for(const auto& change:reconcilePatchBank(all))changes.insert_or_assign(get(change,"key").toString(),get(change,"value"));
  }
  const auto revision=advance(db);juce::Array<juce::var> canonical;
  for(const auto& [key,value]:changes)canonical.add(put(db,key,value,revision));
  exec("COMMIT");return object({{"ok",true},{"revision",(juce::int64)revision},{"fields",canonical}});
 }catch(...){exec("ROLLBACK");throw;}
}
}
