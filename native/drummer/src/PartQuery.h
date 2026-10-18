#pragma once
#include <drummer/PatternAnalysis.h>
#include <drummer/Json.h>
#include <algorithm>
#include <set>

namespace drummer {
// Worker-only SQL over the committed per-part measurements. Applying these
// constraints before representative sampling prevents a rare part from being
// hidden by the density or phrase length of the rest of the kit.
struct PartQuery {
    bool enabled=false;
    int wantedFeel=-1;
    juce::String density="p.density",predicate,measuredFeel;
    static std::set<int> mask(const juce::var& parts) {
        if(parts.isVoid()||parts.isUndefined())return {};
        if(!parts.isArray())throw std::runtime_error("Pattern parts must be MIDI note numbers");
        if(parts.size()>128)throw std::runtime_error("A part mask can contain at most 128 MIDI notes");
        std::set<int> result;
        for(const auto& value:*parts.getArray()){
            if(!value.isInt()&&!value.isInt64()&&!value.isDouble())throw std::runtime_error("Pattern parts must be MIDI note numbers");
            const double note=(double)value;
            if(!std::isfinite(note)||note<0||note>127||note!=std::floor(note))throw std::runtime_error("Pattern parts must be whole MIDI note numbers from 0 to 127");
            result.insert((int)note);
        }
        return result;
    }
    static juce::StringArray roles(const std::set<int>& notes){
        using namespace json;juce::StringArray result;
        static const auto canonical=get(drumMapDefinitions(),"canonical");
        for(const auto& role:canonical.getDynamicObject()->getProperties())if(notes.contains((int)role.value)){
            const auto name=role.name.toString();
            if(!name.containsOnly("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_"))throw std::runtime_error("Unsupported canonical drum role");
            result.add(name);
        }
        return result;
    }
    explicit PartQuery(const juce::var& options,bool filterPartPresence=true) {
        using namespace json;
        const auto parts=get(options,"parts");if(parts.isVoid()||parts.isUndefined())return;
        const auto notes=mask(parts);enabled=true;
        juce::StringArray counts;
        for(const auto& name:roles(notes)){
            // Single-path json_extract returns a numeric SQL scalar, not JSON.
            // https://www.sqlite.org/json1.html#the_json_extract_function
            counts.add("COALESCE(json_extract(p.features,'$.parts."+name+".notes'),0)");
        }
        const auto count=counts.isEmpty()?juce::String("0"):"("+counts.joinIntoString("+")+")";
        density="("+count+"*1.0/NULLIF(p.length,0))";
        predicate=" AND p.length>0";if(filterPartPresence)predicate+=" AND "+count+">0";
        const int n=(int)number(options,"numerator",0),d=(int)number(options,"denominator",0);
        if(options.hasProperty("phraseBars")&&n>0&&d>0){
            const int phrase=std::clamp((int)number(options,"phraseBars",8),1,64);
            juce::StringArray divisors;for(int bars=1;bars<=phrase;++bars)if(phrase%bars==0)divisors.add(juce::String(bars));
            const auto bars="(p.length*1.0*"+juce::String(d)+"/"+juce::String(n*4)+")";
            predicate+=" AND "+bars+">=.99 AND "+bars+"<="+juce::String(phrase)+" AND ABS("+bars+"-ROUND("+bars+"))<=.001 AND CAST(ROUND("+bars+") AS INTEGER) IN("+divisors.joinIntoString(",")+")";
        }
        const auto feel=get(options,"feel").toString();wantedFeel=feel=="straight"?0:feel=="triplet"?1:-1;
        measuredFeel="CASE WHEN COALESCE(json_extract(p.features,'$.tripletFit'),0)>.9 AND COALESCE(json_extract(p.features,'$.sixteenthFit'),1)<.8 THEN 1 WHEN COALESCE(json_extract(p.features,'$.sixteenthFit'),0)>.9 AND COALESCE(json_extract(p.features,'$.tripletFit'),1)<.8 THEN 0 ELSE -1 END";
    }
    juce::String feelPredicate(const juce::String& described,const juce::String& measured={}) const {
        if(!enabled||wantedFeel<0)return {};
        return " AND (CASE WHEN ("+described+")>=0 THEN ("+described+") ELSE ("+(measured.isEmpty()?measuredFeel:measured)+") END) IN(-1,"+juce::String(wantedFeel)+")";
    }
    static int describedFeel(const juce::String& text){const auto words=" "+text+" ";if(words.contains(" triplet ")||words.contains(" swing ")||words.contains(" shuffle "))return 1;if(words.contains(" straight "))return 0;return -1;}
    static juce::String describedFeelSql(const juce::String& text){const auto words="(' '||"+text+"||' ')";return "CASE WHEN INSTR("+words+",' triplet ')>0 OR INSTR("+words+",' swing ')>0 OR INSTR("+words+",' shuffle ')>0 THEN 1 WHEN INSTR("+words+",' straight ')>0 THEN 0 ELSE -1 END";}
    static juce::String sampleSql(const juce::String& ordinal,const juce::String& total,int budget){
        // Integer bucket transitions retain both density endpoints and at most
        // budget rows, without floating-point boundary/tie differences.
        const auto scale=juce::String(budget-1),denominator="NULLIF("+total+"-1,0)";
        return "("+total+"<="+juce::String(budget)+" OR "+ordinal+"=1 OR (("+ordinal+"-1)*"+scale+"/"+denominator+")<>(("+ordinal+"-2)*"+scale+"/"+denominator+"))";
    }
};
}
