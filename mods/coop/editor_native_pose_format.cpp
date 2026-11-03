#include "editor_native_pose_format.h"
#include "editor_native_refpack.h"
#include "net/dbpf.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <new>
#include <set>
#include <stdexcept>

namespace editor_native_pose_format {
namespace {
struct Failure {Result result;const char* message;};
void need(bool value,const char* message){if(!value)throw Failure{Result::Error,message};}
void profile(bool value,const char* message){if(!value)throw Failure{Result::Unsupported,message};}
uint32_t word(std::span<const uint8_t> bytes,size_t at,bool big){
    need(at<=bytes.size() && bytes.size()-at>=4,"Word outside input");
    if(big)return uint32_t(bytes[at])<<24|uint32_t(bytes[at+1])<<16|uint32_t(bytes[at+2])<<8|bytes[at+3];
    return uint32_t(bytes[at])|uint32_t(bytes[at+1])<<8|uint32_t(bytes[at+2])<<16|uint32_t(bytes[at+3])<<24;
}
void put(std::vector<uint8_t>& bytes,size_t at,uint32_t value,bool big){
    need(at<=bytes.size() && bytes.size()-at>=4,"Word outside output");
    for(unsigned n=0;n<4;++n)bytes[at+n]=uint8_t(value>>(big?24-8*n:8*n));
}
// Scan without copying/normalizing property payloads or allocating from their
// declared counts. This is the same v17 grammar as the owned terrain codec.
size_t prefix(std::span<const uint8_t> bytes){
    need(bytes.size()>=12,"Truncated Native header");
    profile(word(bytes,0,false)==0xB523D4F9 && word(bytes,4,false)==17,"Unsupported Native version");
    const auto count=word(bytes,8,true);profile(count<=4096,"Property count profile limit");
    size_t at=12;std::set<uint32_t> ids;
    const auto advance=[&](size_t size){need(at<=bytes.size() && size<=bytes.size()-at,"Truncated property value");at+=size;};
    const auto string=[&](size_t unit){const auto chars=word(bytes,at,true);advance(4);
        need(chars<=(bytes.size()-at)/unit,"Property string overflow");advance(size_t(chars)*unit);};
    for(uint32_t n=0;n<count;++n){
        const auto id=word(bytes,at,true);need(ids.insert(id).second,"Duplicate property identity");
        need(bytes.size()-at>=8,"Truncated property header");
        const auto type=uint16_t(bytes[at+4])<<8|bytes[at+5],flags=uint16_t(bytes[at+6])<<8|bytes[at+7];at+=8;
        if(flags&0x80){
            const auto items=word(bytes,at,true),width=word(bytes,at+4,true);advance(8);
            profile(items<=8192 && width<=kImageLimit,"Property array profile limit");
            if(type==0x12 || type==0x13){for(uint32_t i=0;i<items;++i)string(type==0x13?2:1);}
            else {need(!items || width<=(bytes.size()-at)/items,"Property array overflow");advance(size_t(items)*width);}
        }else switch(type){
            case 0:case 0x20:case 0x31:case 0x32:case 0x33:case 0x34:advance(16);break;
            case 1:case 2:case 5:case 6:advance(1);break;
            case 3:case 7:case 8:advance(2);break;
            case 4:case 9:case 0xA:case 0xD:advance(4);break;
            case 0xB:case 0xC:case 0xE:case 0x30:advance(8);break;
            case 0x38:advance(56);break;
            case 0x12:case 0x13:string(type==0x13?2:1);break;
            default:throw Failure{Result::Unsupported,"Unsupported property type"};
        }
    }
    return at;
}
struct Field {uint32_t key;size_t offset,size;};
using Record=std::vector<Field>;
constexpr size_t kDirectoryLimit=131072;
Record record(std::span<const uint8_t> bytes,size_t at,size_t limit,size_t& end,size_t& entries){
    need(limit<=bytes.size() && at<=limit && limit-at>=12,"Truncated ClassSerializer header");
    profile(word(bytes,at,true)==0x01A80D26,"Unsupported ClassSerializer version");
    const auto length=word(bytes,at+4,true),count=word(bytes,at+8,true);
    need(length>=4 && length<=limit-at-8 && count<=(length-4)/8,"Invalid ClassSerializer directory");
    profile(count && count<=128 && count<=kDirectoryLimit-entries,"ClassSerializer directory profile limit");
    entries+=count;end=at+8+length;size_t cursor=at+12+size_t(count)*8;Record fields;fields.reserve(count);
    for(uint32_t n=0;n<count;++n){
        const auto key=word(bytes,at+12+size_t(n)*8,true),size=word(bytes,at+16+size_t(n)*8,true);
        need(size<=end-cursor,"ClassSerializer payload overrun");fields.push_back({key,cursor,size});cursor+=size;
    }
    need(cursor==end,"ClassSerializer trailing bytes");
    std::sort(fields.begin(),fields.end(),[](const auto& a,const auto& b){return a.key<b.key;});
    for(size_t n=1;n<fields.size();++n)need(fields[n-1].key!=fields[n].key,"Duplicate ClassSerializer field");
    return fields;
}
const Field* find(const Record& fields,uint32_t key){
    const auto at=std::lower_bound(fields.begin(),fields.end(),key,[](const Field& a,uint32_t b){return a.key<b;});
    return at!=fields.end() && at->key==key?&*at:nullptr;
}
const Field& field(const Record& fields,uint32_t key,size_t size){
    const auto* result=find(fields,key);profile(result && result->size==size,"Required existing field/profile absent");return *result;
}
constexpr std::array<uint32_t,10> keys{0x071147E6,0x071147E7,0x071147E8,0x071147E9,0x071147EA,
    0x078D5A93,0x078D5A94,0x078D5895,0x071148E7,0x071148EA};
constexpr std::array<size_t,10> widths{3,4,1,1,1,1,1,3,4,1};
std::array<std::span<const uint32_t>,10> values(const MarkerBits& bits){
    return {bits.position,bits.rotation,std::span(&bits.scale,1),std::span(&bits.relative_scale,1),std::span(&bits.elevation,1),
        std::span(&bits.pitch,1),std::span(&bits.gain,1),bits.destination_position,bits.destination_rotation,std::span(&bits.destination_scale,1)};
}
bool finite(uint32_t bits){return (bits&0x7F800000)!=0x7F800000;}
bool position(const std::array<uint32_t,3>& bits){
    return std::all_of(bits.begin(),bits.end(),[](auto value){return finite(value) && std::abs(std::bit_cast<float>(value))<=1e7f;});
}
bool rotation(const std::array<uint32_t,4>& bits){
    double norm=0;for(auto value:bits){if(!finite(value))return false;const double v=std::bit_cast<float>(value);norm+=v*v;}
    return std::abs(norm-1)<=.01; // Same eligibility test as core, no normalization.
}
struct Parsed {size_t body=0,index=0;std::vector<uint8_t> decoded;std::vector<Record> markers;};
editor_native_refpack::Window window(size_t size){return size>250000?editor_native_refpack::Window::Small:editor_native_refpack::Window::Large;}
Parsed parse(std::span<const uint8_t> base){
    Parsed parsed;const auto tail=prefix(base);need(base.size()-tail>=8+96+36,"Truncated Native DBPF envelope");
    profile(word(base,tail,false)==17,"Unsupported embedded Native version");parsed.body=tail+8;
    const auto body=base.subspan(parsed.body);need(word(base,tail+4,false)==body.size(),"Invalid Native DBPF length");
    const auto magic=word(body,0,false);profile(magic==0x46504244 && word(body,4,false)==3,"Unsupported DBPF version");
    parsed.index=word(body,0x40,false);
    need(parsed.index>=96 && parsed.index<=body.size(),"Invalid DBPF index offset");
    profile(body.size()-parsed.index==36,"Unsupported DBPF index/tail profile");
    std::array<uint8_t,96> header{};header[0]='D';header[1]='B';header[2]='P';header[3]='F';header[4]=3;
    header[0x24]=1;header[0x2C]=36;header[0x3C]=3;
    for(unsigned n=0;n<4;++n)header[0x40+n]=uint8_t(parsed.index>>(8*n));
    profile(std::equal(header.begin(),header.end(),body.begin()),"Unsupported canonical DBPF header");
    const auto index=body.subspan(parsed.index);
    profile(word(index,0,false)==7 && word(index,4,false)==0x01897C18 && word(index,8,false)==0x01897C18 &&
        word(index,12,false)==0 && word(index,16,false)==0x01897C18 && word(index,20,false)==96 && word(index,32,false)==0x0001FFFF,
        "Unsupported core record key/state/compression");
    const auto stored=word(index,24,false),length=word(index,28,false);
    need((stored&0x7FFFFFFF)==parsed.index-96,"Invalid stored DBPF length");
    profile((stored&0x80000000) && length>=50 && length<=editor_native_refpack::kInputLimit,"Decoded size/compression profile limit");
    const auto packed=body.subspan(96,parsed.index-96);
    // Bound the decoder's allocation and reject extended/short headers before
    // entering the generic decoder (which also supports a different envelope).
    need(packed.size()>=5,"Truncated RefPack header");
    profile(packed[0]==(length>250000?0x50:0x10) && packed[1]==0xFB,"Unsupported RefPack envelope/window");
    const auto declared=uint32_t(packed[2])<<16|uint32_t(packed[3])<<8|packed[4];
    need(declared==length,"RefPack/DBPF declared length mismatch");
    need(dbpf::refpack_decode(packed.data(),packed.size(),parsed.decoded) && parsed.decoded.size()==length,"Malformed RefPack payload");
    {std::vector<uint8_t> encoded;std::string why;
        need(editor_native_refpack::encode(parsed.decoded,window(length),encoded,why),"BASE RefPack codec failure");
        profile(encoded.size()==packed.size() && std::equal(encoded.begin(),encoded.end(),packed.begin()),"BASE encoding is not exact Native profile");}
    size_t end=0,entries=0;const auto root=record(parsed.decoded,0,parsed.decoded.size(),end,entries);
    need(end==parsed.decoded.size(),"Root ClassSerializer tail");
    const auto* classes=find(root,0x072A9B58);const auto* acts=find(root,0x07102E6A);const auto* markers=find(root,0x07102E6C);
    profile(classes && acts && markers,"Missing complete root roster");
    need(classes->size>=4 && acts->size>=4 && markers->size>=4,"Truncated root roster count");
    std::set<uint32_t> class_ids;size_t cursor=classes->offset;
    const auto class_count=word(parsed.decoded,cursor,true);cursor+=4;
    profile(class_count && class_count<=1024,"Class count profile limit");
    for(uint32_t n=0;n<class_count;++n){
        need(cursor<=classes->offset+classes->size && classes->offset+classes->size-cursor>=4,"Truncated class identity");
        need(class_ids.insert(word(parsed.decoded,cursor,true)).second,"Duplicate class identity");cursor+=4;
        record(parsed.decoded,cursor,classes->offset+classes->size,end,entries);cursor=end;
    }
    need(cursor==classes->offset+classes->size,"Class list tail");
    cursor=acts->offset;const auto act_count=word(parsed.decoded,cursor,true);cursor+=4;
    profile(act_count && act_count<=8,"Act count profile limit");
    for(uint32_t n=0;n<act_count;++n){record(parsed.decoded,cursor,acts->offset+acts->size,end,entries);cursor=end;}
    need(cursor==acts->offset+acts->size,"Act list tail");
    cursor=markers->offset;const auto count=word(parsed.decoded,cursor,true);cursor+=4;
    profile(count && count<=kMarkerLimit,"Marker count profile limit");parsed.markers.reserve(count);
    for(uint32_t n=0;n<count;++n){
        auto row=record(parsed.decoded,cursor,markers->offset+markers->size,end,entries);cursor=end;
        need(class_ids.contains(word(parsed.decoded,field(row,0x071147E5,4).offset,true)),"Unknown marker class");
        for(size_t i=0;i<keys.size();++i)field(row,keys[i],widths[i]*4);
        const auto& flags=field(row,0x071147EB,36);profile(word(parsed.decoded,flags.offset,true)==32,"Unsupported marker flags shape");
        for(size_t i=4;i<36;++i)need(parsed.decoded[flags.offset+i]<=1,"Invalid marker Boolean flag");
        const auto* distances=find(row,0x071148E9);profile(distances && distances->size>=4,"Missing deprecated-distance field");
        profile(distances->size==4 && word(parsed.decoded,distances->offset,true)==0,"Deprecated distances require Native fallback");
        const auto* per_act=find(row,0x076CA8D3);profile(per_act && per_act->size>=4,"Missing marker Act roster");
        size_t child=per_act->offset;need(word(parsed.decoded,child,true)==act_count,"Marker Act count mismatch");child+=4;
        for(uint32_t i=0;i<act_count;++i){record(parsed.decoded,child,per_act->offset+per_act->size,end,entries);child=end;}
        need(child==per_act->offset+per_act->size,"Marker Act tail");parsed.markers.push_back(std::move(row));
    }
    need(cursor==markers->offset+markers->size,"Marker list tail");return parsed;
}
void pose(std::vector<uint8_t>& bytes,const Record& row,const PoseEdit& edit){
    const auto& p=field(row,keys[0],12);for(size_t n=0;n<3;++n)put(bytes,p.offset+n*4,edit.position[n],true);
    if(edit.rotate){const auto& q=field(row,keys[1],16);for(size_t n=0;n<4;++n)put(bytes,q.offset+n*4,edit.rotation[n],true);}
}
}
Result patch(std::span<const uint8_t> base,std::span<const MarkerRow> roster,std::span<const PoseEdit> edits,
             std::vector<uint8_t>& output,std::string& error){
    try{
        need(!base.empty(),"Empty Native image");profile(base.size()<=kImageLimit,"Native image profile limit");
        profile(!edits.empty() && edits.size()<=kEditLimit && roster.size()<=kMarkerLimit,"Pose edit/roster profile limit");
        auto parsed=parse(base);need(roster.size()==parsed.markers.size(),"Incomplete full marker roster");
        std::vector<const MarkerRow*> slots(roster.size(),nullptr);std::set<editor::ObjectId> ids;
        for(const auto& row:roster){
            need(row.object.id && row.object.generation && row.slot<slots.size() && !slots[row.slot] && ids.insert(row.object).second,"Invalid full marker binding");
            slots[row.slot]=&row;const auto& raw=parsed.markers[row.slot];
            need(word(parsed.decoded,field(raw,0x071147E5,4).offset,true)==row.class_index,"Stale marker class");
            const auto fields=values(row.bits);
            for(size_t i=0;i<keys.size();++i){const auto& value=field(raw,keys[i],widths[i]*4);
                for(size_t n=0;n<widths[i];++n)need(finite(fields[i][n]) && word(parsed.decoded,value.offset+n*4,true)==fields[i][n],"Stale/nonfinite raw BASE word");}
            const auto scale=std::bit_cast<float>(row.bits.scale);
            need(position(row.bits.position) && rotation(row.bits.rotation) && scale>0 && scale<=1000 && position(row.bits.destination_position),"Invalid core BASE pose");
        }
        std::set<uint32_t> changed;
        for(const auto& edit:edits){
            need(edit.slot<slots.size() && changed.insert(edit.slot).second,"Duplicate/absent pose edit slot");
            const auto& row=*slots[edit.slot];
            need(row.object==edit.object && row.class_index==edit.class_index,"Stale full pose edit identity");
            need(position(edit.position) && (!edit.rotate || rotation(edit.rotation)),"Invalid core target pose");
        }
        auto original=parsed.decoded;
        for(const auto& edit:edits)pose(parsed.decoded,parsed.markers[edit.slot],edit);
        // Reverse only the declared raw fields in-place, compare the entire
        // decoded image, then reapply. No third whole-body copy is necessary.
        for(const auto& edit:edits){auto back=edit;back.position=slots[edit.slot]->bits.position;back.rotation=slots[edit.slot]->bits.rotation;
            pose(parsed.decoded,parsed.markers[edit.slot],back);}
        need(parsed.decoded==original,"Unknown decoded byte changed");
        for(const auto& edit:edits)pose(parsed.decoded,parsed.markers[edit.slot],edit);
        std::vector<uint8_t>().swap(original);
        std::vector<uint8_t> packed;std::string why;
        need(editor_native_refpack::encode(parsed.decoded,window(parsed.decoded.size()),packed,why),"AFTER RefPack codec failure");
        // Native's default x87 0x3F733333 threshold is intentionally not imitated
        // near its precision boundary. This profile stays strictly below 50%.
        profile(uint64_t(packed.size())*2<parsed.decoded.size(),"Compression ratio requires Native fallback");
        {std::vector<uint8_t> verified;
            need(dbpf::refpack_decode(packed.data(),packed.size(),verified) && verified==parsed.decoded,"Generated RefPack decode mismatch");}
        const auto new_index=96+packed.size();const auto size=parsed.body+new_index+36;
        profile(size<=kImageLimit,"Produced image profile limit");
        std::vector<uint8_t>().swap(parsed.decoded);std::vector<Record>().swap(parsed.markers);
        std::vector<uint8_t> candidate;candidate.reserve(size);
        candidate.insert(candidate.end(),base.begin(),base.begin()+parsed.body+96);
        candidate.insert(candidate.end(),packed.begin(),packed.end());
        candidate.insert(candidate.end(),base.begin()+parsed.body+parsed.index,base.end());
        put(candidate,parsed.body-4,uint32_t(new_index+36),false);
        put(candidate,parsed.body+0x40,uint32_t(new_index),false);
        put(candidate,parsed.body+new_index+24,uint32_t(packed.size())|0x80000000u,false);
        output=std::move(candidate);error.clear();return Result::Produced;
    }catch(const Failure& failure){error=failure.message;return failure.result;}
    catch(const std::bad_alloc&){error="No memory";return Result::Error;}
    catch(const std::exception& failure){error=failure.what();return Result::Error;}
}
}
