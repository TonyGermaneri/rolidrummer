#include <drummer/SoundMatcher.h>
#include <drummer/TextEmbedding.h>
#include <drummer/VectorSearch.h>
#include <drummer/Json.h>
#include "LibrarySqlite.h"
#include <juce_cryptography/juce_cryptography.h>
#include <algorithm>
#include <map>
#include <set>
#include <regex>
#include <cstring>
namespace drummer {
using namespace json;
namespace {
std::set<int> mask(const juce::var& notes){std::set<int> result;if(auto* list=notes.getArray())for(const auto& note:*list)if((int)note>=0&&(int)note<=127)result.insert((int)note);return result;}
juce::String words(const juce::String& text){
 auto value=text.toLowerCase().replaceCharacters("_-/\\.","      ").toStdString();
 static const std::regex numbers("([a-z])([0-9])");return " "+juce::String(std::regex_replace(value,numbers,"$1 $2"))+" ";
}
std::set<int> infer(const juce::String& text){
 const auto value=words(text);auto has=[&](std::initializer_list<const char*> names){for(auto name:names)if(value.contains(name))return true;return false;};
 if(has({" kick"," bass drum"," bd "," kd "}))return{35,36};
 if(has({" snare"," sd "," sn "}))return{38,40};
 if(has({" open hat"," openhat"," ohh "," oh "}))return{46};
 if(has({" closed hat"," closedhat"," chh "," ch "}))return{42};
 if(has({" pedal hat"," foot hat"}))return{44};
 if(has({" hihat"," hi hat"," hh "," hat "}))return{42,44,46};
 if(has({" clap"," cp "}))return{39};
 if(has({" rim"," side stick"," sidestick"}))return{37};
 if(has({" tom"}))return{41,43,45,47,48,50};
 if(has({" crash"}))return{49,52,55,57};
 if(has({" ride"}))return{51,53,59};
 if(has({" cowbell"}))return{56};
 if(has({" shaker"}))return{70};
 if(has({" conga"}))return{62,63,64};
 return{};
}
bool audio(const juce::File& file){return file.hasFileExtension("wav;aif;aiff;flac;mp3;ogg");}
struct Candidate {juce::var asset;juce::String description;std::set<int> parts;};
}
juce::var matchSoundKit(const juce::File& cache,const juce::var& options){
 const auto query=get(options,"query").toString().trim().substring(0,512);
 if(query.isEmpty())throw std::runtime_error("Describe the sounds to match");
 std::map<juce::String,Candidate> candidates;
 bool limited=false;
 auto add=[&](juce::var asset,juce::String description){
  if(candidates.size()>=5000){limited=true;return;}
  auto path=get(asset,"path").toString();if(path.isEmpty())return;
  auto parts=mask(get(get(asset,"preset"),"parts"));if(parts.empty())parts=infer(get(asset,"name").toString());if(parts.empty())parts=infer(description);
  const auto id=get(asset,"id",path).toString();candidates.try_emplace(id,Candidate{asset,words(description),parts});
 };
 if(auto assets=get(options,"assets");auto* list=assets.getArray())for(const auto& asset:*list)if(!(bool)get(asset,"folder",false))add(asset,get(asset,"name").toString()+" "+get(asset,"path").toString());
 int visited=0;
 std::set<juce::String> directories,files;
 if(auto roots=get(options,"roots");auto* list=roots.getArray())for(int i=0;i<std::min(32,list->size());++i){
  const auto rootPath=(*list)[i].toString();if(!juce::File::isAbsolutePath(rootPath))continue;juce::File root(rootPath);std::vector<juce::File> pending{root};
  while(!pending.empty()&&!limited){
   const auto directory=pending.back();pending.pop_back();if(!directory.isDirectory()||!directories.insert(directory.getFullPathName()).second)continue;
   juce::Array<juce::File> children;directory.findChildFiles(children,juce::File::findFilesAndDirectories,false);
   std::sort(children.begin(),children.end(),[](const auto& a,const auto& b){return a.getFullPathName()<b.getFullPathName();});
   for(const auto& file:children){
    if(++visited>100000){limited=true;break;}
    if(file.isDirectory()){if(!file.isSymbolicLink()&&!file.isHidden())pending.push_back(file);continue;}
    if(!audio(file)||!files.insert(file.getFullPathName()).second)continue;
    const auto path=file.getFullPathName();add(object({{"id",path},{"path",path},{"name",file.getFileName()},{"folder",false},{"size",(juce::int64)file.getSize()}}),file.getFullPathName());
   }
  }
 }
 auto targets=get(options,"keys");if(!targets.isArray()||targets.size()>128)throw std::runtime_error("Choose valid keywaves for sound matching");
 if(candidates.empty()||targets.size()==0)return object({{"assignments",juce::Array<juce::var>()},{"sounds",0},{"limited",limited}});
 auto model=TextEmbedding::acquire(neuralAssetsDirectory());
 cache.getParentDirectory().createDirectory();sql::Database db(cache);db.exec("PRAGMA journal_mode=WAL;CREATE TABLE IF NOT EXISTS sound_vectors(id TEXT PRIMARY KEY,vector BLOB NOT NULL)");
 sql::Statement read(db,"SELECT vector FROM sound_vectors WHERE id=?"),write(db,"INSERT OR IGNORE INTO sound_vectors VALUES(?,?)");
 std::vector<Candidate> rows;std::vector<TextVector> vectors(candidates.size());std::vector<size_t> missing;std::vector<juce::String> ids;
 for(const auto& [id,candidate]:candidates){const auto index=rows.size();rows.push_back(candidate);const auto key=juce::SHA256((model->version()+":sound-1:"+candidate.description).toUTF8()).toHexString();ids.push_back(key);read.reset();read.text(1,key);
  bool found=false;if(read.row()){const auto blob=read.blob(0);if(blob.getSize()==sizeof(TextVector)){std::memcpy(vectors[index].data(),blob.getData(),sizeof(TextVector));found=true;}}
  read.reset();if(!found)missing.push_back(index);
 }
 for(size_t start=0;start<missing.size();start+=32){
  std::vector<juce::String> texts;for(size_t j=start;j<std::min(start+32,missing.size());++j)texts.push_back(rows[missing[j]].description);
  const auto encoded=model->encode(texts);sql::Transaction batch(db);
  for(size_t j=0;j<encoded.size();++j){const auto index=missing[start+j];vectors[index]=encoded[j];write.reset();write.text(1,ids[index]);write.blob(2,juce::MemoryBlock(encoded[j].data(),sizeof(TextVector)));write.done();}
  batch.commit();
 }
 std::vector<juce::String> descriptions;for(const auto& target:*targets.getArray()){auto role=get(target,"part").toString();if(role.isEmpty()||role=="Custom")role=get(target,"name").toString();descriptions.push_back(query+" "+role+" drum sound sample");}
 const auto encoded=model->encode(descriptions);VectorSearch search(std::move(vectors),!(bool)get(options,"cpu",false));juce::Array<juce::var> assignments;
 for(int i=0;i<targets.size();++i){const auto& target=(*targets.getArray())[i];const auto parts=mask(get(target,"parts"));
  for(const auto& match:search.search(encoded[(size_t)i],rows.size())){const auto& candidate=rows[match.row];
   if(!parts.empty()&&!candidate.parts.empty()&&std::none_of(parts.begin(),parts.end(),[&](int note){return candidate.parts.contains(note);}))continue;
   assignments.add(object({{"note",get(target,"note")},{"asset",candidate.asset},{"similarity",match.score}}));break;
  }
 }
 return object({{"assignments",assignments},{"sounds",(int)rows.size()},{"limited",limited},{"provider",search.provider()}});
}
}
