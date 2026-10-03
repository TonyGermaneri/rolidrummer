#include <drummer/SharedStore.h>
#include <sqlite3.h>
#include <stdexcept>
namespace drummer {
namespace {
struct Statement{sqlite3_stmt* p=nullptr;Statement(sqlite3* db,const char* sql){if(sqlite3_prepare_v2(db,sql,-1,&p,nullptr)!=SQLITE_OK)throw std::runtime_error(sqlite3_errmsg(db));}~Statement(){sqlite3_finalize(p);}void text(int n,const juce::String&s){sqlite3_bind_text(p,n,s.toRawUTF8(),-1,SQLITE_TRANSIENT);}void integer(int n,int64_t x){sqlite3_bind_int64(p,n,x);}bool row(){return sqlite3_step(p)==SQLITE_ROW;}void done(){if(sqlite3_step(p)!=SQLITE_DONE)throw std::runtime_error("Shared settings write failed");}};
juce::String text(sqlite3_stmt*p,int n){auto*s=sqlite3_column_text(p,n);return s?juce::String::fromUTF8((const char*)s):juce::String();}
}
void SharedStore::exec(const char*sql){char* error=nullptr;if(sqlite3_exec(db,sql,nullptr,nullptr,&error)!=SQLITE_OK){std::string why=error?error:"Database error";sqlite3_free(error);throw std::runtime_error(why);}}
SharedStore::SharedStore(const juce::File& file){file.getParentDirectory().createDirectory();if(sqlite3_open(file.getFullPathName().toRawUTF8(),&db)!=SQLITE_OK)throw std::runtime_error("Cannot open shared settings");sqlite3_busy_timeout(db,2000);exec("PRAGMA journal_mode=WAL; CREATE TABLE IF NOT EXISTS fields(key TEXT PRIMARY KEY,value TEXT NOT NULL,revision INTEGER NOT NULL); CREATE TABLE IF NOT EXISTS leases(resource TEXT PRIMARY KEY,owner TEXT NOT NULL,expires INTEGER NOT NULL); CREATE TABLE IF NOT EXISTS clock(id INTEGER PRIMARY KEY CHECK(id=1),revision INTEGER NOT NULL); INSERT OR IGNORE INTO clock VALUES(1,0);");}
SharedStore::~SharedStore(){sqlite3_close(db);}
juce::var SharedStore::snapshot(int64_t after){std::lock_guard lock(mutex);juce::Array<juce::var> result;Statement query(db,"SELECT key,value,revision FROM fields WHERE revision>? ORDER BY revision");query.integer(1,after);while(query.row()){auto* o=new juce::DynamicObject;o->setProperty("key",text(query.p,0));o->setProperty("value",juce::JSON::parse(text(query.p,1)));o->setProperty("revision",(juce::int64)sqlite3_column_int64(query.p,2));result.add(o);}return result;}
bool SharedStore::lease(const juce::String& resource,const juce::String& owner,bool release){std::lock_guard lock(mutex);
 if(release){Statement q(db,"DELETE FROM leases WHERE resource=? AND owner=?");q.text(1,resource);q.text(2,owner);q.done();return true;}
 Statement q(db,"INSERT INTO leases VALUES(?,?,?) ON CONFLICT(resource) DO UPDATE SET owner=excluded.owner,expires=excluded.expires WHERE leases.owner=excluded.owner OR leases.expires<?");q.text(1,resource);q.text(2,owner);auto now=juce::Time::currentTimeMillis();q.integer(3,now+5000);q.integer(4,now);q.done();return sqlite3_changes(db)>0;
}
juce::var SharedStore::commit(const juce::var& operations){std::lock_guard lock(mutex);auto* list=operations.getArray();if(!list)throw std::runtime_error("Expected shared field operations");exec("BEGIN IMMEDIATE");try{
 for(auto& op:*list){auto key=op.getProperty("key",{}).toString();if(key.isEmpty())throw std::runtime_error("Empty shared field key");Statement current(db,"SELECT revision FROM fields WHERE key=?");current.text(1,key);auto rev=current.row()?sqlite3_column_int64(current.p,0):0;if(rev!=(int64_t)(juce::int64)op.getProperty("revision",0)){exec("ROLLBACK");auto* o=new juce::DynamicObject;o->setProperty("ok",false);o->setProperty("conflict",key);return o;}}
 exec("UPDATE clock SET revision=revision+1 WHERE id=1");Statement clock(db,"SELECT revision FROM clock WHERE id=1");clock.row();auto rev=sqlite3_column_int64(clock.p,0);
 for(auto& op:*list){Statement put(db,"INSERT INTO fields VALUES(?,?,?) ON CONFLICT(key) DO UPDATE SET value=excluded.value,revision=excluded.revision");put.text(1,op.getProperty("key",{}).toString());put.text(2,juce::JSON::toString(op.getProperty("value",{}),true));put.integer(3,rev);put.done();}
 exec("COMMIT");auto* o=new juce::DynamicObject;o->setProperty("ok",true);o->setProperty("revision",(juce::int64)rev);return o;
 }catch(...){exec("ROLLBACK");throw;}}
}
